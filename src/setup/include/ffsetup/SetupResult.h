#pragma once
#include <windows.h>

namespace ffsetup {

// Common result type for setup/installer operations -- these run during an
// elevated installer flow where "fail loudly and log" (task 3.2, 6.1) means
// the caller needs the Win32 error code, not just a boolean.
struct SetupResult {
    bool success = false;
    DWORD errorCode = 0; // valid GetLastError()-style code when !success

    static SetupResult Ok() noexcept { return SetupResult{true, ERROR_SUCCESS}; }
    static SetupResult FromLastError() noexcept { return SetupResult{false, GetLastError()}; }
    static SetupResult Failure(DWORD code) noexcept { return SetupResult{false, code}; }
};

} // namespace ffsetup
