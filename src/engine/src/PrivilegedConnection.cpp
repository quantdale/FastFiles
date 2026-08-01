#include "PrivilegedConnection.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <iterator>

#include "ffprotocol/Version.h"
#include "ffsetup/AuthenticodeVerification.h"
#include "ffsetup/Identifiers.h"
#include "ffsetup/PinnedSignatures.h"

namespace ffengine {

namespace {

using ffprotocol::MessageType;

constexpr std::chrono::milliseconds kHandshakeReplyTimeout{5000};
constexpr std::chrono::milliseconds kHeartbeatInterval{20000};
constexpr std::chrono::milliseconds kHeartbeatAckTimeout{10000};
constexpr std::chrono::milliseconds kInitialBackoff{1000};
constexpr std::chrono::milliseconds kMaxBackoff{60000};

// Reads one frame with a bounded wait: spawns a reader thread for the
// single blocking ReadFile, and forcibly cancels it via
// CancelSynchronousIo if the timeout elapses before the read completes --
// so a deadlocked-but-not-broken pipe can't wedge the caller forever
// (spec "Heartbeat timeout is treated as disconnection"). Used only for
// the pre-Active handshake exchange; the Active state's own reader thread
// (see ActiveLoop) handles everything once the connection is up.
std::optional<ffipc::ReceivedFrame> ReadFrameWithTimeout(HANDLE pipe, std::chrono::milliseconds timeout) {
    std::optional<ffipc::ReceivedFrame> result;
    std::atomic<bool> done{false};
    HANDLE readerThreadHandle = nullptr;
    HANDLE readyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    std::thread reader([&] {
        result = ffipc::ReadFrame(pipe);
        done = true;
        SetEvent(readyEvent);
    });
    readerThreadHandle = reader.native_handle();

    const DWORD waitResult = WaitForSingleObject(readyEvent, static_cast<DWORD>(timeout.count()));
    if (waitResult != WAIT_OBJECT_0) {
        CancelSynchronousIo(readerThreadHandle);
    }
    reader.join();
    CloseHandle(readyEvent);

    return done ? result : std::nullopt;
}

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
    return ffipc::WriteFrame(pipe, messageType, payload, payloadSize);
}

bool PrivilegedConnection::VerifyServiceIdentity(HANDLE pipe) const {
    ULONG serverPid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &serverPid)) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, serverPid);
    if (process == nullptr) {
        return false;
    }

    auto imagePath = GetProcessImagePath(process);
    CloseHandle(process);
    if (!imagePath) {
        return false;
    }

    wchar_t canonicalPath[MAX_PATH * 4];
    wchar_t canonicalInstallDir[MAX_PATH * 4];
    if (GetFullPathNameW(imagePath->c_str(), static_cast<DWORD>(std::size(canonicalPath)), canonicalPath, nullptr) == 0
        || GetFullPathNameW(installDir_.c_str(), static_cast<DWORD>(std::size(canonicalInstallDir)), canonicalInstallDir, nullptr) == 0) {
        return false;
    }

    std::wstring fullPath(canonicalPath);
    std::wstring dir(canonicalInstallDir);
    if (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) {
        dir.pop_back();
    }
    const size_t lastSlash = fullPath.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) {
        return false;
    }
    const std::wstring fileDir = fullPath.substr(0, lastSlash);
    const std::wstring fileName = fullPath.substr(lastSlash + 1);
    if (_wcsicmp(fileDir.c_str(), dir.c_str()) != 0 || _wcsicmp(fileName.c_str(), ffsetup::kIndexSvcExeName) != 0) {
        return false;
    }

    return ffsetup::VerifyPinnedSignature(*imagePath, ffsetup::kExpectedIndexSvcSignatureThumbprint);
}

HANDLE PrivilegedConnection::ConnectVerifyAndHandshake() {
    SetState(ConnectionState::Connecting, UnavailableReason::None);

    HANDLE pipe = INVALID_HANDLE_VALUE;
    while (running_) {
        pipe = CreateFileW(ffsetup::kCtrlPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
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
        return nullptr;
    }

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    // Task 4.4: verify the connected peer really is the genuine,
    // ACL-installed, signed FastFilesIndexSvc.exe before trusting
    // anything it says -- symmetric to the service's own client
    // verification (design.md D4 "Symmetric Mutual Authentication").
    if (!VerifyServiceIdentity(pipe)) {
        CloseHandle(pipe);
        SetState(ConnectionState::Disconnected, UnavailableReason::UnverifiedServiceIdentity);
        return nullptr;
    }

    SetState(ConnectionState::Handshaking, UnavailableReason::None);

    ffprotocol::HandshakeRequest request{ffprotocol::kCurrentProtocolVersion};
    if (!ffipc::WriteFrame(pipe, static_cast<uint16_t>(MessageType::Handshake), &request, sizeof(request))) {
        CloseHandle(pipe);
        SetState(ConnectionState::Disconnected, UnavailableReason::ServiceNotRunning);
        return nullptr;
    }

    auto reply = ReadFrameWithTimeout(pipe, kHandshakeReplyTimeout);
    if (!reply) {
        CloseHandle(pipe);
        SetState(ConnectionState::Disconnected, UnavailableReason::ServiceNotRunning);
        return nullptr;
    }

    switch (static_cast<MessageType>(reply->header.messageType)) {
        case MessageType::HandshakeAck: {
            if (reply->payload.size() != sizeof(ffprotocol::HandshakeAckPayload)) {
                CloseHandle(pipe);
                SetState(ConnectionState::Disconnected, UnavailableReason::HandshakeRejected);
                return nullptr;
            }
            ffprotocol::HandshakeAckPayload ack{};
            std::memcpy(&ack, reply->payload.data(), sizeof(ack));
            if (!ffprotocol::IsVersionCompatible(ffprotocol::kCurrentProtocolVersion, ack.negotiatedVersion)
                || ack.negotiatedVersion.major > ffprotocol::kCurrentProtocolVersion.major) {
                CloseHandle(pipe);
                SetState(ConnectionState::Disconnected, UnavailableReason::IncompatibleProtocolVersion);
                return nullptr;
            }
            negotiatedMajor_ = ack.negotiatedVersion.major;
            negotiatedMinor_ = ack.negotiatedVersion.minor;
            return pipe;
        }

        case MessageType::IncompatibleVersion:
            // Task 4.9: treat as privileged-path-unavailable, not a
            // transient error to keep retrying at the same cadence --
            // the service self-heals its own build staleness (task 3.9),
            // so this condition is expected to resolve on its own.
            CloseHandle(pipe);
            SetState(ConnectionState::Disconnected, UnavailableReason::IncompatibleProtocolVersion);
            return nullptr;

        case MessageType::HandshakeReject:
        default:
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

    // A single reader thread owns every ReadFrame call on this connection
    // for its Active lifetime -- HeartbeatAck feeds the heartbeat-wait
    // below, every other message type is dispatched to whichever
    // scan/journal callback applies (index-storage-and-scanning tasks.md
    // 6.1). This is what lets scan/journal batches arrive asynchronously,
    // interleaved with the heartbeat cadence, rather than only in
    // lockstep reply to a request.
    std::thread reader([&] {
        for (;;) {
            auto frame = ffipc::ReadFrame(pipe);
            if (!frame) {
                protocolViolation = true; // read failure/disconnect -- reuse the same teardown path
                ackCv.notify_all();
                break;
            }
            DispatchReceivedFrame(*frame, heartbeatAckPending, ackMutex, ackCv, protocolViolation);
            if (protocolViolation.load()) {
                ackCv.notify_all();
                break;
            }
        }
    });
    HANDLE readerNativeHandle = reader.native_handle();

    while (running_ && !idleDropped_ && !protocolViolation.load()) {
        {
            std::lock_guard<std::mutex> lock(ackMutex);
            heartbeatAckPending = true;
        }
        bool sendOk;
        {
            std::lock_guard<std::mutex> lock(writeMutex_);
            sendOk = ffipc::WriteFrame(pipe, static_cast<uint16_t>(MessageType::Heartbeat));
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
    // Unblock the reader thread's in-flight ReadFile (it has no other
    // cancellation hook) before joining it.
    CancelSynchronousIo(readerNativeHandle);
    reader.join();

    const bool wasIdleDropped = idleDropped_.load();
    CloseHandle(pipe);
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
