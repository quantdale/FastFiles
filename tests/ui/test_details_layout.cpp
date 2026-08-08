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
          "visible details pane reserves 300 DIP");
    Check(NearlyEqual(visible.detailsRect.left, 900.0f) &&
              NearlyEqual(visible.detailsRect.right, 1200.0f),
          "details rect is right anchored after the column clip");

    const auto hidden = ffui::ComputeDetailsLayout(1200.0f, 800.0f, false, 1.0f, false);
    Check(NearlyEqual(hidden.availableColumnWidthDip, 1200.0f) &&
              NearlyEqual(hidden.detailsRect.right, 0.0f),
          "hidden details pane returns the full viewport to columns");
}

void TestNarrowAndDualPane() {
    const auto narrow = ffui::ComputeDetailsLayout(600.0f, 700.0f, true, 1.0f, false);
    Check(narrow.paneCollapsed && NearlyEqual(narrow.availableColumnWidthDip, 564.0f),
          "narrow layout collapses the pane to a disclosure rail");
    Check(narrow.columnClipRect.right <= narrow.detailsRect.left,
          "narrow column clip never overlaps the disclosure rail");

    const auto dual = ffui::ComputeDetailsLayout(1200.0f, 800.0f, true, 1.0f, true);
    Check(NearlyEqual(dual.paneWidthDip, 450.0f),
          "dual pane splits the remaining width rather than the full viewport");
    Check(dual.paneWidthDip * 2.0f <= dual.availableColumnWidthDip,
          "both dual panes remain inside the reserved column area");
}

void TestDpiConversion() {
    const auto at100 = ffui::ComputeDetailsLayout(1200.0f, 800.0f, true, 1.0f, false);
    const auto at150 = ffui::ComputeDetailsLayout(1800.0f, 1200.0f, true, 1.5f, false);
    Check(NearlyEqual(at100.viewportWidthDip, at150.viewportWidthDip) &&
              NearlyEqual(at100.availableColumnWidthDip, at150.availableColumnWidthDip),
          "100% and 150% DPI produce identical DIP geometry");
}

} // namespace

int main() {
    TestVisibleAndHidden();
    TestNarrowAndDualPane();
    TestDpiConversion();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
