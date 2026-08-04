#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace ffindexsvc {

// A blocking WriteFile on a byte-mode named pipe blocks the calling thread
// indefinitely once the pipe's outbound buffer is full (the client is not
// reading). A non-reading authenticated client could therefore wedge a
// scan/journal worker thread forever. Windows exposes no per-operation
// timeout for a synchronous-mode pipe write (SetNamedPipeHandleState's
// lpCollectDataTimeout only applies to remote/client-side pipes, and
// CreateNamedPipe's nDefaultTimeOut only affects WaitNamedPipe), so the
// only documented way to interrupt a blocked synchronous write is
// CancelSynchronousIo from another thread. This header wires that up: each
// worker marks its pipe writes in a WriteDeadlineState, and a small
// per-worker watchdog thread calls CancelSynchronousIo on the worker once a
// write exceeds kWriteDeadlineMs, unblocking it and letting the write fail
// (ERROR_OPERATION_ABORTED) so the worker unwinds instead of hanging.
constexpr std::chrono::milliseconds kWriteDeadline{5000};
constexpr std::chrono::milliseconds kWatchdogInterval{100};

struct WriteDeadlineState {
    std::atomic<bool> writeInProgress{false};
    std::atomic<long long> writeStartedAtMs{0};
    std::atomic<bool> finished{false};
    // Real worker thread handle (THREAD_TERMINATE access, not the
    // GetCurrentThread pseudo-handle) that CancelSynchronousIo targets.
    HANDLE threadHandle = nullptr;
};

inline long long NowMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// RAII marker: flags the start/end of a single blocking pipe write so the
// watchdog can detect and cancel a write that has exceeded the deadline.
class WriteDeadlineGuard {
public:
    explicit WriteDeadlineGuard(WriteDeadlineState* state) : state_(state) {
        if (state_ != nullptr) {
            state_->writeStartedAtMs.store(NowMs());
            state_->writeInProgress.store(true);
        }
    }
    ~WriteDeadlineGuard() {
        if (state_ != nullptr) {
            state_->writeInProgress.store(false);
        }
    }
    WriteDeadlineGuard(const WriteDeadlineGuard&) = delete;
    WriteDeadlineGuard& operator=(const WriteDeadlineGuard&) = delete;

private:
    WriteDeadlineState* state_;
};

inline std::thread StartWriteDeadlineWatchdog(WriteDeadlineState* state) {
    return std::thread([state] {
        while (!state->finished.load()) {
            if (state->writeInProgress.load() &&
                NowMs() - state->writeStartedAtMs.load() > kWriteDeadline.count()) {
                // Bound the write: unblock the worker's blocking WriteFile.
                // The worker's write then returns ERROR_OPERATION_ABORTED,
                // the frame write fails, and the worker stops instead of
                // wedging indefinitely.
                if (state->threadHandle != nullptr) {
                    ::CancelSynchronousIo(state->threadHandle);
                }
                break;
            }
            std::this_thread::sleep_for(kWatchdogInterval);
        }
    });
}

// Runs `body` on the current thread with a bounded pipe-write deadline.
// Sets up a per-worker WriteDeadlineState and watchdog thread, hands the
// state to `body` (which must pass it through to its pipe writes so the
// guard marks them), and tears the watchdog down before returning.
template <typename Body>
void RunWithWriteDeadline(Body&& body) {
    WriteDeadlineState deadline;
    if (!::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(),
                           &deadline.threadHandle, THREAD_TERMINATE, /*bInheritHandle=*/FALSE,
                           /*dwOptions=*/0)) {
        // No cancelable handle; the write is still bounded by the client
        // breaking the pipe on disconnect, just not by the deadline.
        deadline.threadHandle = nullptr;
    }
    std::thread watchdog = StartWriteDeadlineWatchdog(&deadline);
    body(&deadline);
    deadline.finished.store(true);
    watchdog.join();
    if (deadline.threadHandle != nullptr) {
        ::CloseHandle(deadline.threadHandle);
    }
}

} // namespace ffindexsvc