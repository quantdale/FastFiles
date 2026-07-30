## ADDED Requirements

### Requirement: Theme Switching via Device-Dependent Resource Recreation
The system SHALL apply a theme change (Light, Dark, or an OS-driven Follow-System change) by recreating Direct2D device-dependent resources (brushes, and any other resources whose values depend on the active theme) through the same resource-recreation routine already required for DPI changes and device loss, rather than through a separate theme-swap mechanism.

#### Scenario: Explicit theme change repaints with new colors
- **WHEN** the user changes the active theme from Light to Dark (or vice versa) in settings
- **THEN** the application SHALL recreate its device-dependent resources using the routine shared with DPI-change handling and SHALL repaint all visible surfaces using the new theme's colors, with no visible elements left rendered in stale-theme colors

#### Scenario: Resource recreation failure is handled like any other device-loss case
- **WHEN** device-dependent resource recreation fails during a theme change (e.g., a transient device-loss condition)
- **THEN** the application SHALL handle the failure using its existing device-loss recovery path rather than crashing or leaving the UI in a partially-themed state

### Requirement: Live System Theme Following
When the theme setting is Follow-System, the system SHALL detect a change to the Windows light/dark app theme setting while running and SHALL apply the corresponding theme without requiring the user to restart the application.

#### Scenario: OS theme toggle is detected while the app is running
- **WHEN** the theme setting is Follow-System and the user changes the Windows "choose your default app mode" setting from light to dark (or vice versa) while `FastFiles` is running
- **THEN** the application SHALL detect the change and apply the corresponding theme without a restart

### Requirement: Minimal, Non-Blocking Theme-Change Animation
The system SHALL apply theme changes with no animation by default, or at most a short, non-blocking cross-fade limited to top-level chrome, and SHALL skip any such animation entirely when the Windows "Show animations" setting is disabled or another UI operation is in progress.

#### Scenario: Theme change does not block input
- **WHEN** a theme change is being applied
- **THEN** the application SHALL remain responsive to user input throughout the transition, with no modal wait imposed on the user

#### Scenario: Animation is skipped when system animations are disabled
- **WHEN** the Windows "Show animations in Windows" accessibility setting is disabled
- **THEN** the application SHALL apply the theme change instantly, with no cross-fade or other transition effect

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
