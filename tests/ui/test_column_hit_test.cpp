#include "ColumnView.h"

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
                                             0.0f, 0.0f, width, false, 0);
    Check(single.columnIndex == 0 && single.itemIndex == 2,
          "single-pane points map to the expected column and row");

    const auto rightPane = ComputeColumnHitTest(D2D1::Point2F(width / 2.0f + 12.0f, badge + row + 1.0f),
                                                0.0f, 0.0f, width, true, 1);
    Check(rightPane.columnIndex == 0 && rightPane.itemIndex == 1,
          "right-pane points subtract the pane origin before hit testing");

    const auto scrolled = ComputeColumnHitTest(D2D1::Point2F(width / 2.0f + 12.0f, badge + 3.0f),
                                               2.0f * ffui::UiMetrics::kColumnWidth,
                                               row, width, true, 1);
    Check(scrolled.columnIndex == 2 && scrolled.itemIndex == 1,
          "horizontal and vertical offsets are applied in content coordinates");

    const auto wrongPane = ComputeColumnHitTest(D2D1::Point2F(12.0f, badge + 1.0f),
                                                0.0f, 0.0f, width, true, 1);
    Check(wrongPane.columnIndex == -1 && wrongPane.itemIndex == -1,
          "a point outside the active pane cannot activate a row");

    const auto badgePoint = ComputeColumnHitTest(D2D1::Point2F(12.0f, badge - 1.0f),
                                                 0.0f, 0.0f, width, false, 0);
    Check(badgePoint.columnIndex == -1 && badgePoint.itemIndex == -1,
          "status badge points are not treated as rows");

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

    return failures == 0 ? 0 : 1;
}
