#pragma once

#include "ffui/ShellLayout.h"

// ui-v3 drag/drop: pure, headless geometry seam for the canonical
// drop-target destination affordance consumed by
// `WindowShell::DrawDropTargetFeedback`. Mirrors the established
// `ShellLayout.h` / `HomeLayout.h` family: a small input struct, a result
// struct, and one total `ComputeDropFeedbackGeometry` function. The header
// owns no rendering state and pulls in no D2D / DirectWrite / UITheme
// dependencies so it can be unit-tested standalone (the test target adds
// only `../../src/ui/include` to its include path).
//
// Metric values intentionally mirror the lived-in `UiMetrics` tokens used by
// the renderer (`kSpaceXs` = 4 vertical viewport inset, `kRadiusMedium` = 8
// outline corner radius) so the headless geometry stays visually identical
// to the painted feedback without depending on `src/ui/src/UITheme.h`.
//
// DPI note: every value here is a DIP in shell coordinates, matching the
// rest of the layout family -- DPI-independent by construction.
//
// Color policy stays deliberately OUT of this seam: High Contrast falls back
// to the system highlight color while normal themes use the shared
// `dropTargetStroke` token, and both need brushes, so the renderer resolves
// them; this seam owns only shape decisions.

namespace ffui {

// ui-v3 drag/drop: spacing/shape constants mirroring UiMetrics (see above).
constexpr float kDropFeedbackViewportInset = 4.0f; // kSpaceXs above/below viewport
constexpr float kDropFeedbackPaneHalfStrip = 1.5f; // strip half-width around the split
constexpr float kDropFeedbackOutlineInset = 1.0f;  // single-pane outline side inset
constexpr float kDropFeedbackOutlineStroke = 2.0f; // single-pane outline stroke width
constexpr float kDropFeedbackOutlineRadius = 8.0f; // kRadiusMedium outline corners

enum class DropFeedbackKind {
    None = 0,        // no feedback drawn (inactive drag or invalid drop point)
    PaneSplitStrip,  // dual-pane: filled band centered on the pane split nearest the drop point
    ViewportOutline, // single-pane: inset rounded outline around the column viewport
};

// One shape decision. For PaneSplitStrip, [left,right] is the filled band
// (half-strip either side of the split) and [top,bottom] is the inset
// viewport band. For ViewportOutline, all four edges describe the rounded
// outline rectangle with cornerRadius/strokeWidth applied by the renderer.
struct DropFeedbackGeometry {
    DropFeedbackKind kind = DropFeedbackKind::None;
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float cornerRadius = 0.0f;
    float strokeWidth = 0.0f;
};

struct DropFeedbackInput {
    MainShellLayout shellLayout; // POD snapshot of the current shell layout
    float dropXDip = 0.0f;       // content-space DIP x from DropPointClientXDip
    size_t activePaneCount = 1;  // navigationWorkspace_.ActivePaneCount()
    bool active = false;         // IDropTarget sink reports an active drag
    bool valid = false;          // converted drop x lies inside the column viewport
};

// Total function: any input yields a well-formed result; suppressed states
// produce DropFeedbackKind::None. Dual-pane draws the split strip nearest
// the drop point (left half -> the internal split at half the available
// column width, otherwise the outer right edge), single pane outlines the
// whole column viewport -- exactly the destination the executed operation
// would use.
inline DropFeedbackGeometry ComputeDropFeedbackGeometry(const DropFeedbackInput& input) noexcept {
    DropFeedbackGeometry shape; // kind None
    if (!input.active || !input.valid) {
        return shape;
    }
    const MainShellLayout& layout = input.shellLayout;
    const float viewportWidth = layout.details.availableColumnWidthDip;
    if (!(viewportWidth > 0.0f)) {
        return shape; // defensive: a valid drop point implies positive width upstream
    }
    const float top = layout.columnViewportRect.top + kDropFeedbackViewportInset;
    const float bottom = layout.columnViewportRect.bottom - kDropFeedbackViewportInset;
    if (!(bottom >= top)) {
        return shape; // defensive: degenerate viewport draws nothing instead of garbage
    }

    if (input.activePaneCount >= 2) {
        const float splitX =
            layout.contentRect.left +
            (input.dropXDip < viewportWidth / 2.0f ? viewportWidth / 2.0f : viewportWidth);
        shape.kind = DropFeedbackKind::PaneSplitStrip;
        shape.left = splitX - kDropFeedbackPaneHalfStrip;
        shape.right = splitX + kDropFeedbackPaneHalfStrip;
        shape.top = top;
        shape.bottom = bottom;
        return shape;
    }

    shape.kind = DropFeedbackKind::ViewportOutline;
    shape.left = layout.columnViewportRect.left + kDropFeedbackOutlineInset;
    shape.top = top;
    shape.right = layout.columnViewportRect.right - kDropFeedbackOutlineInset;
    shape.bottom = bottom;
    shape.cornerRadius = kDropFeedbackOutlineRadius;
    shape.strokeWidth = kDropFeedbackOutlineStroke;
    return shape;
}

} // namespace ffui
