#pragma once

#include <algorithm>

#include "ffui/DetailsLayout.h"

namespace ffui {

// A rectangle in the shell's DIP coordinate space.  Native child HWNDs are
// converted to physical pixels only at their positioning boundary; Direct2D
// and hit-testing consume these same DIP rectangles.
struct ShellLayoutRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    constexpr float Width() const noexcept { return (std::max)(0.0f, right - left); }
    constexpr float Height() const noexcept { return (std::max)(0.0f, bottom - top); }
    constexpr bool Empty() const noexcept { return Width() <= 0.0f || Height() <= 0.0f; }
};

// The single shell-layout result consumed by rendering, hit-testing, and
// alternate native surfaces.  `contentRect` is the workspace after the
// always-visible navigation chrome.  `columnViewportRect` and `detailsRect`
// are global shell coordinates derived from the scene-local DetailsLayout.
struct MainShellLayout {
    float dpiScale = 1.0f;
    ShellLayoutRect clientRect;
    ShellLayoutRect navigationChromeRect;
    ShellLayoutRect navigationSidebarRect;
    ShellLayoutRect contentRect;
    ShellLayoutRect columnViewportRect;
    ShellLayoutRect detailsRect;
    DetailsLayout details;
};

inline MainShellLayout ComputeMainShellLayout(float clientWidthPixels,
                                              float clientHeightPixels,
                                              float navigationSidebarWidthDip,
                                              float navigationChromeHeightDip,
                                              bool detailsPaneVisible,
                                              float dpiScale,
                                              bool dualPane) noexcept {
    MainShellLayout result;
    result.dpiScale = (std::max)(0.01f, dpiScale);
    const float clientWidthDip = (std::max)(0.0f, clientWidthPixels / result.dpiScale);
    const float clientHeightDip = (std::max)(0.0f, clientHeightPixels / result.dpiScale);
    const float sidebarWidthDip = std::clamp(navigationSidebarWidthDip, 0.0f, clientWidthDip);
    const float chromeHeightDip = std::clamp(navigationChromeHeightDip, 0.0f, clientHeightDip);

    result.clientRect = {0.0f, 0.0f, clientWidthDip, clientHeightDip};
    result.navigationChromeRect = {0.0f, 0.0f, clientWidthDip, chromeHeightDip};
    result.navigationSidebarRect = {0.0f, chromeHeightDip, sidebarWidthDip, clientHeightDip};
    result.contentRect = {sidebarWidthDip, chromeHeightDip, clientWidthDip, clientHeightDip};

    // ComputeDetailsLayout remains the authority for the scene-local details
    // reservation.  Feeding it the exact DIP scene converted back to pixels
    // keeps the existing DPI contract while avoiding duplicate subtraction in
    // the shell owner.
    result.details = ComputeDetailsLayout(
        result.contentRect.Width() * result.dpiScale,
        result.contentRect.Height() * result.dpiScale,
        detailsPaneVisible,
        result.dpiScale,
        dualPane);

    result.columnViewportRect = {
        result.contentRect.left + result.details.columnClipRect.left,
        result.contentRect.top + result.details.columnClipRect.top,
        result.contentRect.left + result.details.columnClipRect.right,
        result.contentRect.top + result.details.columnClipRect.bottom};
    if (result.details.detailsRect.right > result.details.detailsRect.left &&
        result.details.detailsRect.bottom > result.details.detailsRect.top) {
        result.detailsRect = {
            result.contentRect.left + result.details.detailsRect.left,
            result.contentRect.top + result.details.detailsRect.top,
            result.contentRect.left + result.details.detailsRect.right,
            result.contentRect.top + result.details.detailsRect.bottom};
    }
    return result;
}

} // namespace ffui
