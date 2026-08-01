#include "NavigationChrome.h"

#include <algorithm>
#include <commctrl.h>
#include <windowsx.h>

namespace ffui {
namespace {
constexpr wchar_t kWindowClass[] = L"FastFilesNavigationChrome";
constexpr int kBackId = 8201;
constexpr int kForwardId = 8202;
constexpr int kDriveId = 8203;
constexpr int kNewTabId = 8204;
constexpr int kTabBaseId = 8300;
constexpr int kTabCloseBaseId = 8400;
constexpr UINT WM_APP_ADDRESS_FOCUS_LOST = WM_APP + 52;
constexpr int kChromeHeight = 72;

void DrawTextSegment(HDC dc, const std::wstring& text, RECT rect, bool clickable) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(clickable ? COLOR_HIGHLIGHT : COLOR_WINDOWTEXT));
    DrawTextW(dc, text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}
}

bool NavigationChrome::Initialize(HWND owner, NavigationWorkspace* workspace, NavigateHandler navigate) {
    owner_ = owner;
    workspace_ = workspace;
    navigate_ = std::move(navigate);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &NavigationChrome::WindowProcThunk;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    windowClass.lpszClassName = kWindowClass;
    RegisterClassW(&windowClass);

    window_ = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                              0, 0, 0, kChromeHeight, owner, nullptr, windowClass.hInstance, this);
    if (!window_) return false;
    back_ = CreateWindowExW(0, L"BUTTON", L"◀", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            0, 0, 32, 30, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackId)), nullptr, nullptr);
    forward_ = CreateWindowExW(0, L"BUTTON", L"▶", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                               0, 0, 32, 30, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kForwardId)), nullptr, nullptr);
    drives_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
                              0, 0, 140, 260, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDriveId)), nullptr, nullptr);
    newTab_ = CreateWindowExW(0, L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                              0, 0, 28, 28, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNewTabId)), nullptr, nullptr);
    addressEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP,
                                   0, 0, 200, 28, window_, nullptr, nullptr, nullptr);
    if (!back_ || !forward_ || !drives_ || !newTab_ || !addressEdit_) return false;
    SetWindowSubclass(addressEdit_, AddressEditProc, 1, reinterpret_cast<DWORD_PTR>(this));
    RebuildDrives();
    Refresh();
    return true;
}

void NavigationChrome::Reposition() {
    if (!window_) return;
    RECT client{};
    GetClientRect(owner_, &client);
    const int width = (std::max)(0L, client.right - client.left);
    SetWindowPos(window_, HWND_TOP, 0, 0, width, kChromeHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetWindowPos(back_, HWND_TOP, 4, 5, 32, 28, SWP_NOACTIVATE);
    SetWindowPos(forward_, HWND_TOP, 38, 5, 32, 28, SWP_NOACTIVATE);
    SetWindowPos(addressEdit_, HWND_TOP, 76, 5, (std::max)(120, width - 250), 28, SWP_NOACTIVATE);
    SetWindowPos(drives_, HWND_TOP, (std::max)(80, width - 168), 5, 164, 260, SWP_NOACTIVATE);
    SetWindowPos(newTab_, HWND_TOP, (std::max)(4, width - 34), 41, 28, 27, SWP_NOACTIVATE);
    RepositionTabs();
    InvalidateRect(window_, nullptr, FALSE);
}

void NavigationChrome::Refresh() {
    if (!window_ || !workspace_) return;
    EnableWindow(back_, workspace_->CanGoBack());
    EnableWindow(forward_, workspace_->CanGoForward());
    RebuildTabs();
    RepositionTabs();
    if (!editing_) InvalidateRect(window_, nullptr, FALSE);
}

void NavigationChrome::FocusAddressBar() { BeginAddressEdit(); }

void NavigationChrome::BeginAddressEdit() {
    if (editing_ || !workspace_) return;
    workspace_->BeginAddressBarEdit();
    editing_ = true;
    SetWindowTextW(addressEdit_, workspace_->ActiveContext().addressBarText.c_str());
    ShowWindow(addressEdit_, SW_SHOW);
    SetFocus(addressEdit_);
    SendMessageW(addressEdit_, EM_SETSEL, 0, -1);
    InvalidateRect(window_, nullptr, FALSE);
}

void NavigationChrome::CommitAddressEdit() {
    if (!editing_ || !workspace_) return;
    std::wstring text(32768, L'\0');
    const int length = GetWindowTextW(addressEdit_, text.data(), static_cast<int>(text.size()));
    text.resize(static_cast<size_t>((std::max)(0, length)));
    const PathCommitResult result = CommitAddressBarPath(*workspace_, text);
    if (result == PathCommitResult::Navigated) {
        editing_ = false;
        ShowWindow(addressEdit_, SW_HIDE);
        NavigateTo(workspace_->ActiveContext().currentPath);
        return;
    }
    InvalidateRect(window_, nullptr, FALSE);
    SetFocus(addressEdit_);
    SendMessageW(addressEdit_, EM_SETSEL, 0, -1);
}

void NavigationChrome::CancelAddressEdit() {
    if (!editing_ || !workspace_) return;
    workspace_->CancelAddressBarEdit();
    editing_ = false;
    ShowWindow(addressEdit_, SW_HIDE);
    SetFocus(window_);
    InvalidateRect(window_, nullptr, FALSE);
}

void NavigationChrome::HandleAddressFocusLost() {
    if (editing_) CancelAddressEdit();
}

void NavigationChrome::NavigateTo(const std::wstring& path) {
    if (!path.empty() && navigate_) navigate_(path);
    Refresh();
}

void NavigationChrome::RebuildDrives() {
    if (!drives_) return;
    drivePaths_ = NavigationWorkspace::EnumerateDrives();
    SendMessageW(drives_, CB_RESETCONTENT, 0, 0);
    for (const auto& path : drivePaths_) {
        const int index = static_cast<int>(SendMessageW(drives_, CB_ADDSTRING, 0,
                                                         reinterpret_cast<LPARAM>(path.c_str())));
        SendMessageW(drives_, CB_SETITEMDATA, index, static_cast<LPARAM>(index));
    }
}

void NavigationChrome::RebuildTabs() {
    if (!window_ || !workspace_) return;
    for (HWND tab : tabButtons_) DestroyWindow(tab);
    for (HWND close : tabCloseButtons_) DestroyWindow(close);
    tabButtons_.clear();
    tabCloseButtons_.clear();
    for (size_t index = 0; index < workspace_->TabCount(); ++index) {
        const std::wstring path = workspace_->TabPath(index);
        std::wstring label = std::to_wstring(index + 1) + L": ";
        const size_t slash = path.find_last_of(L"\\/");
        label += slash == std::wstring::npos || slash + 1 >= path.size()
            ? path : path.substr(slash + 1);
        if (label.empty()) label = L"(empty)";
        HWND tab = CreateWindowExW(0, L"BUTTON", label.c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   0, 0, 120, 27, window_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabBaseId + index)),
                                   GetModuleHandleW(nullptr), nullptr);
        HWND close = CreateWindowExW(0, L"BUTTON", L"×",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     0, 0, 24, 27, window_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabCloseBaseId + index)),
                                     GetModuleHandleW(nullptr), nullptr);
        if (tab != nullptr && close != nullptr) {
            tabButtons_.push_back(tab);
            tabCloseButtons_.push_back(close);
            EnableWindow(tab, index == workspace_->ActiveTabIndex());
        } else {
            if (tab != nullptr) DestroyWindow(tab);
            if (close != nullptr) DestroyWindow(close);
        }
    }
}

void NavigationChrome::RepositionTabs() {
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int available = (std::max)(80L, client.right - 42L);
    const int tabWidth = tabButtons_.empty() ? 0 : (std::max)(100, (available - 4) / static_cast<int>(tabButtons_.size()));
    int x = 4;
    for (size_t index = 0; index < tabButtons_.size(); ++index) {
        const int width = (std::max)(80, (std::min)(220, tabWidth - 24));
        SetWindowPos(tabButtons_[index], HWND_TOP, x, 41, width, 27, SWP_NOACTIVATE);
        SetWindowPos(tabCloseButtons_[index], HWND_TOP, x + width, 41, 24, 27, SWP_NOACTIVATE);
        x += width + 24 + 4;
    }
}

void NavigationChrome::CloseTab(size_t index) {
    if (!workspace_ || index >= workspace_->TabCount()) return;
    if (index != workspace_->ActiveTabIndex()) {
        workspace_->SwitchTab(index);
    }
    if (workspace_->CloseActiveTab()) NavigateTo(workspace_->ActiveContext().currentPath);
    else Refresh();
}

void NavigationChrome::DrawBreadcrumbs(HDC dc, const RECT& client) {
    breadcrumbRects_.clear();
    if (!workspace_) return;
    RECT navigationRect = client;
    navigationRect.bottom = (std::min)(navigationRect.bottom, 40L);
    FillRect(dc, &navigationRect, GetSysColorBrush(COLOR_WINDOW));
    const auto breadcrumbs = workspace_->Breadcrumbs();
    int x = 78;
    for (const auto& segment : breadcrumbs) {
        RECT measured{x, 4, client.right - 8, 34};
        DrawTextW(dc, segment.label.c_str(), -1, &measured, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        const int width = (std::max)(28L, measured.right - measured.left + 16);
        RECT segmentRect{x, 4, (std::min)(client.right - 4L, static_cast<LONG>(x + width)), 34};
        breadcrumbRects_.push_back(segmentRect);
        DrawTextSegment(dc, segment.label, segmentRect, !segment.path.empty());
        x = segmentRect.right;
        if (x + 18 < client.right) {
            RECT separator{x, 4, x + 18, 34};
            DrawTextSegment(dc, L"›", separator, false);
            x += 18;
        }
        if (x >= client.right - 8) break;
    }
    if (!workspace_->ActiveContext().addressBarError.empty()) {
        SetTextColor(dc, RGB(180, 30, 30));
        RECT error{78, 30, client.right - 8, 40};
        DrawTextW(dc, workspace_->ActiveContext().addressBarError.c_str(), -1, &error,
                  DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

LRESULT CALLBACK NavigationChrome::WindowProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    NavigationChrome* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<NavigationChrome*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<NavigationChrome*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->HandleMessage(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK NavigationChrome::AddressEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                    UINT_PTR subclassId, DWORD_PTR referenceData) {
    UNREFERENCED_PARAMETER(subclassId);
    auto* self = reinterpret_cast<NavigationChrome*>(referenceData);
    if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        if (self) self->CommitAddressEdit();
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
        if (self) self->CancelAddressEdit();
        return 0;
    }
    if (message == WM_KILLFOCUS && self) {
        PostMessageW(self->window_, WM_APP_ADDRESS_FOCUS_LOST, 0, 0);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT NavigationChrome::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            Reposition();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT client{};
            GetClientRect(hwnd, &client);
            DrawBreadcrumbs(dc, client);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            for (size_t index = 0; index < breadcrumbRects_.size(); ++index) {
                if (PtInRect(&breadcrumbRects_[index], point) && workspace_ && workspace_->NavigateBreadcrumb(index)) {
                    NavigateTo(workspace_->ActiveContext().currentPath);
                    return 0;
                }
            }
            RECT client{};
            GetClientRect(hwnd, &client);
            if (point.x >= 78 && point.x < (std::max)(78L, client.right - 174L)) {
                BeginAddressEdit();
                return 0;
            }
            break;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == kBackId && HIWORD(wParam) == BN_CLICKED && workspace_ && workspace_->GoBack()) {
                NavigateTo(workspace_->ActiveContext().currentPath);
                return 0;
            }
            if (id == kForwardId && HIWORD(wParam) == BN_CLICKED && workspace_ && workspace_->GoForward()) {
                NavigateTo(workspace_->ActiveContext().currentPath);
                return 0;
            }
            if (id == kDriveId && HIWORD(wParam) == CBN_SELCHANGE) {
                const int index = static_cast<int>(SendMessageW(drives_, CB_GETCURSEL, 0, 0));
                if (index >= 0 && static_cast<size_t>(index) < drivePaths_.size()) {
                    if (workspace_) workspace_->Navigate(drivePaths_[static_cast<size_t>(index)]);
                    NavigateTo(drivePaths_[static_cast<size_t>(index)]);
                }
                return 0;
            }
            if (id == kNewTabId && HIWORD(wParam) == BN_CLICKED && workspace_) {
                workspace_->OpenTab();
                NavigateTo(workspace_->ActiveContext().currentPath);
                return 0;
            }
            if (HIWORD(wParam) == BN_CLICKED && workspace_ && id >= kTabBaseId && id < kTabBaseId + static_cast<int>(workspace_->TabCount())) {
                const size_t index = static_cast<size_t>(id - kTabBaseId);
                if (workspace_->SwitchTab(index)) NavigateTo(workspace_->ActiveContext().currentPath);
                return 0;
            }
            if (HIWORD(wParam) == BN_CLICKED && workspace_ && id >= kTabCloseBaseId && id < kTabCloseBaseId + static_cast<int>(workspace_->TabCount())) {
                CloseTab(static_cast<size_t>(id - kTabCloseBaseId));
                return 0;
            }
            break;
        }
        case WM_APP_ADDRESS_FOCUS_LOST:
            HandleAddressFocusLost();
            return 0;
        case WM_SETFOCUS:
            if (editing_) SetFocus(addressEdit_);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace ffui
