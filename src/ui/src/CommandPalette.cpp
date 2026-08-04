#include "CommandPalette.h"
#include "UITheme.h"

#include <algorithm>

namespace ffui {
namespace {
constexpr int kPaletteEditId = 6301;
constexpr int kPaletteListId = 6302;

// Scale a DIP metric to physical pixels for Win32 control layout.
int Scaled(int dipValue) {
    return static_cast<int>(ffui::UiScale(static_cast<float>(dipValue)));
}

// Lazily creates (once) a GDI brush for a theme color. Cached brushes are
// invalidated on theme changes (SetDarkTheme) and freed by the destructor so
// each theme color holds at most one live brush.
HBRUSH LazyThemeBrush(D2D1_COLOR_F color, HBRUSH& cache) {
    if (cache == nullptr) {
        cache = CreateSolidBrush(ffui::ToColorRef(color));
    }
    return cache;
}

// Deletes a cached GDI brush and resets the cache slot.
void DeleteBrush(HBRUSH& brush) {
    if (brush != nullptr) {
        DeleteObject(brush);
        brush = nullptr;
    }
}
}

CommandPalette::~CommandPalette() {
    DeleteBrush(ctlEditBrush_);
    DeleteBrush(ctlListBrush_);
    DeleteBrush(ctlAccentBrush_);
    if (paletteFont_ != nullptr) {
        DeleteObject(paletteFont_);
        paletteFont_ = nullptr;
    }
}

bool CommandPalette::Initialize(HWND owner, const CommandRegistry* registry, const ShortcutMap* shortcuts,
                                std::function<void(const std::wstring&)> execute) {
    owner_ = owner;
    registry_ = registry;
    shortcuts_ = shortcuts;
    execute_ = std::move(execute);
    edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                            0, 0, 0, 0, owner_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPaletteEditId)), nullptr, nullptr);
    list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                            WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                            0, 0, 0, 0, owner_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPaletteListId)), nullptr, nullptr);
    if (edit_ == nullptr || list_ == nullptr) return false;
    // Task 5.3 theming: Segoe UI 14 DIP body font for both controls plus a
    // fixed owner-draw row height so items match the app's dense row metric.
    const UINT dpi = static_cast<UINT>(ffui::UiDpiScale() * 96.0f + 0.5f);
    const int fontSize = static_cast<int>(ffui::UiMetrics::kFontSizeBody);
    paletteFont_ = CreateFontW(-MulDiv(fontSize, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    const HFONT font = paletteFont_ != nullptr ? paletteFont_
                                               : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(list_, LB_SETITEMHEIGHT, 0, static_cast<LPARAM>(Scaled(static_cast<int>(ffui::UiMetrics::kRowHeight))));
    return true;
}

void CommandPalette::Show(CommandContext context) {
    context_ = context;
    visible_ = true;
    SetWindowTextW(edit_, L"");
    Reposition();
    ShowWindow(edit_, SW_SHOW);
    ShowWindow(list_, SW_SHOW);
    RefreshResults();
    SetFocus(edit_);
}

void CommandPalette::Hide() {
    if (!visible_) return;
    visible_ = false;
    ShowWindow(edit_, SW_HIDE);
    ShowWindow(list_, SW_HIDE);
    SetFocus(owner_);
}

void CommandPalette::Reposition() {
    if (owner_ == nullptr) return;
    RECT client{};
    GetClientRect(owner_, &client);
    const int clientWidth = static_cast<int>(client.right - client.left);
    const int clientHeight = static_cast<int>(client.bottom - client.top);
    const int width = (std::min)(Scaled(560), (std::max)(Scaled(240), clientWidth - Scaled(40)));
    const int height = (std::min)(Scaled(360), (std::max)(Scaled(140), clientHeight - Scaled(80)));
    const int left = (client.right - width) / 2;
    const int top = (std::max)(Scaled(20), (clientHeight - height) / 4);
    SetWindowPos(edit_, HWND_TOP, left, top, width, Scaled(30), SWP_SHOWWINDOW);
    SetWindowPos(list_, HWND_TOP, left, top + Scaled(32), width, height - Scaled(32), SWP_SHOWWINDOW);
}

void CommandPalette::RefreshResults() {
    wchar_t query[512]{};
    GetWindowTextW(edit_, query, static_cast<int>(std::size(query)));
    results_ = SearchCommands(*registry_, *shortcuts_, context_, query);
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    for (const auto& result : results_) {
        std::wstring label = result.command->displayName + L"  —  " + result.command->category;
        if (!result.shortcutText.empty()) label += L"    " + result.shortcutText;
        if (!result.enabled) label += L"    [Unavailable in current context]";
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!results_.empty()) SendMessageW(list_, LB_SETCURSEL, 0, 0);
}

void CommandPalette::ExecuteSelected() {
    const LRESULT selected = SendMessageW(list_, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || static_cast<size_t>(selected) >= results_.size()) return;
    const auto& result = results_[static_cast<size_t>(selected)];
    if (!result.enabled) {
        MessageBoxW(owner_, L"That command is not available for the current selection.",
                    L"Command unavailable", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const std::wstring commandId = result.command->commandId;
    Hide();
    execute_(commandId);
}

bool CommandPalette::HandleMessage(const MSG& message) {
    if (!visible_ || message.message != WM_KEYDOWN) return false;
    if (message.wParam == VK_ESCAPE) {
        Hide();
        return true;
    }
    if (message.wParam == VK_RETURN) {
        ExecuteSelected();
        return true;
    }
    if (message.wParam == VK_DOWN || message.wParam == VK_UP) {
        LRESULT selected = SendMessageW(list_, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR) selected = 0;
        const int delta = message.wParam == VK_DOWN ? 1 : -1;
        const int maximum = static_cast<int>(results_.size()) - 1;
        if (maximum >= 0) SendMessageW(list_, LB_SETCURSEL, std::clamp(static_cast<int>(selected) + delta, 0, maximum), 0);
        return true;
    }
    return false;
}

bool CommandPalette::HandleOwnerCommand(WPARAM wParam, LPARAM lParam) {
    if (!visible_) return false;
    const int id = LOWORD(wParam);
    const int notification = HIWORD(wParam);
    if (id == kPaletteEditId && notification == EN_CHANGE && reinterpret_cast<HWND>(lParam) == edit_) {
        RefreshResults();
        return true;
    }
    if (id == kPaletteListId && notification == LBN_DBLCLK && reinterpret_cast<HWND>(lParam) == list_) {
        ExecuteSelected();
        return true;
    }
    return false;
}

void CommandPalette::SetDarkTheme(bool dark) {
    if (darkTheme_ == dark) return;
    darkTheme_ = dark;
    DeleteBrush(ctlEditBrush_);
    DeleteBrush(ctlListBrush_);
    DeleteBrush(ctlAccentBrush_);
    if (edit_ != nullptr) InvalidateRect(edit_, nullptr, FALSE);
    if (list_ != nullptr) InvalidateRect(list_, nullptr, FALSE);
}

bool CommandPalette::HandleDrawItem(LPARAM lParam) {
    const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
    if (item == nullptr || item->hwndItem != list_ || item->itemID == static_cast<UINT>(-1)) return false;
    wchar_t text[1024]{};
    if (SendMessageW(list_, LB_GETTEXT, item->itemID, reinterpret_cast<LPARAM>(text)) == LB_ERR) return true;
    const bool selected = (item->itemState & ODS_SELECTED) != 0;
    const bool enabled = item->itemID < results_.size() && results_[item->itemID].enabled;
    if (ffui::UiSystemHighContrast()) {
        FillRect(item->hDC, &item->rcItem, GetSysColorBrush(selected && enabled ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        SetTextColor(item->hDC,
                     GetSysColor(!enabled ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    } else {
        const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
        const bool emphasized = selected && enabled;
        const HBRUSH backgroundBrush = LazyThemeBrush(emphasized ? theme.accent : theme.background,
                                                      emphasized ? ctlAccentBrush_ : ctlListBrush_);
        FillRect(item->hDC, &item->rcItem, backgroundBrush);
        SetTextColor(item->hDC, ffui::ToColorRef(!enabled ? theme.textSecondary
                                                          : emphasized ? theme.textOnAccent : theme.text));
    }
    SetBkMode(item->hDC, TRANSPARENT);
    RECT textRect = item->rcItem;
    textRect.left += Scaled(8);
    textRect.right -= Scaled(8);
    DrawTextW(item->hDC, text, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    if ((item->itemState & ODS_FOCUS) != 0) DrawFocusRect(item->hDC, &item->rcItem);
    return true;
}

LRESULT CommandPalette::HandleCtlColor(UINT message, WPARAM wParam, LPARAM lParam) {
    const HDC dc = reinterpret_cast<HDC>(wParam);
    const HWND control = reinterpret_cast<HWND>(lParam);
    if (dc == nullptr || control == nullptr) return 0;
    if (message == WM_CTLCOLOREDIT && control == edit_) {
        if (ffui::UiSystemHighContrast()) {
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
        SetBkColor(dc, ffui::ToColorRef(theme.surfaceElevated));
        SetTextColor(dc, ffui::ToColorRef(theme.text));
        return reinterpret_cast<LRESULT>(LazyThemeBrush(theme.surfaceElevated, ctlEditBrush_));
    }
    if (message == WM_CTLCOLORLISTBOX && control == list_) {
        if (ffui::UiSystemHighContrast()) {
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
        SetBkColor(dc, ffui::ToColorRef(theme.background));
        SetTextColor(dc, ffui::ToColorRef(theme.text));
        return reinterpret_cast<LRESULT>(LazyThemeBrush(theme.background, ctlListBrush_));
    }
    return 0;
}

} // namespace ffui
