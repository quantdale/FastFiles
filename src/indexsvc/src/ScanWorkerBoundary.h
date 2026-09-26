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
template <typename Work, typename OnTransportFailure, typename OnException>
void RunScanWorkerBoundary(HANDLE pipeHandle, std::atomic<bool>& transportFailed,
                           Work&& work, OnTransportFailure&& onTransportFailure,
                           OnException&& onException) noexcept {
    try {
        const ScanRunResult outcome = std::forward<Work>(work)();
        if (outcome != ScanRunResult::TransportFailed) {
            return;
        }
        transportFailed.store(true);
        CancelIoEx(pipeHandle, nullptr);
        try {
            std::forward<OnTransportFailure>(onTransportFailure)();
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
