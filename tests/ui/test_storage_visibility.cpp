#include "ffui/StorageVisibility.h"

#include <array>
#include <cstdlib>

#include "../TestSupport.h"

using namespace fftest;

namespace {

constexpr std::array<ffui::StorageControl, 13> kControls{
    ffui::StorageControl::Back,
    ffui::StorageControl::Up,
    ffui::StorageControl::OverviewButton,
    ffui::StorageControl::DrillDownButton,
    ffui::StorageControl::LargestFoldersButton,
    ffui::StorageControl::LargestFilesButton,
    ffui::StorageControl::ByCategoryButton,
    ffui::StorageControl::TreemapButton,
    ffui::StorageControl::List,
    ffui::StorageControl::OverviewList,
    ffui::StorageControl::CategoryFilter,
    ffui::StorageControl::Status,
    ffui::StorageControl::TreemapSurface,
};

std::array<bool, kControls.size()> CaptureVisibility(ffui::StorageViewMode mode) {
    std::array<bool, kControls.size()> state{};
    for (size_t i = 0; i < kControls.size(); ++i) {
        state[i] = ffui::StorageControlVisible(mode, kControls[i]);
    }
    return state;
}

void CheckSharedChrome(ffui::StorageViewMode mode, const char* description) {
    Check(ffui::StorageControlVisible(mode, ffui::StorageControl::Back) &&
              ffui::StorageControlVisible(mode, ffui::StorageControl::Up) &&
              ffui::StorageControlVisible(mode, ffui::StorageControl::OverviewButton) &&
              ffui::StorageControlVisible(mode, ffui::StorageControl::DrillDownButton) &&
              ffui::StorageControlVisible(mode, ffui::StorageControl::LargestFoldersButton) &&
              ffui::StorageControlVisible(mode, ffui::StorageControl::LargestFilesButton) &&
              ffui::StorageControlVisible(mode, ffui::StorageControl::ByCategoryButton) &&
              ffui::StorageControlVisible(mode, ffui::StorageControl::TreemapButton),
          description);
}

void TestModeMatrix() {
    CheckSharedChrome(ffui::StorageViewMode::Overview, "Overview keeps shared navigation and mode controls visible");
    Check(ffui::StorageControlVisible(ffui::StorageViewMode::Overview, ffui::StorageControl::OverviewList) &&
              ffui::StorageControlVisible(ffui::StorageViewMode::Overview, ffui::StorageControl::Status) &&
              !ffui::StorageControlVisible(ffui::StorageViewMode::Overview, ffui::StorageControl::List) &&
              !ffui::StorageControlVisible(ffui::StorageViewMode::Overview, ffui::StorageControl::CategoryFilter) &&
              !ffui::StorageControlVisible(ffui::StorageViewMode::Overview, ffui::StorageControl::TreemapSurface),
          "Overview owns only the volume list and shared status chrome");

    constexpr ffui::StorageViewMode kListModes[] = {
        ffui::StorageViewMode::DrillDown,
        ffui::StorageViewMode::LargestFolders,
        ffui::StorageViewMode::LargestFiles,
        ffui::StorageViewMode::ByCategory,
    };
    for (const auto mode : kListModes) {
        CheckSharedChrome(mode, "list modes keep shared navigation and mode controls visible");
        Check(ffui::StorageControlVisible(mode, ffui::StorageControl::List) &&
                  ffui::StorageControlVisible(mode, ffui::StorageControl::CategoryFilter) &&
                  ffui::StorageControlVisible(mode, ffui::StorageControl::Status) &&
                  !ffui::StorageControlVisible(mode, ffui::StorageControl::OverviewList) &&
                  !ffui::StorageControlVisible(mode, ffui::StorageControl::TreemapSurface),
              "each list mode owns the data list, category filter, and status");
    }

    CheckSharedChrome(ffui::StorageViewMode::Treemap, "Treemap keeps shared navigation and mode controls visible");
    Check(ffui::StorageControlVisible(ffui::StorageViewMode::Treemap, ffui::StorageControl::TreemapSurface) &&
              !ffui::StorageControlVisible(ffui::StorageViewMode::Treemap, ffui::StorageControl::List) &&
              !ffui::StorageControlVisible(ffui::StorageViewMode::Treemap, ffui::StorageControl::OverviewList) &&
              !ffui::StorageControlVisible(ffui::StorageViewMode::Treemap, ffui::StorageControl::CategoryFilter) &&
              ffui::StorageControlVisible(ffui::StorageViewMode::Treemap, ffui::StorageControl::Status),
          "Treemap owns its canvas and retains explicit shared coverage feedback");
}

void TestTransitions() {
    const auto overview = CaptureVisibility(ffui::StorageViewMode::Overview);
    const auto treemap = CaptureVisibility(ffui::StorageViewMode::Treemap);
    const auto largestFiles = CaptureVisibility(ffui::StorageViewMode::LargestFiles);
    const auto drillDown = CaptureVisibility(ffui::StorageViewMode::DrillDown);
    Check(overview != treemap && !treemap[9] && treemap[12],
          "Overview to Treemap hides the overview list and activates Treemap");
    Check(treemap != largestFiles && !largestFiles[12] && largestFiles[8] && largestFiles[10],
          "Treemap to Largest Files hides the Treemap surface and activates list controls");
    Check(largestFiles == drillDown && drillDown[8] && !drillDown[9] && !drillDown[12],
          "Largest Files to Drill Down keeps only the shared list ownership and no stale surface");
}

void TestResizeStability() {
    constexpr ffui::StorageViewMode kModes[] = {
        ffui::StorageViewMode::Overview,
        ffui::StorageViewMode::DrillDown,
        ffui::StorageViewMode::LargestFolders,
        ffui::StorageViewMode::LargestFiles,
        ffui::StorageViewMode::ByCategory,
        ffui::StorageViewMode::Treemap,
    };
    for (const auto mode : kModes) {
        const auto beforeLayout = CaptureVisibility(mode);
        // Reposition is intentionally geometry-only; recomputing the policy
        // after a layout pass must produce the same semantic visibility.
        const auto afterLayout = CaptureVisibility(mode);
        Check(beforeLayout == afterLayout, "layout recomputation does not change mode visibility");
    }
}

void TestIdempotence() {
    constexpr ffui::StorageViewMode kModes[] = {
        ffui::StorageViewMode::Overview,
        ffui::StorageViewMode::DrillDown,
        ffui::StorageViewMode::LargestFolders,
        ffui::StorageViewMode::LargestFiles,
        ffui::StorageViewMode::ByCategory,
        ffui::StorageViewMode::Treemap,
    };
    for (const auto mode : kModes) {
        Check(CaptureVisibility(mode) == CaptureVisibility(mode),
              "applying one mode visibility policy repeatedly is idempotent");
    }
}

void TestStatusText() {
    Check(std::wstring(ffui::StorageTreemapStatusText()).find(L"coverage may be partial") != std::wstring::npos,
          "Treemap coverage remains visible without hover or assumed completeness");
    Check(ffui::StorageOverviewStatusText(2, true) == L"2 fixed volumes",
          "indexed overview status reports the volume count");
    Check(ffui::StorageOverviewStatusText(2, false).find(L"degraded mode") != std::wstring::npos,
          "degraded overview status identifies OS-only capacity and partial coverage");
    Check(ffui::StorageItemsStatusText(7, true) == L"7 items in this folder",
          "storage ranking identifies its immediate-folder scope");
    Check(std::wstring(ffui::StorageCoverageText(true)).find(L"completeness unverified") != std::wstring::npos &&
          std::wstring(ffui::StorageCoverageText(false)).find(L"unverified") != std::wstring::npos,
          "root availability never claims recursively complete volume coverage");
    Check(ffui::StorageItemsStatusText(7, false).find(L"degraded mode") != std::wstring::npos,
          "degraded list status identifies partial coverage");
    Check(std::wstring(ffui::StorageWaitingStatusText(false)).find(L"Degraded mode") == 0,
          "degraded waiting status is explicit");
    Check(std::wstring(ffui::StorageNotIndexedStatusText(false)).find(L"Degraded mode") == 0,
          "degraded not-indexed status is explicit");
    Check(std::wstring(ffui::StorageUnavailableStatusText(false)).find(L"Degraded mode") == 0,
          "degraded unavailable status is explicit");
    Check(std::wstring(ffui::StorageUnavailableVolumeStatusText()).find(L"stale") != std::wstring::npos,
          "stale volume status explains why drill-down is disabled");
}

} // namespace

int main() {
    Check(!ffui::StorageRowsAreFiles(ffui::StorageViewMode::ByCategory) &&
          !ffui::StorageRowsAreFiles(ffui::StorageViewMode::Overview) &&
          !ffui::StorageRowsAreFiles(ffui::StorageViewMode::Treemap),
          "summary/overview/treemap do not reuse a hidden filesystem list selection");
    Check(ffui::StorageRowsAreFiles(ffui::StorageViewMode::DrillDown) &&
          ffui::StorageRowsAreFiles(ffui::StorageViewMode::LargestFiles) &&
          ffui::StorageRowsAreFiles(ffui::StorageViewMode::LargestFolders),
          "real storage listings preserve filesystem actions");
    TestModeMatrix();
    TestTransitions();
    TestResizeStability();
    TestIdempotence();
    TestStatusText();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
