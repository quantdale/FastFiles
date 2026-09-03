#pragma once

#include <windows.h>

// keyboard-focus-forensics FF-VIS-FOCUS-001: pure, headless seam for the
// address-bar close focus decision. The address edit is a borderless Win32
// EDIT child of the chrome window; while it is open it owns keyboard focus.
// When it closes (commit or cancel) the edit is hidden, and a hidden focused
// window strands keyboard focus on whatever Windows promotes (in practice the
// chrome child, which has no WM_KEYDOWN routing, so Up/Down/Enter/F2/Delete
// and every pane shortcut silently die until the user clicks content).
//
// Contract: closing the address edit must return keyboard focus to the shell
// owner (the main window that routes WM_KEYDOWN into ColumnView), never
// leaving it on the hidden edit or on the chrome child. When the owner is
// gone the chrome window is the only sane fallback.
//
// The HWND decision lives here so it can be unit tested with real windows
// without compiling the chrome (see tests/ui/test_address_bar_focus.cpp);
// NavigationChrome owns the two call sites (commit success, cancel restore).

namespace ffui {

inline HWND AddressEditClosedFocusTarget(HWND owner, HWND chrome) noexcept {
    if (owner != nullptr && IsWindow(owner) != FALSE) {
        return owner;
    }
    return chrome;
}

} // namespace ffui
