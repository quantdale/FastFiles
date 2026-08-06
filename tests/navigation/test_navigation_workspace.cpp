#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <windows.h>

#include "NavigationWorkspace.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {
void TestPathParsing() {
    using namespace ffui;
    Check(ParseNavigationPath(L"  \"C:/Windows/\"  ").path == L"C:\\Windows", "quoted forward-slash path is normalized");
    Check(ParseNavigationPath(L"C:\\Windows\\System32\\..\\").path == L"C:\\Windows", "embedded dot-dot is canonicalized");
    Check(ParseNavigationPath(L"%SystemRoot%\\").error.empty(), "environment variables expand");
    Check(ParseNavigationPath(L"\\\\server\\share\\folder\\").error.empty(), "UNC path is accepted");
    Check(!ParseNavigationPath(L"relative\\path").error.empty(), "relative path is rejected");
    Check(!ParseNavigationPath(L"C:\\bad<name>").error.empty(), "reserved characters are rejected");
    Check(!ParseNavigationPath(L" \t ").error.empty(), "empty input is rejected");
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
    Check(NavigationWorkspace::LoadState(loaded) && loaded.bookmarks.size() == 1 &&
          loaded.bookmarks.front().displayName == L"Projects" && loaded.bookmarks.front().path == L"C:\\Projects" &&
          loaded.sidebarCollapsed && loaded.bookmarksCollapsed, "workspace state persists bookmarks and sidebar state");

    const auto stateFile = localAppData / L"FastFiles" / L"workspace-state.json";
    {
        std::wofstream corrupt(stateFile, std::ios::trunc);
        corrupt << L"{ this is not valid workspace state";
    }
    NavigationWorkspace::LoadState(loaded);
    Check(loaded.bookmarks.empty(), "corrupt workspace state falls back to empty state");
    std::filesystem::remove_all(localAppData, error);
    if (oldLength == 0) SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);
    else SetEnvironmentVariableW(L"LOCALAPPDATA", oldValue.c_str());
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
int main() {
    TestPathParsing(); TestIndependentContextsAndHistory(); TestDualPaneAndClosedTabs();
    TestBreadcrumbAndEditableAddressBar(); TestWorkspacePersistenceFallback(); TestCopyRelativePathBaseResolution();
    TestConcurrentIndependentContextsReadDifferentPaths();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
