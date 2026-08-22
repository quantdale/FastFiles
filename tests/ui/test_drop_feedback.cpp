// ui-v3 drag/drop: headless coverage for the canonical drop-target feedback
// geometry seam (`ffui::ComputeDropFeedbackGeometry`), consumed by
// `WindowShell::DrawDropTargetFeedback`. Pins the dual-pane strip vs
// single-pane outline decision, exact token-derived geometry, the midline
// tie-break shared with the destination provider, and inactive/invalid
// suppression. Color policy (High Contrast -> COLOR_HIGHLIGHT, otherwise
// theme.dropTargetStroke) stays renderer-side by design and is not asserted
// here.

#include "ffui/DropFeedbackLayout.h"

#include "../TestSupport.h"

using namespace fftest;

namespace {

constexpr float kViewportLeft = 200.0f;
constexpr float kViewportTop = 60.0f;
constexpr float kViewportRight = 1000.0f;
constexpr float kViewportBottom = 780.0f;
constexpr float kContentLeft = 200.0f;
constexpr float kAvailableWidth = 800.0f;
constexpr float kInsetTop = kViewportTop + ffui::kDropFeedbackViewportInset;    // 64
constexpr float kInsetBottom = kViewportBottom - ffui::kDropFeedbackViewportInset; // 776

ffui::DropFeedbackInput DualPaneInput(float dropXDip) {
    ffui::DropFeedbackInput input;
    input.shellLayout.contentRect = {kContentLeft, 48.0f, 1200.0f, 800.0f};
    input.shellLayout.columnViewportRect = {kViewportLeft, kViewportTop, kViewportRight, kViewportBottom};
    input.shellLayout.details.availableColumnWidthDip = kAvailableWidth;
    input.dropXDip = dropXDip;
    input.activePaneCount = 2;
    input.active = true;
    input.valid = true;
    return input;
}

bool RectIs(const ffui::DropFeedbackGeometry& g, float l, float t, float r, float b) {
    return g.left == l && g.top == t && g.right == r && g.bottom == b;
}

} // namespace

int main() {
    // Dual pane, drop in the LEFT half: strip on the internal split at
    // contentLeft + availableWidth/2 = 600, half-width 1.5 DIP each side.
    const ffui::DropFeedbackGeometry leftHalf =
        ffui::ComputeDropFeedbackGeometry(DualPaneInput(300.0f));
    Check(leftHalf.kind == ffui::DropFeedbackKind::PaneSplitStrip,
          "left-half dual-pane drop draws the pane-split strip");
    Check(RectIs(leftHalf, 598.5f, kInsetTop, 601.5f, kInsetBottom),
          "left-half strip band centers on the internal split");
    Check(leftHalf.right - leftHalf.left == 2.0f * ffui::kDropFeedbackPaneHalfStrip,
          "strip band spans exactly two half-strips");

    // Midline tie-break: a drop exactly at availableWidth/2 counts as the
    // RIGHT half (strict < preserved), emphasizing the outer right edge at
    // contentLeft + availableWidth = 1000.
    const ffui::DropFeedbackGeometry atMidline =
        ffui::ComputeDropFeedbackGeometry(DualPaneInput(400.0f));
    Check(atMidline.kind == ffui::DropFeedbackKind::PaneSplitStrip,
          "midline dual-pane drop still draws the split strip");
    Check(RectIs(atMidline, 998.5f, kInsetTop, 1001.5f, kInsetBottom),
          "midline drop emphasizes the outer right edge");

    // Right-half drop: same outer edge as the midline case.
    const ffui::DropFeedbackGeometry rightHalf =
        ffui::ComputeDropFeedbackGeometry(DualPaneInput(650.0f));
    Check(RectIs(rightHalf, atMidline.left, kInsetTop, atMidline.right, kInsetBottom),
          "right-half drop matches the outer-edge strip");

    // Single pane: inset rounded outline around the entire column viewport,
    // independent of where inside the viewport the pointer hovers.
    ffui::DropFeedbackInput single = DualPaneInput(120.0f);
    single.activePaneCount = 1;
    const ffui::DropFeedbackGeometry outline = ffui::ComputeDropFeedbackGeometry(single);
    Check(outline.kind == ffui::DropFeedbackKind::ViewportOutline,
          "single-pane drop outlines the whole column viewport");
    Check(RectIs(outline,
                 kViewportLeft + ffui::kDropFeedbackOutlineInset,
                 kInsetTop,
                 kViewportRight - ffui::kDropFeedbackOutlineInset,
                 kInsetBottom),
          "outline insets one DIP per horizontal side and kSpaceXs vertically");
    Check(outline.cornerRadius == ffui::kDropFeedbackOutlineRadius &&
              outline.strokeWidth == ffui::kDropFeedbackOutlineStroke,
          "outline carries the medium radius and 2 DIP stroke contract");

    ffui::DropFeedbackInput singleOtherX = single;
    singleOtherX.dropXDip = 700.0f;
    const ffui::DropFeedbackGeometry outlineMoved = ffui::ComputeDropFeedbackGeometry(singleOtherX);
    Check(RectIs(outlineMoved, outline.left, outline.top, outline.right, outline.bottom),
          "single-pane outline ignores the drop x within the viewport");

    // Suppressed states draw nothing.
    ffui::DropFeedbackInput inactive = DualPaneInput(300.0f);
    inactive.active = false;
    Check(ffui::ComputeDropFeedbackGeometry(inactive).kind == ffui::DropFeedbackKind::None,
          "inactive drag draws nothing");
    ffui::DropFeedbackInput invalid = DualPaneInput(8000.0f);
    invalid.valid = false;
    Check(ffui::ComputeDropFeedbackGeometry(invalid).kind == ffui::DropFeedbackKind::None,
          "out-of-viewport drop point draws nothing");
    ffui::DropFeedbackInput zeroWidth = DualPaneInput(0.0f);
    zeroWidth.shellLayout.details.availableColumnWidthDip = 0.0f;
    Check(ffui::ComputeDropFeedbackGeometry(zeroWidth).kind == ffui::DropFeedbackKind::None,
          "zero-width viewport draws nothing (total-function guard)");
    ffui::DropFeedbackInput degenerate = DualPaneInput(300.0f);
    degenerate.shellLayout.columnViewportRect = {200.0f, 400.0f, 1000.0f, 396.0f};
    Check(ffui::ComputeDropFeedbackGeometry(degenerate).kind == ffui::DropFeedbackKind::None,
          "degenerate viewport inset draws nothing instead of garbage");

    if (FailureCount() != 0) {
        fprintf(stderr, "ffdrop_feedback_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("ffdrop_feedback_tests: all checks passed\n");
    return 0;
}
