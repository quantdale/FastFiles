#include "NavigationChrome.h"
#include "UITheme.h"

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

// Scale a DIP metric to physical pixels for Win32 control layout. Accepts a
// float so themed corner radii (UiMetrics::kRadiusMedium) pass without a
// narrowing conversion.
int Scaled(float dipValue) {
    return static_cast<int>(ffui::UiScale(dipValue));
}

void DrawTextSegment(HDC dc, const std::wstring& text, RECT rect, COLORREF color) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

// Fills a rectangle with a freshly created solid brush (GDI has no rounded-fill
// helper, so the caller layers a rounded region on top of this for pills).
void FillRectColor(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    if (brush) {
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
}

// Fills a rounded-corner region with a solid brush. GDI's RoundRect only strokes;
// a rounded fill needs a region + FillRgn.
void DrawPill(HDC dc, const RECT& rect, COLORREF color, int radius) {
    HRGN region = CreateRoundRectRgn(rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    if (!region) return;
    HBRUSH brush = CreateSolidBrush(color);
    if (brush) {
        FillRgn(dc, region, brush);
        DeleteObject(brush);
    }
    DeleteObject(region);
}

std::wstring GetControlText(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}
} // namespace

NavigationChrome::~NavigationChrome() { Cleanup(); }

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
                              0, 0, 0, Scaled(kChromeHeight), owner, nullptr, windowClass.hInstance, this);
    if (!window_) return false;
    back_ = CreateWindowExW(0, L"BUTTON", L"◀", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            0, 0, Scaled(32), Scaled(30), window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackId)), nullptr, nullptr);
    forward_ = CreateWindowExW(0, L"BUTTON", L"▶", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                               0, 0, Scaled(32), Scaled(30), window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kForwardId)), nullptr, nullptr);
    drives_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
                                 CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
                              0, 0, Scaled(140), Scaled(260), window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDriveId)), nullptr, nullptr);
    newTab_ = CreateWindowExW(0, L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                              0, 0, Scaled(28), Scaled(28), window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNewTabId)), nullptr, nullptr);
    addressEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP,
                                   0, 0, Scaled(200), Scaled(28), window_, nullptr, nullptr, nullptr);
    if (!back_ || !forward_ || !drives_ || !newTab_ || !addressEdit_) return false;
    darkTheme_ = gUiDarkTheme;
    UpdateChromeBrush();
    mdl2Font_ = CreateFontW(-static_cast<int>(UiScale(14.0f)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
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
    SetWindowPos(window_, HWND_TOP, 0, 0, width, Scaled(kChromeHeight), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetWindowPos(back_, HWND_TOP, Scaled(4), Scaled(5), Scaled(32), Scaled(28), SWP_NOACTIVATE);
    SetWindowPos(forward_, HWND_TOP, Scaled(38), Scaled(5), Scaled(32), Scaled(28), SWP_NOACTIVATE);
    SetWindowPos(addressEdit_, HWND_TOP, Scaled(76), Scaled(5), (std::max)(Scaled(120), width - Scaled(250)), Scaled(28), SWP_NOACTIVATE);
    SetWindowPos(drives_, HWND_TOP, (std::max)(Scaled(80), width - Scaled(168)), Scaled(5), Scaled(164), Scaled(260), SWP_NOACTIVATE);
    SetWindowPos(newTab_, HWND_TOP, (std::max)(Scaled(4), width - Scaled(34)), Scaled(41), Scaled(28), Scaled(27), SWP_NOACTIVATE);
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
    hoverSegment_ = -1;
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
    SendMessageW(drives_, CB_SETITEMHEIGHT, 0, Scaled(28));
    SendMessageW(drives_, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), Scaled(24));
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
    hoverCloseTab_ = -1;
    for (size_t index = 0; index < workspace_->TabCount(); ++index) {
        const std::wstring path = workspace_->TabPath(index);
        std::wstring label = std::to_wstring(index + 1) + L": ";
        const size_t slash = path.find_last_of(L"\\/");
        label += slash == std::wstring::npos || slash + 1 >= path.size()
            ? path : path.substr(slash + 1);
        if (label.empty()) label = L"(empty)";
        HWND tab = CreateWindowExW(0, L"BUTTON", label.c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                   0, 0, Scaled(120), Scaled(27), window_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabBaseId + index)),
                                   GetModuleHandleW(nullptr), nullptr);
        HWND close = CreateWindowExW(0, L"BUTTON", L"\uE8BB",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     0, 0, Scaled(24), Scaled(27), window_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabCloseBaseId + index)),
                                     GetModuleHandleW(nullptr), nullptr);
        if (tab != nullptr && close != nullptr) {
            tabButtons_.push_back(tab);
            tabCloseButtons_.push_back(close);
            SetWindowSubclass(close, CloseButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
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
    const int available = (std::max)(Scaled(80), static_cast<int>(client.right - Scaled(42)));
    const int tabWidth = tabButtons_.empty() ? 0 : (std::max)(Scaled(100), (available - Scaled(4)) / static_cast<int>(tabButtons_.size()));
    int x = Scaled(4);
    for (size_t index = 0; index < tabButtons_.size(); ++index) {
        const int width = (std::max)(Scaled(80), (std::min)(Scaled(220), tabWidth - Scaled(24)));
        SetWindowPos(tabButtons_[index], HWND_TOP, x, Scaled(41), width, Scaled(27), SWP_NOACTIVATE);
        SetWindowPos(tabCloseButtons_[index], HWND_TOP, x + width, Scaled(41), Scaled(24), Scaled(27), SWP_NOACTIVATE);
        x += width + Scaled(24) + Scaled(4);
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
    const UiTheme theme = GetUiTheme(darkTheme_);
    const bool highContrast = UiSystemHighContrast();
    RECT navigationRect = client;
    navigationRect.bottom = (std::min)(navigationRect.bottom, 40L);
    FillRectColor(dc, navigationRect,
                  highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.background));
    const auto breadcrumbs = workspace_->Breadcrumbs();
    int x = 78;
    for (size_t index = 0; index < breadcrumbs.size(); ++index) {
        const auto& segment = breadcrumbs[index];
        RECT measured{x, 4, client.right - 8, 34};
        DrawTextW(dc, segment.label.c_str(), -1, &measured, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        const int width = (std::max)(28L, measured.right - measured.left + 16);
        RECT segmentRect{x, 4, (std::min)(client.right - 4L, static_cast<LONG>(x + width)), 34};
        breadcrumbRects_.push_back(segmentRect);
        if (static_cast<int>(index) == hoverSegment_) {
            // Composite the translucent hover overlay over the background to get a
            // solid COLORREF (ToColorRef drops alpha).
            const D2D1_COLOR_F& overlay = theme.hoverOverlay;
            const float alpha = overlay.a;
            D2D1_COLOR_F blended{};
            blended.r = theme.background.r * (1.0f - alpha) + overlay.r * alpha;
            blended.g = theme.background.g * (1.0f - alpha) + overlay.g * alpha;
            blended.b = theme.background.b * (1.0f - alpha) + overlay.b * alpha;
            blended.a = 1.0f;
            DrawPill(dc, segmentRect,
                     highContrast ? GetSysColor(COLOR_HIGHLIGHT) : ToColorRef(blended),
                     Scaled(UiMetrics::kRadiusMedium));
        }
        const bool clickable = !segment.path.empty();
        const COLORREF textColor = highContrast
            ? GetSysColor(clickable ? COLOR_HIGHLIGHT : COLOR_WINDOWTEXT)
            : ToColorRef(clickable ? theme.accent : theme.text);
        DrawTextSegment(dc, segment.label, segmentRect, textColor);
        x = segmentRect.right;
        if (x + 18 < client.right) {
            RECT separator{x, 4, x + 18, 34};
            DrawTextSegment(dc, L"›", separator,
                            highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ToColorRef(theme.textSecondary));
            x += 18;
        }
        if (x >= client.right - 8) break;
    }
    if (!workspace_->ActiveContext().addressBarError.empty()) {
        SetTextColor(dc, highContrast ? GetSysColor(COLOR_HIGHLIGHT) : ToColorRef(theme.error));
        RECT error{78, 30, client.right - 8, 40};
        DrawTextW(dc, workspace_->ActiveContext().addressBarError.c_str(), -1, &error,
                  DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

void NavigationChrome::DrawTabButton(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    const RECT& rect = item->rcItem;
    const UiTheme theme = GetUiTheme(darkTheme_);
    const bool highContrast = UiSystemHighContrast();
    const bool active = (item->itemState & ODS_DISABLED) == 0;
    const COLORREF parentBg = highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.background);
    const COLORREF pill = highContrast
        ? GetSysColor(active ? COLOR_HIGHLIGHT : COLOR_WINDOW)
        : ToColorRef(active ? theme.surfaceSubtle : theme.background);
    // Fill the whole rect with the parent background first so the rounded pill's
    // corners blend into the chrome bar.
    FillRectColor(dc, rect, parentBg);
    DrawPill(dc, rect, pill, Scaled(UiMetrics::kRadiusMedium));
    const COLORREF fg = highContrast
        ? GetSysColor(active ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT)
        : ToColorRef(active ? theme.text : theme.textSecondary);
    RECT textRect = rect;
    textRect.left += Scaled(8);
    textRect.right -= Scaled(8);
    const std::wstring label = GetControlText(item->hwndItem);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    DrawTextW(dc, label.c_str(), -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void NavigationChrome::DrawCloseButton(const DRAWITEMSTRUCT* item) {
    const int index = static_cast<int>(item->CtlID) - kTabCloseBaseId;
    const UiTheme theme = GetUiTheme(darkTheme_);
    const bool highContrast = UiSystemHighContrast();
    HDC dc = item->hDC;
    const RECT& rect = item->rcItem;
    // Blend the thin close strip into the chrome bar; the × only appears on hover.
    FillRectColor(dc, rect, highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.background));
    if (index != hoverCloseTab_) return;
    const COLORREF fg = highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ToColorRef(theme.textSecondary);
    HFONT oldFont = nullptr;
    if (mdl2Font_) oldFont = static_cast<HFONT>(SelectObject(dc, mdl2Font_));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    RECT glyphRect = rect;
    DrawTextW(dc, L"\uE8BB", -1, &glyphRect, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    if (oldFont) SelectObject(dc, oldFont);
}

void NavigationChrome::DrawDriveItem(const DRAWITEMSTRUCT* item) {
    const UINT itemId = item->itemID;
    if (itemId == static_cast<UINT>(-1)) return;
    HDC dc = item->hDC;
    const UiTheme theme = GetUiTheme(darkTheme_);
    const bool highContrast = UiSystemHighContrast();
    const bool selected = (item->itemState & ODS_SELECTED) != 0;
    const COLORREF bg = highContrast
        ? GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW)
        : ToColorRef(selected ? theme.accent : theme.background);
    const COLORREF fg = highContrast
        ? GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT)
        : ToColorRef(selected ? theme.textOnAccent : theme.text);
    FillRectColor(dc, item->rcItem, bg);
    std::wstring text(512, L'\0');
    const int length = static_cast<int>(SendMessageW(item->hwndItem, CB_GETLBTEXT, itemId,
                                                       reinterpret_cast<LPARAM>(text.data())));
    text.resize(static_cast<size_t>((std::max)(0, length)));
    RECT textRect = item->rcItem;
    textRect.left += Scaled(4);
    textRect.right -= Scaled(4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    DrawTextW(dc, text.c_str(), -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void NavigationChrome::UpdateChromeBrush() {
    if (chromeBrush_) {
        DeleteObject(chromeBrush_);
        chromeBrush_ = nullptr;
    }
    chromeBrush_ = CreateSolidBrush(ToColorRef(GetUiTheme(darkTheme_).background));
}

void NavigationChrome::SetDarkTheme(bool dark) {
    darkTheme_ = dark;
    UpdateChromeBrush();
    if (!window_) return;
    InvalidateRect(window_, nullptr, FALSE);
    if (back_) InvalidateRect(back_, nullptr, FALSE);
    if (forward_) InvalidateRect(forward_, nullptr, FALSE);
    if (newTab_) InvalidateRect(newTab_, nullptr, FALSE);
    if (drives_) InvalidateRect(drives_, nullptr, FALSE);
    for (HWND tab : tabButtons_) InvalidateRect(tab, nullptr, FALSE);
    for (HWND close : tabCloseButtons_) InvalidateRect(close, nullptr, FALSE);
}

void NavigationChrome::SetCloseHover(HWND hwnd, bool hover) {
    const int newIndex = hover ? IndexOfCloseButton(hwnd) : -1;
    if (newIndex == hoverCloseTab_) return;
    const int oldIndex = hoverCloseTab_;
    hoverCloseTab_ = newIndex;
    if (oldIndex >= 0 && static_cast<size_t>(oldIndex) < tabCloseButtons_.size()) {
        InvalidateRect(tabCloseButtons_[static_cast<size_t>(oldIndex)], nullptr, FALSE);
    }
    if (newIndex >= 0 && static_cast<size_t>(newIndex) < tabCloseButtons_.size()) {
        InvalidateRect(tabCloseButtons_[static_cast<size_t>(newIndex)], nullptr, FALSE);
    }
}

int NavigationChrome::IndexOfCloseButton(HWND hwnd) const {
    for (size_t index = 0; index < tabCloseButtons_.size(); ++index) {
        if (tabCloseButtons_[index] == hwnd) return static_cast<int>(index);
    }
    return -1;
}

void NavigationChrome::Cleanup() {
    if (chromeBrush_) {
        DeleteObject(chromeBrush_);
        chromeBrush_ = nullptr;
    }
    if (mdl2Font_) {
        DeleteObject(mdl2Font_);
        mdl2Font_ = nullptr;
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

LRESULT CALLBACK NavigationChrome::CloseButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                    UINT_PTR subclassId, DWORD_PTR referenceData) {
    UNREFERENCED_PARAMETER(subclassId);
    auto* self = reinterpret_cast<NavigationChrome*>(referenceData);
    if (self && message == WM_MOUSEMOVE) {
        self->SetCloseHover(hwnd, true);
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
    } else if (self && message == WM_MOUSELEAVE) {
        self->SetCloseHover(hwnd, false);
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
        case WM_MOUSEMOVE: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int hoverSegment = -1;
            for (size_t index = 0; index < breadcrumbRects_.size(); ++index) {
                if (PtInRect(&breadcrumbRects_[index], point)) {
                    hoverSegment = static_cast<int>(index);
                    break;
                }
            }
            if (hoverSegment != hoverSegment_) {
                hoverSegment_ = hoverSegment;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (hoverSegment_ != -1) {
                hoverSegment_ = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
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
            if (point.x >= Scaled(78) && point.x < (std::max)(Scaled(78), static_cast<int>(client.right) - Scaled(174))) {
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
        case WM_DRAWITEM: {
            const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (item->CtlType == ODT_BUTTON) {
                const int id = static_cast<int>(item->CtlID);
                if (id >= kTabBaseId && id < kTabBaseId + static_cast<int>(tabButtons_.size())) {
                    DrawTabButton(item);
                    return TRUE;
                }
                if (id >= kTabCloseBaseId && id < kTabCloseBaseId + static_cast<int>(tabCloseButtons_.size())) {
                    DrawCloseButton(item);
                    return TRUE;
                }
            } else if (item->CtlType == ODT_COMBOBOX) {
                DrawDriveItem(item);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORBTN: {
            HDC controlDc = reinterpret_cast<HDC>(wParam);
            const bool highContrast = UiSystemHighContrast();
            const UiTheme theme = GetUiTheme(darkTheme_);
            const bool enabled = IsWindowEnabled(reinterpret_cast<HWND>(lParam)) != 0;
            const COLORREF bg = highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.background);
            const COLORREF fg = highContrast
                ? GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT)
                : ToColorRef(enabled ? theme.text : theme.textSecondary);
            SetBkColor(controlDc, bg);
            SetTextColor(controlDc, fg);
            return reinterpret_cast<LRESULT>(highContrast ? GetSysColorBrush(COLOR_WINDOW) : chromeBrush_);
        }
        case WM_CTLCOLORLISTBOX: {
            HDC listDc = reinterpret_cast<HDC>(wParam);
            const bool highContrast = UiSystemHighContrast();
            const UiTheme theme = GetUiTheme(darkTheme_);
            SetBkColor(listDc, highContrast ? GetSysColor(COLOR_WINDOW) : ToColorRef(theme.background));
            SetTextColor(listDc, highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ToColorRef(theme.text));
            return reinterpret_cast<LRESULT>(highContrast ? GetSysColorBrush(COLOR_WINDOW) : chromeBrush_);
        }
        case WM_APP_ADDRESS_FOCUS_LOST:
            HandleAddressFocusLost();
            return 0;
        case WM_SETFOCUS:
            if (editing_) SetFocus(addressEdit_);
            return 0;
        case WM_DESTROY:
            Cleanup();
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace ffui