// Owner-paint SysHeader32 subclasses re-apply an empty visual style on
// WM_THEMECHANGED. SetWindowTheme itself sends WM_THEMECHANGED; this test
// proves the re-entry guard returns instead of overflowing the stack.

#include "NativeChrome.h"

#include "../TestSupport.h"

#include <commctrl.h>

using namespace fftest;

int main() {
    INITCOMMONCONTROLSEX init{sizeof(init), ICC_LISTVIEW_CLASSES};
    Check(InitCommonControlsEx(&init) != FALSE, "common controls initialize");

    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_POPUP | LVS_REPORT, 0, 0, 240, 80, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    Check(list != nullptr, "list view created");
    if (list == nullptr) {
        return FailureCount() == 0 ? 1 : 1;
    }
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    wchar_t name[] = L"Name";
    column.pszText = name;
    column.cx = 120;
    ListView_InsertColumn(list, 0, &column);

    ffui::AttachThemedListHeader(list);
    HWND header = ListView_GetHeader(list);
    Check(header != nullptr && IsWindow(header), "list header exists after attach");

    SendMessageW(header, WM_THEMECHANGED, 0, 0);
    SendMessageW(header, WM_THEMECHANGED, 0, 0);
    Check(IsWindow(header) != FALSE, "header HWND survives nested WM_THEMECHANGED");
    Check(IsWindow(list) != FALSE, "list HWND survives nested WM_THEMECHANGED");

    DestroyWindow(list);
    if (FailureCount() != 0) {
        fprintf(stderr, "ffnative_header_theme_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("ffnative_header_theme_tests: all checks passed\n");
    return 0;
}
