#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

#include "NavigationWorkspace.h"

namespace ffui {

class NavigationChrome {
public:
    using NavigateHandler = std::function<void(const std::wstring&)>;

    bool Initialize(HWND owner, NavigationWorkspace* workspace, NavigateHandler navigate);
    void Reposition();
    void Refresh();
    void FocusAddressBar();
    bool Visible() const { return window_ != nullptr; }

private:
    static LRESULT CALLBACK WindowProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK AddressEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR subclassId, DWORD_PTR referenceData);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void BeginAddressEdit();
    void CommitAddressEdit();
    void CancelAddressEdit();
    void HandleAddressFocusLost();
    void NavigateTo(const std::wstring& path);
    void RebuildDrives();
    void DrawBreadcrumbs(HDC dc, const RECT& client);
    void RebuildTabs();
    void RepositionTabs();
    void CloseTab(size_t index);

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND back_ = nullptr;
    HWND forward_ = nullptr;
    HWND drives_ = nullptr;
    HWND addressEdit_ = nullptr;
    HWND newTab_ = nullptr;
    NavigationWorkspace* workspace_ = nullptr;
    NavigateHandler navigate_;
    std::vector<std::wstring> drivePaths_;
    std::vector<RECT> breadcrumbRects_;
    std::vector<HWND> tabButtons_;
    std::vector<HWND> tabCloseButtons_;
    bool editing_ = false;
};

} // namespace ffui
