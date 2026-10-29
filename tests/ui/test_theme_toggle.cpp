// theme-toggle-follow-system: regression coverage for the F9 quick-toggle
// state machine. The historical inline mapping treated Follow-System as Dark
// (`current == Light ? Dark : Light`), so on a light system the FIRST F9 from
// the default Follow-System state was a rendered no-op -- the root cause of the
// gallery-dark/narrow/wide visual failures (evidence:
// openspec/changes/durable-cold-search-index-and-hybrid-residency/evidence/
// gallery-ab-attribution.md).
#include "ThemeToggle.h"

#include "../TestSupport.h"

namespace {

using ffprotocol::ThemePreference;

int Run() {
    // Required: every press flips the RENDERED theme, including the first press
    // from Follow-System on a light system (the historical bug: rendered light
    // -> Light = no-op).
    {
        const ThemePreference next = ffui::NextThemeTogglePreference(ThemePreference::FollowSystem, false);
        fftest::Check(next == ThemePreference::Dark,
                      "first F9 from Follow-System on a light system flips to Dark (never a no-op)");
    }
    {
        const ThemePreference next = ffui::NextThemeTogglePreference(ThemePreference::FollowSystem, true);
        fftest::Check(next == ThemePreference::Light,
                      "first F9 from Follow-System on a dark system flips to Light");
    }
    // Explicit states toggle as before.
    fftest::Check(ffui::NextThemeTogglePreference(ThemePreference::Light, false) == ThemePreference::Dark,
                  "Light toggles to Dark");
    fftest::Check(ffui::NextThemeTogglePreference(ThemePreference::Light, true) == ThemePreference::Dark,
                  "Light toggles to Dark regardless of system state");
    fftest::Check(ffui::NextThemeTogglePreference(ThemePreference::Dark, false) == ThemePreference::Light,
                  "Dark toggles to Light");
    fftest::Check(ffui::NextThemeTogglePreference(ThemePreference::Dark, true) == ThemePreference::Light,
                  "Dark toggles to Light regardless of system state");
    // Rendered-state helper consistency: the toggle always inverts the state
    // the user is looking at.
    for (const bool systemDark : {false, true}) {
        for (const ThemePreference start : {ThemePreference::Light, ThemePreference::Dark,
                                            ThemePreference::FollowSystem}) {
            const bool renderedBefore = ffui::ThemePreferenceRendersDark(start, systemDark);
            const ThemePreference next = ffui::NextThemeTogglePreference(start, systemDark);
            const bool renderedAfter = ffui::ThemePreferenceRendersDark(next, systemDark);
            fftest::Check(renderedBefore != renderedAfter,
                          "toggle inverts the rendered theme for every starting state");
        }
    }
    return 0;
}

} // namespace

int main() {
    Run();
    return fftest::FailureCount();
}
