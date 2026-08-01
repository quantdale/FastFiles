#include "CommandPalette.h"

#include <algorithm>

namespace ffui {
namespace {
constexpr int kPaletteEditId = 6301;
constexpr int kPaletteListId = 6302;
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
                            WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                            0, 0, 0, 0, owner_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPaletteListId)), nullptr, nullptr);
    if (edit_ == nullptr || list_ == nullptr) return false;
    const auto font = reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(edit_, WM_SETFONT, font, TRUE);
    SendMessageW(list_, WM_SETFONT, font, TRUE);
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
    const int width = (std::min)(560, (std::max)(240, clientWidth - 40));
    const int height = (std::min)(360, (std::max)(140, clientHeight - 80));
    const int left = (client.right - width) / 2;
    const int top = (std::max)(20, (clientHeight - height) / 4);
    SetWindowPos(edit_, HWND_TOP, left, top, width, 30, SWP_SHOWWINDOW);
    SetWindowPos(list_, HWND_TOP, left, top + 32, width, height - 32, SWP_SHOWWINDOW);
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

} // namespace ffui
