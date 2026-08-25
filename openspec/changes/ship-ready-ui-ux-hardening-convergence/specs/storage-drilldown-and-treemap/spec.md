## ADDED Requirements

### Requirement: Storage Analysis chrome is keyboard-correct and visibly stateful

Storage Analysis SHALL expose keyboard-reachable Back and Up actions and one
Tab entry into the mutually exclusive view-mode radio group. Arrow keys SHALL
move within that radio group, activation SHALL keep exactly one mode selected,
and the selected, focused, hovered, pressed, and disabled states SHALL remain
visually distinguishable without relying on color alone.

#### Scenario: Back and Up are reachable in every mode
- **WHEN** the user opens Overview, Drill Down, Largest Folders, Largest Files, By Category, or Treemap
- **THEN** Back and Up SHALL participate in keyboard navigation, with Up disabled when no parent scope exists

#### Scenario: Radio group uses one Tab entry and arrow navigation
- **WHEN** keyboard focus enters the Storage mode controls
- **THEN** Tab SHALL enter the group through its group stop, arrow keys SHALL move among the six modes, and activation SHALL preserve mutual exclusivity

#### Scenario: Selected and focused modes have non-color cues
- **WHEN** a mode is selected or focused in light mode, dark mode, or Follow System mode
- **THEN** the control SHALL show a shape/state cue and a visible focus indicator in addition to token-derived color

### Requirement: Storage Analysis state follows theme and availability changes

Storage Analysis controls and data surfaces SHALL repaint immediately when the
theme, High Contrast setting, DPI, snapshot, or engine availability changes.
Degraded, stale, unavailable, partial, calculating, and completed-empty states
SHALL retain truthful labels in every view.

#### Scenario: Runtime theme transition repaints Storage chrome
- **WHEN** the active theme changes while any Storage mode is visible
- **THEN** buttons, radios, lists, headers, status text, and treemap presentation SHALL repaint using the new tokens without requiring a reopen

#### Scenario: High Contrast preserves native accessibility colors
- **WHEN** Windows High Contrast is active
- **THEN** native control semantics and system colors SHALL be used, with token overlays suppressed

#### Scenario: Engine loss updates degraded status
- **WHEN** the engine changes from active to unavailable or degraded while Storage Analysis is visible
- **THEN** the current mode's status SHALL update promptly to the truthful degraded/unavailable wording and stale data SHALL remain explicitly labeled
