#pragma once

#include <windows.h>

// Dialog/modal focus return seam: pure, headless logic for focus restoration
// when secondary dialogs, prompts, and tool workspaces close.
//
// When a modeless dialog (like SettingsDialog) or modal prompt (like
// PromptForLeafName / ConflictDialog) hides or destroys its window,
// Win32 leaves keyboard focus on NULL/desktop unless focus is explicitly
// restored to the logical owner. This seam validates that the owner is a
// live window and returns the correct focus recipient.

namespace ffui {

inline HWND DialogClosedFocusTarget(HWND owner, HWND /*dialog*/) noexcept {
    if (owner != nullptr && IsWindow(owner) != FALSE) {
        return owner;
    }
    return nullptr;
}

} // namespace ffui
