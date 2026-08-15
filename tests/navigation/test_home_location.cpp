#include <vector>
#include <windows.h>

#include "NavigationWorkspace.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {
void TestLocationSemantics() {
    using namespace ffui;
    Check(NavigationLocation::Home().path.empty(),
          "Home() always yields an empty path");
    Check(NavigationLocation::Home().IsHome() && !NavigationLocation::Home().IsFileSystem(),
          "Home() reports IsHome() and not IsFileSystem()");
    Check(NavigationLocation::FromPath(L"C:\\x").IsFileSystem() &&
          NavigationLocation::FromPath(L"C:\\x").path == L"C:\\x",
          "FromPath produces a FileSystem location carrying the path");
    Check(NavigationLocation::FromPath(L"").IsFileSystem() &&
          NavigationLocation::FromPath(L"").path.empty(),
          "FromPath with an empty string is the This PC (FileSystem, empty) root");
    Check(NavigationLocation::Home() == NavigationLocation::Home(),
          "two Home locations compare equal");
    Check(NavigationLocation::FromPath(L"C:\\x") != NavigationLocation::Home(),
          "a filesystem location is not equal to Home");
}

void TestWorkspaceHomeNavigation() {
    using namespace ffui;
    NavigationWorkspace workspace(L"");
    // quick-access-home 1.3: navigating to Home sets the flag and an empty path.
    workspace.Navigate(NavigationLocation::Home());
    const size_t homeHistorySize = workspace.ActiveContext().history.size();
    Check(workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath.empty(),
          "Navigate(Home) flags the context as Home with an empty path");
    // Re-navigating to Home is a kind-aware no-op: history does not grow.
    workspace.Navigate(NavigationLocation::Home());
    Check(workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath.empty() &&
          workspace.ActiveContext().history.size() == homeHistorySize,
          "re-navigating to Home is a kind-aware no-op (history unchanged)");

    // quick-access-home 1.3: navigating to a filesystem path clears the flag.
    workspace.Navigate(L"C:\\Folder");
    Check(!workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath == L"C:\\Folder",
          "Navigate(folder) clears the Home flag and sets the path");

    // Back/Forward restore both the kind and the path.
    Check(workspace.GoBack() && workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath.empty(),
          "GoBack restores the Home location exactly");
    Check(workspace.GoForward() && !workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath == L"C:\\Folder",
          "GoForward restores the filesystem folder exactly");
}

void TestKindAwareNoOp() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\Start");
    // Navigating to a filesystem path while Home is active must NOT no-op.
    workspace.Navigate(NavigationLocation::Home());
    workspace.Navigate(L"C:\\X");
    Check(workspace.ActiveContext().currentPath == L"C:\\X" &&
          !workspace.ActiveContext().isHomeLocation,
          "filesystem navigation while Home is active is not a no-op");
    // Navigating to Home while a filesystem path is active must NOT no-op.
    workspace.Navigate(L"C:\\Y");
    workspace.Navigate(NavigationLocation::Home());
    Check(workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath.empty(),
          "Home navigation while a folder is active is not a no-op");
}

void TestBreadcrumbsAndHomeSegment() {
    using namespace ffui;
    NavigationWorkspace workspace(L"");
    workspace.Navigate(NavigationLocation::Home());
    const auto crumbs = workspace.Breadcrumbs();
    Check(crumbs.size() == 1 && crumbs.front().label == L"Home" && crumbs.front().path.empty(),
          "Breadcrumbs on Home is a single non-clickable 'Home' segment");
    Check(!workspace.NavigateBreadcrumb(0),
          "clicking the Home breadcrumb segment is a no-op (non-navigable)");
}

void TestOpenTabDefaultsAndKinds() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\Start");
    // quick-access-home 1.3 / D7: a default new tab opens Home with history {Home}.
    workspace.OpenTab();
    Check(workspace.ActiveTabIndex() == 1, "a new tab becomes active");
    Check(workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath.empty(),
          "OpenTab() default opens a Home tab");
    Check(workspace.ActiveContext().history.size() == 1 &&
          workspace.ActiveContext().history.front().IsHome(),
          "a default Home tab seeds history exactly {Home}");

    // OpenTab(filesystem path) opens a filesystem tab.
    workspace.OpenTab(L"C:\\Folder");
    Check(!workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath == L"C:\\Folder",
          "OpenTab(path) opens a filesystem tab");

    // OpenTab(NavigationLocation::Home()) opens a Home tab.
    workspace.OpenTab(NavigationLocation::Home());
    const NavigationLocation location = workspace.TabLocation(workspace.TabCount() - 1);
    Check(location.IsHome() && location.path.empty(),
          "OpenTab(NavigationLocation::Home()) opens a Home tab (TabLocation)");
    Check(workspace.TabPath(workspace.TabCount() - 1).empty(),
          "TabPath of a Home tab is empty (same as This PC)");
}

void TestTabIsolation() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\Start");
    workspace.OpenTab(); // tab 1 = Home
    const size_t homeTab = workspace.ActiveTabIndex();
    Check(homeTab == 1 && workspace.ActiveContext().isHomeLocation,
          "the new tab is the Home tab");
    workspace.SwitchTab(0);
    workspace.Navigate(L"C:\\Folder");
    Check(workspace.ActiveContext().currentPath == L"C:\\Folder",
          "tab 0 navigates to a folder independently");
    workspace.SwitchTab(homeTab);
    Check(workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath.empty(),
          "the Home tab keeps its Home location while another tab navigates");
}

void TestPaneIsolation() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\Start");
    workspace.EnableDualPane();
    workspace.ActivatePane(0);
    workspace.Navigate(NavigationLocation::Home());
    workspace.ActivatePane(1);
    workspace.Navigate(L"C:\\Folder");

    Check(workspace.PaneContext(0).isHomeLocation &&
          workspace.PaneContext(0).currentPath.empty(),
          "pane 0 is Home and keeps an empty path");
    Check(!workspace.PaneContext(1).isHomeLocation &&
          workspace.PaneContext(1).currentPath == L"C:\\Folder",
          "pane 1 is a filesystem folder");

    const auto& homeHistory = workspace.PaneContext(0).history;
    const auto& fsHistory = workspace.PaneContext(1).history;
    Check(!homeHistory.empty() && homeHistory.back().IsHome(),
          "pane 0 history ends with the Home entry");
    Check(!fsHistory.empty() && fsHistory.back().IsFileSystem(),
          "pane 1 history ends with the filesystem entry");
    workspace.DisableDualPane();
}
}

void RunHomeLocationTests() {
    TestLocationSemantics();
    TestWorkspaceHomeNavigation();
    TestKindAwareNoOp();
    TestBreadcrumbsAndHomeSegment();
    TestOpenTabDefaultsAndKinds();
    TestTabIsolation();
    TestPaneIsolation();
}
