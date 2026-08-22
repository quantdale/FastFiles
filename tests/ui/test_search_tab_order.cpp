// ui-v3 search a11y hardening: headless coverage for the Search panel's
// keyboard focus-order seam (`ffui::NextSearchFocusTarget` /
// `ffui::PreviousSearchFocusTarget`), consumed by
// `SearchPanel::CycleSearchFocus`. Pins the full collapsed and expanded
// Tab/Shift+Tab cycles, the wrap-around edges, the always-present Filters
// disclosure (the only route to the hidden filter row while collapsed), and
// stale-target clamping after a collapse.

#include "ffui/SearchTabOrder.h"

#include "../TestSupport.h"

using namespace fftest;
using ffui::NextSearchFocusTarget;
using ffui::PreviousSearchFocusTarget;
using ffui::SearchFocusTarget;

namespace {
// /analyze constant-folds the constexpr seam when every argument is a
// literal, which turns enum-vs-enum checks into C6326. Routing arguments
// through a volatile round-trip keeps each check a genuine runtime
// comparison without changing the seam itself.
ffui::SearchFocusTarget Runtime(ffui::SearchFocusTarget value) {
    volatile ffui::SearchFocusTarget opaque = value;
    return opaque;
}
bool Runtime(bool value) {
    volatile bool opaque = value;
    return opaque;
}
} // namespace

namespace {

void TestCollapsedForwardCycle() {
    const bool expanded = false;
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::None), expanded) == SearchFocusTarget::QueryEdit,
          "collapsed: None starts the cycle at the query field");
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::QueryEdit), expanded) == SearchFocusTarget::FiltersToggle,
          "collapsed: query -> Filters disclosure");
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::FiltersToggle), expanded) == SearchFocusTarget::ResultList,
          "collapsed: Filters -> result list (hidden filter row is skipped)");
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::ResultList), expanded) == SearchFocusTarget::QueryEdit,
          "collapsed: list wraps back to the query field");
}

void TestExpandedForwardCycle() {
    const bool expanded = true;
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::None), expanded) == SearchFocusTarget::QueryEdit,
          "expanded: None starts the cycle at the query field");
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::QueryEdit), expanded) == SearchFocusTarget::FiltersToggle,
          "expanded: query -> Filters disclosure");
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::FiltersToggle), expanded) == SearchFocusTarget::ScopeCombo,
          "expanded: Filters -> scope");
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::ScopeCombo), expanded) == SearchFocusTarget::SortCombo,
          "expanded: scope -> sort");
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::SortCombo), expanded) == SearchFocusTarget::SortDirection,
          "expanded: sort -> sort direction");
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::SortDirection), expanded) == SearchFocusTarget::ClearHistory,
          "expanded: sort direction -> clear history");
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::ClearHistory), expanded) == SearchFocusTarget::ResultList,
          "expanded: clear history -> result list");
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::ResultList), expanded) == SearchFocusTarget::QueryEdit,
          "expanded: list wraps back to the query field");
}

void TestBackwardCyclesMirrorForward() {
    // Walk the entire collapsed cycle backwards from QueryEdit (query ->
    // list -> Filters) and prove the third hop returns to the start.
    const SearchFocusTarget hop1 = PreviousSearchFocusTarget(Runtime(SearchFocusTarget::QueryEdit), false);
    Check(hop1 == Runtime(SearchFocusTarget::ResultList),
          "collapsed: Shift+Tab from the query field wraps to the list");
    const SearchFocusTarget hop2 = PreviousSearchFocusTarget(hop1, false);
    Check(hop2 == Runtime(SearchFocusTarget::FiltersToggle),
          "collapsed: Shift+Tab from the list lands on Filters");
    Check(PreviousSearchFocusTarget(hop2, false) == Runtime(SearchFocusTarget::QueryEdit),
          "collapsed backward cycle returns to the query field");
    Check(PreviousSearchFocusTarget(Runtime(SearchFocusTarget::None), true) == SearchFocusTarget::ResultList,
          "expanded: Shift+Tab before any stop starts at the list end");

    Check(PreviousSearchFocusTarget(Runtime(SearchFocusTarget::ClearHistory), true) == SearchFocusTarget::SortDirection,
          "expanded: Shift+Tab clear history -> sort direction");
    Check(PreviousSearchFocusTarget(Runtime(SearchFocusTarget::SortDirection), true) == SearchFocusTarget::SortCombo,
          "expanded: Shift+Tab sort direction -> sort");
    Check(PreviousSearchFocusTarget(Runtime(SearchFocusTarget::SortCombo), true) == SearchFocusTarget::ScopeCombo,
          "expanded: Shift+Tab sort -> scope");
    Check(PreviousSearchFocusTarget(Runtime(SearchFocusTarget::ScopeCombo), true) == SearchFocusTarget::FiltersToggle,
          "expanded: Shift+Tab scope -> Filters");
    Check(PreviousSearchFocusTarget(Runtime(SearchFocusTarget::FiltersToggle), true) == SearchFocusTarget::QueryEdit,
          "expanded: Shift+Tab Filters -> query");
    Check(PreviousSearchFocusTarget(Runtime(SearchFocusTarget::QueryEdit), true) == SearchFocusTarget::ResultList,
          "expanded: Shift+Tab query wraps to the list");
}

void TestStaleTargetsClampAfterCollapse() {
    // Collapsing the disclosure while focus sits on an expanded-row control
    // must never strand focus on a hidden HWND: every such target clamps into
    // the visible cycle instead of routing through hidden controls.
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::ScopeCombo), false) == SearchFocusTarget::QueryEdit,
          "stale scope clamps forward into the visible cycle");
    Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::ClearHistory), false) == SearchFocusTarget::QueryEdit,
          "stale clear-history clamps forward into the visible cycle");
    Check(PreviousSearchFocusTarget(Runtime(SearchFocusTarget::SortCombo), false) == SearchFocusTarget::FiltersToggle,
          "stale sort clamps backward onto the always-visible Filters");
    Check(PreviousSearchFocusTarget(Runtime(SearchFocusTarget::SortDirection), false) == SearchFocusTarget::FiltersToggle,
          "stale sort-direction clamps backward onto Filters");
}

void TestFiltersNeverSkipped() {
    // The disclosure is the only way to reach the hidden filter row while
    // collapsed; both directions must keep it in the order in either state.
    // (Compare against concrete neighbors -- /analyze rightly flags
    // constant-vs-constant comparisons, so nothing here compares against a
    // value the seam provably never returns.)
    for (int expandedInt = 0; expandedInt <= 1; ++expandedInt) {
        const bool expanded = expandedInt != 0;
        Check(NextSearchFocusTarget(Runtime(SearchFocusTarget::QueryEdit), expanded) == SearchFocusTarget::FiltersToggle,
              "forward: Filters stays adjacent to the query field in both states");
        Check(PreviousSearchFocusTarget(Runtime(SearchFocusTarget::FiltersToggle), expanded) == SearchFocusTarget::QueryEdit,
              "backward: Filters stays adjacent to the query field in both states");
    }
}

} // namespace

int main() {
    TestCollapsedForwardCycle();
    TestExpandedForwardCycle();
    TestBackwardCyclesMirrorForward();
    TestStaleTargetsClampAfterCollapse();
    TestFiltersNeverSkipped();
    return fftest::FailureCount() == 0 ? 0 : 1;
}
