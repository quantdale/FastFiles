#pragma once

namespace ffui {

// These are the semantic Storage Analysis presentations.  The policy below
// is intentionally independent of HWNDs so mode ownership can be tested
// without creating a shell window.
enum class StorageViewMode {
    Overview,
    DrillDown,
    LargestFolders,
    LargestFiles,
    ByCategory,
    Treemap
};

enum class StorageControl {
    Back,
    Up,
    OverviewButton,
    DrillDownButton,
    LargestFoldersButton,
    LargestFilesButton,
    ByCategoryButton,
    TreemapButton,
    List,
    OverviewList,
    CategoryFilter,
    Status,
    TreemapSurface
};

// The mode buttons and shared navigation remain available in every mode.
// Only the active mode's data surface and its supporting controls are shown.
constexpr bool StorageControlVisible(StorageViewMode mode, StorageControl control) noexcept {
    switch (control) {
    case StorageControl::Back:
    case StorageControl::Up:
    case StorageControl::OverviewButton:
    case StorageControl::DrillDownButton:
    case StorageControl::LargestFoldersButton:
    case StorageControl::LargestFilesButton:
    case StorageControl::ByCategoryButton:
    case StorageControl::TreemapButton:
        return true;
    case StorageControl::List:
        return mode == StorageViewMode::DrillDown ||
               mode == StorageViewMode::LargestFolders ||
               mode == StorageViewMode::LargestFiles ||
               mode == StorageViewMode::ByCategory;
    case StorageControl::OverviewList:
        return mode == StorageViewMode::Overview;
    case StorageControl::CategoryFilter:
        return mode == StorageViewMode::DrillDown ||
               mode == StorageViewMode::LargestFolders ||
               mode == StorageViewMode::LargestFiles ||
               mode == StorageViewMode::ByCategory;
    case StorageControl::Status:
        return mode != StorageViewMode::Treemap;
    case StorageControl::TreemapSurface:
        return mode == StorageViewMode::Treemap;
    }
    return false;
}

} // namespace ffui
