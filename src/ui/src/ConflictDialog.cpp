#include "ConflictDialog.h"

#include <filesystem>
#include <string>
#include <utility>

#include "UITheme.h"

namespace ffui {

namespace {

constexpr wchar_t kConflictWindowClass[] = L"FastFilesConflictDialog";
constexpr int kReplace = 1001;
constexpr int kSkip = 1002;
constexpr int kKeepBoth = 1003;
constexpr int kApplyAll = 1004;
// ui-v3 conflict dialog: the headline uses primary text; every other static
// renders as a caption. Distinguished by control id in WM_CTLCOLORSTATIC.
constexpr int kHeadlineId = 1100;

// ui-v3 conflict dialog: layout metrics in DIP, scaled at control-creation
// time so the dialog holds its geometry across DPI settings.
int ScaleInt(int dipValue) {
    return static_cast<int>(UiScale(static_cast<float>(dipValue)));
}

// ui-v3 conflict dialog: composites a translucent theme token over an opaque
// base for GDI (no alpha channel) — same idiom as NavigationChrome.
COLORREF CompositeOverColorref(D2D1_COLOR_F overlay, D2D1_COLOR_F base) {
    const float alpha = overlay.a;
    D2D1_COLOR_F blended{};
    blended.r = base.r * (1.0f - alpha) + overlay.r * alpha;
    blended.g = base.g * (1.0f - alpha) + overlay.g * alpha;
    blended.b = base.b * (1.0f - alpha) + overlay.b * alpha;
    blended.a = 1.0f;
    return ToColorRef(blended);
}

// ui-v3 conflict dialog: bounded human-readable size ("812 B", "1.4 MB").
std::wstring FormatSizeBytes(unsigned long long bytes) {
    wchar_t buffer[64]{};
    if (bytes < 1024ull) {
        swprintf_s(buffer, L"%llu B", bytes);
        return buffer;
    }
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0) {
        swprintf_s(buffer, L"%.1f KB", kb);
        return buffer;
    }
    const double mb = kb / 1024.0;
    if (mb < 1024.0) {
        swprintf_s(buffer, L"%.1f MB", mb);
        return buffer;
    }
    swprintf_s(buffer, L"%.1f GB", mb / 1024.0);
    return buffer;
}

struct DialogState {
    ConflictDecision result;
    HWND applyAll = nullptr;
    // §5.4: themed dialog background brush (GetUiTheme(gUiDarkTheme).back-
    // ground), returned from WM_CTLCOLORDLG/WM_CTLCOLORSTATIC. Created per
    // dialog in ShowConflictDialog and freed after the modal loop.
    HBRUSH themeBrush = nullptr;
};

// ui-v3 conflict dialog: the destructive action is owner-drawn so destructive
// emphasis stays localized to the Replace button (warning-colored frame) —
// never an all-red dialog. Hover/pressed/focus resolve like the shell's icon
// buttons; High Contrast falls back to system colors.
void DrawReplaceButton(const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    const RECT& rect = item->rcItem;
    const UiTheme theme = GetUiTheme(gUiDarkTheme);
    const bool highContrast = UiSystemHighContrast();
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const D2D1_COLOR_F fillBase =
        highContrast ? ToD2DColor(GetSysColor(COLOR_BTNFACE)) : theme.surfaceElevated;
    const COLORREF fill =
        pressed ? CompositeOverColorref(theme.pressOverlay, fillBase) : ToColorRef(fillBase);
    HBRUSH brush = CreateSolidBrush(fill);
    if (brush) {
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
    HPEN pen = CreatePen(PS_SOLID, 1, highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                                   : ToColorRef(theme.warning));
    if (pen) {
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
        HBRUSH hollow = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, hollow));
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
    }
    if ((item->itemState & ODS_FOCUS) != 0) {
        RECT ring{rect.left + 2, rect.top + 2, rect.right - 2, rect.bottom - 2};
        HBRUSH frame = CreateSolidBrush(highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                                     : ToColorRef(theme.focusStroke));
        if (frame) {
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, frame));
            const int radius = ScaleInt(static_cast<int>(UiMetrics::kRadiusField));
            HRGN region = CreateRoundRectRgn(ring.left, ring.top, ring.right, ring.bottom,
                                             radius * 2, radius * 2);
            if (region) {
                FrameRgn(dc, region, frame, 2, 2);
                DeleteObject(region);
            }
            SelectObject(dc, oldBrush);
            DeleteObject(frame);
        }
    }
    const std::wstring label = L"Replace";
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ToColorRef(theme.text));
    DrawTextW(dc, label.c_str(), -1, const_cast<LPRECT>(&rect),
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
}

LRESULT CALLBACK DialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<DialogState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_COMMAND && state != nullptr) {
        ConflictChoice choice = ConflictChoice::Cancel;
        switch (LOWORD(wParam)) {
            case kReplace: choice = ConflictChoice::Replace; break;
            case kSkip: choice = ConflictChoice::Skip; break;
            case kKeepBoth: choice = ConflictChoice::KeepBoth; break;
            case IDCANCEL: break;
            default: return DefWindowProcW(window, message, wParam, lParam);
        }
        state->result = {choice, SendMessageW(state->applyAll, BM_GETCHECK, 0, 0) == BST_CHECKED};
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_CLOSE && state != nullptr) {
        state->result = {ConflictChoice::Cancel, false};
        DestroyWindow(window);
        return 0;
    }
    // §5.4: themed dialog background (WM_CTLCOLORDLG) and message-text colors
    // (WM_CTLCOLORSTATIC) so the conflict dialog is never blinding-white in
    // dark mode. Channeled through gUiDarkTheme, which WindowShell publishes.
    if ((message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC) && state != nullptr) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        const UiTheme theme = GetUiTheme(gUiDarkTheme);
        const bool headline = GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == kHeadlineId;
        SetTextColor(hdc, ToColorRef(headline ? theme.text : theme.textSecondary));
        SetBkColor(hdc, ToColorRef(theme.background));
        return reinterpret_cast<LRESULT>(state->themeBrush);
    }
    // ui-v3 conflict dialog: erase the owner-drawn Replace button background
    // with the themed surface so no system-light island appears behind it.
    if (message == WM_CTLCOLORBTN && state != nullptr) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        const UiTheme theme = GetUiTheme(gUiDarkTheme);
        SetBkColor(hdc, ToColorRef(theme.background));
        return reinterpret_cast<LRESULT>(state->themeBrush);
    }
    if (message == WM_DRAWITEM && state != nullptr) {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item->CtlType == ODT_BUTTON && static_cast<int>(item->CtlID) == kReplace) {
            DrawReplaceButton(item);
            return TRUE;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void ApplyFont(HWND window, HFONT font) {
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

} // namespace

ConflictDecision ShowConflictDialog(HWND owner, const std::wstring& source, const std::wstring& destination) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = DialogProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kConflictWindowClass;
        registered = RegisterClassW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered) return {};

    // ui-v3: sizes come from the product type ramp, not literals, so this
    // dialog cannot drift from the shared typography scale.
    HFONT bodyFont = CreateFontW(-ScaleInt(static_cast<int>(UiTypeSize(UiTypeRole::Body))), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT captionFont = CreateFontW(-ScaleInt(static_cast<int>(UiTypeSize(UiTypeRole::Caption))), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    DialogState state{};
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int width = ScaleInt(660);
    const int height = ScaleInt(300);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kConflictWindowClass, L"File name conflict",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height,
                                  owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (dialog == nullptr) {
        if (bodyFont) DeleteObject(bodyFont);
        if (captionFont) DeleteObject(captionFont);
        return {};
    }
    // §5.4: themed dialog surface + immersive dark title bar. The class's
    // hbrBackground is a stable system brush (the class is registered once and
    // reused across calls, so a per-call brush must not live there); the live
    // themed background is delivered per-paint by WM_CTLCOLORDLG below.
    state.themeBrush = CreateSolidBrush(ToColorRef(GetUiTheme(gUiDarkTheme).background));
    if (HMODULE dwmapi = GetModuleHandleW(L"dwmapi.dll")) {
        using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto dwmSetAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
        if (dwmSetAttribute) {
            const BOOL useDark = gUiDarkTheme ? TRUE : FALSE;
            dwmSetAttribute(dialog, 20, &useDark, sizeof(useDark));  // DWMWA_USE_IMMERSIVE_DARK_MODE (20)
        }
    }

    // ui-v3 conflict dialog: identify the conflicting item by name, then lay
    // both sides out side-by-side with aligned label/value typography so the
    // decision is readable at a glance.
    const std::wstring sourceName = std::filesystem::path(source).filename().wstring();
    const std::wstring destinationName = std::filesystem::path(destination).filename().wstring();
    const std::wstring headline =
        L"‘" + destinationName + L"’ already exists in the destination folder.";
    HWND headlineLabel = CreateWindowExW(0, L"STATIC", headline.c_str(), WS_CHILD | WS_VISIBLE,
                                         ScaleInt(24), ScaleInt(16), width - ScaleInt(48),
                                         ScaleInt(24), dialog,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHeadlineId)),
                                         nullptr, nullptr);

    // Bounded metadata reads for display only — failures degrade to "unknown".
    auto describeSide = [](const std::wstring& path) -> std::pair<std::wstring, std::wstring> {
        std::error_code ec;
        const std::filesystem::directory_entry entry{path, ec};
        unsigned long long size = 0;
        if (!ec) {
            const auto sizeResult = entry.file_size(ec);
            if (!ec) size = static_cast<unsigned long long>(sizeResult);
        }
        if (ec) {
            return {L"Size unknown", L""};
        }
        return {FormatSizeBytes(size), L""};
    };
    const auto [sourceSizeText, unusedSource] = describeSide(source);
    const auto [destinationSizeText, unusedDestination] = describeSide(destination);
    (void)unusedSource;
    (void)unusedDestination;

    struct ColumnTexts {
        std::wstring header;
        std::wstring name;
        std::wstring detail;
    };
    const ColumnTexts columns[2] = {
        {L"Incoming", sourceName, sourceSizeText},
        {L"Already there", destinationName, destinationSizeText},
    };
    const int columnX[2] = {ScaleInt(24), ScaleInt(344)};
    HWND columnControls[6]{};
    for (int column = 0; column < 2; ++column) {
        columnControls[column * 3] = CreateWindowExW(
            0, L"STATIC", columns[column].header.c_str(), WS_CHILD | WS_VISIBLE,
            columnX[column], ScaleInt(52), ScaleInt(280), ScaleInt(18), dialog, nullptr, nullptr, nullptr);
        columnControls[column * 3 + 1] = CreateWindowExW(
            0, L"STATIC", columns[column].name.c_str(), WS_CHILD | WS_VISIBLE,
            columnX[column], ScaleInt(74), ScaleInt(290), ScaleInt(22), dialog, nullptr, nullptr, nullptr);
        columnControls[column * 3 + 2] = CreateWindowExW(
            0, L"STATIC", columns[column].detail.c_str(), WS_CHILD | WS_VISIBLE,
            columnX[column], ScaleInt(98), ScaleInt(290), ScaleInt(18), dialog, nullptr, nullptr, nullptr);
    }

    state.applyAll = CreateWindowExW(0, L"BUTTON", L"Do this for all remaining conflicts",
                                     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, ScaleInt(24),
                                     ScaleInt(134), ScaleInt(380), ScaleInt(24),
                                     dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kApplyAll)), nullptr, nullptr);

    HWND warningLabel = CreateWindowExW(
        0, L"STATIC", L"Replacing overwrites the existing file. Skip writes nothing; Keep both renames the incoming copy.",
        WS_CHILD | WS_VISIBLE, ScaleInt(24), ScaleInt(168), width - ScaleInt(48), ScaleInt(18),
        dialog, nullptr, nullptr, nullptr);

    // ui-v3 conflict dialog: exact action labels; Skip is the safe default
    // (nothing is written); Cancel sits apart on the right.
    CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    ScaleInt(24), ScaleInt(206), ScaleInt(128), ScaleInt(34), dialog,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReplace)), nullptr, nullptr);
    HWND skip = CreateWindowExW(0, L"BUTTON", L"Skip", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                ScaleInt(164), ScaleInt(206), ScaleInt(120), ScaleInt(34), dialog,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSkip)), nullptr, nullptr);
    HWND keep = CreateWindowExW(0, L"BUTTON", L"Keep both", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                ScaleInt(296), ScaleInt(206), ScaleInt(120), ScaleInt(34), dialog,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kKeepBoth)), nullptr, nullptr);
    HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  width - ScaleInt(144), ScaleInt(206), ScaleInt(120), ScaleInt(34), dialog,
                                  reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);

    ApplyFont(headlineLabel, bodyFont);
    for (int index = 0; index < 6; ++index) {
        // Headers/details are captions, names are body-size values.
        ApplyFont(columnControls[index], index % 3 == 1 ? bodyFont : captionFont);
    }
    ApplyFont(state.applyAll, captionFont);
    ApplyFont(warningLabel, captionFont);
    ApplyFont(skip, bodyFont);
    ApplyFont(keep, bodyFont);
    ApplyFont(cancel, bodyFont);

    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    SetForegroundWindow(dialog);
    SetFocus(skip);
    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    SetFocus(owner);
    if (state.themeBrush) {
        DeleteObject(state.themeBrush);
        state.themeBrush = nullptr;
    }
    if (bodyFont) {
        DeleteObject(bodyFont);
    }
    if (captionFont) {
        DeleteObject(captionFont);
    }
    return state.result;
}

} // namespace ffui
