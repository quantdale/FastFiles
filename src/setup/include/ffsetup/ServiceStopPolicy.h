#pragma once

#include <windows.h>

namespace ffsetup {

// Bounded wait for a stopping service to reach SERVICE_STOPPED before
// DeleteService. Without the wait, a slow shutdown leaves the delete
// pending while the next CreateServiceW fails with
// ERROR_SERVICE_MARKED_FOR_DELETE (intermittent upgrade failure). The
// policy lives here as pure constants/decisions so tests can cover the
// state machine without touching a real SCM.
constexpr DWORD kServiceStopTimeoutMs = 30000;
constexpr DWORD kServiceStopPollIntervalMs = 200;

// A delete-pending name frees up only after the old process exits and all
// handles close; retrying CreateServiceW briefly covers that window.
constexpr DWORD kMarkedForDeleteRetryMs = 5000;
constexpr DWORD kMarkedForDeleteRetryIntervalMs = 250;

inline bool ShouldKeepWaitingForServiceStop(DWORD currentState, ULONGLONG elapsedMs) {
    return currentState != SERVICE_STOPPED && elapsedMs < kServiceStopTimeoutMs;
}

inline bool IsRetryableCreateError(DWORD error) {
    return error == ERROR_SERVICE_MARKED_FOR_DELETE;
}

} // namespace ffsetup
