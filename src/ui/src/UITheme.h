// FastFiles UI design tokens — the single source of truth for colors,
// typography, and metrics across the shell (Direct2D surfaces and Win32
// chrome). All UI surfaces should source their palette from GetUiTheme()
// instead of file-local literals, so dark/light themes stay coherent.
//
// UI polish: centralizes the previously scattered color literals
// (ColumnView, WindowShell details pane, TreemapView, SearchPanel) and adds
// dark-theme variants for badges and panels that were light-only.
#pragma once

#include <windows.h>
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
    // The Win11-blue accent refinement of modernize-ui-appearance (dark
    // ~#4C8DFF, light ~#0067C0) is intentionally deferred to the consumers of
    // accent/accentHover; the values below stay stable to avoid churn.
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
    // Modernized surfaces (modernize-ui-appearance foundation): elevated and
    // subtle surface levels plus translucent interaction overlays. The
    // alpha-bearing tokens are consumed as-is by Direct2D solid brushes.
    D2D1_COLOR_F surfaceElevated;  // elevated surface (details cards, elevated panels)
    D2D1_COLOR_F surfaceSubtle;    // subtle/hover surface (slightly lifted rows, wells)
    D2D1_COLOR_F hoverOverlay;     // translucent overlay for hover states
    D2D1_COLOR_F pressOverlay;     // translucent overlay for pressed states
    D2D1_COLOR_F selectionSoft;    // soft/unfocused selection fill (translucent accent)
    D2D1_COLOR_F dividerSubtle;    // subtle hairline divider
    D2D1_COLOR_F focusStroke;      // focus ring stroke
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
        t.surfaceElevated    = D2D1::ColorF(0x2A2B2F);
        t.surfaceSubtle      = D2D1::ColorF(0x26282C);
        t.hoverOverlay       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f);
        t.pressOverlay       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f);
        t.selectionSoft      = D2D1::ColorF(0x2B6CDA, 0.12f);
        t.dividerSubtle      = D2D1::ColorF(0xF1F3F4, 0.10f);
        t.focusStroke        = D2D1::ColorF(0x4C8DFF);
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
        t.surfaceElevated    = D2D1::ColorF(0xFFFFFF);
        t.surfaceSubtle      = D2D1::ColorF(0xF7F8F9);
        t.hoverOverlay       = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.05f);
        t.pressOverlay       = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f);
        t.selectionSoft      = D2D1::ColorF(0x2B6CDA, 0.12f);
        t.dividerSubtle      = D2D1::ColorF(0x000000, 0.08f);
        t.focusStroke        = D2D1::ColorF(0x0067C0);
    }
    return t;
}

// Centralized layout metrics. All values are in DIPs (96-DPI units); callers
// that position Win32 controls in physical pixels multiply by UiDpiScale().
namespace UiMetrics {
constexpr float kChromeHeight = 72.0f;
constexpr float kColumnWidth = 240.0f;
constexpr float kRowHeight = 28.0f;   // dense row height (was 24)
constexpr float kBadgeHeight = 28.0f;
constexpr float kControlHeight = 24.0f;
// Corner radii (DIPs)
constexpr float kRadiusSmall = 4.0f;
constexpr float kRadiusMedium = 8.0f;
// Spacing scale (DIPs)
constexpr float kSpaceXxs = 2.0f;
constexpr float kSpaceXs = 4.0f;
constexpr float kSpaceS = 8.0f;
constexpr float kSpaceM = 16.0f;
constexpr float kSpaceL = 24.0f;
// Content (DIPs)
constexpr float kIconSize = 16.0f;
// Typography ramp (DIPs)
constexpr float kFontSizeCaption = 12.0f;
constexpr float kFontSizeBody = 14.0f;
constexpr float kFontSizeSubtitle = 16.0f;
constexpr float kFontSizeTitle = 20.0f;
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

// Converts a D2D1_COLOR_F (0..1 float RGBA) to a Win32 COLORREF (0x00bbggrr),
// clamping channels to [0,255]. Used so GDI/owner-draw chrome surfaces consume
// the same UiTheme tokens as Direct2D surfaces. Alpha is dropped: COLORREF has
// no alpha channel.
inline COLORREF ToColorRef(D2D1_COLOR_F color) {
    auto clampByte = [](float channel) -> BYTE {
        if (channel < 0.0f) {
            channel = 0.0f;
        }
        if (channel > 1.0f) {
            channel = 1.0f;
        }
        return static_cast<BYTE>(static_cast<UINT>(channel * 255.0f + 0.5f));
    };
    return RGB(clampByte(color.r), clampByte(color.g), clampByte(color.b));
}

// The currently active dark-theme flag, published by WindowShell::ApplyTheme so
// owner-drawn chrome surfaces (navigation chrome, command palette, dialogs) that
// do not participate in the per-component SetDarkTheme fan-out can still source
// the active token set. Defaults to false (light); updated on every theme change.
inline bool gUiDarkTheme = false;

// Reports whether Windows High Contrast (accessibility) is active. When true,
// surfaces must suppress token interaction overlays (hover/press/selection-soft)
// and fall back to system colors so the user's accessibility color choices are
// never overridden.
inline bool UiSystemHighContrast() {
    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) != 0 &&
           (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

// Converts a Win32 COLORREF (0x00bbggrr) to an opaque D2D1_COLOR_F (0..1 RGB).
// Used for High-Contrast fallbacks so Direct2D surfaces can source system
// colors (GetSysColor) instead of token overlays when accessibility demands it.
inline D2D1_COLOR_F ToD2DColor(COLORREF color) {
    return D2D1::ColorF(static_cast<float>(GetRValue(color)) / 255.0f,
                        static_cast<float>(GetGValue(color)) / 255.0f,
                        static_cast<float>(GetBValue(color)) / 255.0f, 1.0f);
}

}  // namespace ffui
