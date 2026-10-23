#pragma once
#include "ffprotocol/Settings.h"

namespace ffui {

// theme-toggle-follow-system: pure theme quick-toggle state machine, extracted
// from the WindowShell WM_KEYDOWN handler so the mapping is unit-testable.
//
// Contract under test (tests/ui/test_theme_toggle.cpp):
//   * every quick-toggle press MUST flip the RENDERED theme (a press is never a
//     visible no-op), and
//   * a Follow-System preference is resolved against the system state at press
//     time, so the first press from the default state flips visibly.

// The dark state a preference renders as at a given moment.
inline bool ThemePreferenceRendersDark(ffprotocol::ThemePreference preference, bool systemDark) {
    return preference == ffprotocol::ThemePreference::Dark ||
           (preference == ffprotocol::ThemePreference::FollowSystem && systemDark);
}

// The preference after one quick-toggle press: always the explicit theme that
// INVERTS the currently rendered state (Follow-System is resolved at press
// time), so every press is a visible flip and the mapping is total. The toggle
// deliberately lands on an explicit Light/Dark value rather than cycling back
// to Follow-System: a quick toggle is an override; the user returns to
// Follow-System from Settings.
inline ffprotocol::ThemePreference NextThemeTogglePreference(ffprotocol::ThemePreference current,
                                                             [[maybe_unused]] bool systemDark) {
    return ThemePreferenceRendersDark(current, systemDark) ? ffprotocol::ThemePreference::Light
                                                           : ffprotocol::ThemePreference::Dark;
}

} // namespace ffui
