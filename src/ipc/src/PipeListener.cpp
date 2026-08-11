#include "ffipc/PipeListener.h"

#include <cstdio>

namespace ffipc {

namespace {
constexpr DWORD kPipeBufferSize = 64 * 1024;
}

PipeListener::~PipeListener() {
    Stop();
}

HANDLE PipeListener::CreateInstance(bool isFirstInstance) noexcept {
    const DWORD openMode = PIPE_ACCESS_DUPLEX
        | (useOverlapped_ ? FILE_FLAG_OVERLAPPED : 0)
        | (isFirstInstance ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);

    return CreateNamedPipeW(
        pipeName_.c_str(), openMode,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, kPipeBufferSize, kPipeBufferSize, 0,
        const_cast<SECURITY_ATTRIBUTES*>(securityAttributes_));
}

bool PipeListener::Start(const std::wstring& pipeName, const SECURITY_ATTRIBUTES* securityAttributes,
                          std::function<void(HANDLE)> onConnect, const bool useOverlapped) {
    pipeName_ = pipeName;
    securityAttributes_ = securityAttributes;
    onConnect_ = std::move(onConnect);
    useOverlapped_ = useOverlapped;

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
        // ownership to onConnect_'s thread, or closes it before the loop
        // is left -- including the running_ check at the top, which
        // covers the case where Stop() flips running_ right after a
        // fresh instance was just created below (previously a leaked
        // handle).
        for (;;) {
            if (!running_) {
                CloseHandle(currentInstance);
                break;
            }

            OVERLAPPED connectOverlapped{};
            HANDLE connectEvent = nullptr;
            if (useOverlapped_) {
                connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (connectEvent == nullptr) {
                    CloseHandle(currentInstance);
                    break;
                }
                connectOverlapped.hEvent = connectEvent;
            }

            BOOL connected = ConnectNamedPipe(currentInstance, useOverlapped_ ? &connectOverlapped : nullptr);
            DWORD error = connected ? ERROR_SUCCESS : GetLastError();
            if (!connected && useOverlapped_ && error == ERROR_IO_PENDING) {
                for (;;) {
                    const DWORD waitResult = WaitForSingleObject(connectEvent, 100);
                    if (waitResult == WAIT_OBJECT_0) {
                        DWORD ignored = 0;
                        if (GetOverlappedResult(currentInstance, &connectOverlapped, &ignored, FALSE)) {
                            connected = TRUE;
                            error = ERROR_SUCCESS;
                        } else {
                            error = GetLastError();
                        }
                        break;
                    }
                    if (waitResult != WAIT_TIMEOUT) {
                        error = ERROR_OPERATION_ABORTED;
                        break;
                    }
                    if (!running_) {
                        CancelIoEx(currentInstance, &connectOverlapped);
                        DWORD ignored = 0;
                        GetOverlappedResult(currentInstance, &connectOverlapped, &ignored, TRUE);
                        error = ERROR_OPERATION_ABORTED;
                        break;
                    }
                }
            }
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
            std::thread(onConnect_, acceptedConnection).detach();

            currentInstance = CreateInstance(/*isFirstInstance=*/false);
            if (currentInstance == INVALID_HANDLE_VALUE) {
                break;
            }
        }
    });

    return true;
}

void PipeListener::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;

    // Cancel the listener thread's blocking ConnectNamedPipe directly. The
    // previous self-connect-only approach could deadlock when the server's
    // own token was intentionally absent from the pipe DACL (for example a
    // restricted virtual service account). ERROR_OPERATION_ABORTED then
    // takes the existing !running_ path in the accept loop.
    if (acceptThread_.joinable()) {
        CancelSynchronousIo(reinterpret_cast<HANDLE>(acceptThread_.native_handle()));
    }

    // Keep the throwaway connection as a fallback for Windows versions or
    // transient states where CancelSynchronousIo cannot locate the pending
    // synchronous operation.
    HANDLE unstick = CreateFileW(pipeName_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (unstick != INVALID_HANDLE_VALUE) {
        CloseHandle(unstick);
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
}

} // namespace ffipc
