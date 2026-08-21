// UI-v3 responsive shell states (docs/ui-improvement/02 §10).
//
// Pure classification of available content width into explicit layout states
// so surfaces adapt by design instead of squeezing controls until they crowd.
// Breakpoints are derived from FastFiles' own content minima (sidebar width,
// column viewport, details reservation), not copied from XAML defaults; they
// are expressed in DIPs of *content* width (client width minus sidebar) and
// evaluate dual-pane against the per-pane effective width so two half panes
// never pretend to be wide.
#pragma once

#include <algorithm>

namespace ffui {

enum class UiResponsiveState { Narrow, Normal, Wide };

// Content-width breakpoints in DIPs.
//  - Below kUiNarrowBelow the sidebar must collapse and secondary commands
//    move to overflow: a full column viewport plus details cannot fit.
//  - Wide starts where persistent sidebar + comfortable details + a usable
//    column viewport coexist without crowding.
inline constexpr float kUiNarrowBelowDip = 640.0f;
inline constexpr float kUiWideFromDip = 1040.0f;

// Dual-pane reserves roughly half the content width per pane; classify against
// that effective width so each pane gets an honest state.
inline UiResponsiveState ClassifyContentWidth(float contentWidthDip, bool dualPane) noexcept {
    const float effective = dualPane ? contentWidthDip * 0.5f : std::max(0.0f, contentWidthDip);
    if (effective < kUiNarrowBelowDip) {
        return UiResponsiveState::Narrow;
    }
    if (effective >= kUiWideFromDip) {
        return UiResponsiveState::Wide;
    }
    return UiResponsiveState::Normal;
}

} // namespace ffui
