#include "NavigationSidebar.h"
#include "UITheme.h"
#include "UiStyle.h"

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

// Scale a DIP metric to physical pixels for Win32 control layout.
int Scaled(int dipValue) {
    return static_cast<int>(ffui::UiScale(static_cast<float>(dipValue)));
}

// App-theme state for the GDI-painted sidebar (module-level, shared by DrawLabel/Draw).
bool gSidebarDark = false;

// Fills a rounded pill (CreateRoundRectRgn + FillRgn) with the given color,
// deleting both GDI objects. right/bottom are passed +1 so the region's
// excluded bottom/right edges do not shave a pixel off the pill.
void FillPill(HDC dc, const RECT& rect, int radius, COLORREF color) {
    HRGN region = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1, radius, radius);
    if (!region) return;
    HBRUSH brush = CreateSolidBrush(color);
    if (brush) {
        FillRgn(dc, region, brush);
        DeleteObject(brush);
    }
    DeleteObject(region);
}

void DrawLabel(HDC dc, const std::wstring& text, RECT rect, bool header, bool disabled = false) {
    const ffui::UiTheme theme = ffui::GetUiTheme(gSidebarDark);
    COLORREF textColor;
    if (ffui::UiSystemHighContrast()) {
        textColor = (header || disabled) ? GetSysColor(COLOR_GRAYTEXT) : GetSysColor(COLOR_WINDOWTEXT);
    } else {
        textColor = ffui::ToColorRef((header || disabled) ? theme.textSecondary : theme.text);
    }
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
                              0, Scaled(kChromeHeight), Scaled(kExpandedWidth), 0, owner_, nullptr,
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
    const int height = (std::max)(0L, client.bottom - Scaled(kChromeHeight));
    SetWindowPos(window_, HWND_TOP, 0, Scaled(kChromeHeight), Scaled(Width()), height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
    hoveredRow_ = -1;
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
    const bool highContrast = ffui::UiSystemHighContrast();
    const bool pillsEnabled = !highContrast;
    const COLORREF bg = highContrast ? GetSysColor(COLOR_WINDOW) : ffui::ToColorRef(theme.background);
    const COLORREF headerBg = highContrast ? GetSysColor(COLOR_BTNFACE) : ffui::ToColorRef(theme.surfaceSubtle);
    HBRUSH bgBrush = CreateSolidBrush(bg);
    FillRect(dc, &client, bgBrush);
    DeleteObject(bgBrush);
    if (!workspace_) return;
    if (workspace_->State().sidebarCollapsed) {
        RECT text{0, 8, client.right, 32};
        DrawLabel(dc, L"»", text, true);
        return;
    }
    // True when the row's path is the location currently being browsed (or an
    // ancestor of it), so the item gets the Fluent selection pill.
    const auto isCurrentRow = [this](const Row& row) -> bool {
        if (row.path.empty()) return false;
        const auto normalized = [](const std::wstring& value) -> std::wstring {
            std::wstring result = value;
            std::replace(result.begin(), result.end(), L'/', L'\\');
            while (!result.empty() && result.back() == L'\\') result.pop_back();
            return result;
        };
        const std::wstring current = normalized(workspace_->ActiveContext().currentPath);
        const std::wstring item = normalized(row.path);
        if (current == item) return true;
        return current.size() > item.size() && current.compare(0, item.size(), item) == 0 &&
               current[item.size()] == L'\\';
    };
    const int pillRadius = Scaled(4);
    const int pillInsetX = Scaled(4);
    const int pillInsetY = Scaled(2);
    int y = 0;
    for (size_t index = 0; index < rows_.size(); ++index) {
        Row& row = rows_[index];
        row.bounds = {0, y, client.right, y + (row.kind == RowKind::Section ? kHeaderHeight : kItemHeight)};
        if (row.kind == RowKind::Section) {
            HBRUSH hb = CreateSolidBrush(headerBg);
            FillRect(dc, &row.bounds, hb);
            DeleteObject(hb);
            DrawLabel(dc, row.label, {8, y, client.right - 8, y + kHeaderHeight}, true);
        } else {
            const bool unavailable = row.path.empty();
            const bool hovered = pillsEnabled && static_cast<int>(index) == hoveredRow_;
            const bool selected = pillsEnabled && isCurrentRow(row);
            if (selected || hovered) {
                RECT pill = row.bounds;
                pill.left += pillInsetX;
                pill.right -= pillInsetX;
                pill.top += pillInsetY;
                pill.bottom -= pillInsetY;
                if (selected) {
                    FillPill(dc, pill, pillRadius, ffui::ToColorRef(theme.surfaceSubtle));
                }
                if (hovered) {
                    const D2D1_COLOR_F base = selected ? theme.surfaceSubtle : theme.background;
                    FillPill(dc, pill, pillRadius,
                             ffui::ToColorRef(ffui::UiLerpColor(base, theme.hoverOverlay, theme.hoverOverlay.a)));
                }
            }
            DrawLabel(dc, row.label, {8, y, client.right - 8, y + kItemHeight}, false, unavailable);
        }
        y += row.kind == RowKind::Section ? kHeaderHeight : kItemHeight;
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
        case WM_MOUSEMOVE: {
            if (!workspace_) return 0;
            int newHoveredRow = -1;
            if (!workspace_->State().sidebarCollapsed) {
                const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                for (size_t index = 0; index < rows_.size(); ++index) {
                    const Row& row = rows_[index];
                    if (row.kind == RowKind::Item && PtInRect(&row.bounds, point)) {
                        newHoveredRow = static_cast<int>(index);
                        break;
                    }
                }
            }
            if (newHoveredRow != hoveredRow_) {
                hoveredRow_ = newHoveredRow;
                InvalidateRect(window_, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE: {
            if (hoveredRow_ != -1) {
                hoveredRow_ = -1;
                InvalidateRect(window_, nullptr, FALSE);
            }
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
                    InvalidateRect(window_, nullptr, FALSE);
                }
                return 0;
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace ffui
