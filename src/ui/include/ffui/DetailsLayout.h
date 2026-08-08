#pragma once

#include <algorithm>

namespace ffui {

struct DetailsLayoutRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
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
};

// Inputs are physical pixels; all returned geometry is in DIPs. Keeping the
// conversion at this seam makes layout and hit-testing use the same values at
// 100% and 150% effective DPI.
inline DetailsLayout ComputeDetailsLayout(float viewportWidthPixels,
                                          float viewportHeightPixels,
                                          bool detailsPaneVisible,
                                          float dpiScale,
                                          bool dualPane) {
    const float scale = (std::max)(0.01f, dpiScale);
    const float widthDip = (std::max)(0.0f, viewportWidthPixels / scale);
    const float heightDip = (std::max)(0.0f, viewportHeightPixels / scale);
    constexpr float kDetailsWidthDip = 300.0f;
    constexpr float kCollapsedWidthDip = 36.0f;
    constexpr float kMinimumColumnWidthDip = 420.0f;

    DetailsLayout result;
    result.viewportWidthDip = widthDip;
    result.viewportHeightDip = heightDip;
    result.paneVisible = detailsPaneVisible;

    float reservedWidth = 0.0f;
    if (detailsPaneVisible && widthDip >= kDetailsWidthDip + kMinimumColumnWidthDip) {
        reservedWidth = kDetailsWidthDip;
    } else if (detailsPaneVisible && widthDip > kCollapsedWidthDip + 120.0f) {
        // Narrow-width strategy: keep a thin disclosure rail instead of
        // overlaying a full card over the last usable column.
        reservedWidth = kCollapsedWidthDip;
        result.paneCollapsed = true;
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
