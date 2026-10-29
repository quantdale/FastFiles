#pragma once

#include <cstddef>
#include <string>

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

constexpr bool StorageRowsAreFiles(StorageViewMode mode) noexcept {
    return mode == StorageViewMode::DrillDown || mode == StorageViewMode::LargestFolders ||
           mode == StorageViewMode::LargestFiles;
}

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

inline std::wstring StorageOverviewStatusText(size_t volumeCount, bool engineActive) {
    std::wstring text = std::to_wstring(volumeCount) + L" fixed volumes";
    if (!engineActive) text += L" (degraded mode — capacity from OS only, coverage partial)";
    return text;
}

inline std::wstring StorageItemsStatusText(size_t itemCount, bool engineActive) {
    std::wstring text = std::to_wstring(itemCount) + L" items in this folder";
    if (!engineActive) text += L" (degraded mode — partial coverage)";
    return text;
}

inline constexpr const wchar_t* StorageCoverageText(bool rootAvailable) noexcept {
    return rootAvailable ? L"Root available — completeness unverified" : L"Coverage unverified";
}

inline const wchar_t* StorageWaitingStatusText(bool engineActive) noexcept {
    return engineActive ? L"Waiting for index data…" : L"Degraded mode — browsing via FindFirstFileEx";
}

inline const wchar_t* StorageNotIndexedStatusText(bool engineActive) noexcept {
    return engineActive ? L"Folder not yet indexed." : L"Degraded mode — folder not yet browsed";
}

inline const wchar_t* StorageUnavailableStatusText(bool engineActive) noexcept {
    return engineActive ? L"This folder is not accessible." : L"Degraded mode — folder not accessible";
}

inline constexpr const wchar_t* StorageUnavailableVolumeStatusText() noexcept {
    return L"Volume unavailable — capacity figures are stale; drill-down disabled until it reconnects.";
}

} // namespace ffui
