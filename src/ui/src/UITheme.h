// FastFiles UI design tokens — the single source of truth for colors,
// typography, and metrics across the shell (Direct2D surfaces and Win32
// chrome). All UI surfaces should source their palette from GetUiTheme()
// instead of file-local literals, so dark/light themes stay coherent.
//
// Task 10.x (UI polish): centralizes the previously scattered color literals
// (ColumnView, WindowShell details pane, TreemapView, SearchPanel) and adds
// dark-theme variants for badges and panels that were light-only.
#pragma once

#include <d2d1.h>

namespace ffui {

struct UiTheme {
    // Surfaces
    D2D1_COLOR_F background;     // column/canvas background
    D2D1_COLOR_F surface;        // panels, details pane
    D2D1_COLOR_F border;         // borders and dividers
    // Text
    D2D1_COLOR_F text;           // primary text
    D2D1_COLOR_F textSecondary;  // muted/secondary text
    D2D1_COLOR_F textOnAccent;   // text painted on the accent (selection)
    // Accent / selection
    D2D1_COLOR_F accent;         // selection, focus, links
    D2D1_COLOR_F accentHover;    // hover variant
    // Glyphs
    D2D1_COLOR_F folderGlyph;
    D2D1_COLOR_F fileGlyph;
    // Status
    D2D1_COLOR_F error;
    D2D1_COLOR_F badgeActiveBg;
    D2D1_COLOR_F badgeActiveText;
    D2D1_COLOR_F badgeDegradedBg;
    D2D1_COLOR_F badgeDegradedText;
    D2D1_COLOR_F treemapCalculating;
    // Search edit (Win32-custom-painted)
    D2D1_COLOR_F searchBorder;
    D2D1_COLOR_F searchText;
    D2D1_COLOR_F searchPlaceholder;
};

// The app's two themes. Values follow the existing FastFiles palette where one
// already existed (dark 0x202124 / light 0xFFFFFF backgrounds, 0x2B6CDA accent)
// and extend it with the missing dark variants (badges, surface, secondary).
inline UiTheme GetUiTheme(bool dark) {
    UiTheme t{};
    if (dark) {
        t.background         = D2D1::ColorF(0x202124);
        t.surface            = D2D1::ColorF(0x292B2F);
        t.border             = D2D1::ColorF(0x50535A);
        t.text               = D2D1::ColorF(0xF1F3F4);
        t.textSecondary      = D2D1::ColorF(0x9AA0A6);
        t.textOnAccent       = D2D1::ColorF(0xFFFFFF);
        t.accent             = D2D1::ColorF(0x2B6CDA);
        t.accentHover        = D2D1::ColorF(0x3B7CE6);
        t.folderGlyph        = D2D1::ColorF(0x5B8FE0);
        t.fileGlyph          = D2D1::ColorF(0x9AA0A6);
        t.error              = D2D1::ColorF(0xF28B82);
        t.badgeActiveBg      = D2D1::ColorF(0x2E4A36);
        t.badgeActiveText    = D2D1::ColorF(0x9BE2B0);
        t.badgeDegradedBg    = D2D1::ColorF(0x4A3E22);
        t.badgeDegradedText  = D2D1::ColorF(0xFFD97B);
        t.treemapCalculating = D2D1::ColorF(0xFFF3CD);
        t.searchBorder       = D2D1::ColorF(0x50535A);
        t.searchText         = D2D1::ColorF(0xF1F3F4);
        t.searchPlaceholder  = D2D1::ColorF(0x9AA0A6);
    } else {
        t.background         = D2D1::ColorF(0xFFFFFF);
        t.surface            = D2D1::ColorF(0xF1F3F5);
        t.border             = D2D1::ColorF(0xD8D8D8);
        t.text               = D2D1::ColorF(0x000000);
        t.textSecondary      = D2D1::ColorF(0x5F6368);
        t.textOnAccent       = D2D1::ColorF(0xFFFFFF);
        t.accent             = D2D1::ColorF(0x2B6CDA);
        t.accentHover        = D2D1::ColorF(0x1E5FC8);
        t.folderGlyph        = D2D1::ColorF(0x5B8FE0);
        t.fileGlyph          = D2D1::ColorF(0x5F6368);
        t.error              = D2D1::ColorF(0xB00020);
        t.badgeActiveBg      = D2D1::ColorF(0xDDEFDD);
        t.badgeActiveText    = D2D1::ColorF(0x1B5E20);
        t.badgeDegradedBg    = D2D1::ColorF(0xFFF3CD);
        t.badgeDegradedText  = D2D1::ColorF(0x7A5B00);
        t.treemapCalculating = D2D1::ColorF(0xFFF3CD);
        t.searchBorder       = D2D1::ColorF(0x7A8AA0);
        t.searchText         = D2D1::ColorF(0x1B2430);
        t.searchPlaceholder  = D2D1::ColorF(0x6B7785);
    }
    return t;
}

// Centralized layout metrics. All values are in DIPs (96-DPI units); callers
// that position Win32 controls in physical pixels multiply by UiDpiScale().
namespace UiMetrics {
constexpr float kChromeHeight = 72.0f;
constexpr float kColumnWidth = 240.0f;
constexpr float kRowHeight = 24.0f;
constexpr float kBadgeHeight = 28.0f;
constexpr float kControlHeight = 24.0f;
}  // namespace UiMetrics

// Returns the current DPI scale factor (1.0 at 96 DPI). Uses the system DPI so
// it works before any window exists; WM_DPICHANGED-driven updates re-read it.
inline float UiDpiScale() {
    UINT dpi = 96;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using GetDpiForSystemFn = UINT(WINAPI*)();
        auto getDpi = reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(user32, "GetDpiForSystem"));
        if (getDpi) {
            dpi = getDpi();
        }
    }
    return static_cast<float>(dpi) / 96.0f;
}

// Scales a DIP value to physical pixels for Win32 control layout.
inline float UiScale(float dipValue) { return dipValue * UiDpiScale(); }

}  // namespace ffui
