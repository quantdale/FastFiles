## ADDED Requirements

### Requirement: Critical theme pairings meet readable contrast floors

For Light and Dark token sets, critical normal-text, selected-text, secondary,
tertiary, search, status, and focus pairings SHALL be checked by executable
regression coverage. Normal text pairings SHALL meet a WCAG-style 4.5:1 floor;
focus and non-text indicators SHALL meet the existing non-text contrast floor.

#### Scenario: Light and dark primary pairings pass
- **WHEN** the executable theme contract test evaluates Light and Dark tokens
- **THEN** primary, secondary, tertiary, selected, search, and required status pairings SHALL meet their declared contrast floors

#### Scenario: Alpha overlays are checked after compositing
- **WHEN** a translucent selection, hover, press, or informational status token is tested
- **THEN** the assertion SHALL evaluate the composited foreground/background colors rather than the uncomposited token alone

### Requirement: Theme invalidation reaches lazily created Storage controls

Theme application SHALL invalidate already-created and lazily-created Storage
child controls through the same shared theme transition path, including after
Follow System changes and device/DPI refreshes.

#### Scenario: Storage opened after startup inherits the active theme
- **WHEN** Storage Analysis controls are created after the shell's initial theme application
- **THEN** their first paint SHALL use the current active theme rather than system-light defaults

#### Scenario: Visible Storage controls repaint on theme transition
- **WHEN** Light/Dark/Follow System changes while Storage is open
- **THEN** the visible controls and list/header surfaces SHALL be invalidated immediately
