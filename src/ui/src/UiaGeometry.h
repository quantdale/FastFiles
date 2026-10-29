#pragma once
// semantic-uia-core-surfaces 1.4/2.x: pure UIA geometry helpers.
//
// These replicate the exact render math of ColumnView::Render so provider
// BoundingRectangle values agree with what is actually painted, in DIPs
// first, then converted to physical screen pixels (the UIA contract) via
// dpiScale + ClientToScreen. No 96-DPI assumptions: `dpiScale` comes from
// the live PerMonitorV2 window.
//
// Headless tests compile this header directly and verify the conversion
// with a synthetic UiaViewGeometry (hwnd == nullptr degrades the final
// ClientToScreen step to identity so the math stays testable).

#include <d2d1.h>
#include <windows.h>
#include <cmath>

#include "ffui/UiaSurface.h"
#include "UITheme.h"

namespace ffui {

// Viewport-relative content top: the status badge band, plus the 24-DIP
// status line drawn while a column is loading / stale-with-error.
inline float UiaContentTopDip(bool statusLineShown) {
    return UiMetrics::kBadgeHeight + (statusLineShown ? 24.0f : 0.0f);
}

// Pane rect in viewport DIPs. `paneX` is the pane's left edge (0 or half
// width in dual-pane mode); the badge band is not part of item content.
inline D2D1_RECT_F UiaPaneRectDip(float paneX, float paneWidth, float viewportHeight) {
    return D2D1::RectF(paneX, 0.0f, paneX + paneWidth, viewportHeight);
}

// Column rect in viewport DIPs (full viewport height; the column surface
// spans the badge band too, matching the painted column background).
inline D2D1_RECT_F UiaColumnRectDip(int column, float paneX, float paneWidth,
                                    float hScroll, float viewportHeight) {
    (void)paneWidth; // column width comes from the shared kColumnWidth ramp
    const float x = paneX + static_cast<float>(column) * UiMetrics::kColumnWidth - hScroll;
    return D2D1::RectF(x, UiMetrics::kBadgeHeight, x + UiMetrics::kColumnWidth, viewportHeight);
}

// Browse-row height in DIPs. Compact equals UiMetrics::kRowHeight; Comfortable
// must match ColumnView::BrowseRowHeight() so BoundingRectangle agrees with paint.
inline float UiaBrowseRowHeightDip() {
    return GetUiDensity(gUiDensityProfile).rowHeight;
}

// Item row rect in viewport DIPs (the full row cell that is painted /
// hit-tested).
inline D2D1_RECT_F UiaItemRectDip(int column, int row, float paneX, float paneWidth,
                                  float hScroll, float vScroll, bool statusLineShown,
                                  float viewportHeight) {
    (void)paneWidth;       // row cells span the shared column width
    (void)viewportHeight;  // the rect is the row cell; clipping is IsOffscreen's job
    const float rowHeight = UiaBrowseRowHeightDip();
    const float x = paneX + static_cast<float>(column) * UiMetrics::kColumnWidth - hScroll;
    const float y = UiaContentTopDip(statusLineShown) + static_cast<float>(row) * rowHeight - vScroll;
    return D2D1::RectF(x, y, x + UiMetrics::kColumnWidth, y + rowHeight);
}

// Home item rect in viewport DIPs (Home layout rect + badge band - scroll).
inline D2D1_RECT_F UiaHomeItemRectDip(float paneX, float itemXDip, float itemYDip,
                                      float itemWidthDip, float itemHeightDip, float vScroll) {
    return D2D1::RectF(paneX + itemXDip, UiMetrics::kBadgeHeight + itemYDip - vScroll,
                       paneX + itemXDip + itemWidthDip,
                       UiMetrics::kBadgeHeight + itemYDip - vScroll + itemHeightDip);
}

// Home section header rect in viewport DIPs.
inline D2D1_RECT_F UiaHomeHeaderRectDip(float paneX, float headerXDip, float headerYDip,
                                        float headerWidthDip, float headerHeightDip,
                                        float vScroll) {
    return D2D1::RectF(paneX + headerXDip, UiMetrics::kBadgeHeight + headerYDip - vScroll,
                       paneX + headerXDip + headerWidthDip,
                       UiMetrics::kBadgeHeight + headerYDip - vScroll + headerHeightDip);
}

// True when the rect has no intersection with the pane's visible clip
// (badge band excluded, pane width respected in dual-pane mode). A partially
// visible element is NOT offscreen.
inline bool UiaRectOffscreen(const D2D1_RECT_F& rect, float paneX, float paneWidth,
                             float viewportHeight) {
    const float clipTop = UiMetrics::kBadgeHeight;
    return rect.right <= paneX || rect.left >= paneX + paneWidth ||
           rect.bottom <= clipTop || rect.top >= viewportHeight;
}

// Convert a viewport-DIP rect to physical screen pixels. Client-pixel
// coordinates are (rect * dpiScale) + viewport origin; then ClientToScreen
// when an hwnd is available (null hwnd = headless tests, identity step).
inline RECT UiaRectToScreen(const D2D1_RECT_F& rectDip, const UiaViewGeometry& geometry) {
    const float scale = geometry.dpiScale > 0.0f ? geometry.dpiScale : 1.0f;
    const float leftPixel = rectDip.left * scale + geometry.originXDip * scale;
    const float topPixel = rectDip.top * scale + geometry.originYDip * scale;
    const float rightPixel = rectDip.right * scale + geometry.originXDip * scale;
    const float bottomPixel = rectDip.bottom * scale + geometry.originYDip * scale;
    // Bound the painted fractional footprint conservatively. Truncating both
    // edges can collapse a real subpixel treemap tile to UIA's empty rectangle.
    // Do not inflate zero or inverted source extents into visible elements.
    const bool positiveWidth = rectDip.right > rectDip.left;
    const bool positiveHeight = rectDip.bottom > rectDip.top;
    const LONG left = static_cast<LONG>(positiveWidth ? std::floor(leftPixel) : leftPixel);
    const LONG top = static_cast<LONG>(positiveHeight ? std::floor(topPixel) : topPixel);
    const LONG right = static_cast<LONG>(positiveWidth ? std::ceil(rightPixel) : rightPixel);
    const LONG bottom = static_cast<LONG>(positiveHeight ? std::ceil(bottomPixel) : bottomPixel);
    RECT result{left, top, right, bottom};
    if (geometry.hwnd != nullptr && IsWindow(geometry.hwnd)) {
        POINT client{left, top};
        ClientToScreen(geometry.hwnd, &client);
        const LONG dx = client.x - left;
        const LONG dy = client.y - top;
        result.left += dx;
        result.right += dx;
        result.top += dy;
        result.bottom += dy;
    }
    return result;
}

// Horizontal scroll limits for a pane, mirroring the real scroll clamps.
inline float UiaMaxHorizontalScrollDip(int columnCount, float paneWidth) {
    return (std::max)(0.0f, static_cast<float>(columnCount) * UiMetrics::kColumnWidth - paneWidth);
}

// Vertical scroll limits for a pane, mirroring ColumnView::ScrollVertical /
// EnsureFocusedItemVisible (focused column's item count drives the clamp).
inline float UiaMaxVerticalScrollDip(int focusedColumnItemCount, float viewportHeight) {
    const float visibleHeight = (std::max)(0.0f, viewportHeight - UiMetrics::kBadgeHeight);
    return (std::max)(0.0f, static_cast<float>(focusedColumnItemCount) * UiaBrowseRowHeightDip() - visibleHeight);
}

// Vertical scroll clamp for Home panes (layout content height drives it).
inline float UiaMaxHomeScrollDip(float contentHeightDip, float viewportHeight) {
    const float visibleHeight = (std::max)(0.0f, viewportHeight - UiMetrics::kBadgeHeight);
    return (std::max)(0.0f, contentHeightDip - visibleHeight);
}

// Compute the pane-0 horizontal scroll for the Scroll pattern from the
// geometry struct (the shell pushes the animated value each render).
inline float UiaPaneHorizontalScrollDip(const UiaViewGeometry& geometry, int pane) {
    return pane == 0 ? geometry.hScroll0Dip : geometry.hScroll1Dip;
}

// Short alias used by the provider layer.
inline float UiaPaneScrollDip(const UiaViewGeometry& geometry, int pane) {
    return UiaPaneHorizontalScrollDip(geometry, pane);
}

} // namespace ffui
