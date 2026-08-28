#include "ffui/SidebarDestinations.h"

#include "../TestSupport.h"

using namespace fftest;

int main() {
    ffui::SidebarDestinationInput empty;
    const auto tools = ffui::BuildSidebarDestinations(empty);
    fftest::Check(tools.size() >= 6, "empty input still emits Home, tools, and three section headers");
    fftest::Check(tools[0].kind == ffui::SidebarDestinationKind::Home && tools[0].label == L"Home",
                  "Home is the first sidebar destination");
    fftest::Check(tools[1].kind == ffui::SidebarDestinationKind::Search && tools[1].label == L"Fast Search",
                  "Fast Search follows Home");
    fftest::Check(tools[2].kind == ffui::SidebarDestinationKind::Storage &&
                      tools[2].label == L"Storage Analysis",
                  "Storage Analysis follows Fast Search");
    fftest::Check(tools[3].kind == ffui::SidebarDestinationKind::Section && tools[3].label == L"Drives",
                  "Drives remain the first folder section");
    fftest::Check(ffui::SidebarDestinationIsTool(tools[0].kind) &&
                      ffui::SidebarDestinationIsTool(tools[1].kind) &&
                      ffui::SidebarDestinationIsTool(tools[2].kind),
                  "Home, Fast Search, and Storage Analysis classify as tools");
    fftest::Check(!ffui::SidebarDestinationTreatAsUnavailable(tools[0]) &&
                      !ffui::SidebarDestinationTreatAsUnavailable(tools[1]) &&
                      !ffui::SidebarDestinationTreatAsUnavailable(tools[2]),
                  "tool destinations with empty paths are not unavailable");

    ffui::SidebarDestinationInput populated;
    populated.drives.push_back({L"C:\\", L"C:\\"});
    populated.knownFolders.push_back({L"C:\\Users\\me\\Documents", L"Documents"});
    populated.bookmarks.push_back({L"D:\\Work", L"Work"});
    const auto rows = ffui::BuildSidebarDestinations(populated);
    fftest::Check(rows[4].kind == ffui::SidebarDestinationKind::Drive && rows[4].path == L"C:\\",
                  "expanded Drives emit filesystem rows after the section header");
    fftest::Check(ffui::SidebarDestinationIsFilesystem(rows[4].kind),
                  "drive rows classify as filesystem destinations");

    ffui::SidebarDestinationInput collapsed;
    collapsed.drivesCollapsed = true;
    collapsed.knownFoldersCollapsed = true;
    collapsed.bookmarksCollapsed = true;
    collapsed.drives.push_back({L"C:\\", L"C:\\"});
    const auto hidden = ffui::BuildSidebarDestinations(collapsed);
    bool sawDriveItem = false;
    for (const auto& row : hidden) {
        if (row.kind == ffui::SidebarDestinationKind::Drive) sawDriveItem = true;
    }
    fftest::Check(!sawDriveItem, "collapsed folder sections omit their items");
    fftest::Check(hidden[0].kind == ffui::SidebarDestinationKind::Home &&
                      hidden[1].kind == ffui::SidebarDestinationKind::Search &&
                      hidden[2].kind == ffui::SidebarDestinationKind::Storage,
                  "collapsed folder sections still keep the product-map tools");

    return fftest::FailureCount() == 0 ? 0 : 1;
}
