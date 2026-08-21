#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

#include "NavigationWorkspace.h"

namespace ffui {

// Base control id for tab buttons; tab index = controlId - kTabBaseId.
constexpr int kTabBaseId = 8300;

// Pure owner-draw active-tab identity: the tab pill for `controlId` renders as
// active exactly when it is the workspace's current tab. Used by DrawTabButton
// and kept headless-testable here.
inline bool IsTabDrawItemActive(int controlId, size_t activeTabIndex) {
    return controlId >= kTabBaseId && static_cast<size_t>(controlId - kTabBaseId) == activeTabIndex;
}

class NavigationChrome {
public:
    // The second argument is true only when a tab transition should restore
    // the target tab's saved ColumnView state instead of treating the location
    // as a fresh navigation.  Address, breadcrumb, drive, and history changes
    // remain ordinary navigations. quick-access-home 5.2: the location is now
    // typed so back/forward/new-tab/tab-switch can carry the Home kind.
    using NavigateHandler = std::function<void(const ffui::NavigationLocation&, bool restoreTabState)>;
    using BeforeTabChangeHandler = std::function<void()>;

    ~NavigationChrome();
    bool Initialize(HWND owner, NavigationWorkspace* workspace, NavigateHandler navigate,
                    BeforeTabChangeHandler beforeTabChange = {});
    void Reposition();
    void Refresh();
    void FocusAddressBar();
    void SetDarkTheme(bool dark);
    bool Visible() const { return window_ != nullptr; }

private:
    static LRESULT CALLBACK WindowProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK AddressEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR subclassId, DWORD_PTR referenceData);
    static LRESULT CALLBACK CloseButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR subclassId, DWORD_PTR referenceData);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void BeginAddressEdit();
    void CommitAddressEdit();
    void CancelAddressEdit();
    void EndAddressEdit(bool restoreFocus);
    void HandleAddressFocusLost();
    void NavigateTo(const std::wstring& path, bool restoreTabState = false);
    // quick-access-home 5.2: typed navigation entry. Filesystem destinations
    // (address/drive/breadcrumb commits) go through the FromPath wrapper below;
    // back/forward/new-tab/tab-switch pass the active context's location so
    // Home reaches the shell as a Home location, never as an empty path.
    void NavigateToLocation(const NavigationLocation& location, bool restoreTabState = false);
    void RebuildDrives();
    void DrawBreadcrumbs(HDC dc, const RECT& client);
    void RebuildTabs();
    void RepositionTabs();
    void CloseTab(size_t index);
    void UpdateChromeBrush();
    void SetCloseHover(HWND hwnd, bool hover);
    int IndexOfCloseButton(HWND hwnd) const;
    void DrawTabButton(const DRAWITEMSTRUCT* item);
    void DrawCloseButton(const DRAWITEMSTRUCT* item);
    void DrawDriveItem(const DRAWITEMSTRUCT* item);
    void Cleanup();

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND back_ = nullptr;
    HWND forward_ = nullptr;
    HWND drives_ = nullptr;
    HWND addressEdit_ = nullptr;
    HWND newTab_ = nullptr;
    NavigationWorkspace* workspace_ = nullptr;
    NavigateHandler navigate_;
    BeforeTabChangeHandler beforeTabChange_;
    std::vector<std::wstring> drivePaths_;
    std::vector<RECT> breadcrumbRects_;
    std::vector<HWND> tabButtons_;
    std::vector<HWND> tabCloseButtons_;
    bool editing_ = false;
    bool darkTheme_ = false;
    int hoverSegment_ = -1;
    int hoverCloseTab_ = -1;
    HBRUSH chromeBrush_ = nullptr;
    HFONT mdl2Font_ = nullptr;
};

} // namespace ffui
