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
    fftest::Check(result.totalItemCount == 12,
                  "total item count sums all sections including the 4 quick actions");
    fftest::Check(result.items.size() == 12u, "items vector size matches total item count");
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

    return fftest::FailureCount() == 0 ? 0 : 1;
}
