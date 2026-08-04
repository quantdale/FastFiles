## MODIFIED Requirements

### Requirement: Theme Switching via Device-Dependent Resource Recreation
The system SHALL apply a theme change (Light, Dark, or an OS-driven Follow-System change) by recreating Direct2D device-dependent resources (brushes, text formats, and any other resources whose values depend on the active theme) through the shared recreation pattern established for DPI changes — extended by this change to device loss — rather than through a separate theme-swap mechanism. Theme resources SHALL derive from a single design-token set (colors, radii, spacing, typography, interaction overlays) shared by Direct2D surfaces and Win32/GDI chrome alike; this change extends that token set from the original minimal palette and makes the previously-known device-loss gap (a trigger the original requirement named but which shipped unimplemented) a first-class trigger of the same pattern.

#### Scenario: Explicit theme change repaints with new colors
- **WHEN** the user changes the active theme from Light to Dark (or vice versa) in settings
- **THEN** the application SHALL recreate its device-dependent resources using the routine shared with DPI-change and device-loss handling and SHALL repaint all visible surfaces — including Win32 chrome surfaces (sidebar, navigation chrome, tab strip, dialogs) that previously rendered system or hardcoded colors — using the new theme's tokens, with no visible elements left rendered in stale-theme colors

#### Scenario: Resource recreation failure is handled like any other device-loss case
- **WHEN** device-dependent resource recreation fails during a theme change (e.g., a transient device-loss condition)
- **THEN** the application SHALL handle the failure using its existing device-loss recovery path rather than crashing or leaving the UI in a partially-themed state

#### Scenario: Device loss recovers through the same routine
- **WHEN** the render device is lost independently of any theme change (GPU reset, driver update, `D2DERR_RECREATE_TARGET`)
- **THEN** the application SHALL recreate device-dependent resources via the same shared routine and resume rendering subsequent frames correctly

### Requirement: Minimal, Non-Blocking Theme-Change Animation
The system SHALL apply theme changes with no animation by default, or at most a short (approximately 100–150 ms), non-blocking cross-fade of the pre-change frame over the newly-themed frame, and SHALL skip any such animation entirely when the Windows "Show animations" setting is disabled or another UI operation is in progress. (This change implements the cross-fade the original requirement permitted but that shipped unimplemented.)

#### Scenario: Theme change does not block input
- **WHEN** a theme change is being applied
- **THEN** the application SHALL remain responsive to user input throughout the transition, with no modal wait imposed on the user

#### Scenario: Animation is skipped when system animations are disabled
- **WHEN** the Windows "Show animations in Windows" accessibility setting is disabled
- **THEN** the application SHALL apply the theme change instantly, with no cross-fade or other transition effect, and no animation timers SHALL be active

#### Scenario: Cross-fade is bounded and no surface is left stale
- **WHEN** a theme change cross-fade runs
- **THEN** it SHALL complete in no more than approximately 150 ms, SHALL cover the entire Direct2D-rendered canvas (not only subregions), and Win32 chrome surfaces (which cannot participate in the bitmap fade) SHALL repaint with the new theme's tokens within the same theme-change apply, so no surface is left rendered in stale-theme colors once the transition completes
