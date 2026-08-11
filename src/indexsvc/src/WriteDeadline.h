#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace ffindexsvc {

// A pipe write can block indefinitely once a non-reading client's outbound
// buffer is full. Service Ctrl-pipe instances use overlapped I/O, so the
// watchdog cancels the active pipe operation with CancelIoEx; the state also
// retains the thread handle for synchronous test or diagnostic pipes.
constexpr std::chrono::milliseconds kWriteDeadline{5000};
constexpr std::chrono::milliseconds kWatchdogInterval{100};

struct WriteDeadlineState {
    std::atomic<bool> writeInProgress{false};
    std::atomic<long long> writeStartedAtMs{0};
    std::atomic<bool> finished{false};
    std::atomic<HANDLE> pipeHandle{nullptr};
    // Real worker thread handle (THREAD_TERMINATE access, not the
    // GetCurrentThread pseudo-handle) that CancelSynchronousIo targets.
    HANDLE threadHandle = nullptr;
};

inline long long NowMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// RAII marker: flags the start/end of a single pipe write so the watchdog can
// detect and cancel a write that has exceeded the deadline.
class WriteDeadlineGuard {
public:
    explicit WriteDeadlineGuard(WriteDeadlineState* state, HANDLE pipeHandle = nullptr) : state_(state) {
        if (state_ != nullptr) {
            state_->pipeHandle.store(pipeHandle);
            state_->writeStartedAtMs.store(NowMs());
            state_->writeInProgress.store(true);
        }
    }
    ~WriteDeadlineGuard() {
        if (state_ != nullptr) {
            state_->writeInProgress.store(false);
            state_->pipeHandle.store(nullptr);
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
                // Bound the write. Overlapped service writes are cancelled
                // by pipe handle; synchronous test writes use the thread
                // cancellation fallback.
                const HANDLE pipeHandle = state->pipeHandle.load();
                if (pipeHandle != nullptr) {
                    ::CancelIoEx(pipeHandle, nullptr);
                }
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
