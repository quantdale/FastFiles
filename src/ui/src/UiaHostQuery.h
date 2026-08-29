#pragma once

#include <windows.h>

// Guards UiaHostProviderFromHwnd so WM_GETOBJECT can fall through to the
// native HWND provider for the HWND being queried, and so
// get_HostRawElementProvider does not call UiaHostProviderFromHwnd while a
// host query for that same HWND is already on the stack.
//
// Server-side UIA calls run on a worker thread; UiaHostProviderFromHwnd then
// SendMessage's WM_GETOBJECT to the UI thread. The in-flight HWND set lives
// in one translation unit so every provider sees the same state.
//
// The guard is per-HWND: a host query for a Search button must not make the
// main window's GETOBJECT fall through to the native tree (which has no
// ColumnView fragment). A default-constructed guard still blocks every HWND
// and is reserved for tests that assert the no-op-while-nested contract.

namespace ffui {

struct UiaHostQueryGuard {
    UiaHostQueryGuard() noexcept;
    explicit UiaHostQueryGuard(HWND hwnd) noexcept;
    ~UiaHostQueryGuard() noexcept;

    UiaHostQueryGuard(const UiaHostQueryGuard&) = delete;
    UiaHostQueryGuard& operator=(const UiaHostQueryGuard&) = delete;

    static bool Active() noexcept;
    static bool Active(HWND hwnd) noexcept;

private:
    HWND hwnd_;
};

}  // namespace ffui
