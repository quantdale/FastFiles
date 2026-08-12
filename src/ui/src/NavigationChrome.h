#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

#include "NavigationWorkspace.h"

namespace ffui {

class NavigationChrome {
public:
    // The second argument is true only when a tab transition should restore
    // the target tab's saved ColumnView state instead of treating the path as
    // a fresh navigation.  Address, breadcrumb, drive, and history changes
    // remain ordinary path navigations.
    using NavigateHandler = std::function<void(const std::wstring&, bool restoreTabState)>;
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
