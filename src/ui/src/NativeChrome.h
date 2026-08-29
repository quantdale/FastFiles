#pragma once

// Owner-paint for remaining native SysHeader32 / TabCtrl chrome. ListView
// does not forward header NM_CUSTOMDRAW to the shell owner, so the previous
// WindowShell handler never ran and headers kept classic 3D glyphs. Subclass
// WM_PAINT on the header (and the Settings tab strip) instead.

#include "NativeChromePaint.h"
#include "UiComponents.h"

#include <commctrl.h>
#include <algorithm>

namespace ffui {
namespace {

constexpr UINT_PTR kThemedHeaderSubclassId = 0xFF7EAD01;
constexpr UINT_PTR kThemedTabSubclassId = 0xFF7AB501;

inline void FillSolid(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    if (brush == nullptr) {
        return;
    }
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

inline void DrawHeaderSortGlyph(HDC dc, const RECT& item, bool ascending, COLORREF color) {
    const int height = (std::max)(2, static_cast<int>(item.bottom - item.top));
    const int size = (std::max)(3, height / 5);
    const int cx = item.right - size - (std::max)(4, height / 6);
    const int cy = (item.top + item.bottom) / 2;
    POINT points[3]{};
    if (ascending) {
        points[0] = {cx, cy + size / 2};
        points[1] = {cx + size, cy + size / 2};
        points[2] = {cx + size / 2, cy - size / 2};
    } else {
        points[0] = {cx, cy - size / 2};
        points[1] = {cx + size, cy - size / 2};
        points[2] = {cx + size / 2, cy + size / 2};
    }
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH brush = CreateSolidBrush(color);
    if (pen == nullptr || brush == nullptr) {
        if (pen != nullptr) DeleteObject(pen);
        if (brush != nullptr) DeleteObject(brush);
        return;
    }
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, brush));
    Polygon(dc, points, 3);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

}  // namespace

inline void PaintThemedListHeader(HWND header, HDC dc) {
    if (header == nullptr || dc == nullptr) {
        return;
    }
    const NativeHeaderPalette palette = ResolveNativeHeaderPalette(gUiDarkTheme, UiSystemHighContrast());
    RECT client{};
    GetClientRect(header, &client);
    FillSolid(dc, client, palette.background);

    const int count = Header_GetItemCount(header);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(header, WM_GETFONT, 0, 0));
    if (font == nullptr) {
        font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, palette.text);
    for (int i = 0; i < count; ++i) {
        RECT item{};
        if (Header_GetItemRect(header, i, &item) == FALSE) {
            continue;
        }
        FillSolid(dc, item, palette.background);
        wchar_t text[160]{};
        HDITEMW column{};
        column.mask = HDI_TEXT | HDI_FORMAT;
        column.pszText = text;
        column.cchTextMax = static_cast<int>(sizeof(text) / sizeof(text[0]));
        Header_GetItem(header, i, &column);
        const bool sortUp = (column.fmt & HDF_SORTUP) != 0;
        const bool sortDown = (column.fmt & HDF_SORTDOWN) != 0;
        RECT textRect = item;
        textRect.left += 8;
        if (sortUp || sortDown) {
            textRect.right -= 16;
            DrawHeaderSortGlyph(dc, item, sortUp, palette.sortGlyph);
        }
        DrawTextW(dc, text, -1, &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | DT_LEFT);
        RECT divider = item;
        divider.left = (std::max)(item.left, item.right - static_cast<LONG>(1));
        FillSolid(dc, divider, palette.divider);
    }
    SelectObject(dc, oldFont);
}

inline void PaintThemedTabControl(HWND tab, HDC dc) {
    if (tab == nullptr || dc == nullptr) {
        return;
    }
    const NativeTabPalette palette = ResolveNativeTabPalette(gUiDarkTheme, UiSystemHighContrast());
    RECT client{};
    GetClientRect(tab, &client);
    FillSolid(dc, client, palette.stripBackground);

    const int count = TabCtrl_GetItemCount(tab);
    const int selected = TabCtrl_GetCurSel(tab);
    const bool focused = GetFocus() == tab;
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(tab, WM_GETFONT, 0, 0));
    if (font == nullptr) {
        font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < count; ++i) {
        RECT item{};
        if (TabCtrl_GetItemRect(tab, i, &item) == FALSE) {
            continue;
        }
        wchar_t text[64]{};
        TCITEMW info{};
        info.mask = TCIF_TEXT;
        info.pszText = text;
        info.cchTextMax = static_cast<int>(sizeof(text) / sizeof(text[0]));
        TabCtrl_GetItem(tab, i, &info);
        const bool isSelected = i == selected;
        if (isSelected) {
            FillSolid(dc, item, palette.selectedFill);
            RECT bar = item;
            bar.top = (std::max)(item.top, item.bottom - static_cast<LONG>(2));
            FillSolid(dc, bar, palette.accentBar);
            SetTextColor(dc, palette.selectedText);
            if (focused) {
                RECT ring = item;
                InflateRect(&ring, -2, -2);
                HBRUSH frame = CreateSolidBrush(palette.focusStroke);
                if (frame != nullptr) {
                    FrameRect(dc, &ring, frame);
                    DeleteObject(frame);
                }
            }
        } else {
            SetTextColor(dc, palette.unselectedText);
        }
        DrawTextW(dc, text, -1, &item,
                  DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    SelectObject(dc, oldFont);
}

inline LRESULT CALLBACK ThemedHeaderSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR subclassId, DWORD_PTR /*refData*/) {
    if (subclassId != kThemedHeaderSubclassId) {
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }
    if (UiSystemHighContrast()) {
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        PaintThemedListHeader(hwnd, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT) {
        PaintThemedListHeader(hwnd, reinterpret_cast<HDC>(wParam));
        return 0;
    }
    if (message == WM_THEMECHANGED) {
        DisableNativeWindowTheme(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

inline LRESULT CALLBACK ThemedTabSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                              UINT_PTR subclassId, DWORD_PTR /*refData*/) {
    if (subclassId != kThemedTabSubclassId) {
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }
    if (UiSystemHighContrast()) {
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        PaintThemedTabControl(hwnd, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT) {
        PaintThemedTabControl(hwnd, reinterpret_cast<HDC>(wParam));
        return 0;
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (message == WM_THEMECHANGED) {
        DisableNativeWindowTheme(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

inline void AttachThemedListHeader(HWND list) {
    if (list == nullptr || !IsWindow(list)) {
        return;
    }
    HWND header = ListView_GetHeader(list);
    if (header == nullptr || !IsWindow(header)) {
        return;
    }
    if (UiSystemHighContrast()) {
        ApplyNativeExplorerTheme(header, gUiDarkTheme);
        return;
    }
    DisableNativeWindowTheme(header);
    SetWindowSubclass(header, ThemedHeaderSubclassProc, kThemedHeaderSubclassId, 0);
    InvalidateRect(header, nullptr, TRUE);
}

inline void AttachThemedTabControl(HWND tab) {
    if (tab == nullptr || !IsWindow(tab)) {
        return;
    }
    if (UiSystemHighContrast()) {
        ApplyNativeExplorerTheme(tab, gUiDarkTheme);
        return;
    }
    DisableNativeWindowTheme(tab);
    SetWindowSubclass(tab, ThemedTabSubclassProc, kThemedTabSubclassId, 0);
    InvalidateRect(tab, nullptr, TRUE);
}

}  // namespace ffui
