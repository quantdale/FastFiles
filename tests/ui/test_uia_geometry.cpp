// semantic-uia-core-surfaces 6.4: headless geometry tests for the UIA
// projection layer (client->physical-screen conversion, scroll offsets, DPI
// transformation, offscreen math -- no hardcoded 96-DPI assumptions).

#include "UiaGeometry.h"

#include "../TestSupport.h"

using namespace fftest;

namespace {

void TestItemRectMath() {
    // 100% DPI: badge band + row height math matches the render.
    ffui::UiaViewGeometry geometry;
    geometry.widthDip = 800.0f;
    geometry.heightDip = 600.0f;
    geometry.dpiScale = 1.0f;

    const D2D1_RECT_F row0 = ffui::UiaItemRectDip(0, 0, 0.0f, 800.0f, 0.0f, 0.0f, false, 600.0f);
    Check(row0.top == ffui::UiMetrics::kBadgeHeight && row0.bottom == ffui::UiMetrics::kBadgeHeight + ffui::UiMetrics::kRowHeight,
          "row 0 starts below the badge band");
    Check(row0.left == 0.0f && row0.right == ffui::UiMetrics::kColumnWidth,
          "row 0 spans the first column");

    const D2D1_RECT_F row5 = ffui::UiaItemRectDip(0, 5, 0.0f, 800.0f, 0.0f, 0.0f, false, 600.0f);
    Check(row5.top == ffui::UiMetrics::kBadgeHeight + 5 * ffui::UiMetrics::kRowHeight,
          "row 5 follows the row ramp");

    // Column 2 with horizontal scroll.
    const D2D1_RECT_F col2 = ffui::UiaItemRectDip(2, 5, 0.0f, 800.0f, 120.0f, 0.0f, false, 600.0f);
    Check(col2.left == 2 * ffui::UiMetrics::kColumnWidth - 120.0f,
          "column x honors the horizontal scroll");

    // Vertical scroll moves rows up.
    const D2D1_RECT_F scrolled = ffui::UiaItemRectDip(0, 5, 0.0f, 800.0f, 0.0f, 100.0f, false, 600.0f);
    Check(scrolled.top == ffui::UiMetrics::kBadgeHeight + 5 * ffui::UiMetrics::kRowHeight - 100.0f,
          "rows honor the vertical scroll offset");

    // Loading status line shifts content down by 24 DIP.
    const D2D1_RECT_F loading = ffui::UiaItemRectDip(0, 0, 0.0f, 800.0f, 0.0f, 0.0f, true, 600.0f);
    Check(loading.top == ffui::UiMetrics::kBadgeHeight + 24.0f,
          "loading status line shifts the content top");
}

void TestScreenConversion() {
    // 150% DPI, nonzero viewport origin: physical pixels = DIP * 1.5 +
    // origin * 1.5 (headless: no ClientToScreen).
    ffui::UiaViewGeometry geometry;
    geometry.originXDip = 200.0f;
    geometry.originYDip = 72.0f;
    geometry.widthDip = 800.0f;
    geometry.heightDip = 600.0f;
    geometry.dpiScale = 1.5f;

    const D2D1_RECT_F rowDip = ffui::UiaItemRectDip(0, 0, 0.0f, 800.0f, 0.0f, 0.0f, false, 600.0f);
    const RECT screen = ffui::UiaRectToScreen(rowDip, geometry);
    Check(screen.left == static_cast<LONG>(rowDip.left * 1.5f + 200.0f * 1.5f),
          "screen left = (DIP + origin) * scale");
    Check(screen.top == static_cast<LONG>(rowDip.top * 1.5f + 72.0f * 1.5f),
          "screen top = (DIP + origin) * scale");
    Check(screen.right - screen.left == static_cast<LONG>(ffui::UiMetrics::kColumnWidth * 1.5f),
          "screen width scales with DPI");
}

void TestFractionalScreenBounds() {
    ffui::UiaViewGeometry geometry;
    geometry.dpiScale = 1.25f; // Synthetic 120-DPI geometry; runtime DPI is measured separately.
    geometry.originXDip = 316.0f;
    geometry.originYDip = 299.2f;
    const float width = 701.0f / geometry.dpiScale;
    const float height = 338.0f / geometry.dpiScale;
    const float tinyHeight = height * 0.96f * 43.0f / (3145728.0f + 43.0f);
    const auto parentDip = D2D1::RectF(0.0f, 0.0f, width, height);
    const auto tileDip = D2D1::RectF(width * 0.02f, height * 0.98f - tinyHeight,
                                    width * 0.98f, height * 0.98f);
    const RECT parent = ffui::UiaRectToScreen(parentDip, geometry);
    const RECT tile = ffui::UiaRectToScreen(tileDip, geometry);
    Check(tile.bottom > tile.top && tile.right > tile.left,
          "43-byte tile beside 3 MiB keeps a positive physical bounding box at 120 DPI");
    Check(tile.left <= (tileDip.left + geometry.originXDip) * geometry.dpiScale &&
              tile.top <= (tileDip.top + geometry.originYDip) * geometry.dpiScale &&
              tile.right >= (tileDip.right + geometry.originXDip) * geometry.dpiScale &&
              tile.bottom >= (tileDip.bottom + geometry.originYDip) * geometry.dpiScale,
          "physical bounds enclose the entire fractional rendered tile");
    Check(tile.left >= parent.left && tile.top >= parent.top &&
              tile.right <= parent.right && tile.bottom <= parent.bottom,
          "outward-rounded tiny tile remains inside its outward-rounded parent");

    geometry.originXDip = 0.0f;
    geometry.originYDip = 0.0f;
    const RECT negative = ffui::UiaRectToScreen(D2D1::RectF(-2.2f, -1.3f, -2.1f, -1.2f), geometry);
    Check(negative.left == -3 && negative.top == -2 && negative.right == -2 && negative.bottom == -1,
          "negative fractional coordinates round outward rather than toward zero");
    const RECT exact = ffui::UiaRectToScreen(D2D1::RectF(0.8f, 1.6f, 4.0f, 8.0f), geometry);
    Check(exact.left == 1 && exact.top == 2 && exact.right == 5 && exact.bottom == 10,
          "exact physical pixel edges are unchanged");
    const RECT zero = ffui::UiaRectToScreen(D2D1::RectF(0.3f, -0.3f, 0.3f, -0.3f), geometry);
    Check(zero.left == zero.right && zero.top == zero.bottom,
          "zero-area fractional rectangles are never inflated into visible items");
    const RECT inverted = ffui::UiaRectToScreen(D2D1::RectF(1.3f, 2.3f, 1.2f, 2.2f), geometry);
    Check(inverted.right <= inverted.left && inverted.bottom <= inverted.top,
          "negative extents never become positive through outward rounding");
}

void TestOffscreenMath() {
    ffui::UiaViewGeometry geometry;
    geometry.widthDip = 800.0f;
    geometry.heightDip = 600.0f;
    geometry.dpiScale = 1.0f;

    // Fully visible row.
    const D2D1_RECT_F visible = ffui::UiaItemRectDip(0, 0, 0.0f, 800.0f, 0.0f, 0.0f, false, 600.0f);
    Check(!ffui::UiaRectOffscreen(visible, 0.0f, 800.0f, 600.0f), "visible row is not offscreen");

    // Row scrolled below the viewport.
    const D2D1_RECT_F below = ffui::UiaItemRectDip(0, 50, 0.0f, 800.0f, 0.0f, 0.0f, false, 600.0f);
    Check(ffui::UiaRectOffscreen(below, 0.0f, 800.0f, 600.0f), "row below the viewport is offscreen");

    // Row scrolled above the badge band.
    const D2D1_RECT_F above = ffui::UiaItemRectDip(0, 0, 0.0f, 800.0f, 0.0f, 2000.0f, false, 600.0f);
    Check(ffui::UiaRectOffscreen(above, 0.0f, 800.0f, 600.0f), "row scrolled away is offscreen");

    // Column outside the pane in dual-pane mode.
    const D2D1_RECT_F rightPane = ffui::UiaColumnRectDip(3, 400.0f, 400.0f, 0.0f, 600.0f);
    Check(ffui::UiaRectOffscreen(rightPane, 400.0f, 400.0f, 600.0f),
          "column outside its pane is offscreen");

    // Partially visible rows are NOT offscreen (UIA contract).
    const D2D1_RECT_F partial = ffui::UiaItemRectDip(0, 15, 0.0f, 800.0f, 0.0f, 0.0f, false, 600.0f);
    Check(partial.top < 600.0f && partial.bottom > 600.0f &&
              !ffui::UiaRectOffscreen(partial, 0.0f, 800.0f, 600.0f),
          "partially visible row is not offscreen");
}

void TestComfortableDensityRowHeight() {
    const auto previous = ffui::gUiDensityProfile;
    ffui::gUiDensityProfile = ffui::UiDensityProfile::Comfortable;
    const float row = ffui::UiaBrowseRowHeightDip();
    Check(row == 44.0f, "Comfortable browse row height is 44 DIP");
    const D2D1_RECT_F row0 = ffui::UiaItemRectDip(0, 0, 0.0f, 800.0f, 0.0f, 0.0f, false, 600.0f);
    Check(row0.bottom - row0.top == row, "UIA item rect uses Comfortable row height");
    const D2D1_RECT_F row1 = ffui::UiaItemRectDip(0, 1, 0.0f, 800.0f, 0.0f, 0.0f, false, 600.0f);
    Check(row1.top == ffui::UiMetrics::kBadgeHeight + row, "Comfortable row 1 follows the density ramp");
    Check(ffui::UiaMaxVerticalScrollDip(100, 600.0f) ==
              100 * row - (600.0f - ffui::UiMetrics::kBadgeHeight),
          "Comfortable vertical clamp uses density row height");
    ffui::gUiDensityProfile = previous;
    Check(ffui::UiaBrowseRowHeightDip() == ffui::UiMetrics::kRowHeight,
          "restoring Compact returns the default row height");
}

void TestScrollClamps() {
    // 100 rows -> 2800 DIP content; viewport 600 -> max scroll 2228.
    Check(ffui::UiaMaxVerticalScrollDip(100, 600.0f) == 100 * ffui::UiMetrics::kRowHeight - (600.0f - ffui::UiMetrics::kBadgeHeight),
          "vertical clamp mirrors the real ScrollVertical math");
    // Content smaller than the viewport is not scrollable.
    Check(ffui::UiaMaxVerticalScrollDip(5, 600.0f) == 0.0f, "small content clamps to zero");
    // Horizontal clamp over the pane width.
    Check(ffui::UiaMaxHorizontalScrollDip(4, 400.0f) == 4 * ffui::UiMetrics::kColumnWidth - 400.0f,
          "horizontal clamp mirrors the pane content width");
    // Home clamp against the laid-out content height.
    Check(ffui::UiaMaxHomeScrollDip(900.0f, 600.0f) == 900.0f - (600.0f - ffui::UiMetrics::kBadgeHeight),
          "Home clamp mirrors the layout content height");
}

} // namespace

int main() {
    TestItemRectMath();
    TestScreenConversion();
    TestFractionalScreenBounds();
    TestOffscreenMath();
    TestComfortableDensityRowHeight();
    TestScrollClamps();

    if (FailureCount() != 0) {
        fprintf(stderr, "ffuia_geometry_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("ffuia_geometry_tests: all checks passed\n");
    return 0;
}
