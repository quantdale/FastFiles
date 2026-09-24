#pragma once

#include <atomic>
#include <chrono>

#include "ffipc/PipeFraming.h"

namespace ffindexsvc {

// Every service frame write has its own absolute deadline. The shared framing
// implementation owns the overlapped operation, cancellation, bounded
// collection, and quarantine state; no helper thread or synchronous-I/O
// cancellation is needed at the service boundary.
constexpr std::chrono::milliseconds kWriteDeadline{5000};

inline bool WriteFrameWithDeadline(HANDLE pipeHandle, uint16_t messageType,
                                   const void* payload, uint32_t payloadSize,
                                   std::atomic<bool>* transportFailed = nullptr) noexcept {
    if (transportFailed != nullptr && transportFailed->load()) return false;
    const bool written = ffipc::WriteFrameOverlapped(
        pipeHandle, messageType, payload, payloadSize,
        std::chrono::steady_clock::now() + kWriteDeadline);
    if (!written && transportFailed != nullptr) transportFailed->store(true);
    return written;
}

inline bool WriteFrameWithDeadline(HANDLE pipeHandle, uint16_t messageType,
                                   std::atomic<bool>* transportFailed = nullptr) noexcept {
    return WriteFrameWithDeadline(pipeHandle, messageType, nullptr, 0, transportFailed);
}

} // namespace ffindexsvc
