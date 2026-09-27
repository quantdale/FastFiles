#pragma once

#include <atomic>
#include <exception>
#include <utility>
#include <windows.h>

#include "VolumeScanner.h"

namespace ffindexsvc {

enum class ScanWorkerExceptionKind {
    Standard,
    Unknown,
};

// Shared by the service's real scan worker and the isolated scanner fixture.
// Any transport error or escaped worker exception poisons the connection and
// cancels its outstanding pipe I/O; a possibly partial stream gets no terminal.
// legacy-protocol-correctness-and-raw-volume-cancellation-closure D3 extends
// that to a failure the negotiated peer cannot be told about: the attempt ends
// by aborting the connection rather than writing a false success terminal.
// The callback receives the specific outcome so the caller can report why.
template <typename Work, typename OnConnectionAbort, typename OnException>
void RunScanWorkerBoundary(HANDLE pipeHandle, std::atomic<bool>& transportFailed,
                           Work&& work, OnConnectionAbort&& onConnectionAbort,
                           OnException&& onException) noexcept {
    try {
        const ScanRunResult outcome = std::forward<Work>(work)();
        if (!ScanRunResultRequiresConnectionAbort(outcome)) {
            return;
        }
        transportFailed.store(true);
        CancelIoEx(pipeHandle, nullptr);
        try {
            std::forward<OnConnectionAbort>(onConnectionAbort)(outcome);
        } catch (...) { }
    } catch (const std::exception&) {
        transportFailed.store(true);
        try {
            std::forward<OnException>(onException)(ScanWorkerExceptionKind::Standard);
        } catch (...) { }
        CancelIoEx(pipeHandle, nullptr);
    } catch (...) {
        transportFailed.store(true);
        try {
            std::forward<OnException>(onException)(ScanWorkerExceptionKind::Unknown);
        } catch (...) { }
        CancelIoEx(pipeHandle, nullptr);
    }
}

} // namespace ffindexsvc
