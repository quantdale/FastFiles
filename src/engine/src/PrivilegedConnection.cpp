#include "PrivilegedConnection.h"

#include <windows.h>

#include <algorithm>
#include <iterator>

#include "ffprotocol/Commands.h"
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
// (spec "Heartbeat timeout is treated as disconnection").
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
        case MessageType::HandshakeAck:
            return pipe;

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

bool PrivilegedConnection::SendCommand(uint16_t messageType, const void* payload, uint32_t payloadSize) {
    std::lock_guard<std::mutex> lock(pipeMutex_);
    if (activePipe_ == nullptr) {
        return false;
    }
    return ffipc::WriteFrame(activePipe_, messageType, payload, payloadSize);
}

bool PrivilegedConnection::SendCommand(uint16_t messageType) {
    std::lock_guard<std::mutex> lock(pipeMutex_);
    if (activePipe_ == nullptr) {
        return false;
    }
    return ffipc::WriteFrame(activePipe_, messageType);
}

void PrivilegedConnection::ActiveLoop(HANDLE pipe) {
    {
        std::lock_guard<std::mutex> lock(pipeMutex_);
        activePipe_ = pipe;
    }
    SetState(ConnectionState::Active, UnavailableReason::None);

    // index-storage-and-scanning: the service now pushes ScanBatch/
    // UsnBatch/etc. asynchronously on this same connection (not only
    // heartbeat replies), so a single blocking-read-with-timeout per
    // heartbeat is no longer sufficient -- a dedicated reader thread reads
    // continuously and dispatches, while this thread just paces
    // heartbeats and watches for a liveness timeout.
    std::atomic<bool> readerAlive{true};
    std::atomic<int64_t> lastTrafficMs{
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()};

    std::thread reader([&] {
        while (true) {
            auto frame = ffipc::ReadFrame(pipe);
            if (!frame) {
                break;
            }
            lastTrafficMs.store(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
            if (frame->header.messageType == static_cast<uint16_t>(MessageType::HeartbeatAck)) {
                continue; // consumed purely as a liveness signal
            }
            if (frameHandler_) {
                frameHandler_(frame->header.messageType, frame->payload);
            }
        }
        readerAlive = false;
        {
            std::lock_guard<std::mutex> lock(wakeMutex_);
            wakeRequested_ = true;
        }
        wakeCv_.notify_all(); // wake the pacing loop below promptly on disconnect
    });

    while (running_ && !idleDropped_ && readerAlive.load()) {
        if (!SendCommand(static_cast<uint16_t>(MessageType::Heartbeat))) {
            break;
        }

        std::unique_lock<std::mutex> lock(wakeMutex_);
        wakeCv_.wait_for(lock, kHeartbeatInterval, [this, &readerAlive] { return wakeRequested_ || !running_ || !readerAlive.load(); });
        wakeRequested_ = false;
        lock.unlock();

        if (!readerAlive.load()) {
            break;
        }
        const auto nowMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (nowMs - lastTrafficMs.load() > kHeartbeatAckTimeout.count()) {
            // Spec "Heartbeat timeout is treated as disconnection": no
            // traffic of any kind (ack or pushed batch) since well past
            // the ack timeout means the peer is unresponsive even though
            // the pipe itself may not have errored.
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(pipeMutex_);
        activePipe_ = nullptr; // no SendCommand can reach this handle from here on
    }
    // Unblock the reader thread's pending ReadFrame so it observes the
    // disconnect promptly rather than only whenever the peer next writes.
    CancelSynchronousIo(reader.native_handle());
    reader.join();

    CloseHandle(pipe);
    if (idleDropped_) {
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
