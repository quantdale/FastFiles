#pragma once

#include <windows.h>

namespace ffui {

// Search and Storage native children are created after the command palette
// and would cover it if layout used HWND_TOP / ShowWindow(SW_SHOW). Layout
// must keep z-order stable; the palette overlay raises itself on Show.
inline constexpr UINT kNativeWorkspaceLayoutFlags = SWP_NOACTIVATE | SWP_NOZORDER;

// Palette edit/list: raise above workspace lists without implicit activation
// (focus stays explicitly owned by the query field).
inline constexpr UINT kPaletteOverlayLayoutFlags = SWP_SHOWWINDOW | SWP_NOACTIVATE;

}  // namespace ffui
