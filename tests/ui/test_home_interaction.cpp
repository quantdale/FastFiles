// quick-access-home 4.6: headless tests for the pure Home interaction seams.
// These exercise the pure hit-test / focus-movement math and the layout seam
// consumption only -- no renderer, no engine, no shell (mirrors the existing
// test_column_hit_test.cpp pattern, which includes ColumnView.h for the inline
// geometry seams).

#include "ColumnView.h"
#include "ffui/HomeLayout.h"

#include <cstdio>

#include "TestSupport.h"

namespace {

ffui::HomeItemLayout MakeItem(int globalIndex, float x, float y, float width, float height) {
    ffui::HomeItemLayout item{};
    item.globalIndex = globalIndex;
    item.x = x;
    item.y = y;
    item.width = width;
    item.height = height;
    return item;
}

} // namespace

int main() {
    using ffui::ComputeHomeItemHitTest;
    using ffui::HomeMoveFocus;
    using ffui::HomeFirstItem;
    using ffui::HomeLastItem;
    using ffui::ComputeHomeLayout;

    // --- ComputeHomeItemHitTest -------------------------------------------------
    ffui::HomeLayoutResult hitLayout;
    hitLayout.items = {
        MakeItem(0, 0.0f, 0.0f, 100.0f, 40.0f),
        MakeItem(1, 110.0f, 0.0f, 100.0f, 40.0f),
        MakeItem(2, 0.0f, 50.0f, 100.0f, 40.0f),
    };

    const int hitInside = ComputeHomeItemHitTest(hitLayout, D2D1::Point2F(150.0f, 10.0f), 0.0f);
    fftest::Check(hitInside == 1, "point inside the second item returns its globalIndex");

    const int miss = ComputeHomeItemHitTest(hitLayout, D2D1::Point2F(300.0f, 300.0f), 0.0f);
    fftest::Check(miss == -1, "point outside every item returns -1");

    // A point above the content area (within the status-badge offset) must not
    // match an item that is only below the badge.
    const int aboveContent = ComputeHomeItemHitTest(hitLayout, D2D1::Point2F(10.0f, 10.0f), 28.0f);
    fftest::Check(aboveContent == -1, "point within the badge offset returns -1");

    // Boundary: the right/bottom edges are inclusive per the rect test.
    const int onEdge = ComputeHomeItemHitTest(hitLayout, D2D1::Point2F(100.0f, 20.0f), 0.0f);
    fftest::Check(onEdge == 0, "point on an item's right edge is a hit");

    // Dual-column geometry: a wider layout places item 1 in the second column,
    // but the seam only cares about rect containment, so the same point still
    // resolves to globalIndex 1.
    ffui::HomeLayoutResult twoColHit;
    twoColHit.items = {
        MakeItem(0, 0.0f, 0.0f, 400.0f, 56.0f),
        MakeItem(1, 408.0f, 0.0f, 400.0f, 56.0f),
    };
    const int dualHit = ComputeHomeItemHitTest(twoColHit, D2D1::Point2F(500.0f, 28.0f), 0.0f);
    fftest::Check(dualHit == 1, "second-column item hit returns the correct globalIndex");

    // --- HomeMoveFocus (grid-aware) -------------------------------------------
    ffui::HomeLayoutResult twoCol;
    twoCol.columnCount = 2;
    twoCol.totalItemCount = 5;
    fftest::Check(HomeMoveFocus(twoCol, 0, 0, 1) == 2, "2-col Down from 0 -> 2");
    fftest::Check(HomeMoveFocus(twoCol, 0, 0, -1) == 0, "2-col Up from 0 clamps at 0");
    fftest::Check(HomeMoveFocus(twoCol, 0, 1, 0) == 1, "2-col Right from 0 -> 1");
    fftest::Check(HomeMoveFocus(twoCol, 0, -1, 0) == 0, "2-col Left from 0 clamps at 0");
    fftest::Check(HomeMoveFocus(twoCol, 4, 0, 1) == 4, "2-col Down from last clamps at last");

    ffui::HomeLayoutResult oneCol;
    oneCol.columnCount = 1;
    oneCol.totalItemCount = 5;
    fftest::Check(HomeMoveFocus(oneCol, 0, 0, 1) == 1, "1-col Down from 0 -> 1");
    fftest::Check(HomeMoveFocus(oneCol, 0, 1, 0) == 0, "1-col Right is a no-op (clamps at 0)");

    // A -1 ("no focus") index with any movement selects the first item.
    fftest::Check(HomeMoveFocus(twoCol, -1, 1, 0) == 0, "from -1 with any delta -> 0");

    ffui::HomeLayoutResult emptyMove;
    emptyMove.columnCount = 1;
    emptyMove.totalItemCount = 0;
    fftest::Check(HomeMoveFocus(emptyMove, 0, 0, 1) == -1, "empty layout movement returns -1");

    // --- HomeFirstItem / HomeLastItem -----------------------------------------
    fftest::Check(HomeFirstItem(oneCol) == 0, "HomeFirstItem returns 0");
    fftest::Check(HomeLastItem(oneCol) == 4, "HomeLastItem returns the last index");
    fftest::Check(HomeFirstItem(emptyMove) == -1, "HomeFirstItem on empty returns -1");

    // --- HomeLayoutResult consumption sanity ----------------------------------
    ffui::HomeLayoutInput input;
    input.viewportWidth = 1280.0f;
    input.viewportHeight = 800.0f;
    input.pinnedCount = 2;
    input.recentFolderCount = 3;
    input.recentSearchCount = 1;
    input.knownFolderCount = 2;
    input.showActions = true;
    const ffui::HomeLayoutResult result = ComputeHomeLayout(input);
    fftest::Check(result.totalItemCount == 13,
                  "total item count sums all sections including the 5 quick actions");
    fftest::Check(result.items.size() == 13u, "items vector size matches total item count");
    bool ascending = true;
    for (size_t i = 0; i < result.items.size(); ++i) {
        if (result.items[i].globalIndex != static_cast<int>(i)) {
            ascending = false;
            break;
        }
    }
    fftest::Check(ascending, "items are emitted in ascending global index order");

    // All-zero sections with actions off yield no items at all.
    ffui::HomeLayoutInput emptyInput;
    emptyInput.viewportWidth = 1280.0f;
    emptyInput.showActions = false;
    const ffui::HomeLayoutResult emptyResult = ComputeHomeLayout(emptyInput);
    fftest::Check(emptyResult.totalItemCount == 0,
                  "all-zero sections with actions off yields 0 items");

    // --- BuildHomeFocusList / HomeMoveFocusEntry / ComputeHomeActivation -----
    // quick-access-home hardening (HOME-POST-02/03): the Clear affordances are
    // real keyboard focus targets; the empty-Pinned hint placeholder and
    // unavailable known folders (empty path) are not focusable or activatable.
    ffui::HomeData data;
    data.pinned.clear(); // -> the "Pin folders..." hint placeholder (not focusable)
    data.recentFolders = {L"C:\\A", L"C:\\B"};
    data.recentSearches = {L"query"};
    data.knownFolders = {
        {L"C:\\Users\\Desktop", L"Desktop"},   // available
        {L"", L"Documents"},                   // unavailable: empty path
        {L"C:\\Users\\Downloads", L"Downloads"}}; // available
    data.showActions = true;

    // 2-column layout: Pinned placeholder (global 0), Recent folders (1,2),
    // Recent searches (3), Common locations (4=Desktop, 5=Documents, 6=Downloads),
    // Quick actions (7..11).
    ffui::HomeLayoutInput focusInput;
    focusInput.viewportWidth = 1280.0f;
    focusInput.pinnedCount = 1;
    focusInput.recentFolderCount = 2;
    focusInput.recentSearchCount = 1;
    focusInput.knownFolderCount = 3;
    focusInput.showActions = true;
    const ffui::HomeLayoutResult focusLayout = ComputeHomeLayout(focusInput);
    fftest::Check(focusLayout.columnCount == 2, "the focus-test layout is two columns");

    const std::vector<ffui::HomeFocusEntry> focusList = BuildHomeFocusList(focusLayout, data);
    fftest::Check(focusList.size() == 12,
                  "focus list has 10 items + 2 Clear stops (placeholder and unavailable folder skipped)");
    fftest::Check(focusList[0] == ffui::HomeFocusEntry{ffui::HomeFocusTarget::ClearRecentFolders, -1},
                  "Clear Recent folders precedes its section's items");
    fftest::Check(focusList[3] == ffui::HomeFocusEntry{ffui::HomeFocusTarget::ClearRecentSearches, -1},
                  "Clear Recent searches precedes its section's items");
    bool noPlaceholder = true;
    bool noUnavailable = true;
    for (const auto& entry : focusList) {
        if (entry.target == ffui::HomeFocusTarget::Item) {
            if (entry.itemIndex == 0) noPlaceholder = false;    // Pinned hint
            if (entry.itemIndex == 5) noUnavailable = false;    // Documents (empty path)
        }
    }
    fftest::Check(noPlaceholder, "the empty-Pinned hint is not a focus target");
    fftest::Check(noUnavailable, "the unavailable known folder is not a focus target");

    // Home/End and initial focus land on items only (never on a Clear stop).
    fftest::Check(HomeFirstFocusEntry(focusList) == ffui::HomeFocusEntry{ffui::HomeFocusTarget::Item, 1},
                  "initial/Home focus selects the first item");
    fftest::Check(HomeLastFocusEntry(focusList) == ffui::HomeFocusEntry{ffui::HomeFocusTarget::Item, 11},
                  "End focus selects the last item");

    // Movement, 2 columns.
    auto move = [&](ffui::HomeFocusEntry current, int dx, int dy) {
        return HomeMoveFocusEntry(focusLayout, focusList, current, dx, dy);
    };
    fftest::Check(move({}, 0, 1) == ffui::HomeFocusEntry{ffui::HomeFocusTarget::Item, 1},
                  "any movement with no focus selects the first item");
    fftest::Check(move({ffui::HomeFocusTarget::Item, 1}, 0, 1) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::ClearRecentSearches, -1},
                  "2-col Down from the first Recent folders item lands on the next section's Clear");
    fftest::Check(move({ffui::HomeFocusTarget::ClearRecentSearches, -1}, 0, 1) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::Item, 3},
                  "Down from Clear Recent searches moves to the first Recent searches item");
    fftest::Check(move({ffui::HomeFocusTarget::Item, 1}, 0, -1) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::ClearRecentFolders, -1},
                  "Up from the first Recent folders item lands on its section's Clear");
    fftest::Check(move({ffui::HomeFocusTarget::ClearRecentFolders, -1}, 0, 1) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::Item, 1},
                  "Down from Clear Recent folders moves to the first Recent folders item");
    fftest::Check(move({ffui::HomeFocusTarget::ClearRecentFolders, -1}, 0, -1) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::ClearRecentFolders, -1},
                  "Up from the top Clear stop stays put");
    fftest::Check(move({ffui::HomeFocusTarget::Item, 3}, 0, -1) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::ClearRecentSearches, -1},
                  "Up from the first Recent searches item lands on Clear Recent searches");
    fftest::Check(move({ffui::HomeFocusTarget::Item, 4}, 0, 1) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::Item, 6},
                  "2-col Down from Desktop skips the unavailable Documents and lands on Downloads");
    fftest::Check(move({ffui::HomeFocusTarget::Item, 1}, 1, 0) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::Item, 2},
                  "Right moves to the neighboring column item");
    fftest::Check(move({ffui::HomeFocusTarget::Item, 4}, 1, 0) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::Item, 4},
                  "Right from Desktop stays put when the neighbor is unavailable");
    fftest::Check(move({ffui::HomeFocusTarget::Item, 11}, 0, 1) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::Item, 11},
                  "Down from the last item clamps in place");
    fftest::Check(move({ffui::HomeFocusTarget::ClearRecentFolders, -1}, 1, 0) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::ClearRecentFolders, -1},
                  "horizontal movement from a Clear stop is a no-op");

    // Movement, 1 column: intra-section grid jumps stay intact.
    ffui::HomeLayoutInput narrowInput = focusInput;
    narrowInput.viewportWidth = 400.0f;
    const ffui::HomeLayoutResult narrowLayout = ComputeHomeLayout(narrowInput);
    fftest::Check(narrowLayout.columnCount == 1, "the narrow focus-test layout is one column");
    const std::vector<ffui::HomeFocusEntry> narrowList = BuildHomeFocusList(narrowLayout, data);
    auto moveNarrow = [&](ffui::HomeFocusEntry current, int dx, int dy) {
        return HomeMoveFocusEntry(narrowLayout, narrowList, current, dx, dy);
    };
    fftest::Check(moveNarrow({ffui::HomeFocusTarget::Item, 1}, 0, 1) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::Item, 2},
                  "1-col Down between Recent folders items is a plain grid move");
    fftest::Check(moveNarrow({ffui::HomeFocusTarget::Item, 2}, 0, 1) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::ClearRecentSearches, -1},
                  "1-col Down from the last Recent folders item lands on Clear Recent searches");
    fftest::Check(moveNarrow({ffui::HomeFocusTarget::ClearRecentSearches, -1}, 0, 1) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::Item, 3},
                  "1-col Down from Clear Recent searches moves to the Recent searches item");
    fftest::Check(moveNarrow({ffui::HomeFocusTarget::Item, 4}, 0, 1) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::Item, 6},
                  "1-col Down from Desktop skips the unavailable Documents");

    // Empty layout: movement yields no focus.
    const std::vector<ffui::HomeFocusEntry> emptyList = BuildHomeFocusList(emptyResult, data);
    fftest::Check(HomeMoveFocusEntry(emptyResult, emptyList, {}, 0, 1) ==
                      ffui::HomeFocusEntry{ffui::HomeFocusTarget::None, -1},
                  "movement on an empty focus list yields no focus");

    // --- ComputeHomeActivation (HOME-POST-02) --------------------------------
    using ffui::ComputeHomeActivation;
    using ffui::HomeActionKind;
    const auto activate = [&](int globalIndex, bool newTab) {
        return ComputeHomeActivation(data, focusLayout, globalIndex, newTab);
    };
    const auto desktop = activate(4, false);
    fftest::Check(desktop.has_value() && desktop->kind == HomeActionKind::Folder &&
                  desktop->value == L"C:\\Users\\Desktop" && !desktop->openInNewTab,
                  "an available known folder activates as a Folder");
    const auto downloads = activate(6, true);
    fftest::Check(downloads.has_value() && downloads->kind == HomeActionKind::Folder &&
                  downloads->value == L"C:\\Users\\Downloads" && downloads->openInNewTab,
                  "an available known folder activates as a Folder with new-tab");
    fftest::Check(!activate(5, false).has_value(),
                  "the unavailable known folder (empty path) cannot produce activation");
    fftest::Check(!activate(5, true).has_value(),
                  "the unavailable known folder cannot open in a new tab either");
    fftest::Check(!activate(0, false).has_value(),
                  "the empty-Pinned hint placeholder cannot produce activation");
    const auto recentFolder = activate(1, false);
    fftest::Check(recentFolder.has_value() && recentFolder->kind == HomeActionKind::Folder &&
                  recentFolder->value == L"C:\\A",
                  "a Recent folders item activates as a Folder");
    const auto recentSearch = activate(3, false);
    fftest::Check(recentSearch.has_value() && recentSearch->kind == HomeActionKind::Search &&
                  recentSearch->value == L"query",
                  "a Recent searches item activates as a Search");
    const auto quickAction = activate(7, false);
    fftest::Check(quickAction.has_value() && quickAction->kind == HomeActionKind::QuickAction &&
                  quickAction->value == L"navigation.new-tab",
                  "a Quick action activates as its command");
    fftest::Check(!activate(999, false).has_value(),
                  "an unknown global index cannot produce activation");

    // Defensive: a malformed empty-path Recent folder entry is not activatable.
    ffui::HomeData malformedData = data;
    malformedData.recentFolders = {L""};
    ffui::HomeLayoutInput malformedInput = focusInput;
    malformedInput.recentFolderCount = 1;
    malformedInput.pinnedCount = 1;
    malformedInput.recentSearchCount = 0;
    malformedInput.knownFolderCount = 0;
    malformedInput.showActions = false;
    const ffui::HomeLayoutResult malformedLayout = ComputeHomeLayout(malformedInput);
    const auto malformedActivation = ComputeHomeActivation(malformedData, malformedLayout, 1, false);
    fftest::Check(!malformedActivation.has_value(),
                  "an empty-path Recent folder entry never produces a Folder activation");

    // --- HomeActionKindForFocusTarget (HOME-POST-03) --------------------------
    using ffui::HomeActionKindForFocusTarget;
    fftest::Check(HomeActionKindForFocusTarget(ffui::HomeFocusTarget::ClearRecentFolders) ==
                      HomeActionKind::ClearRecentFolders,
                  "Clear Recent folders focus target maps to its activation kind");
    fftest::Check(HomeActionKindForFocusTarget(ffui::HomeFocusTarget::ClearRecentSearches) ==
                      HomeActionKind::ClearRecentSearches,
                  "Clear Recent searches focus target maps to its activation kind");

    return fftest::FailureCount() == 0 ? 0 : 1;
}
