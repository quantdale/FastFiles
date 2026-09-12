// ui-overhaul phase 11: the shared semantic state resolver that browse rows,
// the sidebar, and the search result list all consume. Pins one state machine
// across both themes and High Contrast so the surfaces cannot drift apart.

#include "UiStatePalette.h"

#include <cmath>
#include <cstdlib>

#include "../TestSupport.h"

using namespace fftest;

namespace {

bool Same(const D2D1_COLOR_F& left, const D2D1_COLOR_F& right) {
    return std::fabs(left.r - right.r) < 0.0005f && std::fabs(left.g - right.g) < 0.0005f &&
           std::fabs(left.b - right.b) < 0.0005f && std::fabs(left.a - right.a) < 0.0005f;
}

void TestRestAndMetadataRoles() {
    for (const bool dark : {false, true}) {
        const ffui::UiTheme theme = ffui::GetUiTheme(dark);
        const ffui::UiStatePalette rest =
            ffui::ResolveUiStatePalette(theme, ffui::UiVisualState::Rest, false);
        Check(!rest.hasFill, "rest state paints no fill");
        Check(!rest.hasRing, "rest state paints no focus ring");
        Check(Same(rest.text, theme.text), "rest primary text is the theme text token");
        Check(Same(rest.secondary, theme.textSecondary),
              "rest secondary text is the muted token");
        Check(Same(rest.tertiary, theme.textTertiary),
              "rest tertiary text is the tertiary token");

        const ffui::UiStatePalette focused =
            ffui::ResolveUiStatePalette(theme, ffui::UiVisualState::Focused, false);
        Check(focused.hasRing && !focused.hasFill,
              "keyboard focus is a ring, independent of any fill");
        Check(Same(focused.ring, theme.focusStroke), "focus ring uses the focusStroke token");
    }
}

void TestInteractionOverlaysPreserveAlpha() {
    for (const bool dark : {false, true}) {
        const ffui::UiTheme theme = ffui::GetUiTheme(dark);
        const ffui::UiStatePalette hover =
            ffui::ResolveUiStatePalette(theme, ffui::UiVisualState::Hover, false);
        const ffui::UiStatePalette pressed =
            ffui::ResolveUiStatePalette(theme, ffui::UiVisualState::Pressed, false);
        Check(hover.hasFill && Same(hover.fill, theme.hoverOverlay),
              "hover fill is the translucent hover overlay (alpha preserved)");
        Check(pressed.hasFill && Same(pressed.fill, theme.pressOverlay),
              "pressed fill is the stronger press overlay (alpha preserved)");
        Check(Same(hover.text, theme.text) && Same(pressed.text, theme.text),
              "hover/press never change the primary text color");
        Check(pressed.fill.a >= hover.fill.a,
              "pressed feedback is at least as strong as hover");
    }
}

void TestSelectionStates() {
    for (const bool dark : {false, true}) {
        const ffui::UiTheme theme = ffui::GetUiTheme(dark);
        const ffui::UiStatePalette soft =
            ffui::ResolveUiStatePalette(theme, ffui::UiVisualState::SelectedUnfocused, false);
        Check(soft.hasFill && Same(soft.fill, theme.selectionSoft),
              "inactive selection is the soft selection token");
        Check(Same(soft.text, theme.text),
              "inactive selection keeps primary text color (no color-only signal)");

        const ffui::UiStatePalette active =
            ffui::ResolveUiStatePalette(theme, ffui::UiVisualState::SelectedFocused, false);
        Check(active.hasFill && Same(active.fill, theme.accent),
              "focused selection is the accent fill");
        Check(Same(active.text, theme.textOnAccent),
              "focused selection flips primary text onto the accent");
        Check(!Same(active.secondary, active.text) && active.secondary.a > 0.5f,
              "focused selection keeps secondary metadata distinct and readable");
        Check(!Same(active.tertiary, active.secondary),
              "focused selection keeps a third metadata tone distinct from the second");
    }
}

void TestDisabledAndDropTarget() {
    for (const bool dark : {false, true}) {
        const ffui::UiTheme theme = ffui::GetUiTheme(dark);
        const ffui::UiStatePalette disabled =
            ffui::ResolveUiStatePalette(theme, ffui::UiVisualState::Disabled, false);
        Check(!disabled.hasFill && Same(disabled.text, theme.textDisabled),
              "disabled rows dim the text without a fill");
        Check(Same(disabled.secondary, disabled.text) && Same(disabled.tertiary, disabled.text),
              "disabled metadata dims with the primary text");

        const ffui::UiStatePalette drop =
            ffui::ResolveUiStatePalette(theme, ffui::UiVisualState::DropTarget, false);
        Check(drop.hasFill && Same(drop.fill, theme.selectionSoft),
              "drop target uses the soft selection wash (stroke is added by the backend)");
    }
}

void TestHighContrastUsesSystemColors() {
    const ffui::UiTheme theme = ffui::GetUiTheme(false);
    const ffui::UiStatePalette selected =
        ffui::ResolveUiStatePalette(theme, ffui::UiVisualState::SelectedFocused, true);
    Check(selected.hasFill &&
              Same(selected.fill, ffui::ToD2DColor(GetSysColor(COLOR_HIGHLIGHT))) &&
              Same(selected.text, ffui::ToD2DColor(GetSysColor(COLOR_HIGHLIGHTTEXT))),
          "High Contrast selection uses the system highlight pair");

    const ffui::UiStatePalette hover =
        ffui::ResolveUiStatePalette(theme, ffui::UiVisualState::Hover, true);
    Check(hover.hasFill && Same(hover.fill, ffui::ToD2DColor(GetSysColor(COLOR_MENUHILIGHT))),
          "High Contrast hover uses the system menu highlight");

    const ffui::UiStatePalette disabled =
        ffui::ResolveUiStatePalette(theme, ffui::UiVisualState::Disabled, true);
    Check(Same(disabled.text, ffui::ToD2DColor(GetSysColor(COLOR_GRAYTEXT))),
          "High Contrast disabled uses the system gray text");

    const ffui::UiStatePalette focused =
        ffui::ResolveUiStatePalette(theme, ffui::UiVisualState::Focused, true);
    Check(focused.hasRing && Same(focused.ring, ffui::ToD2DColor(GetSysColor(COLOR_WINDOWTEXT))),
          "High Contrast focus ring uses the system window text");

    const ffui::UiStatePalette rest =
        ffui::ResolveUiStatePalette(theme, ffui::UiVisualState::Rest, true);
    Check(!rest.hasFill && Same(rest.text, ffui::ToD2DColor(GetSysColor(COLOR_WINDOWTEXT))),
          "High Contrast rest text uses the system window text");
}

} // namespace

int main() {
    TestRestAndMetadataRoles();
    TestInteractionOverlaysPreserveAlpha();
    TestSelectionStates();
    TestDisabledAndDropTarget();
    TestHighContrastUsesSystemColors();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
