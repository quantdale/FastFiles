#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>
#include <windows.h>

namespace ffindexsvc::detail {

struct ActiveWorker {
    uint32_t volumeId = 0;
    std::shared_ptr<std::atomic<bool>> stopFlag;
    std::thread thread;
};

// The scanner and journal reader issue synchronous DeviceIoControl calls.
// CancelSynchronousIo only requests cancellation; it does not provide a
// completion deadline, so joining still waits for the kernel request to return.
inline bool RequestWorkerStop(ActiveWorker& worker) noexcept {
    if (worker.stopFlag) {
        worker.stopFlag->store(true, std::memory_order_release);
    }
    if (!worker.thread.joinable()) {
        return true;
    }

    const BOOL cancelRequested = CancelSynchronousIo(worker.thread.native_handle());
    const DWORD cancelError = cancelRequested != FALSE ? ERROR_SUCCESS : GetLastError();
    // ERROR_NOT_FOUND means there was no synchronous request in flight. Other
    // errors are surfaced to the caller as a failed cancellation request.
    return cancelRequested != FALSE || cancelError == ERROR_NOT_FOUND;
}

inline void StopAndJoin(std::vector<ActiveWorker>& workers, uint32_t volumeId) {
    for (auto it = workers.begin(); it != workers.end(); ++it) {
        if (it->volumeId == volumeId) {
            RequestWorkerStop(*it);
            if (it->thread.joinable()) {
                it->thread.join();
            }
            workers.erase(it);
            return;
        }
    }
}

inline void StopAndJoinAll(std::vector<ActiveWorker>& workers) {
    for (auto& worker : workers) {
        RequestWorkerStop(worker);
    }
    for (auto& worker : workers) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }
    workers.clear();
}

} // namespace ffindexsvc::detail
