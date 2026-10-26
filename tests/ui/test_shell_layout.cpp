#include "ffui/ShellLayout.h"
#include "ffui/SearchLayout.h"
#include "ffui/StorageLayout.h"

#include <cmath>
#include <cstdlib>

#include "../TestSupport.h"

using namespace fftest;

namespace {

bool NearlyEqual(float left, float right) { return std::fabs(left - right) < 0.001f; }

void TestStandardBrowseLayout() {
    const auto layout = ffui::ComputeMainShellLayout(1500.0f, 900.0f, 220.0f, 72.0f,
                                                       true, 1.0f, false);
    Check(NearlyEqual(layout.contentRect.left, 220.0f) &&
              NearlyEqual(layout.contentRect.top, 72.0f),
          "content starts exactly after the sidebar and navigation chrome");
    Check(NearlyEqual(layout.contentRect.right, 1500.0f) &&
              NearlyEqual(layout.contentRect.bottom, 900.0f),
          "content reaches the client edge without a phantom reservation");
    // ui-overhaul branch: the details surface is bounded to 280-320 DIP and
    // the full presentation reserves clamp(contentWidth * 0.25, 280, 320).
    // At 1280 DIP of content that is 320 DIP; the workspace keeps the rest.
    Check(NearlyEqual(layout.columnViewportRect.left, 220.0f) &&
              NearlyEqual(layout.columnViewportRect.right, 1180.0f),
          "column viewport is the details-adjusted workspace");
    Check(NearlyEqual(layout.detailsRect.left, 1180.0f) &&
              NearlyEqual(layout.detailsRect.right, 1500.0f),
          "details pane is right anchored to the same content rect");
    Check(NearlyEqual(layout.columnViewportRect.right, layout.detailsRect.left),
          "column and details rectangles share one edge");
}

void TestHiddenDetailsAndAlternateSurface() {
    const auto hidden = ffui::ComputeMainShellLayout(1008.0f, 581.0f, 220.0f, 72.0f,
                                                      false, 1.0f, false);
    Check(NearlyEqual(hidden.columnViewportRect.left, 220.0f) &&
              NearlyEqual(hidden.columnViewportRect.right, 1008.0f),
          "hidden details returns all post-navigation width to the workspace");
    Check(hidden.detailsRect.Empty(), "hidden details has no global rectangle");

    // Search and Storage consume contentRect, not the scene-local column clip.
    // This is the alternate-surface contract that prevents native HWNDs from
    // covering the persistent navigation sidebar.
    Check(NearlyEqual(hidden.contentRect.left, 220.0f) &&
              NearlyEqual(hidden.contentRect.Width(), 788.0f),
          "alternate surfaces use the full post-navigation content width");

    const auto storage = ffui::ComputeStorageSurfaceLayout(hidden.contentRect);
    const auto treemapStatus = ffui::StorageStatusRect(storage, true);
    Check(treemapStatus.left == storage.categoryFilter.left && treemapStatus.right == storage.status.right &&
              treemapStatus.bottom <= storage.list.top && treemapStatus.Width() >= storage.status.Width(),
          "Treemap coverage reuses the whole hidden-filter row without occupying the canvas");
    Check(ffui::StorageStatusRect(storage, false).left == storage.status.left,
          "ordinary Storage modes retain separate filter and status geometry");
    bool controlsFit = true;
    for (const auto& button : storage.buttons) {
        controlsFit = controlsFit && button.left >= hidden.contentRect.left &&
            button.right <= hidden.contentRect.right && button.top >= hidden.contentRect.top;
    }
    controlsFit = controlsFit && storage.categoryFilter.left >= hidden.contentRect.left &&
        storage.categoryFilter.right <= hidden.contentRect.right &&
        storage.status.left >= hidden.contentRect.left &&
        storage.status.right <= hidden.contentRect.right &&
        storage.list.left >= hidden.contentRect.left &&
        storage.list.right <= hidden.contentRect.right &&
        storage.list.top >= hidden.contentRect.top &&
        storage.list.bottom <= hidden.contentRect.bottom;
    Check(controlsFit, "Storage controls remain inside the post-navigation content rect");
    bool labelsFit = true;
    for (const auto& button : storage.buttons) {
        labelsFit = labelsFit && (button.right - button.left) >= 116.0f;
    }
    Check(labelsFit && storage.buttonRows == 2,
          "compact Storage toolbar wraps to label-fitting rows instead of clipping radio labels (V3-06)");
    Check(storage.categoryFilter.top > storage.buttons.back().bottom,
          "narrow standard Storage surfaces wrap secondary controls below mode buttons");
    Check(NearlyEqual(storage.list.top, storage.categoryFilter.bottom + 8.0f) &&
              storage.list.top >= storage.categoryFilter.bottom &&
              storage.list.bottom >= storage.list.top,
          "Storage data surface begins below the two-row toolbar with its documented gap");

    const auto search = ffui::ComputeSearchSurfaceLayout(hidden.contentRect);
    Check(search.query.left >= hidden.contentRect.left &&
              search.list.right <= hidden.contentRect.right &&
              search.list.bottom <= hidden.contentRect.bottom,
          "Search controls remain inside the post-navigation content rect");
    Check(search.query.right <= search.scope.left &&
              search.scope.right <= search.sort.left &&
              search.sort.right <= search.direction.left,
          "standard Search controls have deterministic non-overlapping order");

    const auto wide = ffui::ComputeMainShellLayout(1500.0f, 900.0f, 220.0f, 72.0f,
                                                    false, 1.0f, false);
    const auto wideStorage = ffui::ComputeStorageSurfaceLayout(wide.contentRect);
    Check(wideStorage.buttonRows == 1 &&
              wideStorage.categoryFilter.right <= wideStorage.status.left &&
              wideStorage.status.right <= wide.contentRect.right,
          "wide Storage keeps the established one-row toolbar inside the client");
}

void TestNarrowAndShortWindows() {
    const auto narrow = ffui::ComputeMainShellLayout(700.0f, 700.0f, 220.0f, 72.0f,
                                                      true, 1.0f, true);
    Check(narrow.contentRect.Width() >= 0.0f && narrow.contentRect.Height() >= 0.0f,
          "narrow content dimensions never become negative");
    Check(narrow.columnViewportRect.right <= narrow.detailsRect.left + 0.001f,
          "narrow details reservation never overlaps the column viewport");
    Check(NearlyEqual(narrow.details.paneWidthDip * 2.0f,
                      narrow.details.availableColumnWidthDip),
          "dual-pane split is computed from the details-adjusted width");

    const auto shortWindow = ffui::ComputeMainShellLayout(700.0f, 60.0f, 220.0f, 72.0f,
                                                           true, 1.0f, false);
    Check(shortWindow.contentRect.Height() == 0.0f &&
              shortWindow.columnViewportRect.Height() == 0.0f,
          "short windows clamp chrome and content without vertical overlap");
}

void TestDpiMatrixAndResizeIdempotence() {
    const float scales[] = {1.0f, 1.25f, 1.5f, 2.0f};
    for (const float scale : scales) {
        const auto layout = ffui::ComputeMainShellLayout(1500.0f * scale, 900.0f * scale,
                                                          220.0f, 72.0f, true, scale, false);
        Check(NearlyEqual(layout.contentRect.left, 220.0f) &&
                  NearlyEqual(layout.contentRect.top, 72.0f) &&
                  NearlyEqual(layout.contentRect.Width(), 1280.0f) &&
                  NearlyEqual(layout.contentRect.Height(), 828.0f),
              "DPI-scaled client sizes preserve equivalent DIP shell geometry");
    }

    const auto first = ffui::ComputeMainShellLayout(1600.0f, 900.0f, 220.0f, 72.0f,
                                                     true, 1.0f, false);
    const auto resized = ffui::ComputeMainShellLayout(700.0f, 700.0f, 220.0f, 72.0f,
                                                       true, 1.0f, false);
    const auto restored = ffui::ComputeMainShellLayout(1600.0f, 900.0f, 220.0f, 72.0f,
                                                        true, 1.0f, false);
    Check(NearlyEqual(first.contentRect.left, restored.contentRect.left) &&
              NearlyEqual(first.columnViewportRect.right, restored.columnViewportRect.right) &&
              NearlyEqual(first.detailsRect.left, restored.detailsRect.left),
          "resize cycles are deterministic and do not accumulate offsets");
    Check(resized.columnViewportRect.right <= resized.contentRect.right + 0.001f,
          "resized column viewport remains inside the content rect");

    const auto narrowSurface = ffui::ShellLayoutRect{220.0f, 72.0f, 700.0f, 700.0f};
    const auto narrowSearch = ffui::ComputeSearchSurfaceLayout(narrowSurface);
    Check(narrowSearch.compact && narrowSearch.list.left >= narrowSurface.left &&
              narrowSearch.list.right <= narrowSurface.right &&
              narrowSearch.list.bottom <= narrowSurface.bottom,
          "narrow Search uses a bounded fallback without negative geometry");
}

void TestFullClientCoverageInvariant() {
    // Closure-pass regression: the shell layout must tile the entire client
    // with no vertical or horizontal remainder. Any gap would expose the
    // premultiplied-alpha composition surface (desktop bleed-through). The
    // live "bottom strip" report was traced to DWM invisible resize borders
    // included by GetWindowRect-based captures, not to layout; this invariant
    // pins the layout side so a real gap cannot return unnoticed.
    const float scales[] = {1.0f, 1.25f, 1.5f, 2.0f};
    for (const float scale : scales) {
        for (const bool detailsPane : {false, true}) {
            for (const bool dualPane : {false, true}) {
                const float clientW = 1289.0f * scale; // deliberately non-round
                const float clientH = 733.0f * scale;
                const auto layout = ffui::ComputeMainShellLayout(clientW, clientH, 220.0f, 72.0f,
                                                                 detailsPane, scale, dualPane);
                Check(NearlyEqual(layout.navigationChromeRect.bottom + layout.details.viewportHeightDip,
                                  clientH / scale),
                      "chrome plus scene height covers the full client height exactly");
                Check(NearlyEqual(layout.contentRect.bottom, clientH / scale),
                      "content rect bottom sits on the client bottom edge");
                Check(NearlyEqual(layout.navigationSidebarRect.Width() +
                                      layout.details.availableColumnWidthDip +
                                      (layout.detailsRect.Empty() ? 0.0f : layout.detailsRect.Width()),
                                  clientW / scale),
                      "sidebar plus scene surfaces tile the full client width exactly");
            }
        }
    }
}

} // namespace

int main() {
    TestStandardBrowseLayout();
    TestHiddenDetailsAndAlternateSurface();
    TestNarrowAndShortWindows();
    TestDpiMatrixAndResizeIdempotence();
    TestFullClientCoverageInvariant();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
