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
// complete-ole-drag-source-move-effect D2: advertises CFSTR_PREFERREDDROPEFFECT
// alongside CF_HDROP so the target can honor the source's intended effect.
HRESULT CreateFileDataObject(const std::vector<std::wstring>& paths, DWORD preferredEffect, IDataObject** object);
DWORD BeginFileDrag(const std::vector<std::wstring>& paths);
// complete-ole-drag-source-move-effect D1/D2: keyState supplies the modifier
// preference advertised on the data object; the returned effect reflects
// CFSTR_PERFORMEDDROPEFFECT when the target published one, else the effect
// DoDragDrop returned.
DWORD BeginFileDrag(const std::vector<std::wstring>& paths, DWORD keyState);
// Reads CFSTR_PERFORMEDDROPEFFECT from the source's data object (the effect the
// target reports it actually performed), or DROPEFFECT_NONE when absent.
DWORD PerformedDropEffectFromDataObject(IDataObject* object);
// complete-ole-drag-source-move-effect D1: returns the drag-out source paths
// that must be Recycle-Bin deleted to complete a MOVE. COPY and LINK never
// delete; MOVE deletes only sources that still exist (a target that moved the
// files itself already removed them, and deleting again would double-delete).
std::vector<std::wstring> SourcePathsToDeleteAfterDrag(DWORD effect, const std::vector<std::wstring>& sourcePaths,
                                                       const std::function<bool(const std::wstring&)>& exists);
DWORD DetermineDropEffect(DWORD keyState, DWORD allowedEffects, const std::vector<std::wstring>& sourcePaths,
                          const std::wstring& destination);
HRESULT CreateFileDropTarget(DropDestinationProvider destination, FileDropHandler handler, IDropTarget** target);
// ui-v3 drag/drop: overload with the presentation-only feedback sink. The
// three-argument form above forwards with an empty sink so existing callers
// and tests stay source-compatible.
HRESULT CreateFileDropTarget(DropDestinationProvider destination, FileDropHandler handler,
                             DropFeedbackSink feedback, IDropTarget** target);

} // namespace ffui
