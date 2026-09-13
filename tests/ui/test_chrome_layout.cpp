// ui-overhaul phase 1: pure responsive policy for the top navigation chrome.
// Pins the narrow/normal/wide classification, the unified command-button
// geometry, the location/search reservation, and the tab overflow window so
// the native chrome cannot drift back into per-control literals.

#include "ffui/ChromeLayout.h"

#include <cmath>
#include <cstdlib>

#include "../TestSupport.h"

using namespace fftest;

namespace {

bool NearlyEqual(float left, float right) { return std::fabs(left - right) < 0.001f; }

void TestWidthClassification() {
    Check(ffui::ClassifyChromeWidth(480.0f) == ffui::ChromeWidthClass::Narrow,
          "sub-760 DIP chrome is Narrow");
    Check(ffui::ClassifyChromeWidth(759.9f) == ffui::ChromeWidthClass::Narrow,
          "Narrow boundary is exclusive at 760 DIP");
    Check(ffui::ClassifyChromeWidth(760.0f) == ffui::ChromeWidthClass::Normal,
          "760 DIP chrome is Normal");
    Check(ffui::ClassifyChromeWidth(1179.9f) == ffui::ChromeWidthClass::Normal,
          "Normal boundary is exclusive at 1180 DIP");
    Check(ffui::ClassifyChromeWidth(1180.0f) == ffui::ChromeWidthClass::Wide,
          "1180 DIP chrome is Wide");
}

void TestCommandFamilyGeometry() {
    const float widths[] = {480.0f, 800.0f, 1280.0f, 1920.0f};
    for (const float width : widths) {
        const auto layout = ffui::ComputeChromeLayout(width, 1, 0);
        const ffui::ChromeRect commands[] = {layout.back, layout.forward, layout.up, layout.settings,
                                             layout.newTab};
        bool uniform = true;
        for (const auto& rect : commands) {
            uniform = uniform && NearlyEqual(rect.size, 28.0f);
        }
        Check(uniform, "every command button is the same 28-DIP square at every width");
        Check(NearlyEqual(layout.back.top, layout.forward.top) &&
                  NearlyEqual(layout.forward.top, layout.up.top),
              "Back/Forward/Up share one toolbar row");
        Check(layout.back.right() <= layout.forward.left + 0.001f &&
                  layout.forward.right() <= layout.up.left + 0.001f,
              "toolbar commands never overlap");
        Check(layout.back.left >= 0.0f && layout.settings.right() <= width + 0.001f,
              "command family stays inside the client width");
        Check(NearlyEqual(layout.newTab.left, width - 4.0f - 28.0f) &&
                  NearlyEqual(layout.settings.right() + 4.0f, layout.newTab.left),
              "New Tab and Settings anchor to the trailing edge with one gap");
        Check(NearlyEqual(layout.back.top, 40.0f) && NearlyEqual(layout.tabs.top, 4.0f),
              "tab row and toolbar row keep their documented tops");
    }
}

void TestLocationAndSearchReservation() {
    const auto normal = ffui::ComputeChromeLayout(1000.0f, 1, 0);
    Check(!normal.searchCollapsed, "Normal width keeps the labelled search field");
    Check(NearlyEqual(normal.search.size, 200.0f), "Normal search field is 200 DIP");
    Check(normal.addressLeft <= normal.addressRight &&
              normal.addressRight <= normal.search.left + 0.001f,
          "address region sits between the Up command and the search affordance");
    Check(normal.addressRight >= normal.addressLeft + 200.0f,
          "Normal width reserves a usable address region");

    const auto wide = ffui::ComputeChromeLayout(1400.0f, 1, 0);
    Check(NearlyEqual(wide.search.size, 240.0f), "Wide search field widens to 240 DIP");

    const auto narrow = ffui::ComputeChromeLayout(600.0f, 1, 0);
    Check(narrow.searchCollapsed, "Narrow width collapses search to its icon");
    Check(NearlyEqual(narrow.search.size, 28.0f), "collapsed search is one command square");
    Check(narrow.addressRight >= narrow.addressLeft &&
              narrow.addressRight <= narrow.search.left + 0.001f,
          "Narrow width keeps the location region before the collapsed search icon");

    const auto degenerate = ffui::ComputeChromeLayout(120.0f, 0, 0);
    Check(degenerate.addressRight >= degenerate.addressLeft,
          "degenerate widths never produce an inverted address region");
    Check(degenerate.search.left <= 120.0f,
          "degenerate widths keep the collapsed search inside the client");
}

void TestTabWidthPolicy() {
    const auto roomy = ffui::ComputeChromeLayout(1400.0f, 4, 0);
    Check(!roomy.tabs.overflow, "four tabs at 1400 DIP do not overflow");
    Check(roomy.tabs.width >= roomy.tabs.minWidth - 0.001f &&
              roomy.tabs.width <= roomy.tabs.maxWidth + 0.001f,
          "tab width stays within the readable floor and ceiling");
    Check(roomy.tabs.visibleCount == 4 && roomy.tabs.visibleFirst == 0,
          "all tabs are visible when there is room");

    const auto capped = ffui::ComputeChromeLayout(2600.0f, 2, 0);
    Check(NearlyEqual(capped.tabs.width, capped.tabs.maxWidth),
          "very wide windows cap tab width instead of stretching tabs");

    const auto zero = ffui::ComputeChromeLayout(1000.0f, 0, 0);
    Check(zero.tabs.visibleCount == 0 && NearlyEqual(zero.tabs.width, 0.0f),
          "no tabs yields zero-width tab geometry");
}

void TestTabOverflowWindow() {
    const int tabCount = 24;
    const auto layout = ffui::ComputeChromeLayout(900.0f, tabCount, 12);
    Check(layout.tabs.overflow, "24 tabs at 900 DIP must overflow");
    Check(NearlyEqual(layout.tabs.width, layout.tabs.minWidth),
          "overflow keeps every visible tab at the readable floor");
    Check(layout.tabs.visibleCount > 0 && layout.tabs.visibleCount < tabCount,
          "overflow shows a bounded window of tabs, not all and not none");
    Check(layout.tabs.visibleFirst <= 12 && 12 < layout.tabs.visibleFirst + layout.tabs.visibleCount,
          "the active tab is always inside the visible window");
    Check(layout.tabs.overflowButton.size > 0.0f &&
              layout.tabs.overflowButton.right() <= layout.tabs.right + 0.001f,
          "the overflow affordance is reserved inside the tab strip");
    Check(layout.tabs.overflowButton.left >=
              layout.tabs.left + layout.tabs.visibleCount * layout.tabs.width,
          "visible tabs never paint under the overflow affordance");

    // Window sliding keeps the active tab visible at both edges.
    const auto atStart = ffui::ComputeChromeLayout(900.0f, tabCount, 0);
    Check(atStart.tabs.visibleFirst == 0, "active first tab keeps the window anchored left");
    const auto atEnd = ffui::ComputeChromeLayout(900.0f, tabCount, tabCount - 1);
    Check(atEnd.tabs.visibleFirst == tabCount - atEnd.tabs.visibleCount,
          "active last tab slides the window to the end");
    Check(atEnd.tabs.visibleFirst + atEnd.tabs.visibleCount == tabCount,
          "the end window shows the last tab");
}

void TestLocationBandInsideChrome() {
    // Regression (6803650): the location band was derived from the search
    // control's width (200/240 DIP), so DT_VCENTER pushed the breadcrumb text
    // far below the 72-DIP chrome and it was clipped invisible.
    const float widths[] = {480.0f, 1000.0f, 1400.0f};
    for (const float width : widths) {
        const auto layout = ffui::ComputeChromeLayout(width, 1, 0);
        const ffui::ChromeBand band = ffui::ChromeLocationBand(layout);
        Check(NearlyEqual(band.Height(), 28.0f),
              "location band keeps the command-row height at every width class");
        Check(NearlyEqual(band.top, layout.search.top),
              "location band starts on the toolbar row");
        Check(NearlyEqual(band.bottom, layout.search.bottom()),
              "location band ends with the toolbar controls");
        Check(band.bottom <= layout.chromeHeight + 0.001f,
              "location band never extends past the chrome clip");
    }
    const auto wide = ffui::ComputeChromeLayout(1400.0f, 1, 0);
    const ffui::ChromeBand wideBand = ffui::ChromeLocationBand(wide);
    Check(wide.search.size >= 240.0f && NearlyEqual(wideBand.Height(), 28.0f),
          "a wide search field never stretches the location band");
}

void TestCompositionInvariants() {
    // Every width in the supported range must produce a non-overlapping,
    // in-bounds composition. This is the invariant that prevents the
    // "controls collide at some window size" class of defect.
    for (float width = 320.0f; width <= 2200.0f; width += 20.0f) {
        for (int tabs = 0; tabs <= 36; tabs += 4) {
            const auto layout = ffui::ComputeChromeLayout(width, tabs, tabs > 0 ? tabs / 2 : 0);
            const bool commandsOverlap =
                layout.settings.right() > layout.newTab.left + 0.001f ||
                layout.back.right() > layout.forward.left + 0.001f ||
                layout.forward.right() > layout.up.left + 0.001f;
            Check(!commandsOverlap, "command buttons never overlap at any width");
            Check(layout.tabs.right <= layout.settings.left + 0.001f,
                  "tab strip never paints into the trailing commands");
            if (tabs > 0) {
                const float visibleRight = layout.tabs.left +
                    static_cast<float>(layout.tabs.visibleCount) * layout.tabs.width +
                    static_cast<float>((std::max)(0, layout.tabs.visibleCount - 1)) * layout.tabs.gap;
                const float limit = layout.tabs.overflow ? layout.tabs.overflowButton.left
                                                         : layout.tabs.right;
                Check(visibleRight <= limit + 0.001f,
                      "visible tab strip stays inside its reserved region");
            }
            Check(layout.search.left >= layout.up.right() - 0.001f,
                  "search affordance never crosses the navigation commands");
            Check(layout.search.right() <= width + 0.001f,
                  "search affordance stays inside the client");
            // Vertical composition: the missing invariant class that let the
            // location band grow past the chrome clip unnoticed.
            const ffui::ChromeBand band = ffui::ChromeLocationBand(layout);
            Check(band.bottom <= layout.chromeHeight + 0.001f,
                  "location band stays inside the chrome at every width");
            Check(layout.back.bottom() <= layout.chromeHeight + 0.001f &&
                      layout.forward.bottom() <= layout.chromeHeight + 0.001f &&
                      layout.up.bottom() <= layout.chromeHeight + 0.001f &&
                      layout.settings.bottom() <= layout.chromeHeight + 0.001f &&
                      layout.newTab.bottom() <= layout.chromeHeight + 0.001f &&
                      layout.search.bottom() <= layout.chromeHeight + 0.001f &&
                      layout.tabs.overflowButton.bottom() <= layout.chromeHeight + 0.001f,
                  "every chrome control stays inside the chrome height");
        }
    }
}

} // namespace

int main() {
    TestWidthClassification();
    TestCommandFamilyGeometry();
    TestLocationAndSearchReservation();
    TestLocationBandInsideChrome();
    TestTabWidthPolicy();
    TestTabOverflowWindow();
    TestCompositionInvariants();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
