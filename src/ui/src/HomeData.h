#pragma once
// quick-access-home 4.1: data + activation contracts for the Home surface.
//
// `HomeData` is the one-way data snapshot the shell pushes into `ColumnView`
// (see `ColumnView::SetHomeData`). It is intentionally a plain value type: the
// renderer never mutates it and never reaches back into the workspace. The
// activation result (`HomeActivation`) is the single route every Home item
// takes to drive navigation / search / commands -- the renderer issues no
// engine request of its own (design D2/D4/D11).

#include <string>
#include <vector>

#include "NavigationWorkspace.h" // for ffui::Bookmark

namespace ffui {

// One pushed snapshot of the data Home surfaces. Counts are already bounded
// upstream (<= 10 recents, <= 100 searches, ...) so the layout seam only
// defensively clamps. `Bookmark` carries { path, displayName }.
struct HomeData {
    std::vector<Bookmark> pinned;          // existing bookmarks (Pinned section)
    std::vector<std::wstring> recentFolders;
    std::vector<std::wstring> recentSearches;
    std::vector<Bookmark> knownFolders;   // common locations (known folders)
    bool showActions = true;              // Quick actions strip
};

// quick-access-home 4.2: what a Home item activation means. The shell routes
// `Folder` -> navigate (new tab when `openInNewTab`), `Search` ->
// `SearchPanel::ExecuteQuery`, `QuickAction` -> the command id in `value`.
// The two `Clear*` kinds come from the Clear affordances drawn in the Recent
// folders / Recent searches section headers; the shell invokes the existing
// recent-folder clear and the existing search-history clear respectively.
enum class HomeActionKind { Folder, Search, QuickAction, ClearRecentFolders, ClearRecentSearches };

struct HomeActivation {
    HomeActionKind kind = HomeActionKind::Folder;
    std::wstring value;                    // folder path / search query / command id
    bool openInNewTab = false;             // Folder + Search only
};

} // namespace ffui
