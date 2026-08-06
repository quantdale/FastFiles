# theming Specification

## Purpose
Theme application across the whole application: device-dependent resource recreation for theme, DPI, and device-loss changes, design-token-sourced rendering, live follow-system detection, bounded non-blocking transitions, and correct composition of theme and DPI changes.

## Requirements
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

### Requirement: Live System Theme Following
When the theme setting is Follow-System, the system SHALL detect a change to the Windows light/dark app theme setting while running and SHALL apply the corresponding theme without requiring the user to restart the application.

#### Scenario: OS theme toggle is detected while the app is running
- **WHEN** the theme setting is Follow-System and the user changes the Windows "choose your default app mode" setting from light to dark (or vice versa) while `FastFiles` is running
- **THEN** the application SHALL detect the change and apply the corresponding theme without a restart

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

### Requirement: Correct Per-Monitor High-DPI Rendering
The system SHALL render all UI surfaces at the correct effective DPI scale for the monitor each window currently occupies, including text, icons, and hit-testing regions, across monitors configured with different DPI scale factors.

#### Scenario: Window on a high-DPI monitor renders crisply
- **WHEN** a `FastFiles` window is displayed on a monitor set to a DPI scale factor other than 100%
- **THEN** text and icons SHALL render at the correct scale for that monitor's DPI with no blurring from incorrect bitmap scaling, and click targets SHALL align with what is visually rendered

#### Scenario: Window dragged between differently-scaled monitors updates correctly
- **WHEN** the user drags a `FastFiles` window from a monitor at one DPI scale factor to a monitor at a different DPI scale factor
- **THEN** the application SHALL recreate its device-dependent resources for the new DPI and SHALL render correctly at the new scale without requiring the window to be moved again or the application restarted

### Requirement: Theme and DPI Changes Compose Correctly
The system SHALL render correctly when a theme change and a DPI change occur close together or while the other condition is already active, without either dimension corrupting the other.

#### Scenario: DPI changes while a non-default theme is active
- **WHEN** the active theme is Dark (or Light) and the window's effective DPI changes (e.g., by moving to a different monitor)
- **THEN** the application SHALL render at the new DPI using the currently active theme's colors, with neither the theme nor the DPI-correct layout reverting or corrupting
