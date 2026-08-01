#include <cstdio>
#include <cstdlib>

#include "NavigationWorkspace.h"

namespace {
int failures = 0;
void Check(bool value, const char* text) { if (!value) { std::fprintf(stderr, "FAIL: %s\n", text); ++failures; } }
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
    Check(workspace.TabCount() == 2 && workspace.ActiveContext().currentPath == L"D:\\Work", "new tab has independent context");
    workspace.SwitchTab(0); Check(workspace.ActiveContext().currentPath == L"C:\\One", "switching preserves first tab state");
    workspace.Navigate(L"C:\\Other"); Check(!workspace.CanGoForward(), "new navigation truncates forward history");
}
void TestDualPaneAndClosedTabs() {
    using namespace ffui;
    NavigationWorkspace workspace(L"C:\\"); workspace.Navigate(L"C:\\Source"); workspace.EnableDualPane(); workspace.ActivatePane(1); workspace.Navigate(L"D:\\Destination"); workspace.ActivatePane(0);
    Check(workspace.ActiveContext().currentPath == L"C:\\Source", "dual panes navigate independently"); workspace.DisableDualPane(); Check(workspace.ActiveContext().currentPath == L"C:\\Source", "disable keeps active pane");
    workspace.OpenTab(L"D:\\Work"); Check(workspace.CloseActiveTab(), "non-last tab closes"); Check(workspace.ReopenClosedTab() && workspace.ActiveContext().currentPath == L"D:\\Work", "recently closed tab reopens at last path");
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
}
int main() { TestPathParsing(); TestIndependentContextsAndHistory(); TestDualPaneAndClosedTabs(); TestBreadcrumbAndEditableAddressBar(); return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE; }
