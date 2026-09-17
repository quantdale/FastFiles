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
