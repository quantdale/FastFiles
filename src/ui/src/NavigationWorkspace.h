#pragma once

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "NavigationLocation.h"

namespace ffui {
enum class AddressBarMode { Breadcrumb, EditableText };
enum class PathCommitResult { Navigated, Invalid, NotFound };
// harden-workspace-state-multi-writer D1: the outcome of reading the persisted
// workspace document. Ok = parsed; Missing = no document (defaults are truth);
// Failed = a document exists but cannot be read, decoded, or trusted, so it
// must not be overwritten with defaults until the user actually mutates state.
enum class WorkspaceLoadStatus { Ok, Missing, Failed };
// quick-access-home 1.2: history is now typed (std::vector<NavigationLocation>)
// so Back/Forward restores Home exactly; isHomeLocation mirrors the active kind.
struct NavigationContext { std::wstring currentPath; std::vector<std::wstring> columnPaths; std::vector<int> columnSelections; std::vector<float> columnScrollOffsets; std::vector<NavigationLocation> history; size_t historyIndex = 0; AddressBarMode addressBarMode = AddressBarMode::Breadcrumb; std::wstring addressBarText; std::wstring addressBarError; bool isHomeLocation = false; };
struct Bookmark { std::wstring path; std::wstring displayName; };
// syntheticRoot marks the always-present "This PC" crumb: its path is empty
// (the synthetic root), but it is a real navigation target.
struct BreadcrumbSegment { std::wstring label; std::wstring path; bool syntheticRoot = false; };
// quick-access-home hardening (HOME-POST-01): the single typed extraction from
// a context to its NavigationLocation. A Home context always maps to the Home
// location (never to an empty filesystem path), so every consumer that must
// remember or clone a location (closed tabs, dual-pane, tab labels) uses this
// helper instead of re-deriving `isHomeLocation ? Home() : FromPath(path)`.
inline NavigationLocation LocationOf(const NavigationContext& context) {
    return context.isHomeLocation ? NavigationLocation::Home()
                                  : NavigationLocation::FromPath(context.currentPath);
}
// quick-access-home 2.4: recentFolders is a new, additive, optional persisted
// list (version stays 1; older files simply load an empty list).
// quick-access-home hardening (HOME-POST-01): recentlyClosedTabs is now typed
// (std::deque<NavigationLocation>) so a closed Home tab reopens as Home. The
// on-disk form stays backward compatible: filesystem entries remain bare path
// strings; Home entries serialize as {"kind": "home"} objects; legacy string
// entries deserialize as FileSystem locations.
struct WorkspaceState { std::vector<Bookmark> bookmarks; std::deque<NavigationLocation> recentlyClosedTabs; std::deque<std::wstring> recentFolders; std::wstring lastLocation; bool lastLocationIsHome = false; bool sidebarCollapsed = false; bool drivesCollapsed = false; bool knownFoldersCollapsed = false; bool bookmarksCollapsed = false; };
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
    // Marks a pending write AND records that the user changed persistent
    // workspace state this session. A failed load only permits a recovery
    // write after a real user mutation (harden-workspace-state-multi-writer D1).
    void MarkStateDirty() { stateDirty_ = true; userStateMutated_ = true; }
    bool FlushState();
    const WorkspaceState& State() const { return state_; } WorkspaceState& State() { return state_; }
    static std::vector<std::wstring> EnumerateDrives(); std::vector<Bookmark> EnumerateKnownFolders();
    void ReResolveKnownFolders();
    // quick-access-home 2.3: recent-folder history accessors (cleared entries
    // persist via the existing dirty-flag/flush machinery).
    void ClearRecentFolders(); std::vector<std::wstring> RecentFoldersForHome(size_t maxCount) const;
    // harden-workspace-state-multi-writer D1: Ok = parsed, Missing = no file
    // (defaults are truth), Failed = the file exists but cannot be trusted;
    // Failed forbids clobbering writes until a user mutation in this session.
    static WorkspaceLoadStatus LoadState(WorkspaceState& state); static bool SaveState(const WorkspaceState& state);
private:
    struct Tab { std::vector<NavigationContext> panes; size_t activePane = 0; };
    // quick-access-home hardening (HOME-POST-04): contexts are created from a
    // TYPED location so a Home context can never be born from an empty
    // filesystem path. History is seeded with exactly that location.
    static NavigationContext MakeContext(const NavigationLocation& location); static std::wstring StateFilePath(); Tab& ActiveTab(); const Tab& ActiveTab() const;
    std::wstring defaultPath_; std::vector<Tab> tabs_; size_t activeTab_ = 0; WorkspaceState state_; bool stateDirty_ = false;
    WorkspaceLoadStatus loadStatus_ = WorkspaceLoadStatus::Missing;
    // True once the user changes persistent state in this session; a failed
    // load may only be replaced after this is set (D1).
    bool userStateMutated_ = false;
    // Session tombstones for bookmarks the user removed: a concurrent writer's
    // copy of that path is not resurrected by the merge on flush (D3).
    std::vector<std::wstring> removedBookmarkPaths_;
    // Exact JSON text read from (or last written to) the workspace file. A
    // flush merges with the on-disk document only when it changed since load,
    // so a single writer's state is never polluted by stale disk content.
    std::wstring persistedDocument_;
    // Reads and parses the on-disk document; nullopt when missing/unreadable.
    // `failed` is set when a document exists but cannot be trusted, and
    // `document` receives the raw text of a document that was actually read.
    static std::optional<WorkspaceState> ReadStateFile(bool& failed, std::wstring* document = nullptr);
    std::vector<Bookmark> knownFoldersCache_; bool knownFoldersDirty_ = true;
};

// quick-access-home 2.5: pure (file-I/O-free) persistence seams so the
// serialization round-trip and older-state migration are headless-testable
// without writing to %LOCALAPPDATA%. SaveState/LoadState are thin wrappers
// around these that only perform file read/write.
std::wstring SerializeState(const WorkspaceState& state);
bool DeserializeState(const std::wstring& json, WorkspaceState& out);
// harden-workspace-state-multi-writer D3: pure merge policy for concurrent
// workspace writers. List fields union across the on-disk document and the
// writer's in-memory state (writer order first, on-disk-only entries appended);
// scalar flags and the last location come from the writer. Bookmark paths
// removed in this session are authoritative and are not resurrected.
WorkspaceState MergeWorkspaceStateForWrite(const WorkspaceState& onDisk, const WorkspaceState& writer,
                                           const std::vector<std::wstring>& removedBookmarkPaths);

struct ParsedPath { std::wstring path; std::wstring error; };
ParsedPath ParseNavigationPath(const std::wstring& input);
// Chooses the startup location without resurrecting a deleted persisted
// directory. Access-denied locations remain eligible so the destination view
// can present its own permission error state.
std::wstring ResolveInitialNavigationPath(bool restorePreviousSession,
                                          const std::wstring& lastLocation,
                                          const std::wstring& startupLocation);
// Typed startup resolution: like ResolveInitialNavigationPath, but a session
// that ended on Home restores as the Home location (lastLocationIsHome) rather
// than silently falling back to the default. Filesystem sessions resolve
// exactly as before.
NavigationLocation ResolveInitialNavigationLocation(bool restorePreviousSession,
                                                    const WorkspaceState& state,
                                                    const std::wstring& startupLocation);
PathCommitResult CommitAddressBarPath(NavigationWorkspace& workspace, const std::wstring& input);
// Returns the filesystem parent used by the shell's Up/Backspace command.
// A value containing an empty string means "This PC" (the synthetic root);
// nullopt means the current location is already that root or has no parent.
std::optional<std::wstring> ParentNavigationPath(const std::wstring& path);
} // namespace ffui
