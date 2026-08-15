#include <deque>
#include <vector>
#include <windows.h>

#include "NavigationWorkspace.h"
#include "RecentFolders.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {
void TestRecentFolderHistoryUnit() {
    using namespace ffui;
    std::deque<std::wstring> store;
    // Most-recent-first ordering.
    RecentFolderHistory::Record(store, L"C:\\A");
    RecentFolderHistory::Record(store, L"C:\\B");
    Check(store.size() == 2 && store[0] == L"C:\\B" && store[1] == L"C:\\A",
          "recording orders most-recent-first");
    // A duplicate moves to front without growing.
    RecentFolderHistory::Record(store, L"C:\\A");
    Check(store.size() == 2 && store[0] == L"C:\\A" && store[1] == L"C:\\B",
          "a duplicate moves to front and does not grow the store");
    // Case-insensitive dedup preserves the first-seen spelling.
    store.clear();
    RecentFolderHistory::Record(store, L"C:\\Folder");
    RecentFolderHistory::Record(store, L"c:\\folder");
    Check(store.size() == 1 && store[0] == L"C:\\Folder",
          "case-insensitive dedup preserves the first-seen spelling");
    // Trailing-slash dedup preserves the first-seen spelling.
    store.clear();
    RecentFolderHistory::Record(store, L"C:\\Folder\\");
    RecentFolderHistory::Record(store, L"C:\\Folder");
    Check(store.size() == 1 && store[0] == L"C:\\Folder\\",
          "trailing-slash dedup preserves the first-seen spelling");
    // Drive-root trailing separator is preserved (never trimmed to "C:").
    Check(RecentFolderHistory::Equivalent(L"C:\\", L"C:\\"),
          "a drive root compares equal to itself");
    store.clear();
    RecentFolderHistory::Record(store, L"C:\\");
    Check(store.size() == 1 && store[0] == L"C:\\",
          "a drive root's trailing separator is preserved");
    // Share-root trailing separator is preserved.
    Check(RecentFolderHistory::Equivalent(L"\\\\server\\share\\", L"\\\\server\\share"),
          "a share root compares equal across a trailing separator");
    store.clear();
    RecentFolderHistory::Record(store, L"\\\\server\\share\\");
    Check(store.size() == 1 && store[0] == L"\\\\server\\share\\",
          "a share root's trailing separator is preserved");
    // Persisted cap bound: 60 records keep only the newest 50.
    store.clear();
    for (int index = 0; index < 60; ++index) {
        RecentFolderHistory::Record(store, L"C:\\Folder" + std::to_wstring(index));
    }
    Check(store.size() == RecentFolderHistory::kMaxRecentFoldersPersisted,
          "the persisted cap is kMaxRecentFoldersPersisted (50)");
    Check(store.front() == L"C:\\Folder59",
          "the most-recently recorded folder sits at the front");
    // RecentForHome caps the displayed count.
    const std::vector<std::wstring> shown =
        RecentFolderHistory::RecentForHome(store, RecentFolderHistory::kHomeRecentFoldersShown);
    Check(shown.size() == RecentFolderHistory::kHomeRecentFoldersShown,
          "RecentForHome caps at kHomeRecentFoldersShown (10)");
}

void TestRecentFoldersWorkspaceIntegration() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\Start");
    workspace.State().recentFolders.clear();
    // Navigating to a filesystem folder records it.
    workspace.Navigate(L"C:\\A");
    std::vector<std::wstring> recents = workspace.RecentFoldersForHome(50);
    Check(recents.size() == 1 && recents[0] == L"C:\\A",
          "Navigate records a filesystem folder");
    // Revisiting a folder moves it to front without duplicating.
    workspace.Navigate(L"C:\\B");
    workspace.Navigate(L"C:\\A");
    recents = workspace.RecentFoldersForHome(50);
    Check(recents.size() == 2 && recents[0] == L"C:\\A" && recents[1] == L"C:\\B",
          "revisiting a folder moves it to front and does not duplicate");
    // Home never records.
    workspace.Navigate(NavigationLocation::Home());
    recents = workspace.RecentFoldersForHome(50);
    Check(recents.size() == 2, "navigating to Home does not record a recent folder");
    // This PC (empty path) never records.
    workspace.Navigate(L"");
    recents = workspace.RecentFoldersForHome(50);
    Check(recents.size() == 2, "navigating to This PC does not record a recent folder");
}

void TestRecentFoldersDirtyAndClear() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\Start");
    workspace.State().recentFolders.clear();
    Check(!workspace.HasPendingStateSave(),
          "a fresh workspace has no pending state save");
    workspace.Navigate(L"C:\\Dirty");
    Check(workspace.HasPendingStateSave(),
          "recording a folder marks the workspace state dirty");
    workspace.ClearRecentFolders();
    Check(workspace.RecentFoldersForHome(50).empty(),
          "ClearRecentFolders empties the recent-folder list");
    Check(workspace.HasPendingStateSave(),
          "ClearRecentFolders marks the state dirty so the clear persists");
}

void TestPersistenceSeams() {
    using namespace ffui;
    // quick-access-home 2.5: pure, file-I/O-free serialization seams.
    WorkspaceState state;
    state.lastLocation = L"C:\\Start";
    state.bookmarks = {{L"C:\\X", L"X"}};
    state.recentlyClosedTabs = {L"D:\\Closed"};
    state.recentFolders = {L"C:\\A", L"C:\\B"};
    state.sidebarCollapsed = true;
    const std::wstring json = SerializeState(state);

    Check(json.find(L"\"version\": 1") != std::wstring::npos,
          "serialized state keeps version 1 (no migration bump)");

    WorkspaceState loaded{};
    Check(DeserializeState(json, loaded) && loaded.recentFolders.size() == 2 &&
          loaded.recentFolders[0] == L"C:\\A" && loaded.recentFolders[1] == L"C:\\B" &&
          loaded.lastLocation == L"C:\\Start" && loaded.bookmarks.size() == 1 &&
          loaded.bookmarks.front().path == L"C:\\X" &&
          loaded.recentlyClosedTabs.size() == 1 && loaded.recentlyClosedTabs.front() == L"D:\\Closed" &&
          loaded.sidebarCollapsed,
          "round-trip through SerializeState/DeserializeState preserves recentFolders and other fields");

    // Older documents without the recentFolders field load an empty list.
    const std::wstring legacy = L"{\"version\": 1, \"bookmarks\": [{\"path\": \"C:\\\\X\", \"displayName\": \"X\"}]}";
    WorkspaceState legacyLoaded{};
    Check(DeserializeState(legacy, legacyLoaded) && legacyLoaded.recentFolders.empty(),
          "a state without the recentFolders field loads an empty list");

    // A non-string entry is skipped without failing the whole document.
    const std::wstring malformed = L"{\"version\": 1, \"recentFolders\": [123, \"C:\\\\A\"]}";
    WorkspaceState malformedLoaded{};
    Check(DeserializeState(malformed, malformedLoaded) &&
          malformedLoaded.recentFolders.size() == 1 &&
          malformedLoaded.recentFolders[0] == L"C:\\A",
          "a non-string recentFolders entry is skipped");

    // An unquoted (malformed) entry is skipped, leaving the list empty.
    const std::wstring unquoted = L"{\"version\": 1, \"recentFolders\": [C:\\\\A]}";
    WorkspaceState unquotedLoaded{};
    Check(DeserializeState(unquoted, unquotedLoaded) && unquotedLoaded.recentFolders.empty(),
          "an unquoted recentFolders entry is skipped");

    // Over-long lists are trimmed to the persisted cap on load.
    std::wstring oversized = L"{\"version\": 1, \"recentFolders\": [";
    for (int index = 0; index < 60; ++index) {
        if (index != 0) oversized += L", ";
        oversized += L"\"C:\\\\F" + std::to_wstring(index) + L"\"";
    }
    oversized += L"]}";
    WorkspaceState oversizedLoaded{};
    Check(DeserializeState(oversized, oversizedLoaded) &&
          oversizedLoaded.recentFolders.size() == RecentFolderHistory::kMaxRecentFoldersPersisted,
          "an over-long recentFolders list is trimmed to the persisted cap on load");
}
}

void RunRecentFoldersTests() {
    TestRecentFolderHistoryUnit();
    TestRecentFoldersWorkspaceIntegration();
    TestRecentFoldersDirtyAndClear();
    TestPersistenceSeams();
}
