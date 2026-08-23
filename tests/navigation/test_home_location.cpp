#include <filesystem>
#include <vector>
#include <windows.h>

#include "NavigationChrome.h"
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
    workspace.Navigate(NavigationLocation::Home());
    const size_t homeHistorySize = workspace.ActiveContext().history.size();
    Check(workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath.empty(),
          "Navigate(Home) flags the context as Home with an empty path");
    workspace.Navigate(NavigationLocation::Home());
    Check(workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath.empty() &&
          workspace.ActiveContext().history.size() == homeHistorySize,
          "re-navigating to Home is a kind-aware no-op (history unchanged)");

    workspace.Navigate(L"C:\\Folder");
    Check(!workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath == L"C:\\Folder",
          "Navigate(folder) clears the Home flag and sets the path");

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
    workspace.Navigate(NavigationLocation::Home());
    workspace.Navigate(L"C:\\X");
    Check(workspace.ActiveContext().currentPath == L"C:\\X" &&
          !workspace.ActiveContext().isHomeLocation,
          "filesystem navigation while Home is active is not a no-op");
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
    workspace.OpenTab();
    Check(workspace.ActiveTabIndex() == 1, "a new tab becomes active");
    Check(workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath.empty(),
          "OpenTab() default opens a Home tab");
    Check(workspace.ActiveContext().history.size() == 1 &&
          workspace.ActiveContext().history.front().IsHome(),
          "a default Home tab seeds history exactly {Home}");

    workspace.OpenTab(L"C:\\Folder");
    Check(!workspace.ActiveContext().isHomeLocation &&
          workspace.ActiveContext().currentPath == L"C:\\Folder",
          "OpenTab(path) opens a filesystem tab");

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
    workspace.OpenTab();
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

void TestDualPaneFromHomePreservesKind() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\Start");
    workspace.Navigate(NavigationLocation::Home());
    workspace.EnableDualPane();
    Check(workspace.ActivePaneCount() == 2, "dual pane is enabled");
    Check(workspace.PaneContext(0).isHomeLocation && workspace.PaneContext(0).currentPath.empty(),
          "pane 0 stays Home when dual pane is enabled from Home");
    Check(workspace.PaneContext(1).isHomeLocation && workspace.PaneContext(1).currentPath.empty(),
          "the new pane starts Home when dual pane is enabled from Home");
    Check(!workspace.PaneContext(0).history.empty() && workspace.PaneContext(0).history.back().IsHome(),
          "pane 0 keeps its own history ending at Home");
    Check(workspace.PaneContext(1).history.size() == 1 && workspace.PaneContext(1).history.front().IsHome(),
          "pane 1 history is exactly {Home}");

    workspace.ActivatePane(1);
    workspace.Navigate(L"C:\\Folder");
    Check(workspace.PaneContext(1).currentPath == L"C:\\Folder" && !workspace.PaneContext(1).isHomeLocation,
          "pane 1 navigates to a filesystem folder");
    Check(workspace.PaneContext(0).isHomeLocation && workspace.PaneContext(0).currentPath.empty(),
          "pane 0 remains Home after pane 1 navigates");
    Check(workspace.PaneContext(1).history.size() == 2 && workspace.PaneContext(1).history.back().IsFileSystem() &&
          workspace.PaneContext(1).history.front().IsHome(),
          "pane 1 history is {Home, folder} -- its typed initial location is preserved");
    workspace.DisableDualPane();
    Check(!workspace.ActiveContext().isHomeLocation && workspace.ActiveContext().currentPath == L"C:\\Folder",
          "disabling dual pane keeps the active pane (pane 1, filesystem)");
}

void TestDualPaneFromFilesystemAndThisPc() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\Start");
    workspace.Navigate(L"C:\\Foo");
    workspace.EnableDualPane();
    Check(!workspace.PaneContext(1).isHomeLocation &&
          workspace.PaneContext(1).currentPath == L"C:\\Foo" &&
          workspace.PaneContext(1).history.size() == 1 &&
          workspace.PaneContext(1).history.front() == NavigationLocation::FromPath(L"C:\\Foo"),
          "dual pane from a filesystem folder clones the typed folder with a fresh {folder} history");
    workspace.DisableDualPane();

    workspace.Navigate(L"");
    workspace.EnableDualPane();
    Check(!workspace.PaneContext(1).isHomeLocation && workspace.PaneContext(1).currentPath.empty() &&
          workspace.PaneContext(1).history.size() == 1 &&
          workspace.PaneContext(1).history.front() == NavigationLocation::FromPath(L""),
          "dual pane from This PC clones FileSystem(empty), never Home");
}

void TestDisplayOnlyShortNameExpansion() {
    using namespace ffui;
    Check(ExpandLocalShortNameForDisplay(L"Normal Folder", L"C:\\Normal Folder") == L"Normal Folder",
          "display helper avoids filesystem resolution when the label has no 8.3 marker");
    Check(ExpandLocalShortNameForDisplay(L"SHARE~1", L"\\\\server\\share\\SHARE~1") == L"SHARE~1",
          "display helper never performs UNC/network short-name resolution");

    wchar_t tempPath[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempPath) == 0) return;
    const std::filesystem::path root = std::filesystem::path(tempPath) /
        (L"FastFiles-short-name-display-" + std::to_wstring(GetCurrentProcessId()));
    const std::filesystem::path longFolder = root / L"Very Long Folder Name For FastFiles Display";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(longFolder, error);
    if (error) return;

    wchar_t shortBuffer[32768]{};
    const DWORD shortLength = GetShortPathNameW(longFolder.c_str(), shortBuffer,
                                                static_cast<DWORD>(std::size(shortBuffer)));
    if (shortLength > 0 && shortLength < std::size(shortBuffer)) {
        const std::wstring shortPath(shortBuffer, shortLength);
        const size_t slash = shortPath.find_last_of(L"\\/");
        const std::wstring shortLeaf = slash == std::wstring::npos ? shortPath : shortPath.substr(slash + 1);
        if (shortLeaf.find(L'~') != std::wstring::npos) {
            const std::wstring expected = longFolder.filename().wstring();
            Check(ExpandLocalShortNameForDisplay(shortLeaf, shortPath) == expected,
                  "existing local 8.3 leaf expands to its long display name");
            BreadcrumbSegment segment(shortLeaf, shortPath);
            Check(segment.label == expected && segment.path == shortPath,
                  "breadcrumb expands only its label while preserving canonical short navigation path");
            const std::wstring tabLabel = TabLabelForLocation(2, NavigationLocation::FromPath(shortPath));
            Check(tabLabel == L"2: " + expected,
                  "tab label shares breadcrumb 8.3 display normalization");
        }
    }
    std::filesystem::remove_all(root, error);
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
    TestDualPaneFromHomePreservesKind();
    TestDualPaneFromFilesystemAndThisPc();
    TestDisplayOnlyShortNameExpansion();
}
