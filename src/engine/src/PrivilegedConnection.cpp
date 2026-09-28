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

PrivilegedConnection::OwnedPipe::~OwnedPipe() {
    if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
        CloseHandle(handle);
    }
}

PrivilegedConnection::~PrivilegedConnection() {
    Stop();
    // Closed only after Stop() has joined the lifecycle thread, so the Active
    // reader can never observe a dangling event handle.
    if (shutdownEvent_ != nullptr) {
        CloseHandle(shutdownEvent_);
        shutdownEvent_ = nullptr;
    }
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
    // Manual-reset shutdown event: the Active reader blocks on {frame
    // completion, shutdownEvent} with an INFINITE deadline, so Stop()/
    // DropForIdle() wake it deterministically instead of relying on
    // CancelIoEx -- which is a no-op when the reader is between frames.
    if (shutdownEvent_ == nullptr) {
        shutdownEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    } else {
        ResetEvent(shutdownEvent_);
    }
    running_ = true;
    lifecycleThread_ = std::thread(&PrivilegedConnection::RunLoop, this);
}

void PrivilegedConnection::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    // Wake the Active reader even when no overlapped I/O is pending (it may be
    // between frames inside a batch callback); CancelIoEx below cannot do that,
    // and the reader would otherwise re-arm an unbounded read and hang join().
    if (shutdownEvent_ != nullptr) {
        SetEvent(shutdownEvent_);
    }
    PipePtr connecting;
    PipePtr active;
    {
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        connecting = connectingPipe_;
        active = activePipe_;
    }
    if (connecting && connecting->handle != INVALID_HANDLE_VALUE) {
        CancelIoEx(connecting->handle, nullptr);
    }
    if (active && active->handle != INVALID_HANDLE_VALUE) {
        CancelIoEx(active->handle, nullptr);
    }
    RequestReconnect(); // wake the loop so it observes running_ == false promptly
    if (lifecycleThread_.joinable()) {
        lifecycleThread_.join();
    }
}

void PrivilegedConnection::DropForIdle() {
    idleDropped_ = true;
    // Same reasoning as Stop(): release the Active reader from its INFINITE
    // read so ActiveLoop can tear down and RunLoop can park for reconnect.
    // Without this the lifecycle thread hangs in reader.join() and
    // RequestReconnect() is never observed -- the privileged path stays stuck
    // until process restart.
    if (shutdownEvent_ != nullptr) {
        SetEvent(shutdownEvent_);
    }
    SetState(ConnectionState::Disconnected, UnavailableReason::IdleDropped);
    // Wake the loop WITHOUT clearing idleDropped_: RunLoop must observe the
    // flag and park until RequestReconnect. RequestReconnect() would reset
    // the flag here and instantly undo the idle drop (reconnect churn).
    WakeLoop();
}

void PrivilegedConnection::ResetForScanAuthority() {
    // Do not park like DropForIdle(): this is a correctness recovery that
    // needs a fresh connection-scoped VolumeId as soon as the Active reader
    // has drained. The state callback clears the engine's old sessions before
    // any queued terminal from this pipe can regain authority.
    idleDropped_ = false;
    if (shutdownEvent_ != nullptr) {
        SetEvent(shutdownEvent_);
    }
    SetState(ConnectionState::Disconnected, UnavailableReason::None);
    WakeLoop();
}

void PrivilegedConnection::RequestReconnect() {
    idleDropped_ = false;
    WakeLoop();
}

void PrivilegedConnection::WakeLoop() {
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        wakeRequested_ = true;
    }
    wakeCv_.notify_all();
}

bool PrivilegedConnection::SendRequest(uint16_t messageType, const void* payload, uint32_t payloadSize) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    PipePtr pipe;
    {
        std::lock_guard<std::mutex> stateLock(pipeStateMutex_);
        pipe = activePipe_;
    }
    if (!pipe || pipe->handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    return ffipc::WriteFrameOverlapped(
        pipe->handle, messageType, payload, payloadSize,
        std::chrono::steady_clock::now() + std::chrono::seconds(5));
}

bool PrivilegedConnection::VerifyServiceIdentity(HANDLE pipe) const {
#ifdef FF_PRIVILEGED_CONNECTION_TESTING
    if (serviceIdentityVerifier_) {
        return serviceIdentityVerifier_(pipe);
    }
#endif
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

PrivilegedConnection::PipePtr PrivilegedConnection::ConnectVerifyAndHandshake() {
    SetState(ConnectionState::Connecting, UnavailableReason::None);

#if defined(FF_PRIVILEGED_CONNECTION_TESTING)
    const std::wstring& controlPipeName = controlPipeNameForTesting_.empty()
        ? ffsetup::kCtrlPipeName
        : controlPipeNameForTesting_;
#else
    const std::wstring& controlPipeName = ffsetup::kCtrlPipeName;
#endif

    PipePtr pipe;
    while (running_) {
        const HANDLE rawPipe = CreateFileW(controlPipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                           FILE_FLAG_OVERLAPPED, nullptr);
        if (rawPipe != INVALID_HANDLE_VALUE) {
            pipe = std::make_shared<OwnedPipe>(rawPipe);
            std::lock_guard<std::mutex> lock(pipeStateMutex_);
            connectingPipe_ = pipe;
            break;
        }
        if (GetLastError() != ERROR_PIPE_BUSY) {
            SetState(ConnectionState::Disconnected, UnavailableReason::ServiceNotRunning);
            return nullptr;
        }
        // Bounded by running_ so Stop() can't be blocked indefinitely
        // under sustained pipe contention (unlike every other wait in
        // this class, WaitNamedPipeW itself has no cancellation hook).
        if (!WaitNamedPipeW(controlPipeName.c_str(), 2000)) {
            SetState(ConnectionState::Disconnected, UnavailableReason::ServiceNotRunning);
            return nullptr;
        }
    }
    if (!running_) {
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        if (connectingPipe_ == pipe) connectingPipe_.reset();
        return nullptr;
    }

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe->handle, &mode, nullptr, nullptr);

    // Task 4.4: verify the connected peer really is the genuine,
    // ACL-installed, signed FastFilesIndexSvc.exe before trusting
    // anything it says -- symmetric to the service's own client
    // verification (design.md D4 "Symmetric Mutual Authentication").
    if (!VerifyServiceIdentity(pipe->handle)) {
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        if (connectingPipe_ == pipe) connectingPipe_.reset();
        SetState(ConnectionState::Disconnected, UnavailableReason::UnverifiedServiceIdentity);
        return nullptr;
    }

    SetState(ConnectionState::Handshaking, UnavailableReason::None);

    ffprotocol::HandshakeRequest request{ffprotocol::kCurrentProtocolVersion};
    if (!ffipc::WriteFrameOverlapped(
            pipe->handle, static_cast<uint16_t>(MessageType::Handshake), &request, sizeof(request),
            std::chrono::steady_clock::now() + kHandshakeReplyTimeout)) {
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        if (connectingPipe_ == pipe) connectingPipe_.reset();
        SetState(ConnectionState::Disconnected, UnavailableReason::ServiceNotRunning);
        return nullptr;
    }

    auto reply = ffipc::ReadFrameOverlapped(pipe->handle, std::chrono::steady_clock::now() + kHandshakeReplyTimeout);
    if (!reply) {
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        if (connectingPipe_ == pipe) connectingPipe_.reset();
        SetState(ConnectionState::Disconnected, UnavailableReason::ServiceNotRunning);
        return nullptr;
    }

    switch (static_cast<MessageType>(reply->header.messageType)) {
        case MessageType::HandshakeAck: {
            if (reply->payload.size() != sizeof(ffprotocol::HandshakeAckPayload)) {
                std::lock_guard<std::mutex> lock(pipeStateMutex_);
                if (connectingPipe_ == pipe) connectingPipe_.reset();
                SetState(ConnectionState::Disconnected, UnavailableReason::HandshakeRejected);
                return nullptr;
            }
            ffprotocol::HandshakeAckPayload ack{};
            std::memcpy(&ack, reply->payload.data(), sizeof(ack));
            if (!ffprotocol::IsVersionCompatible(ffprotocol::kCurrentProtocolVersion, ack.negotiatedVersion)
                || ack.negotiatedVersion.major > ffprotocol::kCurrentProtocolVersion.major) {
                std::lock_guard<std::mutex> lock(pipeStateMutex_);
                if (connectingPipe_ == pipe) connectingPipe_.reset();
                SetState(ConnectionState::Disconnected, UnavailableReason::IncompatibleProtocolVersion);
                return nullptr;
            }
            negotiatedMajor_ = ack.negotiatedVersion.major;
            negotiatedMinor_ = ack.negotiatedVersion.minor;
            {
                std::lock_guard<std::mutex> lock(pipeStateMutex_);
                if (connectingPipe_ == pipe) connectingPipe_.reset();
            }
            return pipe;
        }

        case MessageType::IncompatibleVersion:
            // Task 4.9: treat as privileged-path-unavailable, not a
            // transient error to keep retrying at the same cadence --
            // the service self-heals its own build staleness (task 3.9),
            // so this condition is expected to resolve on its own.
            {
                std::lock_guard<std::mutex> lock(pipeStateMutex_);
                if (connectingPipe_ == pipe) connectingPipe_.reset();
            }
            SetState(ConnectionState::Disconnected, UnavailableReason::IncompatibleProtocolVersion);
            return nullptr;

        case MessageType::HandshakeReject:
        default:
            {
                std::lock_guard<std::mutex> lock(pipeStateMutex_);
                if (connectingPipe_ == pipe) connectingPipe_.reset();
            }
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

        case MessageType::ScanRecordBatchV2: {
            // allocated-size-storage-accuracy 5.1: additive MFT record
            // layout with allocated size; distinct message type plus an
            // explicit recordFormatVersion so a V1 payload can never be
            // misinterpreted as the larger fixed records.
            if (frame.payload.size() < sizeof(ffprotocol::ScanRecordBatchV2Header)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::ScanRecordBatchV2Header header{};
            std::memcpy(&header, frame.payload.data(), sizeof(header));
            if (header.recordFormatVersion != 2) {
                protocolViolation = true;
                break;
            }
            const size_t afterHeader = sizeof(header);
            if (frame.payload.size() < afterHeader + header.resumeCursorLengthBytes) {
                protocolViolation = true;
                break;
            }
            std::vector<uint8_t> cursor(
                frame.payload.begin() + afterHeader, frame.payload.begin() + afterHeader + header.resumeCursorLengthBytes);
            const size_t recordsOffset = afterHeader + header.resumeCursorLengthBytes;
            auto records = ffprotocol::ParseMftBatchV2(
                frame.payload.data() + recordsOffset, frame.payload.size() - recordsOffset, header.recordCount);
            if (!records) {
                protocolViolation = true;
                break;
            }
            if (scanBatchV2Callback_) {
                scanBatchV2Callback_(header.volumeId, std::move(cursor), std::move(*records));
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

        case MessageType::ScanFailed: {
            // index-durability-false-success-closure D1: exact-length check
            // mirrors ScanComplete -- a re-sized frame is a violation, never
            // a silently-tolerated variant.
            if (frame.payload.size() != sizeof(ffprotocol::ScanFailedPayload)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::ScanFailedPayload payload{};
            std::memcpy(&payload, frame.payload.data(), sizeof(payload));
            if (scanFailedCallback_) {
                scanFailedCallback_(payload.volumeId);
            }
            break;
        }

        case MessageType::ScanAttemptStarted: {
            // scan-generation-integrity-and-residual-certification-closure
            // 3.4: exact-length and reply-only. A longer payload is never an
            // attempt discriminator extension; it is a protocol violation.
            if (frame.payload.size() != sizeof(ffprotocol::ScanAttemptStartedPayload)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::ScanAttemptStartedPayload payload{};
            std::memcpy(&payload, frame.payload.data(), sizeof(payload));
            if (scanAttemptStartedCallback_) {
                scanAttemptStartedCallback_(payload.volumeId);
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
                std::vector<ffprotocol::UsnDeltaV2> upgraded;
                upgraded.reserve(records->size());
                for (const auto& record : *records) {
                    upgraded.push_back(ffprotocol::UpgradeUsnDeltaV1(record));
                }
                journalBatchCallback_(header.volumeId, header.latestUsn, std::move(upgraded));
            }
            break;
        }

        case MessageType::JournalRecordBatchV2: {
            if (frame.payload.size() < sizeof(ffprotocol::JournalRecordBatchV2Header)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::JournalRecordBatchV2Header header{};
            std::memcpy(&header, frame.payload.data(), sizeof(header));
            if (header.recordFormatVersion != 2) {
                protocolViolation = true;
                break;
            }
            const size_t recordsOffset = sizeof(header);
            auto records = ffprotocol::ParseUsnDeltaV2Batch(
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

        case MessageType::JournalRecordBatchV3: {
            // allocated-size-storage-accuracy 5.1: additive USN V3 layout
            // (V2 + allocated-size presence bit).
            if (frame.payload.size() < sizeof(ffprotocol::JournalRecordBatchV3Header)) {
                protocolViolation = true;
                break;
            }
            ffprotocol::JournalRecordBatchV3Header header{};
            std::memcpy(&header, frame.payload.data(), sizeof(header));
            if (header.recordFormatVersion != 3) {
                protocolViolation = true;
                break;
            }
            const size_t recordsOffset = sizeof(header);
            auto records = ffprotocol::ParseUsnDeltaV3Batch(
                frame.payload.data() + recordsOffset, frame.payload.size() - recordsOffset, header.recordCount);
            if (!records) {
                protocolViolation = true;
                break;
            }
            if (journalBatchV3Callback_) {
                journalBatchV3Callback_(header.volumeId, header.latestUsn, std::move(*records));
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

#ifdef FF_PRIVILEGED_CONNECTION_TESTING
bool PrivilegedConnection::DispatchFrameForTesting(const ffipc::ReceivedFrame& frame) {
    std::atomic<bool> heartbeatAckPending{false};
    std::mutex ackMutex;
    std::condition_variable ackCv;
    std::atomic<bool> protocolViolation{false};
    DispatchReceivedFrame(frame, heartbeatAckPending, ackMutex, ackCv, protocolViolation);
    return !protocolViolation.load();
}
#endif

void PrivilegedConnection::ActiveLoop(const PipePtr& pipe) {
    // State observers synchronously send their initial EnumerateVolumes
    // request when they see Active, so publish the verified pipe first.
    // Reversing these two operations drops that first request because
    // SendRequest observes a null activePipe_.
    {
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        activePipe_ = pipe;
    }
    // A previous DropForIdle()/reconnect cycle leaves the manual-reset event
    // signaled; clear it so this Active lifetime starts unsignaled. A Stop()
    // racing this reset is covered by the running_ gate in the reader loop.
    const HANDLE shutdownEvent = shutdownEvent_;
    if (shutdownEvent != nullptr) {
        ResetEvent(shutdownEvent);
    }
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
            // Re-arm gate: never issue a fresh read once teardown has begun.
            // Without this, a reader that finishes a dispatch callback after
            // the main loop exited would re-issue an unbounded read against a
            // still-open, idle peer and deadlock the CancelIoEx/join below.
            if (!running_.load(std::memory_order_acquire) || idleDropped_.load(std::memory_order_acquire)) {
                break;
            }
            // Shutdown-aware blocking: wait INFINITE for either frame
            // completion or shutdownEvent. CancelIoEx alone cannot release a
            // reader that is between frames.
            ffipc::ReceivedFrame frameOut;
            const ffipc::FrameReadStatus status = ffipc::ReadFrameOverlappedDetailed(
                pipe->handle, shutdownEvent, std::chrono::steady_clock::time_point::max(), &frameOut);
            if (status != ffipc::FrameReadStatus::Success) {
                // Cancelled/Disconnected/IoError all end this Active lifetime.
                // Timeout is impossible with an INFINITE deadline; treat it
                // defensively as a disconnect rather than silently re-arming.
                protocolViolation = true; // read failure/disconnect -- reuse the same teardown path
                ackCv.notify_all();
                break;
            }
            try {
                DispatchReceivedFrame(frameOut, heartbeatAckPending, ackMutex, ackCv, protocolViolation);
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
                pipe->handle, static_cast<uint16_t>(MessageType::Heartbeat), nullptr, 0,
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

        // harden-ipc-trust-boundary-gaps D2: D4 requires symmetric mutual
        // authentication to be re-checked periodically on long-lived
        // connections. The engine re-runs the service image-path + pin
        // verification on the heartbeat cadence; a failure is treated exactly
        // like a heartbeat timeout (off Active into degraded mode).
        if (!VerifyServiceIdentity(pipe->handle)) {
            ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                          ffprotocol::DiagnosticCode::FFDIAG_ENGINE_DISCONNECTED,
                                          {}, {}, L"heartbeat-service-identity-revalidation", ERROR_INVALID_DATA, 0});
            break;
        }

        std::unique_lock<std::mutex> wakeLock(wakeMutex_);
        wakeCv_.wait_for(wakeLock, kHeartbeatInterval,
                          [this, &protocolViolation] { return wakeRequested_ || !running_ || protocolViolation.load(); });
        wakeRequested_ = false;
    }

    {
        std::lock_guard<std::mutex> lock(pipeStateMutex_);
        if (activePipe_ == pipe) activePipe_.reset();
    }
    // Unblock the reader and any concurrent writer through the same
    // overlapped handle. The reader owns its event/operation state and will
    // return before the pipe handle is finally closed below. Signaling the
    // shutdown event as well covers every exit reason from the loop above --
    // including a heartbeat timeout, where running_ is still true and
    // CancelIoEx finds no pending I/O because the reader is between frames.
    CancelIoEx(pipe->handle, nullptr);
    if (shutdownEvent != nullptr) {
        SetEvent(shutdownEvent);
    }
    reader.join();

    const bool wasIdleDropped = idleDropped_.load();
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        CancelIoEx(pipe->handle, nullptr);
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

        PipePtr pipe = ConnectVerifyAndHandshake();
        if (!pipe) {
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
