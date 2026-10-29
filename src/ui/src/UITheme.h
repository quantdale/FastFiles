// FastFiles UI design tokens — the single source of truth for colors,
// typography, and metrics across the shell (Direct2D surfaces and Win32
// chrome). All UI surfaces should source their palette from GetUiTheme()
// instead of file-local literals, so dark/light themes stay coherent.
//
// UI v3 reference lock: the palette intentionally follows an information-first
// productivity-tool direction validated against Refero research (Cal.com,
// Perplexity, shadcn/ui, Dropbox/Cake-style file managers, and Linear): quiet
// neutral surfaces, minimal border contrast, one disciplined blue accent, and
// stronger hierarchy through typography/spacing rather than decoration.
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
    // Surface hierarchy and interaction overlays. Alpha-bearing tokens are
    // consumed as-is by Direct2D and composited for GDI owner-drawn chrome.
    D2D1_COLOR_F surfaceElevated;  // dialogs, details cards, elevated panels
    D2D1_COLOR_F surfaceSubtle;    // active tabs, wells, quiet grouped regions
    D2D1_COLOR_F hoverOverlay;     // restrained pointer feedback
    D2D1_COLOR_F pressOverlay;     // immediate stronger pointer feedback
    D2D1_COLOR_F selectionSoft;    // inactive/unfocused selection
    D2D1_COLOR_F dividerSubtle;    // hairline structural separator
    D2D1_COLOR_F focusStroke;      // keyboard focus ring
    // Semantic extensions. Status colors must always be paired with a non-color
    // cue at call sites — icon/text/shape — never used as the sole carrier.
    D2D1_COLOR_F textTertiary;
    D2D1_COLOR_F textDisabled;
    D2D1_COLOR_F success;
    D2D1_COLOR_F warning;
    D2D1_COLOR_F info;
    D2D1_COLOR_F dropTargetStroke;
};

// Calm productivity palette. Contrast is validated by tests/ui/test_ui_style.cpp.
// The two modes intentionally share the same semantic hierarchy rather than
// behaving as unrelated skins.
inline UiTheme GetUiTheme(bool dark) {
    UiTheme t{};
    if (dark) {
        // Things-like calm organization with Dropbox's tonal dark hierarchy:
        // the canvas, navigation surface, and primary work surface are close
        // neighbors, while the blue accent is reserved for interaction.
        t.background         = D2D1::ColorF(0x111315);
        t.surface            = D2D1::ColorF(0x1A1E22);
        t.border             = D2D1::ColorF(0x2A3036);
        t.text               = D2D1::ColorF(0xF3F5F7);
        t.textSecondary      = D2D1::ColorF(0xA7B0BA);
        t.textOnAccent       = D2D1::ColorF(0x101820);
        t.accent             = D2D1::ColorF(0x4C8DFF);
        t.accentHover        = D2D1::ColorF(0x6A9FFF);
        t.folderGlyph        = D2D1::ColorF(0x6A9FFF);
        t.fileGlyph          = D2D1::ColorF(0xA7B0BA);
        t.error              = D2D1::ColorF(0xFF7B72);
        t.badgeActiveBg      = D2D1::ColorF(0x173826);
        t.badgeActiveText    = D2D1::ColorF(0x8DDBA8);
        t.badgeDegradedBg    = D2D1::ColorF(0x433517);
        t.badgeDegradedText  = D2D1::ColorF(0xFFD36A);
        t.treemapCalculating = D2D1::ColorF(0x5A4616);
        t.searchBorder       = D2D1::ColorF(0x3B424D);
        t.searchText         = D2D1::ColorF(0xF4F6F8);
        t.searchPlaceholder  = D2D1::ColorF(0x929CAA);
        t.surfaceElevated    = D2D1::ColorF(0x20252A);
        t.surfaceSubtle      = D2D1::ColorF(0x15181B);
        t.hoverOverlay       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.060f);
        t.pressOverlay       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.105f);
        t.selectionSoft      = D2D1::ColorF(0x4C8DFF, 0.16f);
        t.dividerSubtle      = D2D1::ColorF(0xF4F6F8, 0.075f);
        t.focusStroke        = D2D1::ColorF(0x6A9FFF);
        t.textTertiary       = D2D1::ColorF(0x74808C);
        t.textDisabled       = D2D1::ColorF(0x69747F);
        t.success            = D2D1::ColorF(0x81C995);
        t.warning            = D2D1::ColorF(0xF9AB00);
        t.info               = D2D1::ColorF(0x8AB4F8);
        t.dropTargetStroke   = D2D1::ColorF(0x82B1FF);
    } else {
        // Native Precision Workspace light mode: a cool canvas, a slightly
        // deeper navigation surface, and white work surfaces keep structure
        // visible without turning the browser into a wall of cards.
        t.background         = D2D1::ColorF(0xF7F8FA);
        t.surface            = D2D1::ColorF(0xFFFFFF);
        t.border             = D2D1::ColorF(0xE1E5EA);
        t.text               = D2D1::ColorF(0x202428);
        t.textSecondary      = D2D1::ColorF(0x66717D);
        t.textOnAccent       = D2D1::ColorF(0xFFFFFF);
        t.accent             = D2D1::ColorF(0x2A70D1);
        t.accentHover        = D2D1::ColorF(0x205EBA);
        t.folderGlyph        = D2D1::ColorF(0x2F78E9);
        t.fileGlyph          = D2D1::ColorF(0x6B7682);
        t.error              = D2D1::ColorF(0xB42318);
        t.badgeActiveBg      = D2D1::ColorF(0xE7F6EC);
        t.badgeActiveText    = D2D1::ColorF(0x176B35);
        t.badgeDegradedBg    = D2D1::ColorF(0xFFF4D6);
        t.badgeDegradedText  = D2D1::ColorF(0x785A00);
        t.treemapCalculating = D2D1::ColorF(0xFFF4D6);
        t.searchBorder       = D2D1::ColorF(0xC5CBD3);
        t.searchText         = D2D1::ColorF(0x202428);
        t.searchPlaceholder  = D2D1::ColorF(0x66717D);
        t.surfaceElevated    = D2D1::ColorF(0xFFFFFF);
        t.surfaceSubtle      = D2D1::ColorF(0xF1F3F5);
        t.hoverOverlay       = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.035f);
        t.pressOverlay       = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.070f);
        t.selectionSoft      = D2D1::ColorF(0x2A70D1, 0.10f);
        t.dividerSubtle      = D2D1::ColorF(0x1D232C, 0.070f);
        t.focusStroke        = D2D1::ColorF(0x2A70D1);
        // Caption text stays close to the secondary role on light surfaces so
        // small metadata never falls below the product's AA contrast floor.
        t.textTertiary       = D2D1::ColorF(0x66717D);
        t.textDisabled       = D2D1::ColorF(0x929CA6);
        t.success            = D2D1::ColorF(0x188038);
        t.warning            = D2D1::ColorF(0xB54708);
        t.info               = D2D1::ColorF(0x2A70D1);
        t.dropTargetStroke   = D2D1::ColorF(0x2A70D1);
    }
    return t;
}

// Centralized layout metrics. All values are in DIPs (96-DPI units); callers
// that position Win32 controls in physical pixels multiply by UiDpiScale().
namespace UiMetrics {
constexpr float kChromeHeight = 72.0f;
constexpr float kColumnWidth = 240.0f;
constexpr float kRowHeight = 38.0f;
constexpr float kBadgeHeight = 28.0f;
constexpr float kControlHeight = 32.0f;
// fit-native-command-palette-to-results: maximum envelope; sparse lists shrink
// inside it while the query keeps the same anchor.
constexpr float kPaletteWidth = 560.0f;
constexpr float kPaletteMaxHeight = 360.0f;
constexpr float kPaletteMargin = 20.0f;
constexpr float kPaletteVerticalClearance = 80.0f;
constexpr float kPaletteQueryExtraHeight = 6.0f;
// Corner radii: small geometry keeps a desktop-tool character; pills are
// reserved for semantically pill-shaped chips.
constexpr float kRadiusSmall = 6.0f;
constexpr float kRadiusMedium = 8.0f;
// Spacing scale (DIPs)
constexpr float kSpaceXxs = 2.0f;
constexpr float kSpaceXs = 4.0f;
constexpr float kSpaceS = 8.0f;
constexpr float kSpaceSm = 12.0f;
constexpr float kSpaceM = 16.0f;
constexpr float kSpaceL = 24.0f;
constexpr float kSpaceXl = 32.0f;
// Content (DIPs)
constexpr float kIconSize = 16.0f;
constexpr float kIconSizeLg = 20.0f;
constexpr float kTargetMinSize = 28.0f;
constexpr float kRadiusField = 7.0f;
// Typography ramp (DIPs)
constexpr float kFontSizeCaption = 12.0f;
constexpr float kFontSizeBody = 14.0f;
constexpr float kFontSizeSubtitle = 16.0f;
constexpr float kFontSizeTitle = 20.0f;
constexpr float kFontSizeLargeTitle = 28.0f;
}  // namespace UiMetrics

enum class UiTypeRole { Caption, Body, BodyStrong, Subtitle, PageTitle, LargeTitle };

inline float UiTypeSize(UiTypeRole role) {
    switch (role) {
        case UiTypeRole::Caption: return UiMetrics::kFontSizeCaption;
        case UiTypeRole::BodyStrong:
        case UiTypeRole::Body: return UiMetrics::kFontSizeBody;
        case UiTypeRole::Subtitle: return UiMetrics::kFontSizeSubtitle;
        case UiTypeRole::PageTitle: return UiMetrics::kFontSizeTitle;
        case UiTypeRole::LargeTitle: return UiMetrics::kFontSizeLargeTitle;
    }
    return UiMetrics::kFontSizeBody;
}

inline bool UiTypeSemibold(UiTypeRole role) {
    switch (role) {
        case UiTypeRole::BodyStrong:
        case UiTypeRole::Subtitle:
        case UiTypeRole::PageTitle:
        case UiTypeRole::LargeTitle: return true;
        default: return false;
    }
}

// One coherent density structure so row height, command targets, icon size,
// and list rhythm never drift into unrelated per-file constants.
enum class UiDensityProfile { Compact, Comfortable };

struct UiDensity {
    float rowHeight;
    float controlHeight;
    float iconSize;
    float rowGap;
    float commandGap;
};

inline UiDensity GetUiDensity(UiDensityProfile profile) {
    if (profile == UiDensityProfile::Comfortable) {
        return {/*rowHeight=*/44.0f, /*controlHeight=*/36.0f, /*iconSize=*/20.0f,
                /*rowGap=*/8.0f, /*commandGap=*/8.0f};
    }
    return {/*rowHeight=*/UiMetrics::kRowHeight, /*controlHeight=*/UiMetrics::kControlHeight,
            /*iconSize=*/UiMetrics::kIconSize, /*rowGap=*/UiMetrics::kSpaceXs,
            /*commandGap=*/UiMetrics::kSpaceXs};
}

inline UiDensityProfile gUiDensityProfile = UiDensityProfile::Compact;

enum class UiVisualState {
    Rest,
    Hover,
    Pressed,
    Focused,
    SelectedFocused,
    SelectedUnfocused,
    Disabled,
    Busy,
    DropTarget,
};

// Returns the current DPI scale factor (1.0 at 96 DPI). Uses the system DPI as
// the process-wide fallback; per-window shell layout carries explicit DPI where
// geometry depends on monitor transitions.
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

inline float UiScale(float dipValue) { return dipValue * UiDpiScale(); }

inline COLORREF ToColorRef(D2D1_COLOR_F color) {
    auto clampByte = [](float channel) -> BYTE {
        if (channel < 0.0f) channel = 0.0f;
        if (channel > 1.0f) channel = 1.0f;
        return static_cast<BYTE>(static_cast<UINT>(channel * 255.0f + 0.5f));
    };
    return RGB(clampByte(color.r), clampByte(color.g), clampByte(color.b));
}

inline bool gUiDarkTheme = false;

inline bool UiSystemHighContrast() {
    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) != 0 &&
           (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

inline D2D1_COLOR_F ToD2DColor(COLORREF color) {
    return D2D1::ColorF(static_cast<float>(GetRValue(color)) / 255.0f,
                        static_cast<float>(GetGValue(color)) / 255.0f,
                        static_cast<float>(GetBValue(color)) / 255.0f, 1.0f);
}

}  // namespace ffui
