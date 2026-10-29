#include "CommandPalette.h"
#include "UITheme.h"
#include "../TestSupport.h"

#include <array>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
LRESULT CALLBACK OwnerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* palette = reinterpret_cast<ffui::CommandPalette*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (palette != nullptr) {
        if (message == WM_COMMAND && palette->HandleOwnerCommand(wParam, lParam)) return 0;
        if (message == WM_SIZE) palette->Reposition();
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

RECT ChildRect(HWND child, HWND owner) {
    RECT rectangle{};
    fftest::Check(GetWindowRect(child, &rectangle) != FALSE, "native child rectangle available");
    MapWindowPoints(HWND_DESKTOP, owner, reinterpret_cast<POINT*>(&rectangle), 2);
    return rectangle;
}

void CheckBounds(HWND child, HWND owner) {
    RECT client{};
    GetClientRect(owner, &client);
    const RECT rectangle = ChildRect(child, owner);
    fftest::Check(rectangle.left >= 0 && rectangle.top >= 0 && rectangle.right <= client.right &&
                  rectangle.bottom <= client.bottom && rectangle.right >= rectangle.left && rectangle.bottom >= rectangle.top,
                  "palette control stays inside its actual owner client");
}

void CheckViewport(const ffui::CommandRegistry& registry, const ffui::ShortcutMap& shortcuts, SIZE viewport, bool dark) {
    using fftest::Check;
    ffui::CommandPalette palette;
    const HWND owner = CreateWindowExW(0, L"FastFilesPaletteGeometryFixture", L"Owned invisible palette fixture",
        WS_POPUP, 0, 0, viewport.cx, viewport.cy, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(owner != nullptr, "owned invisible parent created");
    if (owner == nullptr) return;
    SetWindowLongPtrW(owner, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&palette));
    unsigned int executions = 0;
    const bool initialized = palette.Initialize(owner, &registry, &shortcuts, [&](const std::wstring&) { ++executions; });
    Check(initialized, "actual production palette initialized");
    if (!initialized) { DestroyWindow(owner); return; }
    const HWND edit = GetDlgItem(owner, 6301);
    const HWND list = GetDlgItem(owner, 6302);
    ffui::CommandContext context{};
    context.fileOperationsOutstanding = true;
    palette.SetDarkTheme(dark);
    palette.Show(context);
    const RECT anchor = ChildRect(edit, owner);
    const int rowHeight = static_cast<int>(SendMessageW(list, LB_GETITEMHEIGHT, 0, 0));
    Check(rowHeight > 0, "native row height available");
    Check(SendMessageW(list, LB_GETCOUNT, 0, 0) == 24, "many-result native list populated");
    RECT listClient{};
    GetClientRect(list, &listClient);
    if (rowHeight > 0) Check(listClient.bottom % rowHeight == 0, "long list viewport contains complete rows");
    CheckBounds(edit, owner);
    CheckBounds(list, owner);

    SetWindowTextW(edit, L"unique");
    Check(SendMessageW(list, LB_GETCOUNT, 0, 0) == 1, "real EN_CHANGE filtering yields one result");
    GetClientRect(list, &listClient);
    const bool supportedHeight = viewport.cy >= static_cast<LONG>(ffui::UiScale(220.0f));
    Check(supportedHeight ? listClient.bottom == rowHeight : listClient.bottom <= rowHeight,
          "single result uses one full native row when space permits");
    const RECT filteredAnchor = ChildRect(edit, owner);
    Check(EqualRect(&anchor, &filteredAnchor) != FALSE, "filtering keeps the edit anchored");
    CheckBounds(edit, owner);
    CheckBounds(list, owner);
    std::cout << "observed viewport=" << viewport.cx << 'x' << viewport.cy
              << " theme=" << (dark ? "dark" : "light")
              << " dpi=" << GetDpiForWindow(owner) << " row=" << rowHeight
              << " oneResultHeight=" << listClient.bottom << '\n';

    SetWindowTextW(edit, L"zzzzzzzzzzzzzzzzzzzz");
    Check(SendMessageW(list, LB_GETCOUNT, 0, 0) == 1, "no match retains the real inert placeholder row");
    GetClientRect(list, &listClient);
    Check(supportedHeight ? listClient.bottom == rowHeight : listClient.bottom <= rowHeight,
          "no-match state does not reserve an empty result stage");
    MSG enter{};
    enter.hwnd = edit; enter.message = WM_KEYDOWN; enter.wParam = VK_RETURN;
    Check(palette.HandleMessage(enter) && executions == 0 && palette.Visible(), "no-match Enter cannot execute a command");

    SetWindowTextW(edit, L"action");
    Check(SendMessageW(list, LB_GETCOUNT, 0, 0) == 23, "clearing a no-match state recovers matching commands");
    SendMessageW(list, LB_SETCURSEL, 18, 0);
    SendMessageW(list, LB_SETTOPINDEX, 10, 0);
    SendMessageW(edit, EM_SETSEL, 3, 3);
    const LRESULT selected = SendMessageW(list, LB_GETCURSEL, 0, 0);
    const LRESULT scroll = SendMessageW(list, LB_GETTOPINDEX, 0, 0);
    const DWORD caret = static_cast<DWORD>(SendMessageW(edit, EM_GETSEL, 0, 0));
    const HWND focus = GetFocus();
    context.fileOperationsOutstanding = false;
    palette.UpdateContext(context);
    palette.Reposition();
    wchar_t query[32]{};
    GetWindowTextW(edit, query, 32);
    Check(std::wstring(query) == L"action" && SendMessageW(list, LB_GETCURSEL, 0, 0) == selected &&
          SendMessageW(list, LB_GETTOPINDEX, 0, 0) == scroll &&
          static_cast<DWORD>(SendMessageW(edit, EM_GETSEL, 0, 0)) == caret && GetFocus() == focus,
          "live availability and placement preserve query selection scroll caret and thread focus");
    MSG down{};
    down.hwnd = edit; down.message = WM_KEYDOWN; down.wParam = VK_DOWN;
    for (int index = 0; index < 24; ++index) palette.HandleMessage(down);
    Check(SendMessageW(list, LB_GETCURSEL, 0, 0) == 22, "native keyboard traversal reaches the last command");
    CheckBounds(edit, owner);
    CheckBounds(list, owner);
    MSG escape{};
    escape.hwnd = edit; escape.message = WM_KEYDOWN; escape.wParam = VK_ESCAPE;
    Check(palette.HandleMessage(escape) && !palette.Visible(), "Escape closes the real palette controller");
    palette.Reposition();
    Check((GetWindowLongPtrW(edit, GWL_STYLE) & WS_VISIBLE) == 0 &&
          (GetWindowLongPtrW(list, GWL_STYLE) & WS_VISIBLE) == 0, "closed palette remains hidden after layout");
    Check((GetWindowLongPtrW(owner, GWL_STYLE) & WS_VISIBLE) == 0, "fixture never exposes a desktop frame");
    SetWindowLongPtrW(owner, GWLP_USERDATA, 0);
    Check(DestroyWindow(owner) != FALSE, "owned parent and native children destroyed normally");
}
}

int main() {
    const auto previousDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = OwnerProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"FastFilesPaletteGeometryFixture";
    fftest::Check(RegisterClassW(&windowClass) != 0, "owned fixture class registered");
    ffui::CommandRegistry registry;
    for (int index = 0; index < 24; ++index) {
        ffui::CommandDescriptor command;
        command.commandId = L"fixture.command." + std::to_wstring(index);
        command.displayName = index == 0 ? L"Unique" : L"Action " + std::to_wstring(index);
        command.applicability = ffui::AppliesTo(ffui::SelectionKind::Empty);
        command.scope = ffui::ShortcutScope::Global;
        command.handler = [](const std::vector<std::wstring>&) {};
        command.enabledPredicate = [](const ffui::CommandContext& context) { return context.fileOperationsOutstanding; };
        fftest::Check(registry.Register(std::move(command)), "fixture command registered");
    }
    if (registry.Commands().size() != 24) return fftest::FailureCount();
    ffui::ShortcutMap shortcuts;
    shortcuts.ResetDefaults(registry);
    for (const auto density : {ffui::UiDensityProfile::Compact, ffui::UiDensityProfile::Comfortable}) {
        ffui::gUiDensityProfile = density;
        for (const bool dark : {false, true}) {
            ffui::gUiDarkTheme = dark;
            for (const SIZE viewport : std::array<SIZE, 4>{{{640,480},{1008,632},{1600,900},{160,120}}}) {
                CheckViewport(registry, shortcuts, viewport, dark);
            }
        }
    }
    if (previousDpi != nullptr) SetThreadDpiAwarenessContext(previousDpi);
    return fftest::FailureCount();
}
