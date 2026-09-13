#pragma once

#include <algorithm>

namespace ffui {

struct DetailsLayoutRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

enum class DetailsPresentation {
    Hidden,
    DisclosureRail,
    Full,
};

struct DetailsLayout {
    float viewportWidthDip = 0.0f;
    float viewportHeightDip = 0.0f;
    float availableColumnWidthDip = 0.0f;
    float paneWidthDip = 0.0f;
    DetailsLayoutRect detailsRect;
    DetailsLayoutRect columnClipRect;
    bool paneVisible = false;
    bool paneCollapsed = false;
    DetailsPresentation presentation = DetailsPresentation::Hidden;
};

// Inputs are physical pixels; all returned geometry is in DIPs. Keeping the
// conversion at this seam makes layout and hit-testing use the same values at
// 100% and 150% effective DPI.
//
// The details surface is deliberately subordinate to the browsing workspace:
// its full width scales modestly with the window, while dual-pane mode requires
// enough room for two useful file panes before reserving a full details card.
// When that contract cannot be met, the pane becomes a disclosure rail rather
// than squeezing the primary task surface.
inline DetailsLayout ComputeDetailsLayout(float viewportWidthPixels,
                                          float viewportHeightPixels,
                                          bool detailsPaneVisible,
                                          float dpiScale,
                                          bool dualPane) {
    const float scale = (std::max)(0.01f, dpiScale);
    const float widthDip = (std::max)(0.0f, viewportWidthPixels / scale);
    const float heightDip = (std::max)(0.0f, viewportHeightPixels / scale);

    constexpr float kDetailsMinWidthDip = 280.0f;
    constexpr float kDetailsMaxWidthDip = 320.0f;
    constexpr float kDetailsWidthFraction = 0.25f;
    constexpr float kCollapsedWidthDip = 36.0f;
    constexpr float kMinimumSinglePaneWorkspaceDip = 420.0f;
    constexpr float kMinimumDualPaneWorkspaceDip = 720.0f;
    constexpr float kMinimumRailWorkspaceDip = 160.0f;

    DetailsLayout result;
    result.viewportWidthDip = widthDip;
    result.viewportHeightDip = heightDip;
    result.paneVisible = detailsPaneVisible;

    const float preferredDetailsWidth =
        std::clamp(widthDip * kDetailsWidthFraction, kDetailsMinWidthDip, kDetailsMaxWidthDip);
    const float minimumWorkspaceWidth =
        dualPane ? kMinimumDualPaneWorkspaceDip : kMinimumSinglePaneWorkspaceDip;

    float reservedWidth = 0.0f;
    if (detailsPaneVisible && widthDip >= preferredDetailsWidth + minimumWorkspaceWidth) {
        reservedWidth = preferredDetailsWidth;
        result.presentation = DetailsPresentation::Full;
    } else if (detailsPaneVisible && widthDip > kCollapsedWidthDip + kMinimumRailWorkspaceDip) {
        reservedWidth = kCollapsedWidthDip;
        result.paneCollapsed = true;
        result.presentation = DetailsPresentation::DisclosureRail;
    }

    result.availableColumnWidthDip = (std::max)(0.0f, widthDip - reservedWidth);
    result.paneWidthDip = dualPane ? result.availableColumnWidthDip / 2.0f
                                   : result.availableColumnWidthDip;
    result.columnClipRect = {0.0f, 0.0f, result.availableColumnWidthDip, heightDip};
    if (reservedWidth > 0.0f) {
        result.detailsRect = {result.availableColumnWidthDip, 0.0f, widthDip, heightDip};
    }
    return result;
}

} // namespace ffui