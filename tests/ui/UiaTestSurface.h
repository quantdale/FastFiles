#pragma once
// semantic-uia-core-surfaces 6.x: shared headless UiaSurface fake for the
// provider tests. Mirrors the exact query/action contract of the real
// surfaces (ColumnView/SearchPanel) with configurable state and an action
// log, so the provider tree can be exercised without a window, renderer, or
// engine.

#include <string>
#include <vector>

#include "ffui/UiaSurface.h"

namespace fftest {

class FakeUiaSurface : public ffui::UiaSurface {
public:
    struct FakeItem {
        std::wstring name;
        bool isDirectory = false;
        uint64_t fileIdLow = 0;   // 0 = degraded identity (path-based)
        int64_t volumeRowId = 0;
        bool selected = false;
        bool focused = false;
    };

    struct FakeColumn {
        std::wstring path;
        std::vector<FakeItem> items;
        int focusIndex = -1;
        ffui::UiaColumnStatus status = ffui::UiaColumnStatus::Current;
        std::wstring statusText;
    };

    struct FakePane {
        bool home = false;
        std::vector<FakeColumn> columns;
        int focusedColumn = 0;
        float vScroll = 0.0f;
    };

    struct FakeHomeItem {
        int sectionKind = 0;
        int indexInSection = 0;
        std::wstring primary;
        std::wstring secondary;
        std::wstring value;
        bool enabled = true;
        float x = 0.0f, y = 0.0f, width = 200.0f, height = 40.0f;
    };

    struct FakeHomeSection {
        int sectionKind = 0;
        int itemCount = 0;
        int firstGlobalIndex = 0;
        float headerX = 0.0f, headerY = 0.0f, headerWidth = 400.0f, headerHeight = 28.0f;
        bool clearAffordance = false;
    };

    // --- column-view state -------------------------------------------------
    std::vector<FakePane> panes;   // 1 or 2 entries
    int activePane = 0;
    bool browseContentActive = true;
    ffui::UiaViewGeometry geometry;

    // --- home state ---------------------------------------------------------
    std::vector<FakeHomeSection> homeSections;
    std::vector<FakeHomeItem> homeItems;
    int homeFocusTarget = ffui::kUiaHomeFocusNone;
    int homeFocusIndex = -1;
    int homeColumnCount = 2;
    float homeContentHeight = 800.0f;

    // --- search state -------------------------------------------------------
    std::vector<ffui::UiaSearchResultInfo> searchResults;
    bool searchVisible = false;
    bool searchFocused = false;
    RECT searchRect{0, 0, 800, 600};
    int searchRowHeight = 28;

    // --- action log ---------------------------------------------------------
    std::vector<std::wstring> actions;

    // UiaSurface: navigation surface.
    ffui::UiaViewGeometry UiaGeometry() const override { return geometry; }
    bool UiaBrowseContentActive() const override { return browseContentActive; }
    int UiaPaneCount() const override { return static_cast<int>(panes.size()); }
    int UiaActivePane() const override { return activePane; }
    bool UiaPaneIsHome(int pane) const override {
        return pane >= 0 && pane < static_cast<int>(panes.size()) && panes[static_cast<size_t>(pane)].home;
    }
    int UiaColumnCount(int pane) const override {
        if (pane < 0 || pane >= static_cast<int>(panes.size())) return 0;
        return static_cast<int>(panes[static_cast<size_t>(pane)].columns.size());
    }
    ffui::UiaColumnInfo UiaColumnInfoOf(int pane, int col) const override {
        ffui::UiaColumnInfo info;
        if (pane < 0 || pane >= static_cast<int>(panes.size())) return info;
        const auto& columns = panes[static_cast<size_t>(pane)].columns;
        if (col < 0 || col >= static_cast<int>(columns.size())) return info;
        info.path = columns[static_cast<size_t>(col)].path;
        info.itemCount = static_cast<int>(columns[static_cast<size_t>(col)].items.size());
        info.status = columns[static_cast<size_t>(col)].status;
        info.statusText = columns[static_cast<size_t>(col)].statusText;
        return info;
    }
    ffui::UiaItemInfo UiaItemInfoOf(int pane, int col, int row) const override {
        ffui::UiaItemInfo info;
        if (pane < 0 || pane >= static_cast<int>(panes.size())) return info;
        const auto& columns = panes[static_cast<size_t>(pane)].columns;
        if (col < 0 || col >= static_cast<int>(columns.size())) return info;
        const auto& column = columns[static_cast<size_t>(col)];
        if (row < 0 || row >= static_cast<int>(column.items.size())) return info;
        const auto& item = column.items[static_cast<size_t>(row)];
        info.name = item.name;
        info.isDirectory = item.isDirectory;
        info.volumeRowId = item.volumeRowId;
        info.fileIdLow = item.fileIdLow;
        info.fullPath = column.path + L"\\" + item.name;
        info.selected = item.selected;
        info.focused = item.focused;
        return info;
    }
    int UiaFocusedColumn(int pane) const override {
        if (pane < 0 || pane >= static_cast<int>(panes.size())) return 0;
        return panes[static_cast<size_t>(pane)].focusedColumn;
    }
    int UiaFocusedRow(int pane, int column) const override {
        if (pane < 0 || pane >= static_cast<int>(panes.size())) return -1;
        const auto& columns = panes[static_cast<size_t>(pane)].columns;
        if (column < 0 || column >= static_cast<int>(columns.size())) return -1;
        return columns[static_cast<size_t>(column)].focusIndex;
    }
    float UiaVerticalScroll(int pane) const override {
        if (pane < 0 || pane >= static_cast<int>(panes.size())) return 0.0f;
        return panes[static_cast<size_t>(pane)].vScroll;
    }

    // UiaSurface: home.
    int UiaHomeItemCount(int pane) const override { (void)pane; return static_cast<int>(homeItems.size()); }
    ffui::UiaHomeItemInfo UiaHomeItemInfoOf(int pane, int globalIndex) const override {
        (void)pane;
        ffui::UiaHomeItemInfo info;
        if (globalIndex < 0 || globalIndex >= static_cast<int>(homeItems.size())) return info;
        const auto& item = homeItems[static_cast<size_t>(globalIndex)];
        info.sectionKind = item.sectionKind;
        info.indexInSection = item.indexInSection;
        info.globalIndex = globalIndex;
        info.primary = item.primary;
        info.secondary = item.secondary;
        info.value = item.value;
        info.enabled = item.enabled;
        info.x = item.x;
        info.y = item.y;
        info.width = item.width;
        info.height = item.height;
        return info;
    }
    int UiaHomeSectionCount(int pane) const override { (void)pane; return static_cast<int>(homeSections.size()); }
    ffui::UiaHomeSectionInfo UiaHomeSectionInfoOf(int pane, int section) const override {
        (void)pane;
        ffui::UiaHomeSectionInfo info;
        if (section < 0 || section >= static_cast<int>(homeSections.size())) return info;
        const auto& s = homeSections[static_cast<size_t>(section)];
        info.sectionKind = s.sectionKind;
        info.itemCount = s.itemCount;
        info.firstGlobalIndex = s.firstGlobalIndex;
        info.headerX = s.headerX;
        info.headerY = s.headerY;
        info.headerWidth = s.headerWidth;
        info.headerHeight = s.headerHeight;
        info.clearAffordance = s.clearAffordance;
        return info;
    }
    ffui::UiaHomeScrollInfo UiaHomeScrollInfoOf(int pane) const override {
        (void)pane;
        ffui::UiaHomeScrollInfo info;
        info.columnCount = homeColumnCount;
        info.contentHeightDip = homeContentHeight;
        return info;
    }
    int UiaHomeFocusTarget(int pane) const override { (void)pane; return homeFocusTarget; }
    int UiaHomeFocusIndex(int pane) const override { (void)pane; return homeFocusIndex; }

    // UiaSurface: actions.
    void UiaActivatePane(int pane) override {
        actions.push_back(L"activate-pane:" + std::to_wstring(pane));
        activePane = pane;
    }
    void UiaSelectItem(int pane, int col, int row) override {
        actions.push_back(L"select:" + std::to_wstring(pane) + L":" + std::to_wstring(col) + L":" + std::to_wstring(row));
        auto& column = panes[static_cast<size_t>(pane)].columns[static_cast<size_t>(col)];
        for (auto& item : column.items) item.selected = false;
        column.items[static_cast<size_t>(row)].selected = true;
        column.focusIndex = row;
        panes[static_cast<size_t>(pane)].focusedColumn = col;
    }
    void UiaToggleItem(int pane, int col, int row) override {
        actions.push_back(L"toggle:" + std::to_wstring(pane) + L":" + std::to_wstring(col) + L":" + std::to_wstring(row));
        auto& item = panes[static_cast<size_t>(pane)].columns[static_cast<size_t>(col)].items[static_cast<size_t>(row)];
        item.selected = !item.selected;
    }
    void UiaRangeSelectItem(int pane, int col, int row) override {
        actions.push_back(L"range:" + std::to_wstring(pane) + L":" + std::to_wstring(col) + L":" + std::to_wstring(row));
    }
    void UiaActivateItem(int pane, int col, int row) override {
        actions.push_back(L"activate:" + std::to_wstring(pane) + L":" + std::to_wstring(col) + L":" + std::to_wstring(row));
    }
    void UiaSetItemFocus(int pane, int col, int row) override {
        actions.push_back(L"focus:" + std::to_wstring(pane) + L":" + std::to_wstring(col) + L":" + std::to_wstring(row));
        auto& column = panes[static_cast<size_t>(pane)].columns[static_cast<size_t>(col)];
        column.focusIndex = row;
        panes[static_cast<size_t>(pane)].focusedColumn = col;
    }
    void UiaSetColumnFocus(int pane, int col) override {
        actions.push_back(L"column-focus:" + std::to_wstring(pane) + L":" + std::to_wstring(col));
        panes[static_cast<size_t>(pane)].focusedColumn = col;
    }
    void UiaScrollPane(int pane, float deltaX, float deltaY) override {
        actions.push_back(L"scroll:" + std::to_wstring(pane) + L":" +
                          std::to_wstring(static_cast<int>(deltaX)) + L":" +
                          std::to_wstring(static_cast<int>(deltaY)));
        if (pane >= 0 && pane < static_cast<int>(panes.size())) {
            panes[static_cast<size_t>(pane)].vScroll += deltaY;
        }
    }
    void UiaSetHorizontalScroll(int pane, float offsetDip) override {
        actions.push_back(L"hscroll:" + std::to_wstring(pane) + L":" +
                          std::to_wstring(static_cast<int>(offsetDip)));
    }
    void UiaScrollItemIntoView(int pane, int col, int row) override {
        actions.push_back(L"scroll-item:" + std::to_wstring(pane) + L":" +
                          std::to_wstring(col) + L":" + std::to_wstring(row));
    }
    void UiaActivateHomeItem(int pane, int globalIndex, bool openInNewTab) override {
        actions.push_back(L"home-activate:" + std::to_wstring(pane) + L":" +
                          std::to_wstring(globalIndex) + L":" + std::to_wstring(openInNewTab ? 1 : 0));
    }
    void UiaActivateHomeClear(int pane, int clearKind) override {
        actions.push_back(L"home-clear:" + std::to_wstring(pane) + L":" + std::to_wstring(clearKind));
    }
    void UiaSetHomeFocusItem(int pane, int globalIndex) override {
        actions.push_back(L"home-focus:" + std::to_wstring(pane) + L":" + std::to_wstring(globalIndex));
        homeFocusTarget = ffui::kUiaHomeFocusItem;
        homeFocusIndex = globalIndex;
    }
    void UiaSetHomeFocusClear(int pane, int clearKind) override {
        actions.push_back(L"home-focus-clear:" + std::to_wstring(pane) + L":" + std::to_wstring(clearKind));
        homeFocusTarget = clearKind == ffui::kUiaHomeClearRecentFolders
            ? ffui::kUiaHomeFocusClearRecentFolders : ffui::kUiaHomeFocusClearRecentSearches;
        homeFocusIndex = -1;
    }
    void UiaScrollHomeItemIntoView(int pane, int globalIndex) override {
        actions.push_back(L"home-scroll-item:" + std::to_wstring(pane) + L":" +
                          std::to_wstring(globalIndex));
    }

    // UiaSurface: search.
    size_t UiaSearchResultCount() const override { return searchResults.size(); }
    ffui::UiaSearchResultInfo UiaSearchResultInfoOf(size_t index) const override {
        if (index >= searchResults.size()) return {};
        return searchResults[index];
    }
    bool UiaSearchResultRect(size_t index, RECT& outScreen) const override {
        if (index >= searchResults.size()) return false;
        outScreen = searchRect;
        outScreen.top += static_cast<LONG>(index) * searchRowHeight;
        outScreen.bottom = outScreen.top + searchRowHeight;
        return true;
    }
    bool UiaSearchResultOffscreen(size_t index) const override {
        if (index >= searchResults.size()) return true;
        RECT rect{};
        UiaSearchResultRect(index, rect);
        return rect.bottom <= searchRect.top || rect.top >= searchRect.bottom;
    }
    bool UiaSearchVisible() const override { return searchVisible; }
    bool UiaSearchHasKeyboardFocus() const override { return searchFocused; }
    HWND UiaSearchHwnd() const override { return nullptr; }
    void UiaSearchSelectItem(size_t index) override {
        actions.push_back(L"search-select:" + std::to_wstring(index));
        for (auto& result : searchResults) result.selected = false;
        if (index < searchResults.size()) searchResults[index].selected = true;
    }
    void UiaSearchAddToSelection(size_t index) override {
        actions.push_back(L"search-add:" + std::to_wstring(index));
        if (index < searchResults.size()) searchResults[index].selected = true;
    }
    void UiaSearchRemoveFromSelection(size_t index) override {
        actions.push_back(L"search-remove:" + std::to_wstring(index));
        if (index < searchResults.size()) searchResults[index].selected = false;
    }
    void UiaSearchSetFocusItem(size_t index) override {
        actions.push_back(L"search-focus:" + std::to_wstring(index));
        if (index < searchResults.size()) searchResults[index].focused = true;
    }
    void UiaSearchActivateItem(size_t index) override {
        actions.push_back(L"search-activate:" + std::to_wstring(index));
    }
    void UiaSearchScrollIntoView(size_t index) override {
        actions.push_back(L"search-scroll:" + std::to_wstring(index));
    }
};

} // namespace fftest
