#include "ColumnView.h"
#include "UiaGeometry.h"

#include <cstdio>

namespace {

int failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

} // namespace

int main() {
    using ffui::ComputeColumnHitTest;
    const float badge = ffui::UiMetrics::kBadgeHeight;
    const float row = ffui::UiMetrics::kRowHeight;
    const float width = 1000.0f;

    const auto single = ComputeColumnHitTest(D2D1::Point2F(12.0f, badge + 2.0f * row + 1.0f),
                                             0.0f, 0.0f, 0.0f, width, false, 0);
    Check(single.columnIndex == 0 && single.itemIndex == 2,
          "single-pane points map to the expected column and row");

    const auto rightPane = ComputeColumnHitTest(D2D1::Point2F(width / 2.0f + 12.0f, badge + row + 1.0f),
                                                0.0f, 0.0f, 0.0f, width, true, 1);
    Check(rightPane.columnIndex == 0 && rightPane.itemIndex == 1,
          "right-pane points subtract the pane origin before hit testing");

    const auto scrolled = ComputeColumnHitTest(D2D1::Point2F(width / 2.0f + 12.0f, badge + 3.0f),
                                               2.0f * ffui::UiMetrics::kColumnWidth,
                                               row, 0.0f, width, true, 1);
    Check(scrolled.columnIndex == 2 && scrolled.itemIndex == 1,
          "horizontal and vertical offsets are applied in content coordinates");

    const auto wrongPane = ComputeColumnHitTest(D2D1::Point2F(12.0f, badge + 1.0f),
                                                0.0f, 0.0f, 0.0f, width, true, 1);
    Check(wrongPane.columnIndex == -1 && wrongPane.itemIndex == -1,
          "a point outside the active pane cannot activate a row");

    const auto badgePoint = ComputeColumnHitTest(D2D1::Point2F(12.0f, badge - 1.0f),
                                                 0.0f, 0.0f, 0.0f, width, false, 0);
    Check(badgePoint.columnIndex == -1 && badgePoint.itemIndex == -1,
          "status badge points are not treated as rows");

    // selection-input-hardening: the status-banner inset must be part of the
    // hit-test so render and input share one content origin.
    const float bannerInset = ffui::ColumnContentTopInset(ffui::SnapshotViewState::Loading);
    Check(bannerInset == ffui::kStatusBannerInsetDips,
          "a Loading column carries the full status-banner inset");
    Check(ffui::ColumnContentTopInset(ffui::SnapshotViewState::Current) == 0.0f,
          "a Current column carries no status-banner inset");
    const auto withBanner = ComputeColumnHitTest(D2D1::Point2F(12.0f, badge + bannerInset + 3.0f * row + 1.0f),
                                                 0.0f, 0.0f, bannerInset, width, false, 0);
    Check(withBanner.columnIndex == 0 && withBanner.itemIndex == 3,
          "with the banner visible the same visual row still hit-tests to row 3");
    // A point near the bottom of visual row 0: ignoring the inset resolves to
    // the next row (the off-by-one the audit observed).
    const float nearRowBottom = badge + bannerInset + row - 1.0f;
    const auto hitWithInset = ComputeColumnHitTest(D2D1::Point2F(12.0f, nearRowBottom),
                                                   0.0f, 0.0f, bannerInset, width, false, 0);
    const auto hitWithoutInset = ComputeColumnHitTest(D2D1::Point2F(12.0f, nearRowBottom),
                                                      0.0f, 0.0f, 0.0f, width, false, 0);
    Check(hitWithInset.itemIndex == 0,
          "with the shared inset the point lands on visual row 0");
    Check(hitWithoutInset.itemIndex == 1,
          "omitting the inset shifts the hit one row down (the old defect)");
    Check(ffui::ComputeColumnIndexAtX(D2D1::Point2F(12.0f, badge), 0.0f, width, false, 0) == 0 &&
          ffui::ComputeColumnIndexAtX(D2D1::Point2F(-1.0f, badge), 0.0f, width, false, 0) == -1,
          "ComputeColumnIndexAtX resolves the column before the inset lookup");

    // selection-input-hardening: Enter opens files, navigates directories.
    Check(ffui::EnterOpensFile(true, false), "Enter over a focused file requests the open path");
    Check(!ffui::EnterOpensFile(true, true), "Enter over a directory keeps navigate-into-column");
    Check(!ffui::EnterOpensFile(false, false), "Enter with no focused item is a no-op");

    const std::vector<ffui::SelectionItem> fileSelection{{L"C:\\file.txt", false}};
    const std::vector<ffui::SelectionItem> folderSelection{{L"C:\\Folder", true}};
    const std::vector<ffui::SelectionItem> mixedSelection{{L"C:\\file.txt", false},
                                                           {L"C:\\other.txt", false}};
    Check(ffui::ShouldOpenSelectionOnDoubleClick(fileSelection),
          "a single selected file is opened by the double-click policy");
    Check(!ffui::ShouldOpenSelectionOnDoubleClick(folderSelection),
          "a folder is not activated twice by the double-click policy");
    Check(!ffui::ShouldOpenSelectionOnDoubleClick(mixedSelection),
          "multi-selection is not opened by the single-file double-click policy");

    const auto driveChain = ffui::BuildNavigationPathChain(L"C:\\Users\\Alice");
    Check(driveChain == std::vector<std::wstring>{L"C:\\", L"C:\\Users", L"C:\\Users\\Alice"},
          "direct drive navigation materializes every Finder-style ancestor column");
    const auto uncChain = ffui::BuildNavigationPathChain(L"\\\\server\\share\\folder");
    Check(uncChain == std::vector<std::wstring>{L"\\\\server\\share", L"\\\\server\\share\\folder"},
          "direct UNC navigation keeps the server/share root visible");
    const auto extendedDriveChain = ffui::BuildNavigationPathChain(L"\\\\?\\C:\\Users\\Alice");
    Check(extendedDriveChain == std::vector<std::wstring>{L"\\\\?\\C:\\", L"\\\\?\\C:\\Users", L"\\\\?\\C:\\Users\\Alice"},
          "extended drive navigation preserves every Finder-style ancestor column");
    const auto extendedUncChain = ffui::BuildNavigationPathChain(L"\\\\?\\UNC\\server\\share\\folder");
    Check(extendedUncChain == std::vector<std::wstring>{L"\\\\?\\UNC\\server\\share", L"\\\\?\\UNC\\server\\share\\folder"},
          "extended UNC navigation keeps the server/share root visible");

    // ui-overhaul phase 3: cut-ghost path join and match contract.
    {
        const std::set<std::wstring> cutPaths{L"C:\\dir\\cut.txt", L"D:\\folder"};
        std::wstring joined;
        ffui::AppendColumnItemPath(joined, L"C:\\dir", L"cut.txt");
        Check(joined == L"C:\\dir\\cut.txt" && ffui::PathIsInCutSet(cutPaths, joined),
              "cut-ghost matches a file under its parent path");
        ffui::AppendColumnItemPath(joined, L"C:\\dir\\", L"cut.txt");
        Check(joined == L"C:\\dir\\cut.txt", "a trailing separator is not doubled when joining");
        ffui::AppendColumnItemPath(joined, L"C:\\other", L"cut.txt");
        Check(!ffui::PathIsInCutSet(cutPaths, joined),
              "a same-named file in another directory is not ghosted");
        ffui::AppendColumnItemPath(joined, L"D:\\", L"folder");
        Check(ffui::PathIsInCutSet(cutPaths, joined), "drive-root joins match the cut set");
        Check(!ffui::PathIsInCutSet({}, joined), "an empty cut set never ghosts a row");

        // The repaint decision rides the same pure contract: repeated copies of
        // the same selection must not repaint, and any real membership delta
        // must.
        const std::set<std::wstring> current{L"C:\\dir\\cut.txt", L"D:\\folder"};
        Check(!ffui::CutPathSetChanged(current, {L"C:\\dir\\cut.txt", L"D:\\folder"}),
              "an identical cut set is not a change");
        Check(!ffui::CutPathSetChanged(current, {L"C:\\dir\\cut.txt", L"D:\\folder", L""}),
              "empty entries do not fabricate a change");
        Check(!ffui::CutPathSetChanged(current, {L"D:\\folder", L"C:\\dir\\cut.txt", L"D:\\folder"}),
              "duplicate and reordered entries are not a change");
        Check(ffui::CutPathSetChanged(current, {L"C:\\dir\\cut.txt"}), "a removal is a change");
        Check(ffui::CutPathSetChanged(current, {L"C:\\dir\\cut.txt", L"D:\\folder", L"E:\\new.txt"}),
              "an addition is a change");
        Check(ffui::CutPathSetChanged({}, {L"C:\\dir\\cut.txt"}), "empty to populated is a change");
        Check(!ffui::CutPathSetChanged({}, {}), "empty to empty is not a change");
    }

    // selection-model-interactions click fix: dual-pane pane-1 round trip.
    // Mirrors the live failing geometry: viewport 1000 DIPs wide (pane width
    // 500), a deep address-bar navigation leaves the terminal column (index 3)
    // past the pane's right edge, the UIA Scroll pattern scrolls pane 1 to the
    // max offset, then a native click lands at the row's reported center. The
    // provider rect (UiaItemRectDip) and the hit-test (ComputeColumnHitTest)
    // must agree on the SAME horizontal offset or the click misses.
    {
        const float paneWidth = width / 2.0f;
        const int terminalColumn = 3;
        const int targetRow = 5; // selF.txt
        const float maxScroll =
            ffui::UiaMaxHorizontalScrollDip(terminalColumn + 1, paneWidth);
        Check(maxScroll > 0.0f,
              "the mirrored fixture geometry actually requires horizontal scroll");
        const D2D1_RECT_F syncedRect = ffui::UiaItemRectDip(
            terminalColumn, targetRow, paneWidth, paneWidth, maxScroll, 0.0f,
            false, 600.0f);
        const D2D1_POINT_2F syncedCenter{
            (syncedRect.left + syncedRect.right) / 2.0f,
            (syncedRect.top + syncedRect.bottom) / 2.0f};
        const auto syncedHit = ComputeColumnHitTest(
            syncedCenter, maxScroll, 0.0f, 0.0f, width, true, 1);
        Check(syncedHit.columnIndex == terminalColumn && syncedHit.itemIndex == targetRow,
              "a pane-1 row center computed with the scrolled offset hit-tests back to that row");

        // The defect: the Scroll-pattern write moved scrollOffset2_ (what
        // render/hit-test use) but not the cached UIA geometry the provider
        // reports from, so the click landed at the pre-scroll position.
        const D2D1_RECT_F staleRect = ffui::UiaItemRectDip(
            terminalColumn, targetRow, paneWidth, paneWidth, 0.0f, 0.0f,
            false, 600.0f);
        const D2D1_POINT_2F staleCenter{
            (staleRect.left + staleRect.right) / 2.0f,
            (staleRect.top + staleRect.bottom) / 2.0f};
        const auto staleHit = ComputeColumnHitTest(
            staleCenter, maxScroll, 0.0f, 0.0f, width, true, 1);
        Check(staleHit.columnIndex != terminalColumn || staleHit.itemIndex != targetRow,
              "a center computed from the stale pre-scroll geometry misses the row (the old defect)");

        // Banner inset states must stay part of the shared computation for
        // pane 1 too: a Loading terminal column shifts rows by the inset in
        // both the provider rect and the hit-test.
        const float inset = ffui::ColumnContentTopInset(ffui::SnapshotViewState::Loading);
        const D2D1_RECT_F bannerRect = ffui::UiaItemRectDip(
            terminalColumn, targetRow, paneWidth, paneWidth, maxScroll, 0.0f,
            true, 600.0f);
        const auto bannerHit = ComputeColumnHitTest(
            D2D1::Point2F((bannerRect.left + bannerRect.right) / 2.0f,
                          (bannerRect.top + bannerRect.bottom) / 2.0f),
            maxScroll, 0.0f, inset, width, true, 1);
        Check(bannerHit.columnIndex == terminalColumn && bannerHit.itemIndex == targetRow,
              "pane-1 centers stay consistent while the status banner is visible");
    }

    {
        const auto previous = ffui::gUiDensityProfile;
        ffui::gUiDensityProfile = ffui::UiDensityProfile::Comfortable;
        const float comfortableRow = ffui::BrowseRowHeight();
        Check(comfortableRow == 34.0f, "Comfortable hit-test uses the 34 DIP row height");
        const auto comfortable = ComputeColumnHitTest(
            D2D1::Point2F(12.0f, badge + 2.0f * comfortableRow + 1.0f),
            0.0f, 0.0f, 0.0f, width, false, 0);
        Check(comfortable.columnIndex == 0 && comfortable.itemIndex == 2,
              "Comfortable row 2 still hit-tests to item 2");
        const D2D1_RECT_F rect = ffui::UiaItemRectDip(0, 2, 0.0f, width, 0.0f, 0.0f, false, 600.0f);
        const auto fromUia = ComputeColumnHitTest(
            D2D1::Point2F((rect.left + rect.right) / 2.0f, (rect.top + rect.bottom) / 2.0f),
            0.0f, 0.0f, 0.0f, width, false, 0);
        Check(fromUia.columnIndex == 0 && fromUia.itemIndex == 2,
              "Comfortable UIA item rect and hit-test agree on the same row");
        ffui::gUiDensityProfile = previous;
    }

    return failures == 0 ? 0 : 1;
}
