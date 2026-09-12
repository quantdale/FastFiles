#include "ffui/DetailsLayout.h"

#include <cmath>
#include <cstdlib>

#include "../TestSupport.h"

using namespace fftest;

namespace {

bool NearlyEqual(float left, float right) { return std::fabs(left - right) < 0.001f; }

void TestVisibleAndHidden() {
    const auto visible = ffui::ComputeDetailsLayout(1200.0f, 800.0f, true, 1.0f, false);
    Check(NearlyEqual(visible.availableColumnWidthDip, 900.0f),
          "visible details pane reserves 300 DIP at a 1200-DIP viewport");
    Check(visible.presentation == ffui::DetailsPresentation::Full,
          "normal single-pane layout uses the full details presentation");
    Check(NearlyEqual(visible.detailsRect.left, 900.0f) &&
              NearlyEqual(visible.detailsRect.right, 1200.0f),
          "details rect is right anchored after the column clip");

    const auto hidden = ffui::ComputeDetailsLayout(1200.0f, 800.0f, false, 1.0f, false);
    Check(NearlyEqual(hidden.availableColumnWidthDip, 1200.0f) &&
              NearlyEqual(hidden.detailsRect.right, 0.0f),
          "hidden details pane returns the full viewport to columns");
    Check(hidden.presentation == ffui::DetailsPresentation::Hidden,
          "disabled details pane reports a hidden presentation");
}

void TestNarrowAndDualPane() {
    const auto narrow = ffui::ComputeDetailsLayout(600.0f, 700.0f, true, 1.0f, false);
    Check(narrow.paneCollapsed && NearlyEqual(narrow.availableColumnWidthDip, 564.0f),
          "narrow layout collapses the pane to a disclosure rail");
    Check(narrow.presentation == ffui::DetailsPresentation::DisclosureRail,
          "narrow layout reports disclosure-rail presentation");
    Check(narrow.columnClipRect.right <= narrow.detailsRect.left,
          "narrow column clip never overlaps the disclosure rail");

    const auto dual = ffui::ComputeDetailsLayout(1200.0f, 800.0f, true, 1.0f, true);
    Check(NearlyEqual(dual.paneWidthDip, 450.0f),
          "wide dual pane splits the remaining width rather than the full viewport");
    Check(dual.presentation == ffui::DetailsPresentation::Full,
          "wide dual pane can retain the full details surface");
    Check(dual.paneWidthDip * 2.0f <= dual.availableColumnWidthDip,
          "both dual panes remain inside the reserved column area");
}

void TestDualPaneProtectsPrimaryWorkspace() {
    const auto constrainedDual = ffui::ComputeDetailsLayout(900.0f, 700.0f, true, 1.0f, true);
    Check(constrainedDual.paneCollapsed,
          "constrained dual-pane layout collapses secondary details before squeezing both panes");
    Check(constrainedDual.presentation == ffui::DetailsPresentation::DisclosureRail,
          "constrained dual-pane layout reports the disclosure presentation");
    Check(NearlyEqual(constrainedDual.availableColumnWidthDip, 864.0f) &&
              NearlyEqual(constrainedDual.paneWidthDip, 432.0f),
          "dual-pane disclosure rail preserves useful browsing width");
    Check(!constrainedDual.contentConstrained,
          "dual-pane disclosure strategy keeps each pane above the preferred workspace minimum");
}

void TestDetailsWidthIsBounded() {
    const auto wide = ffui::ComputeDetailsLayout(1800.0f, 1000.0f, true, 1.0f, false);
    Check(wide.presentation == ffui::DetailsPresentation::Full,
          "wide layout presents details as a full surface");
    Check(NearlyEqual(wide.detailsRect.right - wide.detailsRect.left, 320.0f),
          "wide layout caps details width at 320 DIP instead of growing with the window");
}

void TestBelowMinimumUsesNonOverlappingStrategy() {
    const auto collapsed = ffui::ComputeDetailsLayout(300.0f, 500.0f, true, 1.0f, false);
    Check(collapsed.paneCollapsed && NearlyEqual(collapsed.availableColumnWidthDip, 264.0f),
          "below the full-card minimum the details pane collapses to a disclosure rail");
    Check(collapsed.columnClipRect.right <= collapsed.detailsRect.left,
          "collapsed-pane geometry never overlaps the column clip");

    const auto tooNarrow = ffui::ComputeDetailsLayout(100.0f, 500.0f, true, 1.0f, false);
    Check(!tooNarrow.paneCollapsed && NearlyEqual(tooNarrow.availableColumnWidthDip, 100.0f),
          "below the disclosure-rail threshold the details pane is hidden");
    Check(tooNarrow.presentation == ffui::DetailsPresentation::Hidden,
          "too-narrow layout explicitly reports a hidden details presentation");
    Check(NearlyEqual(tooNarrow.detailsRect.left, 0.0f) &&
              NearlyEqual(tooNarrow.detailsRect.right, 0.0f) &&
              NearlyEqual(tooNarrow.columnClipRect.right, tooNarrow.availableColumnWidthDip),
          "hidden-pane geometry leaves the full column clip with no details region");
}

void TestDpiConversion() {
    const auto at100 = ffui::ComputeDetailsLayout(1200.0f, 800.0f, true, 1.0f, false);
    const auto at150 = ffui::ComputeDetailsLayout(1800.0f, 1200.0f, true, 1.5f, false);
    Check(NearlyEqual(at100.viewportWidthDip, at150.viewportWidthDip) &&
              NearlyEqual(at100.availableColumnWidthDip, at150.availableColumnWidthDip),
          "100% and 150% DPI produce identical DIP geometry");
    Check(at100.presentation == at150.presentation,
          "presentation class is invariant for equivalent DIP geometry across DPI scales");
}

} // namespace

int main() {
    TestVisibleAndHidden();
    TestNarrowAndDualPane();
    TestDualPaneProtectsPrimaryWorkspace();
    TestDetailsWidthIsBounded();
    TestBelowMinimumUsesNonOverlappingStrategy();
    TestDpiConversion();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}