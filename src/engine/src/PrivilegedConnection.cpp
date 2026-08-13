#include "PrivilegedConnection.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <iterator>

#include "ffprotocol/Version.h"
#include "ffprotocol/Diagnostics.h"
#include "ffsetup/AuthenticodeVerification.h"
#include "ffsetup/Identifiers.h"
#include "ffsetup/PeerImageMatch.h"
#include "ffsetup/PinnedSignatures.h"

namespace ffengine {

namespace {

using ffprotocol::MessageType;

constexpr std::chrono::milliseconds kHandshakeReplyTimeout{5000};
constexpr std::chrono::milliseconds kHeartbeatInterval{20000};
constexpr std::chrono::milliseconds kHeartbeatAckTimeout{10000};
constexpr std::chrono::milliseconds kInitialBackoff{1000};
constexpr std::chrono::milliseconds kMaxBackoff{60000};

std::optional<std::wstring> GetProcessImagePath(HANDLE processHandle) {
    wchar_t buffer[MAX_PATH * 4];
    DWORD size = static_cast<DWORD>(std::size(buffer));
    if (!QueryFullProcessImageNameW(processHandle, 0, buffer, &size)) {
        return std::nullopt;
    }
    return std::wstring(buffer, size);
}

std::optional<std::vector<ffprotocol::VolumeInfo>> ParseVolumeList(const std::vector<uint8_t>& payload) {
    if (payload.size() < sizeof(ffprotocol::VolumeListHeader)) {
        return std::nullopt;
    }
    ffprotocol::VolumeListHeader header{};
    std::memcpy(&header, payload.data(), sizeof(header));
    const size_t expectedSize = sizeof(header) + static_cast<size_t>(header.count) * sizeof(ffprotocol::VolumeInfo);
    if (payload.size() != expectedSize) {
        return std::nullopt;
    }
    std::vector<ffprotocol::VolumeInfo> volumes(header.count);
    if (header.count > 0) {
        std::memcpy(volumes.data(), payload.data() + sizeof(header), header.count * sizeof(ffprotocol::VolumeInfo));
    }
    return volumes;
}

} // namespace

PrivilegedConnection::~PrivilegedConnection() {
    Stop();
}

void PrivilegedConnection::SetState(ConnectionState state, UnavailableReason reason) {
    state_ = state;
    if (onStateChange_) {
        onStateChange_(state, reason);
    }
}

void PrivilegedConnection::Start(std::wstring installDir, StateChangeCallback onStateChange) {
    installDir_ = std::move(installDir);
    onStateChange_ = std::move(onStateChange);
    running_ = true;
    lifecycleThread_ = std::thread(&PrivilegedConnection::RunLoop, this);
}

void PrivilegedConnection::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    const HANDLE connecting = connectingPipe_.load(std::memory_order_acquire);
    if (connecting != nullptr) {
        CancelIoEx(connecting, nullptr);
    }
    const HANDLE active = activePipe_.load(std::memory_order_acquire);
    if (active != nullptr) {
        CancelIoEx(active, nullptr);
    }
    RequestReconnect(); // wake the loop so it observes running_ == false promptly
    if (lifecycleThread_.joinable()) {
        lifecycleThread_.join();
    }
}

void PrivilegedConnection::DropForIdle() {
    idleDropped_ = true;
    SetState(ConnectionState::Disconnected, UnavailableReason::IdleDropped);
    RequestReconnect(); // wakes the loop, which then observes idleDropped_ and waits
}

void PrivilegedConnection::RequestReconnect() {
    idleDropped_ = false;
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        wakeRequested_ = true;
    }
    wakeCv_.notify_all();
}

bool PrivilegedConnection::SendRequest(uint16_t messageType, const void* payload, uint32_t payloadSize) {
    HANDLE pipe = activePipe_.load();
    if (pipe == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(writeMutex_);
    // Re-check under the lock: the pipe could have been torn down between
    // the load above and acquiring the lock.
    if (activePipe_.load() != pipe) {
        return false;
    }
    return ffipc::WriteFrameOverlapped(
        pipe, messageType, payload, payloadSize,
        std::chrono::steady_clock::now() + std::chrono::seconds(5));
}

bool PrivilegedConnection::VerifyServiceIdentity(HANDLE pipe) const {
    const auto reject = [](const wchar_t* stage, DWORD error) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_ENGINE_CONNECT_FAILED,
                                      {}, {}, stage, error, 0});
        return false;
    };

    ULONG serverPid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &serverPid)) {
        return reject(L"service-identity-server-pid", GetLastError());
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, serverPid);
    if (process == nullptr) {
        return reject(L"service-identity-open-process", GetLastError());
    }

    auto imagePath = GetProcessImagePath(process);
    CloseHandle(process);
    if (!imagePath) {
        return reject(L"service-identity-image-path", GetLastError());
    }

    if (!ffsetup::IsExpectedInstalledBinary(*imagePath, installDir_, ffsetup::kIndexSvcExeName)) {
        return reject(L"service-identity-image-path-mismatch", ERROR_INVALID_DATA);
    }

    if (!ffsetup::VerifyPinnedSignature(*imagePath, ffsetup::kExpectedIndexSvcSignatureThumbprint)) {
        return reject(L"service-identity-signature", ERROR_INVALID_DATA);
    }
    return true;
}

HANDLE PrivilegedConnection::ConnectVerifyAndHandshake() {
    SetState(ConnectionState::Connecting, UnavailableReason::None);

    HANDLE pipe = INVALID_HANDLE_VALUE;
    while (running_) {
        pipe = CreateFileW(ffsetup::kCtrlPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            connectingPipe_.store(pipe, std::memory_order_release);
            break;
        }
        if (GetLastError() != ERROR_PIPE_BUSY) {
            SetState(ConnectionState::Disconnected, UnavailableReason::ServiceNotRunning);
            return nullptr;
        }
        // Bounded by running_ so Stop() can't be blocked indefinitely
        // under sustained pipe contention (unlike every other wait in
        // this class, WaitNamedPipeW itself has no cancellation hook).
        if (!WaitNamedPipeW(ffsetup::kCtrlPipeName, 2000)) {
            SetState(ConnectionState::Disconnected, UnavailableReason::ServiceNotRunning);
            return nullptr;
        }
    }
    if (!running_) {
        connectingPipe_.store(nullptr, std::memory_order_release);
        if (pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
        }
        return nullptr;
    }

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    // Task 4.4: verify the connected peer really is the genuine,
    // ACL-installed, signed FastFilesIndexSvc.exe before trusting
    // anything it says -- symmetric to the service's own client
    // verification (design.md D4 "Symmetric Mutual Authentication").
    if (!VerifyServiceIdentity(pipe)) {
        connectingPipe_.store(nullptr, std::memory_order_release);
        CloseHandle(pipe);
        SetState(ConnectionState::Disconnected, UnavailableReason::UnverifiedServiceIdentity);
        return nullptr;
    }

    SetState(ConnectionState::Handshaking, UnavailableReason::None);

    ffprotocol::HandshakeRequest request{ffprotocol::kCurrentProtocolVersion};
    if (!ffipc::WriteFrameOverlapped(
            pipe, static_cast<uint16_t>(MessageType::Handshake), &request, sizeof(request),
            std::chrono::steady_clock::now() + kHandshakeReplyTimeout)) {
        connectingPipe_.store(nullptr, std::memory_order_release);
        CloseHandle(pipe);
        SetState(ConnectionState::Disconnected, UnavailableReason::ServiceNotRunning);
        return nullptr;
    }

    auto reply = ffipc::ReadFrameOverlapped(pipe, std::chrono::steady_clock::now() + kHandshakeReplyTimeout);
    if (!reply) {
        connectingPipe_.store(nullptr, std::memory_order_release);
        CloseHandle(pipe);
        SetState(ConnectionState::Disconnected, UnavailableReason::ServiceNotRunning);
        return nullptr;
    }

    switch (static_cast<MessageType>(reply->header.messageType)) {
        case MessageType::HandshakeAck: {
            if (reply->payload.size() != sizeof(ffprotocol::HandshakeAckPayload)) {
                connectingPipe_.store(nullptr, std::memory_order_release);
                CloseHandle(pipe);
                SetState(ConnectionState::Disconnected, UnavailableReason::HandshakeRejected);
                return nullptr;
            }
            ffprotocol::HandshakeAckPayload ack{};
            std::memcpy(&ack, reply->payload.data(), sizeof(ack));
            if (!ffprotocol::IsVersionCompatible(ffprotocol::kCurrentProtocolVersion, ack.negotiatedVersion)
                || ack.negotiatedVersion.major > ffprotocol::kCurrentProtocolVersion.major) {
                connectingPipe_.store(nullptr, std::memory_order_release);
                CloseHandle(pipe);
                SetState(ConnectionState::Disconnected, UnavailableReason::IncompatibleProtocolVersion);
                return nullptr;
            }
            negotiatedMajor_ = ack.negotiatedVersion.major;
            negotiatedMinor_ = ack.negotiatedVersion.minor;
            connectingPipe_.store(nullptr, std::memory_order_release);
            return pipe;
        }

        case MessageType::IncompatibleVersion:
            // Task 4.9: treat as privileged-path-unavailable, not a
            // transient error to keep retrying at the same cadence --
            // the service self-heals its own build staleness (task 3.9),
            // so this condition is expected to resolve on its own.
            connectingPipe_.store(nullptr, std::memory_order_release);
            CloseHandle(pipe);
            SetState(ConnectionState::Disconnected, UnavailableReason::IncompatibleProtocolVersion);
            return nullptr;

        case MessageType::HandshakeReject:
        default:
            connectingPipe_.store(nullptr, std::memory_order_release);
            CloseHandle(pipe);
            SetState(ConnectionState::Disconnected, UnavailableReason::HandshakeRejected);
            return nullptr;
    }
}

void PrivilegedConnection::DispatchReceivedFrame(
    const ffipc::ReceivedFrame& frame, std::atomic<bool>& heartbeatAckPending, std::mutex& ackMutex,
    std::condition_variable& ackCv, std::atomic<bool>& protocolViolation) {
    auto messageType = ffprotocol::ToMessageType(frame.header.messageType);
    if (!messageType) {
        protocolViolation = true;
        return;
    }

    switch (*messageType) {
        case MessageType::HeartbeatAck: {
            std::lock_guard<std::mutex> lock(ackMutex);
            heartbeatAckPending = false;
            ackCv.notify_all();
            break;
        }

        case MessageType::VolumeList: {
            auto volumes = ParseVolumeList(frame.payload);
            if (!volumes) {
                protocolViolation = true;
                break;
            }
            if (volumeListCallback_) {
                volumeListCallback_(std::move(*volumes));
            }
            break;
        }

        case MessageType::ScanRecordBatch: {
            if (frame.payload.size() < sizeof(ffprotocol::ScanRecordBatchHeader)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::ScanRecordBatchHeader header{};
            std::memcpy(&header, frame.payload.data(), sizeof(header));
            const size_t afterHeader = sizeof(header);
            if (frame.payload.size() < afterHeader + header.resumeCursorLengthBytes) {
                protocolViolation = true;
                break;
            }
            std::vector<uint8_t> cursor(
                frame.payload.begin() + afterHeader, frame.payload.begin() + afterHeader + header.resumeCursorLengthBytes);
            const size_t recordsOffset = afterHeader + header.resumeCursorLengthBytes;
            auto records = ffprotocol::ParseMftBatch(
                frame.payload.data() + recordsOffset, frame.payload.size() - recordsOffset, header.recordCount);
            if (!records) {
                protocolViolation = true;
                break;
            }
            if (scanBatchCallback_) {
                scanBatchCallback_(header.volumeId, std::move(cursor), std::move(*records));
            }
            break;
        }

        case MessageType::ScanComplete: {
            if (frame.payload.size() != sizeof(ffprotocol::ScanCompletePayload)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::ScanCompletePayload payload{};
            std::memcpy(&payload, frame.payload.data(), sizeof(payload));
            if (scanCompleteCallback_) {
                scanCompleteCallback_(payload.volumeId);
            }
            break;
        }

        case MessageType::UsnJournalOpened: {
            if (frame.payload.size() != sizeof(ffprotocol::UsnJournalOpenedPayload)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::UsnJournalOpenedPayload payload{};
            std::memcpy(&payload, frame.payload.data(), sizeof(payload));
            if (journalOpenedCallback_) {
                journalOpenedCallback_(payload.volumeId, payload.journalId, payload.currentUsn);
            }
            break;
        }

        case MessageType::JournalResumeInvalid: {
            if (frame.payload.size() != sizeof(ffprotocol::JournalResumeInvalidPayload)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::JournalResumeInvalidPayload payload{};
            std::memcpy(&payload, frame.payload.data(), sizeof(payload));
            if (journalResumeInvalidCallback_) {
                journalResumeInvalidCallback_(payload.volumeId);
            }
            break;
        }

        case MessageType::JournalRecordBatch: {
            if (frame.payload.size() < sizeof(ffprotocol::JournalRecordBatchHeader)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::JournalRecordBatchHeader header{};
            std::memcpy(&header, frame.payload.data(), sizeof(header));
            const size_t recordsOffset = sizeof(header);
            auto records = ffprotocol::ParseUsnDeltaBatch(
                frame.payload.data() + recordsOffset, frame.payload.size() - recordsOffset, header.recordCount);
            if (!records) {
                protocolViolation = true;
                break;
            }
            if (journalBatchCallback_) {
                journalBatchCallback_(header.volumeId, header.latestUsn, std::move(*records));
            }
            break;
        }

        default:
            // Handshake/HandshakeAck/etc. have no business appearing once
            // the connection is Active.
            protocolViolation = true;
            break;
    }
}

void PrivilegedConnection::ActiveLoop(HANDLE pipe) {
    // State observers synchronously send their initial EnumerateVolumes
    // request when they see Active, so publish the verified pipe first.
    // Reversing these two operations drops that first request because
    // SendRequest observes a null activePipe_.
    activePipe_ = pipe;
    SetState(ConnectionState::Active, UnavailableReason::None);

    std::atomic<bool> heartbeatAckPending{false};
    std::atomic<bool> protocolViolation{false};
    std::mutex ackMutex;
    std::condition_variable ackCv;

    // A single reader thread owns every overlapped frame read on this connection
    // for its Active lifetime -- HeartbeatAck feeds the heartbeat-wait
    // below, every other message type is dispatched to whichever
    // scan/journal callback applies (index-storage-and-scanning tasks.md
    // 6.1). This is what lets scan/journal batches arrive asynchronously,
    // interleaved with the heartbeat cadence, rather than only in
    // lockstep reply to a request.
    std::thread reader([&] {
        for (;;) {
            auto frame = ffipc::ReadFrameOverlapped(
                pipe, std::chrono::steady_clock::time_point::max());
            if (!frame) {
                protocolViolation = true; // read failure/disconnect -- reuse the same teardown path
                ackCv.notify_all();
                break;
            }
            try {
                DispatchReceivedFrame(*frame, heartbeatAckPending, ackMutex, ackCv, protocolViolation);
            } catch (...) {
                // A callback owns durable/index projection work and may still
                // encounter a bounded allocation or data-structure failure
                // even after frame validation. Never let an exception escape
                // this reader thread into std::terminate; disconnect so the
                // normal engine retry/degraded-mode path can recover.
                ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::IndexingError,
                                              ffprotocol::DiagnosticCode::FFDIAG_ENGINE_DISCONNECTED,
                                              {}, {}, L"active-frame-dispatch-exception", ERROR_UNHANDLED_EXCEPTION, 0});
                protocolViolation = true;
                ackCv.notify_all();
                break;
            }
            if (protocolViolation.load()) {
                ackCv.notify_all();
                break;
            }
        }
    });
    while (running_ && !idleDropped_ && !protocolViolation.load()) {
        {
            std::lock_guard<std::mutex> lock(ackMutex);
            heartbeatAckPending = true;
        }
        bool sendOk;
        {
            std::lock_guard<std::mutex> lock(writeMutex_);
            sendOk = ffipc::WriteFrameOverlapped(
                pipe, static_cast<uint16_t>(MessageType::Heartbeat), nullptr, 0,
                std::chrono::steady_clock::now() + kHeartbeatAckTimeout);
        }
        if (!sendOk) {
            break;
        }

        std::unique_lock<std::mutex> ackLock(ackMutex);
        const bool signaled = ackCv.wait_for(ackLock, kHeartbeatAckTimeout,
                                              [&] { return !heartbeatAckPending.load() || protocolViolation.load(); });
        const bool ackOk = signaled && !heartbeatAckPending.load() && !protocolViolation.load();
        ackLock.unlock();
        if (!ackOk) {
            // Spec "Heartbeat timeout is treated as disconnection": force
            // close even though the pipe itself may not have errored.
            break;
        }

        std::unique_lock<std::mutex> wakeLock(wakeMutex_);
        wakeCv_.wait_for(wakeLock, kHeartbeatInterval,
                          [this, &protocolViolation] { return wakeRequested_ || !running_ || protocolViolation.load(); });
        wakeRequested_ = false;
    }

    activePipe_ = nullptr;
    // Unblock the reader and any concurrent writer through the same
    // overlapped handle. The reader owns its event/operation state and will
    // return before the pipe handle is finally closed below.
    CancelIoEx(pipe, nullptr);
    reader.join();

    const bool wasIdleDropped = idleDropped_.load();
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        CancelIoEx(pipe, nullptr);
        CloseHandle(pipe);
    }
    if (wasIdleDropped) {
        SetState(ConnectionState::Disconnected, UnavailableReason::IdleDropped);
    } else {
        SetState(ConnectionState::Disconnected, UnavailableReason::ServiceNotRunning);
    }
}

void PrivilegedConnection::RunLoop() {
    std::chrono::milliseconds backoff = kInitialBackoff;

    while (running_) {
        if (idleDropped_) {
            std::unique_lock<std::mutex> lock(wakeMutex_);
            wakeCv_.wait(lock, [this] { return wakeRequested_ || !running_; });
            wakeRequested_ = false;
            continue;
        }

        HANDLE pipe = ConnectVerifyAndHandshake();
        if (pipe == nullptr) {
            if (!running_) {
                break;
            }
            // Task 4.2: exponential backoff, capped, rather than a tight
            // retry loop.
            std::unique_lock<std::mutex> lock(wakeMutex_);
            wakeCv_.wait_for(lock, backoff, [this] { return wakeRequested_ || !running_; });
            const bool wasWoken = wakeRequested_;
            wakeRequested_ = false;
            backoff = wasWoken ? kInitialBackoff : std::min(backoff * 2, kMaxBackoff);
            continue;
        }

        backoff = kInitialBackoff;
        ActiveLoop(pipe); // returns when the connection drops for any reason
    }

    SetState(ConnectionState::Disconnected, UnavailableReason::None);
}

} // namespace ffengine
