// FastFiles UI design tokens — the single source of truth for colors,
// typography, and metrics across the shell (Direct2D surfaces and Win32
// chrome). All UI surfaces should source their palette from GetUiTheme()
// instead of file-local literals, so dark/light themes stay coherent.
//
// UI polish: centralizes the previously scattered color literals
// (ColumnView, WindowShell details pane, TreemapView, SearchPanel) and adds
// dark-theme variants for badges and panels that were light-only. The
// ship-ready stabilization pass advances the previously deferred Win11-blue
// accent/surface refinement here so every consumer changes coherently.
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
    // The ship-ready palette uses the Win11-blue refinement that was
    // previously deferred here (dark ~#4C8DFF, light ~#0067C0).
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
    // UI-v3 semantic extensions (additive; ui-v3 foundation wave). Status
    // colors must always be paired with a non-color cue (icon/text/shape) at
    // their call sites — never the sole carrier of meaning.
    D2D1_COLOR_F textTertiary;     // tertiary metadata (timestamps, counts)
    D2D1_COLOR_F textDisabled;     // disabled control labels (still readable)
    D2D1_COLOR_F success;          // completed/healthy status
    D2D1_COLOR_F warning;          // degraded/partial status
    D2D1_COLOR_F info;             // informational status
    D2D1_COLOR_F dropTargetStroke; // unambiguous drop-destination affordance
};

// The app's two themes. The ship-ready palette retains the existing token
// contract but gives canvas/panel/elevated surfaces a deliberate hierarchy and
// uses one Windows-11-aligned blue accent family across both themes.
inline UiTheme GetUiTheme(bool dark) {
    UiTheme t{};
    if (dark) {
        t.background         = D2D1::ColorF(0x15171A);
        t.surface            = D2D1::ColorF(0x1C1F24);
        t.border             = D2D1::ColorF(0x343A46);
        t.text               = D2D1::ColorF(0xF5F7FA);
        t.textSecondary      = D2D1::ColorF(0xAAB2BF);
        // #101828 on #4C8DFF is ~5.5:1; white on this accent is only ~3.2:1.
        // Selection text therefore uses the dark foreground in dark mode.
        t.textOnAccent       = D2D1::ColorF(0x101828);
        t.accent             = D2D1::ColorF(0x4C8DFF);
        t.accentHover        = D2D1::ColorF(0x6BA1FF);
        t.folderGlyph        = D2D1::ColorF(0x6BA1FF);
        t.fileGlyph          = D2D1::ColorF(0x9EA7B5);
        t.error              = D2D1::ColorF(0xFF7B72);
        t.badgeActiveBg      = D2D1::ColorF(0x173826);
        t.badgeActiveText    = D2D1::ColorF(0x8DDBA8);
        t.badgeDegradedBg    = D2D1::ColorF(0x433517);
        t.badgeDegradedText  = D2D1::ColorF(0xFFD36A);
        t.treemapCalculating = D2D1::ColorF(0x5A4616);
        t.searchBorder       = D2D1::ColorF(0x47505E);
        t.searchText         = D2D1::ColorF(0xF5F7FA);
        t.searchPlaceholder  = D2D1::ColorF(0x8E98A7);
        t.surfaceElevated    = D2D1::ColorF(0x242830);
        t.surfaceSubtle      = D2D1::ColorF(0x20242A);
        t.hoverOverlay       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.075f);
        t.pressOverlay       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.13f);
        t.selectionSoft      = D2D1::ColorF(0x4C8DFF, 0.18f);
        t.dividerSubtle      = D2D1::ColorF(0xF5F7FA, 0.09f);
        t.focusStroke        = D2D1::ColorF(0x6BA1FF);
        t.textTertiary       = D2D1::ColorF(0x7F8998);
        t.textDisabled       = D2D1::ColorF(0x687180);
        t.success            = D2D1::ColorF(0x81C995);
        t.warning            = D2D1::ColorF(0xF9AB00);
        t.info               = D2D1::ColorF(0x8AB4F8);
        t.dropTargetStroke   = D2D1::ColorF(0x82B1FF);
    } else {
        t.background         = D2D1::ColorF(0xF7F9FC);
        t.surface            = D2D1::ColorF(0xFFFFFF);
        t.border             = D2D1::ColorF(0xD2D9E4);
        t.text               = D2D1::ColorF(0x172033);
        t.textSecondary      = D2D1::ColorF(0x596579);
        t.textOnAccent       = D2D1::ColorF(0xFFFFFF);
        t.accent             = D2D1::ColorF(0x0067C0);
        t.accentHover        = D2D1::ColorF(0x005A9E);
        t.folderGlyph        = D2D1::ColorF(0x0067C0);
        t.fileGlyph          = D2D1::ColorF(0x667085);
        t.error              = D2D1::ColorF(0xB42318);
        t.badgeActiveBg      = D2D1::ColorF(0xE7F6EC);
        t.badgeActiveText    = D2D1::ColorF(0x176B35);
        t.badgeDegradedBg    = D2D1::ColorF(0xFFF4D6);
        t.badgeDegradedText  = D2D1::ColorF(0x785A00);
        t.treemapCalculating = D2D1::ColorF(0xFFF4D6);
        t.searchBorder       = D2D1::ColorF(0x8995A6);
        t.searchText         = D2D1::ColorF(0x172033);
        t.searchPlaceholder  = D2D1::ColorF(0x667085);
        t.surfaceElevated    = D2D1::ColorF(0xFFFFFF);
        t.surfaceSubtle      = D2D1::ColorF(0xF0F4F9);
        t.hoverOverlay       = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.045f);
        t.pressOverlay       = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.085f);
        t.selectionSoft      = D2D1::ColorF(0x0067C0, 0.13f);
        t.dividerSubtle      = D2D1::ColorF(0x172033, 0.08f);
        t.focusStroke        = D2D1::ColorF(0x0067C0);
        // 0x667085 is the same readable neutral used by file/search metadata;
        // it stays above 4.5:1 on both the light canvas and white surfaces.
        t.textTertiary       = D2D1::ColorF(0x667085);
        t.textDisabled       = D2D1::ColorF(0x9AA4B2);
        t.success            = D2D1::ColorF(0x188038);
        t.warning            = D2D1::ColorF(0xB54708);
        t.info               = D2D1::ColorF(0x0067C0);
        t.dropTargetStroke   = D2D1::ColorF(0x0067C0);
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
constexpr float kSpaceSm = 12.0f;  // ui-v3: label/control intermediate gap
constexpr float kSpaceM = 16.0f;
constexpr float kSpaceL = 24.0f;
constexpr float kSpaceXl = 32.0f;  // ui-v3: major page-level separation (sparing)
// Content (DIPs)
constexpr float kIconSize = 16.0f;
constexpr float kIconSizeLg = 20.0f;   // ui-v3: shell command glyphs
constexpr float kTargetMinSize = 24.0f; // ui-v3: pointer-target floor (WCAG 2.2-informed)
// Corner radii contract (ui-v3): rows/buttons kRadiusSmall, text fields
// kRadiusField, cards/panels/flyouts kRadiusMedium. Chips use half-height pills.
constexpr float kRadiusField = 6.0f;
// Typography ramp (DIPs)
constexpr float kFontSizeCaption = 12.0f;
constexpr float kFontSizeBody = 14.0f;
constexpr float kFontSizeSubtitle = 16.0f;
constexpr float kFontSizeTitle = 20.0f;
constexpr float kFontSizeLargeTitle = 28.0f; // ui-v3: rare page titles only
}  // namespace UiMetrics

// UI-v3 typography roles (docs/ui-improvement/02 §3). Size comes from the
// ramp above; weight is regular except BodyStrong/Subtitle/PageTitle/
// LargeTitle which render semibold — hierarchy via weight, not heavy bold.
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

// UI-v3 density contract (docs/ui-improvement/02 §4). One coherent structure
// so row height, command targets, icon size, and list rhythm never drift back
// into unrelated per-file constants. Compact matches the shipped geometry.
// The profile is not yet a user setting; surfaces consume GetUiDensity().
enum class UiDensityProfile { Compact, Comfortable };

struct UiDensity {
    float rowHeight;      // file/search/nav result rows
    float controlHeight;  // buttons, chips, inputs
    float iconSize;       // list-row glyph size
    float rowGap;         // vertical gap between grouped rows/cards
    float commandGap;     // horizontal gap between adjacent commands
};

inline UiDensity GetUiDensity(UiDensityProfile profile) {
    if (profile == UiDensityProfile::Comfortable) {
        return {/*rowHeight=*/34.0f, /*controlHeight=*/28.0f, /*iconSize=*/20.0f,
                /*rowGap=*/8.0f, /*commandGap=*/8.0f};
    }
    return {/*rowHeight=*/UiMetrics::kRowHeight, /*controlHeight=*/UiMetrics::kControlHeight,
            /*iconSize=*/UiMetrics::kIconSize, /*rowGap=*/UiMetrics::kSpaceXs,
            /*commandGap=*/UiMetrics::kSpaceXs};
}

// Active density profile, published like gUiDarkTheme; defaults to Compact so
// existing geometry is unchanged until a surface opts into Comfortable.
inline UiDensityProfile gUiDensityProfile = UiDensityProfile::Compact;

// UI-v3 canonical visual states (docs/ui-improvement/02 §8). Surfaces resolve
// interaction state through this enum (see UiComponents.h resolvers) instead of
// inventing per-surface hover/selected/focus combinations.
enum class UiVisualState {
    Rest,
    Hover,
    Pressed,
    Focused,           // keyboard focus, not selected
    SelectedFocused,   // selection in the active pane/window
    SelectedUnfocused, // selection outside the active pane/window
    Disabled,
    Busy,
    DropTarget,
};

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