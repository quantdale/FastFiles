#include "NavigationSidebar.h"
#include "UITheme.h"

#include <algorithm>
#include <commctrl.h>
#include <windowsx.h>

namespace ffui {
namespace {
constexpr wchar_t kWindowClass[] = L"FastFilesNavigationSidebar";
constexpr int kExpandedWidth = 220;
constexpr int kCollapsedWidth = 32;
constexpr int kChromeHeight = 72;
constexpr int kHeaderHeight = 28;
constexpr int kItemHeight = 26;

// App-theme state for the GDI-painted sidebar (module-level, shared by DrawLabel/Draw).
bool gSidebarDark = false;

void DrawLabel(HDC dc, const std::wstring& text, RECT rect, bool header, bool disabled = false) {
    const ffui::UiTheme theme = ffui::GetUiTheme(gSidebarDark);
    COLORREF textColor;
    if (disabled) textColor = gSidebarDark ? RGB(0x9A, 0xA0, 0xA6) : GetSysColor(COLOR_GRAYTEXT);
    else if (header) textColor = gSidebarDark ? RGB(0x9A, 0xA0, 0xA6) : GetSysColor(COLOR_WINDOWTEXT);
    else textColor = gSidebarDark ? RGB(0xF1, 0xF3, 0xF4) : GetSysColor(COLOR_BTNTEXT);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, textColor);
    DrawTextW(dc, text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}
}

bool NavigationSidebar::Initialize(HWND owner, NavigationWorkspace* workspace, NavigateHandler navigate) {
    owner_ = owner;
    workspace_ = workspace;
    navigate_ = std::move(navigate);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &NavigationSidebar::WindowProcThunk;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    windowClass.lpszClassName = kWindowClass;
    RegisterClassW(&windowClass);
    window_ = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                              0, kChromeHeight, kExpandedWidth, 0, owner_, nullptr,
                              windowClass.hInstance, this);
    if (!window_) return false;
    RebuildRows();
    Reposition();
    return true;
}

int NavigationSidebar::Width() const {
    return workspace_ != nullptr && workspace_->State().sidebarCollapsed ? kCollapsedWidth : kExpandedWidth;
}

void NavigationSidebar::Reposition() {
    if (!window_) return;
    RECT client{};
    GetClientRect(owner_, &client);
    const int height = (std::max)(0L, client.bottom - kChromeHeight);
    SetWindowPos(window_, HWND_TOP, 0, kChromeHeight, Width(), height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(window_, nullptr, FALSE);
}

void NavigationSidebar::Refresh() {
    if (!window_) return;
    RebuildRows();
    Reposition();
    InvalidateRect(window_, nullptr, FALSE);
}

void NavigationSidebar::SetDarkTheme(bool dark) {
    darkTheme_ = dark;
    gSidebarDark = dark;
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void NavigationSidebar::RebuildRows() {
    rows_.clear();
    if (!workspace_ || workspace_->State().sidebarCollapsed) return;
    const auto addSection = [this](int section, const wchar_t* label, bool collapsed) {
        rows_.push_back({RowKind::Section, section, 0, {}, std::wstring(collapsed ? L"▸ " : L"▾ ") + label, {}});
    };
    const auto addItems = [this](int section, const std::vector<Bookmark>& items) {
        for (size_t index = 0; index < items.size(); ++index) {
            rows_.push_back({RowKind::Item, section, index, {}, L"  " + items[index].displayName, items[index].path});
        }
    };
    const auto& state = workspace_->State();
    addSection(0, L"Drives", state.drivesCollapsed);
    if (!state.drivesCollapsed) {
        std::vector<Bookmark> drives;
        for (const auto& path : NavigationWorkspace::EnumerateDrives()) drives.push_back({path, path});
        addItems(0, drives);
    }
    addSection(1, L"Known Folders", state.knownFoldersCollapsed);
    if (!state.knownFoldersCollapsed) addItems(1, workspace_->EnumerateKnownFolders());
    addSection(2, L"Bookmarks", state.bookmarksCollapsed);
    if (!state.bookmarksCollapsed) addItems(2, state.bookmarks);
}

void NavigationSidebar::Draw(HDC dc, const RECT& client) {
    const ffui::UiTheme theme = ffui::GetUiTheme(gSidebarDark);
    const COLORREF bg = gSidebarDark ? RGB(static_cast<int>(theme.background.r * 255), static_cast<int>(theme.background.g * 255), static_cast<int>(theme.background.b * 255)) : GetSysColor(COLOR_WINDOW);
    const COLORREF headerBg = gSidebarDark ? RGB(0x29, 0x2B, 0x2F) : GetSysColor(COLOR_BTNFACE);
    HBRUSH bgBrush = CreateSolidBrush(bg);
    FillRect(dc, &client, bgBrush);
    DeleteObject(bgBrush);
    if (!workspace_) return;
    if (workspace_->State().sidebarCollapsed) {
        RECT text{0, 8, client.right, 32};
        DrawLabel(dc, L"»", text, true);
        return;
    }
    int y = 0;
    for (auto& row : rows_) {
        row.bounds = {0, y, client.right, y + (row.kind == RowKind::Section ? kHeaderHeight : kItemHeight)};
        if (row.kind == RowKind::Section) {
            HBRUSH hb = CreateSolidBrush(headerBg);
            FillRect(dc, &row.bounds, hb);
            DeleteObject(hb);
            DrawLabel(dc, row.label, {8, y, client.right - 8, y + kHeaderHeight}, true);
            y += kHeaderHeight;
        } else {
            const bool unavailable = row.path.empty();
            DrawLabel(dc, row.label, {8, y, client.right - 8, y + kItemHeight}, false, unavailable);
            y += kItemHeight;
        }
    }
}

void NavigationSidebar::ToggleSection(int section) {
    if (!workspace_) return;
    auto& state = workspace_->State();
    if (section == 0) state.drivesCollapsed = !state.drivesCollapsed;
    else if (section == 1) state.knownFoldersCollapsed = !state.knownFoldersCollapsed;
    else state.bookmarksCollapsed = !state.bookmarksCollapsed;
    workspace_->MarkStateDirty();
    Refresh();
}

LRESULT CALLBACK NavigationSidebar::WindowProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    NavigationSidebar* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<NavigationSidebar*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<NavigationSidebar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->HandleMessage(hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT NavigationSidebar::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT client{};
            GetClientRect(hwnd, &client);
            Draw(dc, client);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            if (!workspace_) return 0;
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (workspace_->State().sidebarCollapsed) {
                workspace_->State().sidebarCollapsed = false;
                workspace_->MarkStateDirty();
                Refresh();
                return 0;
            }
            for (const auto& row : rows_) {
                if (!PtInRect(&row.bounds, point)) continue;
                if (row.kind == RowKind::Section) {
                    ToggleSection(row.section);
                } else if (navigate_) {
                    navigate_({row.path, row.label, message == WM_MBUTTONDOWN});
                }
                return 0;
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace ffui
