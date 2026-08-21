#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>
#include <objidl.h>

namespace ffui {

// stabilize-live-uia-e2e: the handler receives the destination resolved from
// the FINAL drop point, so the executed operation always matches the effect
// decision. Re-deriving the destination from the active pane at execution
// time would route every drag to the pane the drag's own mouse-down
// activated (the source pane) instead of the pane the user dropped on.
using FileDropHandler = std::function<void(std::vector<std::wstring>, DWORD, std::wstring)>;
// stabilize-live-uia-e2e: the drop destination resolves from the drop
// point (screen POINTL) so pane-to-pane drags land in the DROPPED-ON pane,
// not the active pane (whose activation the drag's own mouse-down changed).
using DropDestinationProvider = std::function<std::wstring(POINTL)>;
// ui-v3 drag/drop: presentation-only feedback hook invoked while a drag is
// over the window (active=true with the last known point/effect) and once
// when it leaves or drops (active=false). Never consulted for operation
// decisions; clearing state here must not outlive the drag.
using DropFeedbackSink = std::function<void(bool active, POINTL point, DWORD effect)>;

// stabilize-live-uia-e2e: the pane a drop point falls into, for dual-pane
// layouts (content split by half width). Points outside the content region
// fall back to the active pane (index 0 sentinel handled by the caller).
inline size_t PaneIndexForDropPoint(float clientXDip, float viewportWidthDip) {
    if (viewportWidthDip <= 0.0f) return 0;
    return clientXDip < viewportWidthDip / 2.0f ? 0u : 1u;
}

// stabilize-live-uia-e2e: convert a drop point (screen POINTL) to a
// viewport-DIP client X, inverting the UIA screen conversion
// (screen = (dip + originDip) * scale + clientOrigin). Without the
// ClientToScreen client-origin term, a pane-1 drop point on a window that
// sits right of the screen edge computes past the viewport width and the
// caller's out-of-range fallback silently routes the drop to the ACTIVE
// (source) pane.
inline float DropPointClientXDip(POINTL point, POINT clientOrigin, float scale, float contentLeftDip) {
    if (scale <= 0.0f) scale = 1.0f;
    return (static_cast<float>(point.x) - static_cast<float>(clientOrigin.x)) / scale - contentLeftDip;
}

HRESULT CreateFileDataObject(const std::vector<std::wstring>& paths, IDataObject** object);
DWORD BeginFileDrag(const std::vector<std::wstring>& paths);
DWORD DetermineDropEffect(DWORD keyState, DWORD allowedEffects, const std::vector<std::wstring>& sourcePaths,
                          const std::wstring& destination);
HRESULT CreateFileDropTarget(DropDestinationProvider destination, FileDropHandler handler, IDropTarget** target);
// ui-v3 drag/drop: overload with the presentation-only feedback sink. The
// three-argument form above forwards with an empty sink so existing callers
// and tests stay source-compatible.
HRESULT CreateFileDropTarget(DropDestinationProvider destination, FileDropHandler handler,
                             DropFeedbackSink feedback, IDropTarget** target);

} // namespace ffui
