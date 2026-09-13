#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <windows.h>

#include "NavigationWorkspace.h"
#include "NavigationChrome.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {
void TestPathParsing() {
    using namespace ffui;
    Check(ParseNavigationPath(L"  \"C:/Windows/\"  ").path == L"C:\\Windows", "quoted forward-slash path is normalized");
    Check(ParseNavigationPath(L"C:\\Windows\\System32\\..\\").path == L"C:\\Windows", "embedded dot-dot is canonicalized");
    Check(ParseNavigationPath(L"%SystemRoot%\\").error.empty(), "environment variables expand");
    Check(ParseNavigationPath(L"\\\\server\\share\\folder\\").error.empty(), "UNC path is accepted");
    Check(ParseNavigationPath(L"\\\\?\\C:\\Windows\\").error.empty(), "extended-length drive path is accepted");
    Check(ParseNavigationPath(L"\\\\?\\UNC\\server\\share\\folder\\").error.empty(), "extended-length UNC path is accepted");
    Check(!ParseNavigationPath(L"\\\\.\\PhysicalDrive0").error.empty(), "device namespace paths are rejected");
    Check(!ParseNavigationPath(L"\\\\?\\C:\\bad?name").error.empty(), "wildcards remain rejected after an extended prefix");
    Check(!ParseNavigationPath(L"relative\\path").error.empty(), "relative path is rejected");
    Check(!ParseNavigationPath(L"C:\\bad<name>").error.empty(), "reserved characters are rejected");
    Check(!ParseNavigationPath(L" \t ").error.empty(), "empty input is rejected");
    const auto thisPc = ParseNavigationPath(L"  This PC  ");
    Check(thisPc.error.empty() && thisPc.path.empty(), "logical This PC label resolves to the synthetic root");
}
void TestParentNavigationPath() {
    using namespace ffui;
    Check(ParentNavigationPath(L"C:\\Users\\Alice").value_or(L"") == L"C:\\Users",
          "Up returns the immediate drive-directory parent");
    Check(ParentNavigationPath(L"C:\\").has_value() && ParentNavigationPath(L"C:\\")->empty(),
          "Up from a drive root returns the synthetic This PC target");
    Check(ParentNavigationPath(L"\\\\server\\share\\folder").value_or(L"") == L"\\\\server\\share",
          "Up returns the immediate UNC parent");
    Check(ParentNavigationPath(L"\\\\server\\share").has_value() && ParentNavigationPath(L"\\\\server\\share")->empty(),
          "Up from a UNC share root returns the synthetic This PC target");
    Check(ParentNavigationPath(L"\\\\?\\C:\\Users\\Alice").value_or(L"") == L"\\\\?\\C:\\Users",
          "Up preserves the extended drive namespace");
    Check(ParentNavigationPath(L"\\\\?\\C:\\").has_value() && ParentNavigationPath(L"\\\\?\\C:\\")->empty(),
          "Up from an extended drive root returns the synthetic This PC target");
    Check(ParentNavigationPath(L"\\\\?\\UNC\\server\\share\\folder").value_or(L"") == L"\\\\?\\UNC\\server\\share",
          "Up preserves the extended UNC namespace");
    Check(!ParentNavigationPath(L"").has_value(),
          "Up from the synthetic This PC root has no parent");
}
void TestSyntheticRootContext() {
    using namespace ffui;
    NavigationWorkspace workspace(L"");
    Check(workspace.ActiveContext().currentPath.empty(),
          "an empty workspace path represents the synthetic This PC root");
    Check(workspace.Breadcrumbs().size() == 1 && workspace.Breadcrumbs().front().label == L"This PC" &&
          workspace.Breadcrumbs().front().path.empty(),
          "the synthetic root exposes a non-clickable This PC breadcrumb");
    NavigationWorkspace extendedWorkspace(L"");
    extendedWorkspace.Navigate(L"\\\\?\\C:\\Users\\Alice");
    const auto extendedBreadcrumbs = extendedWorkspace.Breadcrumbs();
    Check(extendedBreadcrumbs.size() == 4 && extendedBreadcrumbs[1].label == L"C:" &&
          extendedBreadcrumbs.back().path == L"\\\\?\\C:\\Users\\Alice",
          "extended drive breadcrumbs preserve their real navigation paths");
    workspace.BeginAddressBarEdit();
    Check(workspace.ActiveContext().addressBarText == L"This PC",
          "editing the synthetic root presents its logical This PC label");
    workspace.Navigate(L"C:\\");
    Check(CommitAddressBarPath(workspace, L"This PC") == PathCommitResult::Navigated &&
          workspace.ActiveContext().currentPath.empty(),
          "committing This PC returns to the synthetic root");
}
void TestIndependentContextsAndHistory() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\");
    workspace.Navigate(L"C:\\One"); workspace.Navigate(L"C:\\Two");
    Check(workspace.GoBack() && workspace.ActiveContext().currentPath == L"C:\\One", "back changes active context only");
    workspace.OpenTab(L"D:\\"); workspace.Navigate(L"D:\\Work");
    Check(workspace.TabCount() == 2 && workspace.ActiveContext().currentPath == L"D:\\Work" &&
          workspace.TabPath(1) == L"D:\\Work", "new tab has independent context");
    workspace.SwitchTab(0); Check(workspace.ActiveContext().currentPath == L"C:\\One", "switching preserves first tab state");
    workspace.Navigate(L"C:\\Other"); Check(!workspace.CanGoForward(), "new navigation truncates forward history");
}
void TestDualPaneAndClosedTabs() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\"); workspace.Navigate(L"C:\\Source"); workspace.EnableDualPane(); workspace.ActivatePane(1); workspace.Navigate(L"D:\\Destination"); workspace.ActivatePane(0);
    Check(workspace.ActiveContext().currentPath == L"C:\\Source", "dual panes navigate independently"); workspace.DisableDualPane(); Check(workspace.ActiveContext().currentPath == L"C:\\Source", "disable keeps active pane");
    workspace.OpenTab(L"D:\\Work"); Check(workspace.CloseActiveTab(), "non-last tab closes"); Check(workspace.ReopenClosedTab() && workspace.ActiveContext().currentPath == L"D:\\Work", "recently closed tab reopens at last path");
    Check(workspace.CloseActiveTab(), "reopened tab can be closed");
    Check(!workspace.CloseActiveTab(), "the final tab cannot be closed");
}
void TestBreadcrumbAndEditableAddressBar() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\Users\\me\\Documents");
    const auto breadcrumbs = workspace.Breadcrumbs();
    Check(breadcrumbs.size() == 5 && breadcrumbs[3].label == L"me", "breadcrumb segments represent every path component");
    Check(workspace.NavigateBreadcrumb(2) && workspace.ActiveContext().currentPath == L"C:\\Users", "breadcrumb ancestor navigation changes only the active context");
    workspace.BeginAddressBarEdit();
    Check(workspace.ActiveContext().addressBarMode == AddressBarMode::EditableText && workspace.ActiveContext().addressBarText == L"C:\\Users", "editable address bar is prefilled from current path");
    workspace.ActiveContext().addressBarText = L"C:\\Different"; workspace.CancelAddressBarEdit();
    Check(workspace.ActiveContext().addressBarMode == AddressBarMode::Breadcrumb && workspace.ActiveContext().currentPath == L"C:\\Users", "cancelled edit discards text without navigation");

    const std::wstring canonical = L"\\\\?\\C:\\Users\\MICHAE~1\\Documents";
    NavigationWorkspace canonicalWorkspace(canonical);
    canonicalWorkspace.BeginAddressBarEdit();
    Check(canonicalWorkspace.ActiveContext().addressBarText == canonical &&
              canonicalWorkspace.ActiveContext().currentPath == canonical,
          "editable address bar exposes the exact canonical extended short-name path");
}
void TestWorkspacePersistenceFallback() {
    using namespace ffui;
    wchar_t tempPath[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, tempPath) != 0, "temporary path is available");
    const std::filesystem::path localAppData = std::filesystem::path(tempPath) /
        (L"FastFiles-navigation-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(localAppData, error);
    const DWORD oldLength = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    std::wstring oldValue(oldLength, L'\0');
    if (oldLength != 0) GetEnvironmentVariableW(L"LOCALAPPDATA", oldValue.data(), oldLength);
    SetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.c_str());

    NavigationWorkspace workspace(L"C:\\");
    workspace.AddBookmark(L"C:\\Projects", L"Projects");
    workspace.State().sidebarCollapsed = true;
    workspace.State().bookmarksCollapsed = true;
    workspace.MarkStateDirty();
    Check(workspace.HasPendingStateSave() && workspace.FlushState(), "workspace changes flush through the debounced save boundary");
    WorkspaceState loaded{};
    bool leftoverTemporary = false;
    for (const auto& entry : std::filesystem::directory_iterator(localAppData / L"FastFiles", error)) {
        const std::wstring name = entry.path().filename().wstring();
        if (name.starts_with(L"workspace-state.json") && name.ends_with(L".tmp")) leftoverTemporary = true;
    }
    Check(!leftoverTemporary, "successful workspace save removes its temporary replacement file");
    Check(NavigationWorkspace::LoadState(loaded) == WorkspaceLoadStatus::Ok && loaded.bookmarks.size() == 1 &&
          loaded.bookmarks.front().displayName == L"Projects" && loaded.bookmarks.front().path == L"C:\\Projects" &&
          loaded.sidebarCollapsed && loaded.bookmarksCollapsed && loaded.lastLocation == L"C:\\",
          "workspace state persists bookmarks, location, and sidebar state");

    const std::wstring unicodePath = L"C:\\日本語\\é\\Ω";
    const std::wstring unicodeName = L"旅行 📁";
    workspace.Navigate(unicodePath);
    workspace.State().bookmarks = {{unicodePath, unicodeName}};
    workspace.State().lastLocation = unicodePath;
    workspace.State().recentlyClosedTabs = {NavigationLocation::FromPath(unicodePath)};
    workspace.MarkStateDirty();
    Check(workspace.FlushState(), "workspace state accepts Unicode paths and labels");
    WorkspaceState unicodeLoaded{};
    Check(NavigationWorkspace::LoadState(unicodeLoaded) == WorkspaceLoadStatus::Ok && unicodeLoaded.lastLocation == unicodePath &&
          unicodeLoaded.bookmarks.size() == 1 && unicodeLoaded.bookmarks.front().path == unicodePath &&
          unicodeLoaded.bookmarks.front().displayName == unicodeName &&
          unicodeLoaded.recentlyClosedTabs.size() == 1 && unicodeLoaded.recentlyClosedTabs.front().path == unicodePath,
          "workspace state round-trips Unicode paths and labels");

    const auto stateFile = localAppData / L"FastFiles" / L"workspace-state.json";
    {
        std::wofstream corrupt(stateFile, std::ios::trunc);
        corrupt << L"{ this is not valid workspace state";
    }
    Check(NavigationWorkspace::LoadState(loaded) == WorkspaceLoadStatus::Failed && loaded.bookmarks.empty(),
          "corrupt workspace state reports Failed and falls back to empty state");

    {
        std::ofstream invalidUtf8(stateFile, std::ios::binary | std::ios::trunc);
        invalidUtf8 << "{\"version\": 1, \"lastLocation\": \"";
        invalidUtf8.put(static_cast<char>(0xff));
        invalidUtf8 << "\"}";
    }
    Check(NavigationWorkspace::LoadState(loaded) == WorkspaceLoadStatus::Failed &&
          loaded.bookmarks.empty() && loaded.lastLocation.empty(),
          "invalid UTF-8 workspace state reports Failed and falls back to empty state");

    {
        std::wofstream unsupported(stateFile, std::ios::trunc);
        unsupported << L"{\"version\": 10, \"bookmarks\": [{\"path\": \"C:\\\\Unexpected\", \"displayName\": \"Unexpected\"}]}";
    }
    Check(NavigationWorkspace::LoadState(loaded) == WorkspaceLoadStatus::Failed && loaded.bookmarks.empty(),
          "unsupported workspace state versions report Failed and are ignored");

    {
        std::wofstream oversized(stateFile, std::ios::trunc);
        oversized << L"{\"version\": 1, \"recentlyClosedTabs\": [";
        for (int index = 0; index < 20; ++index) {
            if (index != 0) oversized << L", ";
            oversized << L"\"C:\\\\Closed-" << index << L"\"";
        }
        oversized << L"]}";
    }
    Check(NavigationWorkspace::LoadState(loaded) == WorkspaceLoadStatus::Ok &&
          loaded.recentlyClosedTabs.size() == 16 &&
              loaded.recentlyClosedTabs.front().path == L"C:\\Closed-0" &&
              loaded.recentlyClosedTabs.back().path == L"C:\\Closed-15",
          "workspace restore keeps the supported recently-closed-tab bound");

    std::filesystem::remove_all(localAppData, error);
    if (oldLength == 0) SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);
    else SetEnvironmentVariableW(L"LOCALAPPDATA", oldValue.c_str());
}
void TestInitialNavigationFallback() {
    using namespace ffui;
    wchar_t tempPath[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, tempPath) != 0, "temporary path is available for startup resolution");
    const std::filesystem::path root = std::filesystem::path(tempPath) /
        (L"FastFiles-startup-resolution-" + std::to_wstring(GetCurrentProcessId()));
    const std::filesystem::path existing = root / L"existing";
    const std::filesystem::path existingFile = root / L"file.txt";
    const std::filesystem::path missing = root / L"removed";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(existing, error);
    {
        std::wofstream file(existingFile);
        file << L"not a directory";
    }

    Check(ResolveInitialNavigationPath(true, existing.wstring(), L"This PC") == existing.wstring(),
          "an existing persisted directory is restored");
    Check(ResolveInitialNavigationPath(true, missing.wstring(), existing.wstring()) == existing.wstring(),
          "a deleted persisted directory falls back to the configured startup directory");
    Check(ResolveInitialNavigationPath(true, existingFile.wstring(), existing.wstring()) == existing.wstring(),
          "a persisted file path is not restored as a navigation directory");
    Check(ResolveInitialNavigationPath(true, L"not an absolute path", existing.wstring()) == existing.wstring(),
          "a malformed persisted location falls back to startup");
    Check(ResolveInitialNavigationPath(true, missing.wstring(), L"This PC").empty(),
          "a deleted persisted directory falls back to the synthetic This PC root");
    std::filesystem::remove_all(root, error);
}
void TestCopyRelativePathBaseResolution() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\");
    Check(!workspace.OtherPanePath().has_value(), "single pane exposes no other-pane base");
    workspace.EnableDualPane();
    Check(workspace.OtherPanePath().value_or(L"") == L"C:\\", "dual pane exposes the other pane's location");
    workspace.ActivatePane(1); workspace.Navigate(L"D:\\Destination");
    Check(workspace.OtherPanePath().value_or(L"") == L"C:\\", "other pane path tracks pane switching");
    workspace.ActivatePane(0);
    Check(workspace.OtherPanePath().value_or(L"") == L"D:\\Destination", "other pane is the inactive pane");
    workspace.DisableDualPane();
    Check(!workspace.OtherPanePath().has_value(), "single pane after disable exposes no other-pane base");
}
void TestPerTabPaneContexts() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\Left");
    // This test exercises bookmark identity, not persistence from a user's
    // real profile. Clear any loaded profile entries so the assertion remains
    // deterministic after an interactive UI run has saved a bookmark.
    workspace.State().bookmarks.clear();
    workspace.AddBookmark(L"C:\\Left", L"Left");
    workspace.AddBookmark(L"c:\\left", L"Duplicate");
    Check(workspace.State().bookmarks.size() == 1 && workspace.State().bookmarks.front().displayName == L"Left",
          "bookmark insertion is case-insensitively idempotent for Windows paths");
    workspace.EnableDualPane();
    workspace.ActivatePane(1);
    workspace.Navigate(L"D:\\Right");
    workspace.PaneContext(0).columnPaths = {L"C:\\", L"C:\\Left"};
    workspace.PaneContext(1).columnPaths = {L"D:\\", L"D:\\Right"};
    Check(workspace.ActivePaneCount() == 2 && workspace.ActivePaneIndex() == 1,
          "dual-pane tab exposes both contexts and the selected active pane");
    workspace.OpenTab(L"E:\\Single");
    Check(workspace.ActivePaneCount() == 1 && workspace.ActiveContext().currentPath == L"E:\\Single",
          "new tab has an independent single-pane context");
    workspace.SwitchTab(0);
    Check(workspace.ActivePaneCount() == 2 && workspace.PaneContext(0).columnPaths.size() == 2 &&
          workspace.PaneContext(1).columnPaths.back() == L"D:\\Right" &&
          workspace.ActivePaneIndex() == 1,
          "switching tabs preserves both dual-pane contexts and active-pane identity");
}

// interactive-ui-runtime-determinism 3.1/3.3: exercise the production
// NavigationWorkspace transition seam as a deterministic state machine. The
// test uses typed locations only; it never asks Win32, the engine, or the
// service to resolve a path merely because a pane was created.
void TestDualPaneLifecycleDeterminism() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\Start");
    workspace.Navigate(L"C:\\Left");
    workspace.PaneContext(0).columnPaths = {L"C:\\", L"C:\\Left"};
    workspace.PaneContext(0).columnSelections = {1};

    Check(workspace.ActivePaneCount() == 1 && workspace.ActivePaneIndex() == 0,
          "dual-pane lifecycle starts with one active pane");
    workspace.EnableDualPane();
    Check(workspace.ActivePaneCount() == 2 && workspace.ActivePaneIndex() == 0,
          "enabling dual pane creates exactly one inactive companion and keeps Pane.0 active");
    Check(workspace.PaneContext(1).currentPath == L"C:\\Left" &&
          !workspace.PaneContext(1).isHomeLocation &&
          workspace.PaneContext(1).history.size() == 1 &&
          workspace.PaneContext(1).history.front() == NavigationLocation::FromPath(L"C:\\Left"),
          "enabling clones the active typed filesystem location with a fresh history");

    workspace.ActivatePane(1);
    workspace.Navigate(L"D:\\Right");
    workspace.PaneContext(1).columnPaths = {L"D:\\", L"D:\\Right"};
    Check(workspace.PaneContext(0).currentPath == L"C:\\Left" &&
          workspace.ActiveContext().currentPath == L"D:\\Right",
          "navigation in Pane.1 leaves Pane.0 independent");
    workspace.DisableDualPane();
    Check(workspace.ActivePaneCount() == 1 && workspace.ActivePaneIndex() == 0 &&
          workspace.ActiveContext().currentPath == L"D:\\Right" &&
          workspace.ActiveContext().columnPaths == std::vector<std::wstring>{L"D:\\", L"D:\\Right"},
          "disabling while Pane.1 is active keeps Pane.1 state and migrates focus to the kept pane");

    workspace.EnableDualPane();
    Check(workspace.PaneContext(0).currentPath == L"D:\\Right" &&
          workspace.PaneContext(1).currentPath == L"D:\\Right",
          "re-enabling after a removal clones the surviving typed state");

    workspace.DisableDualPane();
    workspace.ActivatePane(0);
    workspace.Navigate(NavigationLocation::Home());
    workspace.EnableDualPane();
    Check(workspace.PaneContext(0).isHomeLocation && workspace.PaneContext(0).currentPath.empty() &&
          workspace.PaneContext(1).isHomeLocation && workspace.PaneContext(1).currentPath.empty(),
          "Home-to-dual transition preserves Home kind in both panes");
    workspace.ActivatePane(1);
    workspace.Navigate(L"E:\\Filesystem");
    Check(workspace.PaneContext(0).isHomeLocation &&
          !workspace.PaneContext(1).isHomeLocation &&
          workspace.PaneContext(1).currentPath == L"E:\\Filesystem",
          "Home and filesystem locations remain isolated across panes");
    workspace.DisableDualPane();
    Check(!workspace.ActiveContext().isHomeLocation && workspace.ActiveContext().currentPath == L"E:\\Filesystem",
          "disabling a Home/filesystem pair keeps the active filesystem pane");

    workspace.EnableDualPane();
    workspace.OpenTab(NavigationLocation::Home());
    Check(workspace.ActivePaneCount() == 1 && workspace.ActiveContext().isHomeLocation,
          "tab change enters an independent single-pane Home context");
    Check(workspace.SwitchTab(0) && workspace.IsDualPane() &&
          workspace.PaneContext(0).isHomeLocation == false &&
          workspace.PaneContext(0).currentPath == L"E:\\Filesystem",
          "switching tabs restores the prior pane layout and typed active state");

    bool cyclesClean = true;
    for (int cycle = 0; cycle < 50; ++cycle) {
        if (workspace.IsDualPane()) {
            workspace.ActivatePane(1);
            workspace.DisableDualPane();
            if (workspace.ActivePaneCount() != 1 || workspace.ActivePaneIndex() != 0 ||
                workspace.ActiveContext().currentPath != L"E:\\Filesystem") {
                cyclesClean = false;
            }
        } else {
            workspace.EnableDualPane();
            if (workspace.ActivePaneCount() != 2 || workspace.ActivePaneIndex() != 0 ||
                workspace.PaneContext(0).isHomeLocation || workspace.PaneContext(1).isHomeLocation ||
                workspace.PaneContext(1).currentPath != L"E:\\Filesystem") {
                cyclesClean = false;
            }
        }
    }
    Check(cyclesClean, "50 deterministic enable/disable cycles preserve active-pane and typed-location invariants");
}
}
void TestConcurrentIndependentContextsReadDifferentPaths() {
    using namespace ffui;
    // Task 1.4: two or more concurrently live NavigationContext instances
    // reading different paths produce correct, independent results with zero
    // IPC round-trips. NavigationWorkspace holds plain UI state over the
    // shared in-process index snapshot (static audit: no pipe/mapped-file/
    // window-message primitive exists in NavigationWorkspace.cpp), so
    // "concurrently live" here means three contexts alive at once -- tab 0
    // pane 0 (C:\), tab 0 pane 1 (D:\), tab 1 (E:\) -- each with its own
    // path, columns, history, and breadcrumbs; interleaved navigation must
    // never bleed state across contexts.
    NavigationWorkspace workspace(L"C:\\Alpha");
    workspace.Navigate(L"C:\\Alpha\\A1");
    workspace.Navigate(L"C:\\Alpha\\A2");
    workspace.EnableDualPane();
    workspace.ActivatePane(1);
    workspace.Navigate(L"D:\\Beta");
    workspace.Navigate(L"D:\\Beta\\B1");
    workspace.OpenTab();
    workspace.Navigate(L"E:\\Gamma");
    workspace.Navigate(L"E:\\Gamma\\G1\\G2");

    Check(workspace.TabCount() == 2, "three concurrent contexts are live: two tabs");
    workspace.SwitchTab(0);
    Check(workspace.IsDualPane(), "dual pane is still live on tab 0");
    workspace.ActivatePane(0);
    Check(workspace.ActiveContext().currentPath == L"C:\\Alpha\\A2" &&
          workspace.ActiveContext().history.size() == 3,
          "context 1 keeps its path and full history");
    Check(workspace.GoBack() && workspace.ActiveContext().currentPath == L"C:\\Alpha\\A1",
          "context 1 back-navigates within its own history");
    workspace.ActivatePane(1);
    Check(workspace.ActiveContext().currentPath == L"D:\\Beta\\B1",
          "context 2 keeps its path while context 1 moved");
    Check(workspace.GoBack() && workspace.ActiveContext().currentPath == L"D:\\Beta",
          "context 2 history is independent of context 1");
    workspace.SwitchTab(1);
    Check(workspace.ActiveContext().currentPath == L"E:\\Gamma\\G1\\G2",
          "context 3 keeps its path after both tab-0 panes navigated");
    Check(workspace.Breadcrumbs().size() == 5 && workspace.Breadcrumbs().back().label == L"G2",
          "context 3 breadcrumbs derive from its own path only");
    workspace.GoBack();
    Check(workspace.ActiveContext().currentPath == L"E:\\Gamma", "context 3 back works");
    workspace.SwitchTab(0);
    Check(workspace.ActiveContext().currentPath == L"D:\\Beta",
          "context 2 untouched by context 3 navigation");
    workspace.ActivatePane(0);
    Check(workspace.ActiveContext().currentPath == L"C:\\Alpha\\A1",
          "context 1 untouched by context 3 navigation");
    workspace.SwitchTab(1);
    workspace.Navigate(L"E:\\Gamma\\G1\\G2\\Deep");
    workspace.SwitchTab(0);
    workspace.ActivatePane(1);
    Check(workspace.ActiveContext().currentPath == L"D:\\Beta",
          "forward navigation in context 3 leaves tab-0 panes at their paths");
    Check(workspace.CanGoForward(), "context 3 forward history intact after interleave");
}
// Test runners defined in the other translation units of this executable.
void RunHomeLocationTests();
void RunRecentFoldersTests();

namespace {
void TestAddressBarErrorClearedOnHistoryNavigation() {
    using namespace ffui;
    // P2 regression: a failed address-bar commit must not keep painting its
    // error over the breadcrumbs of locations reached via Back/Forward.
    NavigationWorkspace workspace(L"C:\\");
    workspace.Navigate(L"C:\\Users");
    Check(CommitAddressBarPath(workspace, L"C:\\DefinitelyMissing") == PathCommitResult::NotFound,
          "an invalid address-bar commit reports NotFound");
    Check(!workspace.ActiveContext().addressBarError.empty(),
          "a failed commit sets the address-bar error text");
    Check(workspace.GoBack() && workspace.ActiveContext().addressBarError.empty() &&
          workspace.ActiveContext().addressBarMode == AddressBarMode::Breadcrumb,
          "Back clears stale address-bar error text and restores breadcrumb mode");
    Check(workspace.GoForward() && workspace.ActiveContext().addressBarError.empty(),
          "Forward clears stale address-bar error text too");
}

void TestUncBreadcrumbsMergeServerAndShare() {
    using namespace ffui;
    // P3 regression: bare \\server is not enumerable, so the server crumb must
    // never exist; server+share merge into one share-root segment.
    NavigationWorkspace workspace(L"\\\\server\\share\\a\\b");
    const auto crumbs = workspace.Breadcrumbs();
    Check(crumbs.size() == 4, "plain UNC breadcrumbs are This PC, share root, and each subfolder");
    Check(crumbs[1].path == L"\\\\server\\share", "the first UNC segment targets the share root");
    for (const auto& crumb : crumbs) {
        Check(crumb.path != L"\\\\server", "no UNC breadcrumb navigates to the non-enumerable bare server");
    }
    NavigationWorkspace extended(L"\\\\?\\UNC\\server\\share\\a");
    const auto extendedCrumbs = extended.Breadcrumbs();
    Check(extendedCrumbs.size() == 3, "extended UNC breadcrumbs merge server and share likewise");
    Check(extendedCrumbs[1].path == L"\\\\?\\UNC\\server\\share",
          "the first extended-UNC segment targets the share root");
}

void TestThisPcBreadcrumbNavigable() {
    using namespace ffui;
    // P3 regression: the This PC crumb is drawn as a real navigation target.
    NavigationWorkspace workspace(L"C:\\Users");
    const auto crumbs = workspace.Breadcrumbs();
    Check(crumbs.front().label == L"This PC" && crumbs.front().syntheticRoot,
          "the first breadcrumb is the synthetic This PC root");
    Check(workspace.NavigateBreadcrumb(0) && workspace.ActiveContext().currentPath.empty() &&
              !workspace.ActiveContext().isHomeLocation,
          "clicking This PC navigates to the synthetic root, not Home");
}

void TestDriveRootBookmarkName() {
    using namespace ffui;
    // P3 regression: bookmarking a drive root must not produce a blank label.
    NavigationWorkspace workspace(L"C:\\");
    workspace.State().bookmarks.clear();
    workspace.AddBookmark(L"C:\\");
    Check(!workspace.State().bookmarks.empty() &&
              !workspace.State().bookmarks.back().displayName.empty(),
          "a drive-root bookmark derives a non-empty display name");
}

void TestTabActiveIdentitySeam() {
    using namespace ffui;
    // P2 regression seam: owner-draw active state comes from the real active
    // tab index, not ODS_DISABLED (which is never set).
    Check(IsTabDrawItemActive(kTabBaseId + 2, 2), "the active tab's control id maps to active draw state");
    Check(!IsTabDrawItemActive(kTabBaseId + 1, 2), "inactive tab control ids map to inactive draw state");
    Check(!IsTabDrawItemActive(kTabBaseId - 1, 0), "non-tab control ids never map to active draw state");
}

void TestExtendedPrefixNormalizedInLocations() {
    using namespace ffui;
    // Display hygiene at the UI boundary: the model keeps extended-length
    // prefixes (long-path navigation depends on them), but tab labels never
    // print "\\?\".
    Check(NavigationLocation::FromPath(L"\\\\?\\C:\\Users\\Me\\Documents").path ==
              L"\\\\?\\C:\\Users\\Me\\Documents",
          "the model preserves extended-length paths for navigation");
    Check(TabLabelForLocation(1, NavigationLocation::FromPath(L"\\\\?\\C:\\Users\\Me\\Documents")) == L"1: Documents",
          "tab labels strip the extended prefix and show the leaf");
    Check(TabLabelForLocation(2, NavigationLocation::FromPath(L"\\\\?\\UNC\\server\\share\\docs")) == L"2: docs",
          "extended UNC labels convert to plain UNC and show the leaf");
    Check(TabLabelForLocation(3, NavigationLocation::FromPath(L"C:\\plain")) == L"3: plain",
          "ordinary labels are unchanged");
    Check(TabLabelForLocation(4, NavigationLocation::Home()) == L"4: Home",
          "Home tabs label as Home");
    Check(TabLabelForLocation(5, NavigationLocation::FromPath(L"")) == L"5: This PC",
          "empty-path tabs label as This PC");
}

void TestHomeSessionRestoreRoundTrip() {
    using namespace ffui;
    // P3 regression: ending a session on Home must restore Home, not silently
    // fall back to the default location.
    WorkspaceState state;
    state.lastLocationIsHome = true;
    const std::wstring json = SerializeState(state);
    WorkspaceState loaded{};
    Check(DeserializeState(json, loaded) && loaded.lastLocationIsHome,
          "the Home last-location marker round-trips through serialization");
    const auto location = ResolveInitialNavigationLocation(true, loaded, L"C:\\fallback");
    Check(location.IsHome(), "a session that ended on Home restores as Home");

    WorkspaceState filesystemState{};
    filesystemState.lastLocation = L"C:\\Windows";
    const auto filesystemLocation = ResolveInitialNavigationLocation(true, filesystemState, L"C:\\fallback");
    Check(!filesystemLocation.IsHome() && filesystemLocation.path == L"C:\\Windows",
          "filesystem sessions still restore their persisted directory");

    WorkspaceState legacy{};
    Check(DeserializeState(L"{\"version\": 1, \"lastLocation\": \"C:\\\\Old\"}", legacy) &&
              !legacy.lastLocationIsHome,
          "legacy documents without the marker default to filesystem restore");
}

void TestTypedClosedTabReopen() {
    using namespace ffui;
    // quick-access-home hardening (HOME-POST-01): closing/reopening preserves
    // the TYPED location, never an empty-string collapse.
    NavigationWorkspace workspace(L"C:\\Start");

    // A closed Home tab reopens as Home with history {Home}.
    workspace.OpenTab(); // tab 1: Home (new-tab default)
    Check(workspace.ActiveContext().isHomeLocation, "tab 1 is Home");
    Check(workspace.CloseActiveTab(), "the Home tab closes");
    Check(workspace.ReopenClosedTab() && workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath.empty() &&
          workspace.ActiveContext().history.size() == 1 &&
          workspace.ActiveContext().history.front().IsHome(),
          "a closed Home tab reopens as Home with history {Home}");

    // A closed filesystem tab reopens at the same folder.
    Check(workspace.CloseActiveTab(), "the reopened Home tab closes again");
    workspace.OpenTab(L"D:\\Work");
    Check(workspace.CloseActiveTab(), "the filesystem tab closes");
    Check(workspace.ReopenClosedTab() && !workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath == L"D:\\Work",
          "a closed filesystem tab reopens at the same folder");

    // A closed This PC tab reopens as This PC (FileSystem, empty) -- never Home.
    Check(workspace.CloseActiveTab(), "the filesystem tab closes again");
    workspace.OpenTab(L"");
    Check(workspace.CloseActiveTab(), "the This PC tab closes");
    Check(workspace.ReopenClosedTab() && !workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath.empty(),
          "a closed This PC tab reopens as This PC, not Home");
}

void TestTypedClosedTabStackOrder() {
    using namespace ffui;
    // quick-access-home hardening (HOME-POST-01): the mixed recently-closed
    // stack preserves ordering AND kinds; reopening is LIFO per kind.
    NavigationWorkspace workspace(L"C:\\Start");
    workspace.State().recentlyClosedTabs.clear();
    workspace.OpenTab(NavigationLocation::Home());
    workspace.CloseActiveTab(); // stack: [Home]
    workspace.OpenTab(L"D:\\Work");
    workspace.CloseActiveTab(); // stack: [FS(D:\Work), Home]
    workspace.OpenTab(L"");
    workspace.CloseActiveTab(); // stack: [FS(""), FS(D:\Work), Home]
    const auto& stack = workspace.State().recentlyClosedTabs;
    Check(stack.size() == 3, "three typed entries are retained");
    Check(stack[0] == NavigationLocation::FromPath(L"") && stack[0].IsFileSystem() &&
          !stack[0].IsHome(), "most recent closed tab is This PC (FileSystem)");
    Check(stack[1] == NavigationLocation::FromPath(L"D:\\Work") && stack[1].IsFileSystem(),
          "second entry is the filesystem folder");
    Check(stack[2] == NavigationLocation::Home() && stack[2].IsHome(),
          "oldest entry is Home");
    Check(workspace.ReopenClosedTab() && !workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath.empty(), "reopen 1 restores This PC");
    Check(workspace.ReopenClosedTab() && workspace.ActiveContext().currentPath == L"D:\\Work",
          "reopen 2 restores the folder");
    Check(workspace.ReopenClosedTab() && workspace.ActiveContext().isHomeLocation,
          "reopen 3 restores Home");
    Check(!workspace.ReopenClosedTab(), "the stack is empty after three reopens");
}

void TestTypedClosedTabPersistence() {
    using namespace ffui;
    // quick-access-home hardening (HOME-POST-01): typed entries survive the
    // serialize -> deserialize round trip (the headless model of a workspace
    // restart) and reopen as their kind.
    WorkspaceState state;
    state.recentlyClosedTabs = {
        NavigationLocation::Home(),
        NavigationLocation::FromPath(L"C:\\X"),
        NavigationLocation::FromPath(L"")};
    const std::wstring json = SerializeState(state);
    WorkspaceState loaded{};
    Check(DeserializeState(json, loaded) && loaded.recentlyClosedTabs.size() == 3 &&
          loaded.recentlyClosedTabs[0] == NavigationLocation::Home() &&
          loaded.recentlyClosedTabs[1] == NavigationLocation::FromPath(L"C:\\X") &&
          loaded.recentlyClosedTabs[2] == NavigationLocation::FromPath(L""),
          "typed closed-tab entries round-trip through serialization");

    // A persisted Home closed tab reopens as Home after a simulated restart.
    NavigationWorkspace workspace(L"C:\\Start");
    workspace.State().recentlyClosedTabs = loaded.recentlyClosedTabs;
    Check(workspace.ReopenClosedTab() && workspace.ActiveContext().isHomeLocation,
          "a persisted Home closed tab reopens as Home after restart");

    // Legacy string entries migrate as FileSystem locations (including the
    // empty string, which means This PC -- never Home).
    const std::wstring legacy = L"{\"version\": 1, \"recentlyClosedTabs\": [\"C:\\\\Old\", \"\"]}";
    WorkspaceState legacyLoaded{};
    Check(DeserializeState(legacy, legacyLoaded) && legacyLoaded.recentlyClosedTabs.size() == 2 &&
          legacyLoaded.recentlyClosedTabs[0] == NavigationLocation::FromPath(L"C:\\Old") &&
          legacyLoaded.recentlyClosedTabs[1] == NavigationLocation::FromPath(L""),
          "legacy string closed-tab entries migrate as FileSystem locations");

    // The new object form for Home entries parses, and malformed entries are
    // skipped without failing the whole document.
    const std::wstring typed = L"{\"version\": 1, \"recentlyClosedTabs\": [{\"kind\": \"home\"}, 123, "
                               L"{\"kind\": \"filesystem\", \"path\": \"C:\\\\New\"}]}";
    WorkspaceState typedLoaded{};
    Check(DeserializeState(typed, typedLoaded) && typedLoaded.recentlyClosedTabs.size() == 2 &&
          typedLoaded.recentlyClosedTabs[0] == NavigationLocation::Home() &&
          typedLoaded.recentlyClosedTabs[1] == NavigationLocation::FromPath(L"C:\\New"),
          "typed object entries parse and a malformed entry is skipped");
}

// harden-workspace-state-multi-writer 3.x: failed loads never clobber; two
// sequential writers (two live windows) union their list fields; a bookmark
// removed in one window is not resurrected by the other's stale snapshot.
void TestWorkspaceMultiWriterHardening() {
    using namespace ffui;
    wchar_t tempPath[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, tempPath) != 0, "temporary path is available");
    const std::filesystem::path localAppData = std::filesystem::path(tempPath) /
        (L"FastFiles-multiwriter-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(localAppData, error);
    const DWORD oldLength = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    std::wstring oldValue(oldLength, L'\0');
    if (oldLength != 0) GetEnvironmentVariableW(L"LOCALAPPDATA", oldValue.data(), oldLength);
    SetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.c_str());
    const auto stateFile = localAppData / L"FastFiles" / L"workspace-state.json";

    // 3.1: an undecodable document survives a flush with no user mutation.
    {
        std::filesystem::create_directories(stateFile.parent_path(), error);
        {
            std::ofstream invalidUtf8(stateFile, std::ios::binary | std::ios::trunc);
            invalidUtf8 << "{\"version\": 1, \"bookmarks\": [{\"path\": \"C:\\\\Good\", \"displayName\": \"Good\"}], \"lastLocation\": \"";
            invalidUtf8.put(static_cast<char>(0xff));
            invalidUtf8 << "\"}";
        }
        std::vector<char> original;
        {
            std::ifstream input(stateFile, std::ios::binary);
            original.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
        NavigationWorkspace workspace(L"C:\\Somewhere");
        Check(!workspace.HasPendingStateSave(), "a failed load does not dirty the workspace");
        Check(workspace.FlushState(), "a failed-load flush without a user mutation is a reliable no-op");
        std::vector<char> after;
        {
            std::ifstream input(stateFile, std::ios::binary);
            after.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
        Check(after == original, "a failed load plus flush left the original bytes intact");

        // A real user mutation licenses a recovery write in the same session.
        workspace.AddBookmark(L"C:\\Recovered", L"Recovered");
        Check(workspace.FlushState(), "a user mutation after a failed load writes a recovery document");
        WorkspaceState recovered{};
        Check(NavigationWorkspace::LoadState(recovered) == WorkspaceLoadStatus::Ok &&
              recovered.bookmarks.size() == 1 && recovered.bookmarks.front().path == L"C:\\Recovered",
              "the recovery write persists the new bookmark");
    }

    // 3.2: two sequential writers union their bookmarks.
    {
        std::filesystem::remove(stateFile, error);
        NavigationWorkspace windowA(L"C:\\");
        windowA.State().bookmarks.clear();
        windowA.AddBookmark(L"D:\\Alpha", L"Alpha");
        Check(windowA.FlushState(), "first writer flush failed");

        NavigationWorkspace windowB(L"C:\\");
        Check(windowB.State().bookmarks.size() == 1, "second writer loaded the first writer's bookmark");
        windowB.AddBookmark(L"D:\\Beta", L"Beta");
        Check(windowB.FlushState(), "second writer flush failed");

        WorkspaceState merged{};
        Check(NavigationWorkspace::LoadState(merged) == WorkspaceLoadStatus::Ok && merged.bookmarks.size() == 2,
              "both writers' bookmarks survive the merge");
        bool hasAlpha = false;
        bool hasBeta = false;
        for (const auto& bookmark : merged.bookmarks) {
            hasAlpha = hasAlpha || bookmark.path == L"D:\\Alpha";
            hasBeta = hasBeta || bookmark.path == L"D:\\Beta";
        }
        Check(hasAlpha && hasBeta, "the merged document contains both writers' distinct bookmarks");
    }

    // A bookmark removed in this session is not resurrected from the other
    // writer's still-stale on-disk copy (session tombstone, D3).
    {
        std::filesystem::remove(stateFile, error);
        NavigationWorkspace windowA(L"C:\\");
        windowA.State().bookmarks.clear();
        windowA.AddBookmark(L"D:\\Keep", L"Keep");
        windowA.AddBookmark(L"D:\\Remove", L"Remove");
        Check(windowA.FlushState(), "tombstone fixture flush failed");

        NavigationWorkspace windowB(L"C:\\");
        Check(windowB.State().bookmarks.size() == 2, "second window loaded both bookmarks");
        for (size_t index = 0; index < windowB.State().bookmarks.size(); ++index) {
            if (windowB.State().bookmarks[index].path == L"D:\\Remove") {
                Check(windowB.RemoveBookmark(index), "removing the tombstoned bookmark failed");
                break;
            }
        }
        Check(windowB.FlushState(), "tombstone writer flush failed");
        WorkspaceState afterRemoval{};
        Check(NavigationWorkspace::LoadState(afterRemoval) == WorkspaceLoadStatus::Ok &&
              afterRemoval.bookmarks.size() == 1 && afterRemoval.bookmarks.front().path == L"D:\\Keep",
              "a removed bookmark is not resurrected by the merge");
    }

    // quick-access-home recentFolders stays additive and merges by value.
    {
        std::filesystem::remove(stateFile, error);
        NavigationWorkspace windowA(L"C:\\");
        windowA.State().recentFolders = {L"C:\\RecentA"};
        windowA.MarkStateDirty();
        Check(windowA.FlushState(), "recentFolders first writer flush failed");
        NavigationWorkspace windowB(L"C:\\");
        windowB.State().recentFolders.push_front(L"C:\\RecentB");
        windowB.MarkStateDirty();
        Check(windowB.FlushState(), "recentFolders second writer flush failed");
        WorkspaceState merged{};
        Check(NavigationWorkspace::LoadState(merged) == WorkspaceLoadStatus::Ok && merged.recentFolders.size() == 2,
              "recentFolders union across writers without duplicating");
    }

    std::filesystem::remove_all(localAppData, error);
    if (oldLength == 0) SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);
    else SetEnvironmentVariableW(L"LOCALAPPDATA", oldValue.c_str());
}
} // namespace

int main() {
    TestPathParsing(); TestParentNavigationPath(); TestSyntheticRootContext(); TestIndependentContextsAndHistory(); TestDualPaneAndClosedTabs();
    TestBreadcrumbAndEditableAddressBar(); TestWorkspacePersistenceFallback(); TestInitialNavigationFallback(); TestCopyRelativePathBaseResolution();
    TestPerTabPaneContexts();
    TestDualPaneLifecycleDeterminism();
    TestConcurrentIndependentContextsReadDifferentPaths();
    TestTypedClosedTabReopen();
    TestTypedClosedTabStackOrder();
    TestTypedClosedTabPersistence();
    TestWorkspaceMultiWriterHardening();
    TestAddressBarErrorClearedOnHistoryNavigation();
    TestUncBreadcrumbsMergeServerAndShare();
    TestThisPcBreadcrumbNavigable();
    TestDriveRootBookmarkName();
    TestTabActiveIdentitySeam();
    TestExtendedPrefixNormalizedInLocations();
    TestHomeSessionRestoreRoundTrip();
    RunHomeLocationTests();
    RunRecentFoldersTests();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
