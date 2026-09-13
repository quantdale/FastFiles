#include "ffipc/PipeListener.h"

#include <cstdio>
#include <memory>

namespace ffipc {

namespace {
constexpr DWORD kPipeBufferSize = 64 * 1024;
}

PipeListener::OwnedHandle::~OwnedHandle() {
    if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr) {
        CloseHandle(value_);
    }
}

HANDLE PipeListener::OwnedHandle::Release() noexcept {
    const HANDLE value = value_;
    value_ = INVALID_HANDLE_VALUE;
    return value;
}

PipeListener::~PipeListener() {
    Stop();
    // Closed only after Stop() has joined every connection callback, so no
    // reader can observe a dangling event handle.
    if (shutdownEvent_ != nullptr) {
        CloseHandle(shutdownEvent_);
        shutdownEvent_ = nullptr;
    }
}

HANDLE PipeListener::CreateInstance(bool isFirstInstance) noexcept {
    const DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED
        | (isFirstInstance ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);

    // harden-ipc-trust-boundary-gaps D1: every instance rejects remote SMB
    // clients at the kernel layer, so both the service control pipe and the
    // per-session UI pipe inherit the flag. No silent retry without it.
    constexpr DWORD kInstanceMode =
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
    lastCreateMode_.store(kInstanceMode, std::memory_order_relaxed);
    return CreateNamedPipeW(
        pipeName_.c_str(), openMode, kInstanceMode,
        PIPE_UNLIMITED_INSTANCES, kPipeBufferSize, kPipeBufferSize, 0,
        const_cast<SECURITY_ATTRIBUTES*>(securityAttributes_));
}

bool PipeListener::Start(const std::wstring& pipeName, const SECURITY_ATTRIBUTES* securityAttributes,
                         std::function<void(HANDLE)> onConnect) {
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
    if (running_.load(std::memory_order_acquire) || acceptThread_.joinable()) {
        return false;
    }
    pipeName_ = pipeName;
    securityAttributes_ = securityAttributes;
    onConnect_ = std::move(onConnect);
    HANDLE firstInstance = CreateInstance(/*isFirstInstance=*/true);
    if (firstInstance == INVALID_HANDLE_VALUE) {
        // Spec "Pipe-name collision fails loudly": most commonly
        // ERROR_ACCESS_DENIED because some other process already created
        // a pipe of this exact name. Refuse to proceed rather than
        // silently operating without the expected pipe.
        std::fwprintf(stderr, L"FastFiles: CreateNamedPipe failed for %ls (error %lu) -- refusing to start\n",
                      pipeName_.c_str(), GetLastError());
        return false;
    }

    // Created before the accept thread starts, so a connection callback can
    // read ShutdownEvent() as soon as it is invoked. Reset on restart, since a
    // previous Stop() leaves it signaled.
    if (shutdownEvent_ == nullptr) {
        shutdownEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    } else {
        ResetEvent(shutdownEvent_);
    }

    running_ = true;

    acceptThread_ = std::thread([this, firstInstance] {
        OwnedHandlePtr currentInstance = std::make_shared<OwnedHandle>(firstInstance);

        // Every path through this loop either transfers currentInstance's
        // ownership to onConnect_'s thread, or closes it before the loop is
        // left. Overlapped ConnectNamedPipe is event-driven and is cancelled
        // directly by Stop() through the shared pending control object.
        for (;;) {
            if (!running_) {
                break;
            }

            OVERLAPPED connectOverlapped{};
            HANDLE connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (connectEvent == nullptr) {
                break;
            }
            connectOverlapped.hEvent = connectEvent;

            BOOL connected = FALSE;
            DWORD error = ERROR_OPERATION_ABORTED;
            bool initiated = false;
            {
                // Stop() sets running_ before it takes pendingMutex_. Keep
                // this short critical section through operation initiation:
                // if Stop() wins first, no overlapped operation is started;
                // if the accept thread wins the lock, Stop() waits until
                // ConnectNamedPipe has published the pending operation and
                // can then cancel it. Publishing the handle before the
                // initiation without this handshake leaves a cancellation
                // window in which Stop() can join an uncancellable wait.
                std::lock_guard<std::mutex> lock(pendingMutex_);
                if (running_) {
                    pendingInstance_ = currentInstance;
                    connected = ConnectNamedPipe(currentInstance->Get(), &connectOverlapped);
                    error = connected ? ERROR_SUCCESS : GetLastError();
                    initiated = true;
                }
            }
            if (!initiated) {
                CloseHandle(connectEvent);
                break;
            }
            if (!connected && error == ERROR_IO_PENDING) {
                const DWORD waitResult = WaitForSingleObject(connectEvent, INFINITE);
                if (waitResult == WAIT_OBJECT_0) {
                    DWORD ignored = 0;
                    if (GetOverlappedResult(currentInstance->Get(), &connectOverlapped, &ignored, FALSE)) {
                        connected = TRUE;
                        error = ERROR_SUCCESS;
                    } else {
                        error = GetLastError();
                    }
                } else {
                    error = ERROR_OPERATION_ABORTED;
                }
            }
            {
                std::lock_guard<std::mutex> lock(pendingMutex_);
                if (pendingInstance_ == currentInstance) {
                    pendingInstance_.reset();
                }
            }
            if (connectEvent != nullptr) {
                CloseHandle(connectEvent);
            }

            if (!running_) {
                break;
            }

            if (!connected && error != ERROR_PIPE_CONNECTED) {
                // Unexpected failure on this instance; drop it and try a
                // fresh one rather than tearing down the whole listener.
                const HANDLE replacement = CreateInstance(/*isFirstInstance=*/false);
                if (replacement == INVALID_HANDLE_VALUE) {
                    break;
                }
                currentInstance = std::make_shared<OwnedHandle>(replacement);
                continue;
            }

            const HANDLE acceptedConnection = currentInstance->Release();
            HANDLE cancellationHandle = INVALID_HANDLE_VALUE;
            if (!DuplicateHandle(GetCurrentProcess(), acceptedConnection, GetCurrentProcess(),
                                 &cancellationHandle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                // Do not hand a live connection to an untracked callback if
                // the listener cannot retain an independent cancellation
                // handle for its shutdown barrier.
                CloseHandle(acceptedConnection);
                const HANDLE replacement = CreateInstance(/*isFirstInstance=*/false);
                if (replacement == INVALID_HANDLE_VALUE) {
                    break;
                }
                currentInstance = std::make_shared<OwnedHandle>(replacement);
                continue;
            }
            const OwnedHandlePtr cancellation = std::make_shared<OwnedHandle>(cancellationHandle);
            // Reap threads whose connection callbacks already returned so the
            // registry holds only live connections plus at most the new one
            // (bounded by concurrent clients, not by total history).
            ReapFinishedConnectionThreads();
            {
                std::lock_guard<std::mutex> lock(connectionMutex_);
                connectionHandles_.push_back(cancellation);
                auto finished = std::make_shared<std::atomic<bool>>(false);
                connectionThreads_.push_back(ConnectionThread{
                    std::thread([this, acceptedConnection, cancellation, finished] {
                        onConnect_(acceptedConnection);
                        {
                            std::lock_guard<std::mutex> lock(connectionMutex_);
                            for (auto it = connectionHandles_.begin(); it != connectionHandles_.end(); ++it) {
                                if (*it == cancellation) {
                                    connectionHandles_.erase(it);
                                    break;
                                }
                            }
                        }
                        // Published after the handle deregistration: a reaper
                        // that observes this flag is guaranteed to join a
                        // thread whose shared state is quiescent.
                        finished->store(true, std::memory_order_release);
                    }),
                    finished});
            }

            const HANDLE replacement = CreateInstance(/*isFirstInstance=*/false);
            if (replacement == INVALID_HANDLE_VALUE) {
                break;
            }
            currentInstance = std::make_shared<OwnedHandle>(replacement);
        }
    });

    return true;
}

void PipeListener::ReapFinishedConnectionThreads() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (auto it = connectionThreads_.begin(); it != connectionThreads_.end();) {
        if (it->finished->load(std::memory_order_acquire)) {
            if (it->thread.joinable()) {
                it->thread.join();
            }
            it = connectionThreads_.erase(it);
        } else {
            ++it;
        }
    }
}

void PipeListener::JoinConnectionThreads() {
    std::vector<ConnectionThread> threads;
    {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        threads = std::move(connectionThreads_);
        connectionThreads_.clear();
    }
    for (auto& thread : threads) {
        if (thread.thread.joinable()) {
            thread.thread.join();
        }
    }
}

void PipeListener::Stop() {
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
    const bool wasRunning = running_.exchange(false);
    // Wake every connection callback's reader even when it has no overlapped
    // I/O pending (it may be parked between frames, e.g. inside a durable
    // scan/journal step). The CancelIoEx sweep below cannot do that, and such
    // a reader would re-arm a fresh bounded read and stall the
    // JoinConnectionThreads() below for up to that whole deadline.
    if (shutdownEvent_ != nullptr) {
        SetEvent(shutdownEvent_);
    }
    OwnedHandlePtr pending;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending = pendingInstance_;
    }
    if (pending && pending->Get() != INVALID_HANDLE_VALUE) {
        CancelIoEx(pending->Get(), nullptr);
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    // Accepted product connections are all overlapped. Copy the shared
    // cancellation owners before cancelling and joining callbacks. The
    // registry contains owners, never borrow-only numeric HANDLE values, so
    // a callback cannot close a value while Stop() can still legally cancel it.
    std::vector<OwnedHandlePtr> connections;
    {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        connections = connectionHandles_;
    }
    for (const auto& connection : connections) {
        if (connection && connection->Get() != INVALID_HANDLE_VALUE) {
            CancelIoEx(connection->Get(), nullptr);
        }
    }
    JoinConnectionThreads();
    (void)wasRunning;
}

} // namespace ffipc
