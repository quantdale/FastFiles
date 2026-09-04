#pragma once

#include <windows.h>

// Command-palette focus readiness seam: pure, headless logic for the
// final-local-certification-closure FC-001 focus-race repair.
//
// Root cause: CommandPalette::Show() positioned the edit/list with
// SWP_NOACTIVATE and then issued a single unchecked SetFocus(edit_). When the
// shell had just completed a dual-pane transition (or the owner was not yet
// the foreground-active window), that fire-and-forget SetFocus silently failed
// and keyboard focus stayed on the ColumnView pane. The UIA harness then
// entered text before the edit owned focus (iteration-22 failure), while a
// later rerun passed only because the activation race happened to win.
//
// Durable contract: Show() must verify acquisition (EnsureEditFocus) and
// callers/harness may enter text only when the readiness predicate below
// holds: the edit exists, is enabled and visible, and is the verified focus
// owner. The harness keeps its existing HasKeyboardFocus observation gate; it
// does not paper over the race with more retries.

namespace ffui {

// Returns true only when the verified focused window is the palette edit.
// Both handles are compared by identity; a null edit never owns focus.
inline bool PaletteEditOwnsFocus(HWND edit, HWND focused) noexcept {
    return edit != nullptr && focused == edit;
}

// Full readiness predicate for text entry: every condition must hold.
inline bool PaletteFocusReady(bool editPresent, bool editEnabled, bool editVisible,
                              bool editOwnsFocus) noexcept {
    return editPresent && editEnabled && editVisible && editOwnsFocus;
}

} // namespace ffui
