#pragma once

// ui-overhaul phase 11: one canonical semantic state->color resolver shared by
// every surface that paints a selectable row-like element. Column View /
// Home (Direct2D), the sidebar, and the search result list each used to carry
// their own copy of this state machine; keeping them here means hover, pressed,
// focused, selected-focused, selected-unfocused, disabled, and drop-target
// treatments can no longer drift between products-of-one-codebase.
//
// The resolver returns raw semantic colors. Translucent fills (hover/press/
// selection overlays) keep their alpha so a GDI caller composites them over its
// own background the way Direct2D blends them over the target, and opaque
// states (accent selection, system highlight) pass through unchanged. High
// Contrast always falls back to system colors, never token overlays.

#include "UITheme.h"
#include "UiStyle.h" // UiLerpColor

namespace ffui {

struct UiStatePalette {
    D2D1_COLOR_F fill = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f);
    bool hasFill = false;
    D2D1_COLOR_F text = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
    D2D1_COLOR_F secondary = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
    D2D1_COLOR_F tertiary = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
    D2D1_COLOR_F ring = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
    bool hasRing = false;
};

inline UiStatePalette ResolveUiStatePalette(const UiTheme& theme, UiVisualState state,
                                            bool highContrast) {
    UiStatePalette palette;
    if (highContrast) {
        // Accessibility fallback: the user's system colors always win over the
        // product tokens; no translucent overlays are ever layered.
        palette.text = ToD2DColor(GetSysColor(COLOR_WINDOWTEXT));
        palette.secondary = palette.text;
        palette.tertiary = palette.text;
        switch (state) {
            case UiVisualState::SelectedFocused:
            case UiVisualState::SelectedUnfocused:
                palette.fill = ToD2DColor(GetSysColor(COLOR_HIGHLIGHT));
                palette.hasFill = true;
                palette.text = ToD2DColor(GetSysColor(COLOR_HIGHLIGHTTEXT));
                palette.secondary = palette.text;
                palette.tertiary = palette.text;
                break;
            case UiVisualState::Hover:
            case UiVisualState::Pressed:
                palette.fill = ToD2DColor(GetSysColor(COLOR_MENUHILIGHT));
                palette.hasFill = true;
                palette.text = ToD2DColor(GetSysColor(COLOR_HIGHLIGHTTEXT));
                palette.secondary = palette.text;
                palette.tertiary = palette.text;
                break;
            case UiVisualState::Disabled:
                palette.text = ToD2DColor(GetSysColor(COLOR_GRAYTEXT));
                palette.secondary = palette.text;
                palette.tertiary = palette.text;
                break;
            case UiVisualState::Focused:
                palette.ring = ToD2DColor(GetSysColor(COLOR_WINDOWTEXT));
                palette.hasRing = true;
                break;
            default:
                break;
        }
        return palette;
    }

    palette.text = theme.text;
    palette.secondary = theme.textSecondary;
    palette.tertiary = theme.textTertiary;
    switch (state) {
        case UiVisualState::Hover:
            palette.fill = theme.hoverOverlay;
            palette.hasFill = true;
            break;
        case UiVisualState::Pressed:
            palette.fill = theme.pressOverlay;
            palette.hasFill = true;
            break;
        case UiVisualState::SelectedUnfocused:
            palette.fill = theme.selectionSoft;
            palette.hasFill = true;
            break;
        case UiVisualState::SelectedFocused:
            palette.fill = theme.accent;
            palette.hasFill = true;
            palette.text = theme.textOnAccent;
            // Secondary/tertiary text on the accent fill leans back toward the
            // accent instead of dropping to muted grays, keeping metadata
            // readable without competing with the item name.
            palette.secondary = UiLerpColor(theme.textOnAccent, theme.accent, 0.22f);
            palette.tertiary = UiLerpColor(theme.textOnAccent, theme.accent, 0.38f);
            break;
        case UiVisualState::Disabled:
            palette.text = theme.textDisabled;
            palette.secondary = palette.text;
            palette.tertiary = palette.text;
            break;
        case UiVisualState::DropTarget:
            palette.fill = theme.selectionSoft;
            palette.hasFill = true;
            break;
        case UiVisualState::Focused:
            palette.ring = theme.focusStroke;
            palette.hasRing = true;
            break;
        case UiVisualState::Busy:
        case UiVisualState::Rest:
            break;
    }
    return palette;
}

} // namespace ffui
