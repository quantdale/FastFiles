#pragma once

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "NavigationLocation.h"

namespace ffui {
enum class AddressBarMode { Breadcrumb, EditableText };
enum class PathCommitResult { Navigated, Invalid, NotFound };
// quick-access-home 1.2: history is now typed (std::vector<NavigationLocation>)
// so Back/Forward restores Home exactly; isHomeLocation mirrors the active kind.
struct NavigationContext { std::wstring currentPath; std::vector<std::wstring> columnPaths; std::vector<int> columnSelections; std::vector<float> columnScrollOffsets; std::vector<NavigationLocation> history; size_t historyIndex = 0; AddressBarMode addressBarMode = AddressBarMode::Breadcrumb; std::wstring addressBarText; std::wstring addressBarError; bool isHomeLocation = false; };
struct Bookmark { std::wstring path; std::wstring displayName; };
struct BreadcrumbSegment { std::wstring label; std::wstring path; };
// quick-access-home 2.4: recentFolders is a new, additive, optional persisted
// list (version stays 1; older files simply load an empty list).
struct WorkspaceState { std::vector<Bookmark> bookmarks; std::deque<std::wstring> recentlyClosedTabs; std::deque<std::wstring> recentFolders; std::wstring lastLocation; bool sidebarCollapsed = false; bool drivesCollapsed = false; bool knownFoldersCollapsed = false; bool bookmarksCollapsed = false; };
// Plain UI state: no IPC/engine handles. All contexts use the shared EngineClient snapshot.
class NavigationWorkspace {
public:
    explicit NavigationWorkspace(std::wstring defaultPath);
    NavigationContext& ActiveContext(); const NavigationContext& ActiveContext() const;
    // The active tab owns one context per visible pane. These accessors let
    // the shell persist and restore every pane without changing focus.
    NavigationContext& PaneContext(size_t paneIndex);
    const NavigationContext& PaneContext(size_t paneIndex) const;
    size_t ActivePaneIndex() const;
    size_t ActivePaneCount() const;
    size_t ActiveTabIndex() const { return activeTab_; } size_t TabCount() const { return tabs_.size(); }
    std::wstring TabPath(size_t index) const; NavigationLocation TabLocation(size_t index) const;
    bool IsDualPane() const; bool CanGoBack() const; bool CanGoForward() const;
    void Navigate(const std::wstring& path); void Navigate(const NavigationLocation& location); bool GoBack(); bool GoForward();
    std::vector<BreadcrumbSegment> Breadcrumbs() const;
    void BeginAddressBarEdit(); void CancelAddressBarEdit();
    bool NavigateBreadcrumb(size_t segmentIndex);
    void OpenTab(); void OpenTab(const NavigationLocation& location); void OpenTab(const std::wstring& path); bool CloseActiveTab(); bool SwitchTab(size_t index); bool ReopenClosedTab();
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
    // quick-access-home 2.3: recent-folder history accessors (cleared entries
    // persist via the existing dirty-flag/flush machinery).
    void ClearRecentFolders(); std::vector<std::wstring> RecentFoldersForHome(size_t maxCount) const;
    static bool LoadState(WorkspaceState& state); static bool SaveState(const WorkspaceState& state);
private:
    struct Tab { std::vector<NavigationContext> panes; size_t activePane = 0; };
    static NavigationContext MakeContext(const std::wstring& path); static std::wstring StateFilePath(); Tab& ActiveTab(); const Tab& ActiveTab() const;
    std::wstring defaultPath_; std::vector<Tab> tabs_; size_t activeTab_ = 0; WorkspaceState state_; bool stateDirty_ = false;
    std::vector<Bookmark> knownFoldersCache_; bool knownFoldersDirty_ = true;
};

// quick-access-home 2.5: pure (file-I/O-free) persistence seams so the
// serialization round-trip and older-state migration are headless-testable
// without writing to %LOCALAPPDATA%. SaveState/LoadState are thin wrappers
// around these that only perform file read/write.
std::wstring SerializeState(const WorkspaceState& state);
bool DeserializeState(const std::wstring& json, WorkspaceState& out);

struct ParsedPath { std::wstring path; std::wstring error; };
ParsedPath ParseNavigationPath(const std::wstring& input);
// Chooses the startup location without resurrecting a deleted persisted
// directory. Access-denied locations remain eligible so the destination view
// can present its own permission error state.
std::wstring ResolveInitialNavigationPath(bool restorePreviousSession,
                                          const std::wstring& lastLocation,
                                          const std::wstring& startupLocation);
PathCommitResult CommitAddressBarPath(NavigationWorkspace& workspace, const std::wstring& input);
// Returns the filesystem parent used by the shell's Up/Backspace command.
// A value containing an empty string means "This PC" (the synthetic root);
// nullopt means the current location is already that root or has no parent.
std::optional<std::wstring> ParentNavigationPath(const std::wstring& path);
} // namespace ffui
