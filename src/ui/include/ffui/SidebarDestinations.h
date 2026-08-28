#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ffui {

// final-product-completion-and-production-readiness 2.4: pure sidebar product-map
// order so Home, Fast Search, and Storage Analysis stay above folder groups and
// tool rows never classify as unavailable filesystem targets.

enum class SidebarDestinationKind {
    Home,
    Search,
    Storage,
    Section,
    Drive,
    KnownFolder,
    Bookmark
};

struct SidebarDestination {
    SidebarDestinationKind kind = SidebarDestinationKind::Section;
    std::wstring label;
    std::wstring path;
};

inline bool SidebarDestinationIsTool(SidebarDestinationKind kind) noexcept {
    return kind == SidebarDestinationKind::Home || kind == SidebarDestinationKind::Search ||
           kind == SidebarDestinationKind::Storage;
}

inline bool SidebarDestinationIsFilesystem(SidebarDestinationKind kind) noexcept {
    return kind == SidebarDestinationKind::Drive || kind == SidebarDestinationKind::KnownFolder ||
           kind == SidebarDestinationKind::Bookmark;
}

inline bool SidebarDestinationTreatAsUnavailable(const SidebarDestination& destination) noexcept {
    return destination.path.empty() && !SidebarDestinationIsTool(destination.kind);
}

struct SidebarDestinationInput {
    std::vector<std::pair<std::wstring, std::wstring>> drives;
    std::vector<std::pair<std::wstring, std::wstring>> knownFolders;
    std::vector<std::pair<std::wstring, std::wstring>> bookmarks;
    bool drivesCollapsed = false;
    bool knownFoldersCollapsed = false;
    bool bookmarksCollapsed = false;
};

inline std::vector<SidebarDestination> BuildSidebarDestinations(const SidebarDestinationInput& input) {
    std::vector<SidebarDestination> rows;
    rows.push_back({SidebarDestinationKind::Home, L"Home", {}});
    rows.push_back({SidebarDestinationKind::Search, L"Fast Search", {}});
    rows.push_back({SidebarDestinationKind::Storage, L"Storage Analysis", {}});
    rows.push_back({SidebarDestinationKind::Section, L"Drives", {}});
    if (!input.drivesCollapsed) {
        for (const auto& drive : input.drives) {
            rows.push_back({SidebarDestinationKind::Drive, drive.second, drive.first});
        }
    }
    rows.push_back({SidebarDestinationKind::Section, L"Known Folders", {}});
    if (!input.knownFoldersCollapsed) {
        for (const auto& folder : input.knownFolders) {
            rows.push_back({SidebarDestinationKind::KnownFolder, folder.second, folder.first});
        }
    }
    rows.push_back({SidebarDestinationKind::Section, L"Bookmarks", {}});
    if (!input.bookmarksCollapsed) {
        for (const auto& bookmark : input.bookmarks) {
            rows.push_back({SidebarDestinationKind::Bookmark, bookmark.second, bookmark.first});
        }
    }
    return rows;
}

} // namespace ffui
