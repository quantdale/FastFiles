#include "CommandPalette.h"
#include "UITheme.h"
#include "UiComponents.h"
#include "ffui/NativeOverlayLayout.h"
#include "ffui/PaletteFocus.h"

#include <algorithm>
#include <commctrl.h>

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
    edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                            0, 0, 0, 0, owner_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPaletteEditId)), nullptr, nullptr);
    list_ = CreateWindowExW(0, L"LISTBOX", L"",
                            WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                            0, 0, 0, 0, owner_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPaletteListId)), nullptr, nullptr);
    if (edit_ == nullptr || list_ == nullptr) return false;
    // Task 5.3 theming: Segoe UI 14 DIP body font for both controls plus a
    // density-aware owner-draw row height (Compact matches kRowHeight).
    const UINT dpi = static_cast<UINT>(ffui::UiDpiScale() * 96.0f + 0.5f);
    const int fontSize = static_cast<int>(ffui::UiMetrics::kFontSizeBody);
    paletteFont_ = CreateFontW(-MulDiv(fontSize, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    const HFONT font = paletteFont_ != nullptr ? paletteFont_
                                               : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ApplyDensityMetrics();
    ApplyNativeExplorerTheme(edit_, ffui::gUiDarkTheme, NativeExplorerTheme::Combo);
    ApplyNativeExplorerTheme(list_, ffui::gUiDarkTheme);
    return true;
}

void CommandPalette::ApplyDensityMetrics() {
    if (list_ == nullptr) return;
    const UiDensity density = GetUiDensity(gUiDensityProfile);
    SendMessageW(list_, LB_SETITEMHEIGHT, 0,
                 static_cast<LPARAM>(Scaled(static_cast<int>(density.rowHeight))));
}

void CommandPalette::Show(CommandContext context) {
    context_ = context;
    visible_ = true;
    SetWindowTextW(edit_, L"");
    ApplyDensityMetrics();
    Reposition();
    RefreshResults();
    // final-local-certification-closure FC-001: verified (not fire-and-forget)
    // focus acquisition. Best-effort: when the owner is not yet foreground-
    // active the OS may still refuse; callers must observe EditHasFocus() /
    // PaletteFocusReady before entering text rather than assuming success.
    EnsureEditFocus();
}

bool CommandPalette::EditHasFocus() const {
    if (edit_ == nullptr) return false;
    return PaletteEditOwnsFocus(edit_, GetFocus());
}

bool CommandPalette::EnsureEditFocus() {
    if (edit_ == nullptr || IsWindow(edit_) == FALSE) return false;
    if (EditHasFocus()) return true;
    // The edit is shown with SWP_NOACTIVATE by design, so focus rests solely
    // on this SetFocus. A single unverified
    // call was the iteration-22 race: verify, then re-assert visibility and
    // try once more to cover a first call that raced SetWindowPos.
    if (IsWindowEnabled(edit_) == FALSE) return false;
    SetFocus(edit_);
    if (EditHasFocus()) return true;
    ShowWindow(edit_, SW_SHOW);
    SetFocus(edit_);
    return EditHasFocus();
}

void CommandPalette::Hide() {
    if (!visible_) return;
    visible_ = false;
    ShowWindow(edit_, SW_HIDE);
    ShowWindow(list_, SW_HIDE);
    // Guard the focus return against a dead/null owner (never hand focus to
    // a destroyed window); best-effort like Show().
    if (owner_ != nullptr && IsWindow(owner_) != FALSE) {
        SetFocus(owner_);
    }
}

void CommandPalette::Reposition() {
    // The controls are created hidden and are shown only by Show().  WM_SIZE
    // reaches this method while the palette is closed; repositioning with
    // SWP_SHOWWINDOW in that state made the edit/list overlay appear on every
    // fresh startup and could steal focus from the file browser.
    if (owner_ == nullptr || !visible_) return;
    RECT client{};
    GetClientRect(owner_, &client);
    const int clientWidth = static_cast<int>(client.right - client.left);
    const int clientHeight = static_cast<int>(client.bottom - client.top);
    const int width = (std::min)(Scaled(560), (std::max)(Scaled(240), clientWidth - Scaled(40)));
    const int height = (std::min)(Scaled(360), (std::max)(Scaled(140), clientHeight - Scaled(80)));
    const int left = (client.right - width) / 2;
    const int top = (std::max)(Scaled(20), (clientHeight - height) / 4);
    const int editHeight = Scaled(static_cast<int>(GetUiDensity(gUiDensityProfile).controlHeight + 6.0f));
    SetWindowPos(edit_, HWND_TOP, left, top, width, editHeight, kPaletteOverlayLayoutFlags);
    SetWindowPos(list_, HWND_TOP, left, top + editHeight + Scaled(2), width,
                 height - editHeight - Scaled(2), kPaletteOverlayLayoutFlags);
}

void CommandPalette::UpdateContext(CommandContext context) {
    context_ = context;
    if (!visible_) return;
    bool changed = false;
    for (auto& result : results_) {
        const bool enabled = CommandEnabled(*result.command, context_);
        changed = changed || result.enabled != enabled;
        result.enabled = enabled;
    }
    // original-vision-journey-recovery 4.7: repaint availability in place;
    // rebuilding the list would reset the query selection and scroll position.
    if (changed) InvalidateRect(list_, nullptr, FALSE);
}

void CommandPalette::RefreshResults() {
    wchar_t query[512]{};
    GetWindowTextW(edit_, query, static_cast<int>(std::size(query)));
    results_ = SearchCommands(*registry_, *shortcuts_, context_, query);
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    if (results_.empty()) {
        // ui-overhaul phase 6: a no-match query is a deliberate state, not a
        // blank list. The row is inert (not backed by a result) and reads as
        // explanatory text rather than a command.
        SendMessageW(list_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"No matching commands"));
        return;
    }
    for (const auto& result : results_) {
        // ui-v3 commands: the list stores the bare display name; HandleDrawItem
        // composes category/shortcut/disabled presentation from results_ so
        // each column gets its own typography instead of one flat string.
        SendMessageW(list_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(result.command->displayName.c_str()));
    }
    SendMessageW(list_, LB_SETCURSEL, 0, 0);
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
    if (!visible_) return false;
    if (message.message == WM_LBUTTONDOWN || message.message == WM_RBUTTONDOWN) {
        const bool inPalette = message.hwnd == edit_ || message.hwnd == list_ ||
            IsChild(edit_, message.hwnd) || IsChild(list_, message.hwnd);
        if (!inPalette) {
            // original-vision-journey-recovery: consume the dismissal click
            // without sending it to a background file/workspace control.
            Hide();
            return true;
        }
    }
    if (message.message != WM_KEYDOWN) return false;
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
    ApplyDensityMetrics();
    if (darkTheme_ == dark) {
        if (visible_) Reposition();
        return;
    }
    darkTheme_ = dark;
    DeleteBrush(ctlEditBrush_);
    DeleteBrush(ctlListBrush_);
    DeleteBrush(ctlAccentBrush_);
    ApplyNativeExplorerTheme(edit_, dark, NativeExplorerTheme::Combo);
    ApplyNativeExplorerTheme(list_, dark);
    if (edit_ != nullptr) InvalidateRect(edit_, nullptr, FALSE);
    if (list_ != nullptr) InvalidateRect(list_, nullptr, FALSE);
    if (visible_) Reposition();
}

bool CommandPalette::HandleDrawItem(LPARAM lParam) {
    const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
    if (item == nullptr || item->hwndItem != list_ || item->itemID == static_cast<UINT>(-1)) return false;
    wchar_t text[1024]{};
    if (SendMessageW(list_, LB_GETTEXT, item->itemID, reinterpret_cast<LPARAM>(text)) == LB_ERR) return true;
    const bool selected = (item->itemState & ODS_SELECTED) != 0;
    // ui-v3 commands: structured result row — command name primary, category
    // secondary after the name, shortcut right-aligned in tertiary, disabled
    // commands dimmed with an explicit "(unavailable)" cue.
    const bool haveResult = item->itemID < results_.size();
    const bool enabled = !haveResult || results_[item->itemID].enabled;
    const wchar_t* category = haveResult ? results_[item->itemID].command->category.c_str() : L"";
    const std::wstring* shortcut = haveResult ? &results_[item->itemID].shortcutText : nullptr;
    HDC dc = item->hDC;
    if (ffui::UiSystemHighContrast()) {
        FillRect(dc, &item->rcItem, GetSysColorBrush(selected && enabled ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        SetTextColor(dc,
                     GetSysColor(!enabled ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    } else {
        const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
        const bool emphasized = selected && enabled && haveResult;
        const HBRUSH backgroundBrush = LazyThemeBrush(emphasized ? theme.accent : theme.background,
                                                      emphasized ? ctlAccentBrush_ : ctlListBrush_);
        FillRect(dc, &item->rcItem, backgroundBrush);
        SetTextColor(dc, ffui::ToColorRef(!enabled ? theme.textDisabled
                                                    : !haveResult ? theme.textSecondary
                                                                  : emphasized ? theme.textOnAccent
                                                                               : theme.text));
    }
    SetBkMode(dc, TRANSPARENT);
    RECT textRect = item->rcItem;
    textRect.left += Scaled(12);
    textRect.right -= Scaled(12);
    const int rightEdge = item->rcItem.right - Scaled(12);
    // ui-overhaul phase 6: reserve the trailing shortcut column before drawing
    // the name so a long command name can never paint under its shortcut.
    RECT nameRect = textRect;
    if (shortcut != nullptr && !shortcut->empty()) {
        nameRect.right = (std::max)(static_cast<int>(nameRect.left), rightEdge - Scaled(188));
    }
    DrawTextW(dc, text, -1, &nameRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    // Secondary columns measure against the name so nothing overlaps.
    int nameWidth = 0;
    SIZE measured{};
    const int textLength = static_cast<int>(wcslen(text));
    if (GetTextExtentPoint32W(dc, text, textLength, &measured)) {
        nameWidth = (std::min)(measured.cx, nameRect.right - nameRect.left);
    }
    if (!enabled) {
        // Explicit non-color cue paired with the dimmed name.
        RECT unavailableRect{textRect.left + nameWidth + Scaled(8), item->rcItem.top,
                             rightEdge, item->rcItem.bottom};
        SetTextColor(dc, ffui::UiSystemHighContrast()
                             ? GetSysColor(COLOR_GRAYTEXT)
                             : ffui::ToColorRef(ffui::GetUiTheme(ffui::gUiDarkTheme).textTertiary));
        DrawTextW(dc, L"(unavailable)", -1, &unavailableRect,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    } else if (!ffui::UiSystemHighContrast()) {
        const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
        if (category != nullptr && category[0] != L'\0') {
            RECT categoryRect{item->rcItem.left + Scaled(12) + nameWidth + Scaled(10),
                              item->rcItem.top, rightEdge, item->rcItem.bottom};
            SetTextColor(dc, ffui::ToColorRef(selected ? theme.textOnAccent : theme.textSecondary));
            DrawTextW(dc, category, -1, &categoryRect,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        if (shortcut != nullptr && !shortcut->empty()) {
            RECT shortcutRect{rightEdge - Scaled(180), item->rcItem.top, rightEdge,
                              item->rcItem.bottom};
            SetTextColor(dc, ffui::ToColorRef(selected ? theme.textOnAccent : theme.textTertiary));
            DrawTextW(dc, shortcut->c_str(), -1, &shortcutRect,
                      DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
        }
    }
    if ((item->itemState & ODS_FOCUS) != 0) {
        // ui-v3 commands: solid inset focus ring instead of the dotted rect —
        // same grammar as every other keyboard-focus indicator.
        const ffui::UiTheme theme = ffui::GetUiTheme(ffui::gUiDarkTheme);
        RECT ring{item->rcItem.left + 2, item->rcItem.top + 2, item->rcItem.right - 2,
                  item->rcItem.bottom - 2};
        HBRUSH frame = CreateSolidBrush(ffui::UiSystemHighContrast()
                                            ? GetSysColor(COLOR_WINDOWTEXT)
                                            : ffui::ToColorRef(theme.focusStroke));
        if (frame != nullptr) {
            HRGN region = CreateRoundRectRgn(ring.left, ring.top, ring.right, ring.bottom,
                                             Scaled(4), Scaled(4));
            if (region != nullptr) {
                FrameRgn(dc, region, frame, 2, 2);
                DeleteObject(region);
            }
            DeleteObject(frame);
        }
    }
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
