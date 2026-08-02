#pragma once

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace ffui {
enum class AddressBarMode { Breadcrumb, EditableText };
enum class PathCommitResult { Navigated, Invalid, NotFound };
struct NavigationContext { std::wstring currentPath; std::vector<std::wstring> columnPaths; std::vector<int> columnSelections; std::vector<float> columnScrollOffsets; std::vector<std::wstring> history; size_t historyIndex = 0; AddressBarMode addressBarMode = AddressBarMode::Breadcrumb; std::wstring addressBarText; std::wstring addressBarError; };
struct Bookmark { std::wstring path; std::wstring displayName; };
struct BreadcrumbSegment { std::wstring label; std::wstring path; };
struct WorkspaceState { std::vector<Bookmark> bookmarks; std::deque<std::wstring> recentlyClosedTabs; bool sidebarCollapsed = false; bool drivesCollapsed = false; bool knownFoldersCollapsed = false; bool bookmarksCollapsed = false; };
// Plain UI state: no IPC/engine handles. All contexts use the shared EngineClient snapshot.
class NavigationWorkspace {
public:
    explicit NavigationWorkspace(std::wstring defaultPath);
    NavigationContext& ActiveContext(); const NavigationContext& ActiveContext() const;
    size_t ActiveTabIndex() const { return activeTab_; } size_t TabCount() const { return tabs_.size(); }
    std::wstring TabPath(size_t index) const;
    bool IsDualPane() const; bool CanGoBack() const; bool CanGoForward() const;
    void Navigate(const std::wstring& path); bool GoBack(); bool GoForward();
    std::vector<BreadcrumbSegment> Breadcrumbs() const;
    void BeginAddressBarEdit(); void CancelAddressBarEdit();
    bool NavigateBreadcrumb(size_t segmentIndex);
    void OpenTab(const std::optional<std::wstring>& path = std::nullopt); bool CloseActiveTab(); bool SwitchTab(size_t index); bool ReopenClosedTab();
    void EnableDualPane(); void DisableDualPane(); void ActivatePane(size_t paneIndex);
    // Returns the other (inactive) pane's current location in dual-pane mode
    // (the base folder Copy Relative Path resolves against per the
    // shell-integration-and-commands spec), or nullopt when only a single
    // pane exists.
    std::optional<std::wstring> OtherPanePath() const;
    void AddBookmark(const std::wstring& path, std::wstring displayName = {}); bool RenameBookmark(size_t index, const std::wstring& displayName); bool ReorderBookmark(size_t from, size_t to); bool RemoveBookmark(size_t index);
    bool HasPendingStateSave() const { return stateDirty_; }
    void MarkStateDirty() { stateDirty_ = true; }
    bool FlushState();
    const WorkspaceState& State() const { return state_; } WorkspaceState& State() { return state_; }
    static std::vector<std::wstring> EnumerateDrives(); std::vector<Bookmark> EnumerateKnownFolders();
    void ReResolveKnownFolders();
    static bool LoadState(WorkspaceState& state); static bool SaveState(const WorkspaceState& state);
private:
    struct Tab { std::vector<NavigationContext> panes; size_t activePane = 0; };
    static NavigationContext MakeContext(const std::wstring& path); static std::wstring StateFilePath(); Tab& ActiveTab(); const Tab& ActiveTab() const;
    std::wstring defaultPath_; std::vector<Tab> tabs_; size_t activeTab_ = 0; WorkspaceState state_; bool stateDirty_ = false;
    std::vector<Bookmark> knownFoldersCache_; bool knownFoldersDirty_ = true;
};
struct ParsedPath { std::wstring path; std::wstring error; };
ParsedPath ParseNavigationPath(const std::wstring& input);
PathCommitResult CommitAddressBarPath(NavigationWorkspace& workspace, const std::wstring& input);
} // namespace ffui
