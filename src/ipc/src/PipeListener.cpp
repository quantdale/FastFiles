#include "ffipc/PipeListener.h"

#include <algorithm>
#include <cstdio>

namespace ffipc {

namespace {
constexpr DWORD kPipeBufferSize = 64 * 1024;
}

PipeListener::~PipeListener() {
    Stop();
}

HANDLE PipeListener::CreateInstance(bool isFirstInstance) noexcept {
    const DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED
        | (isFirstInstance ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);

    return CreateNamedPipeW(
        pipeName_.c_str(), openMode,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, kPipeBufferSize, kPipeBufferSize, 0,
        const_cast<SECURITY_ATTRIBUTES*>(securityAttributes_));
}

bool PipeListener::Start(const std::wstring& pipeName, const SECURITY_ATTRIBUTES* securityAttributes,
                         std::function<void(HANDLE)> onConnect) {
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

    running_ = true;

    acceptThread_ = std::thread([this, firstInstance] {
        HANDLE currentInstance = firstInstance;

        // Every path through this loop either transfers currentInstance's
        // ownership to onConnect_'s thread, or closes it before the loop is
        // left. Overlapped ConnectNamedPipe is event-driven and is cancelled
        // directly by Stop() through pendingInstance_.
        for (;;) {
            if (!running_) {
                CloseHandle(currentInstance);
                break;
            }

            OVERLAPPED connectOverlapped{};
            HANDLE connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (connectEvent == nullptr) {
                CloseHandle(currentInstance);
                break;
            }
            connectOverlapped.hEvent = connectEvent;

            pendingInstance_.store(currentInstance, std::memory_order_release);
            BOOL connected = ConnectNamedPipe(currentInstance, &connectOverlapped);
            DWORD error = connected ? ERROR_SUCCESS : GetLastError();
            if (!connected && error == ERROR_IO_PENDING) {
                const DWORD waitResult = WaitForSingleObject(connectEvent, INFINITE);
                if (waitResult == WAIT_OBJECT_0) {
                    DWORD ignored = 0;
                    if (GetOverlappedResult(currentInstance, &connectOverlapped, &ignored, FALSE)) {
                        connected = TRUE;
                        error = ERROR_SUCCESS;
                    } else {
                        error = GetLastError();
                    }
                } else {
                    error = ERROR_OPERATION_ABORTED;
                }
            }
            pendingInstance_.store(INVALID_HANDLE_VALUE, std::memory_order_release);
            if (connectEvent != nullptr) {
                CloseHandle(connectEvent);
            }

            if (!running_) {
                CloseHandle(currentInstance);
                break;
            }

            if (!connected && error != ERROR_PIPE_CONNECTED) {
                // Unexpected failure on this instance; drop it and try a
                // fresh one rather than tearing down the whole listener.
                CloseHandle(currentInstance);
                currentInstance = CreateInstance(/*isFirstInstance=*/false);
                if (currentInstance == INVALID_HANDLE_VALUE) {
                    break;
                }
                continue;
            }

            HANDLE acceptedConnection = currentInstance;
            HANDLE cancellationHandle = INVALID_HANDLE_VALUE;
            if (!DuplicateHandle(GetCurrentProcess(), acceptedConnection, GetCurrentProcess(),
                                 &cancellationHandle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                // Do not hand a live connection to an untracked callback if
                // the listener cannot retain an independent cancellation
                // handle for its shutdown barrier.
                CloseHandle(acceptedConnection);
                currentInstance = CreateInstance(/*isFirstInstance=*/false);
                if (currentInstance == INVALID_HANDLE_VALUE) {
                    break;
                }
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(connectionMutex_);
                connectionHandles_.push_back(cancellationHandle);
                connectionThreads_.emplace_back([this, acceptedConnection, cancellationHandle] {
                    onConnect_(acceptedConnection);
                    CloseHandle(cancellationHandle);
                    std::lock_guard<std::mutex> lock(connectionMutex_);
                    connectionHandles_.erase(
                        std::remove(connectionHandles_.begin(), connectionHandles_.end(), cancellationHandle),
                        connectionHandles_.end());
                });
            }

            currentInstance = CreateInstance(/*isFirstInstance=*/false);
            if (currentInstance == INVALID_HANDLE_VALUE) {
                break;
            }
        }
    });

    return true;
}

void PipeListener::JoinConnectionThreads() {
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        threads = std::move(connectionThreads_);
        connectionThreads_.clear();
    }
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void PipeListener::Stop() {
    const bool wasRunning = running_.exchange(false);
    if (!wasRunning) {
        JoinConnectionThreads();
        return;
    }

    const HANDLE pending = pendingInstance_.load(std::memory_order_acquire);
    if (pending != INVALID_HANDLE_VALUE) {
        CancelIoEx(pending, nullptr);
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    // Accepted product connections are all overlapped. Cancel each tracked
    // handle before joining its callback; this makes listener Stop a real
    // teardown barrier with no detached connection threads.
    {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        for (const HANDLE pipe : connectionHandles_) {
            if (pipe != INVALID_HANDLE_VALUE) {
                CancelIoEx(pipe, nullptr);
            }
        }
    }
    JoinConnectionThreads();
}

} // namespace ffipc
