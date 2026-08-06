## ADDED Requirements

### Requirement: Details Pane Reserves Layout Width
The details pane SHALL be a reserved right-hand region in the window layout: when the pane is visible, the system SHALL compute the available column-view width as the viewport width minus the details-pane reserved width, and SHALL render column content and accept pointer interaction only within that computed area, so that no column content or interactive element renders underneath the details pane.

#### Scenario: Column content never renders under the details pane
- **WHEN** the details pane is visible and the user browses a column view whose content would otherwise extend beneath the pane
- **THEN** the system SHALL clip column content to the available column-view width, and SHALL treat clicks within the details-pane region as belonging to the pane, not to any column behind it

#### Scenario: Available width shrinks when the pane is visible
- **WHEN** the details pane transitions from hidden to visible
- **THEN** the available column-view width SHALL shrink by the details-pane reserved width, and SHALL return to the full width when the pane is hidden, with no dead or overlapping region

### Requirement: Narrow-Width Details Pane Strategy
When the window width falls below a documented minimum-width threshold at which the full details pane and the column view cannot both fit in the available viewport, the system SHALL apply a documented narrow-width strategy (collapsing the pane to a thin disclosure bar, hiding it, or an equivalent non-overlapping treatment) rather than rendering the pane over column content, and SHALL preserve usable navigation and selection. The minimum-width threshold SHALL be expressed in DIPs and SHALL scale with effective DPI (cross-referenced under geometry handling below).

#### Scenario: Narrow window collapses the pane instead of overlaying
- **WHEN** the window width falls below the documented minimum-width threshold at which the full details pane and column view cannot both fit comfortably
- **THEN** the system SHALL collapse, hide, or otherwise remove the full details pane from the layout so that no column content is occluded, and SHALL provide a way to restore the full pane (for example, a disclosure control or menu action)

#### Scenario: Narrow window preserves navigation and selection
- **WHEN** the window is in the narrow-width strategy state
- **THEN** the user SHALL still be able to navigate columns, change selection, and perform the normal read-only navigation and selection actions, with no interactive element made unreachable

### Requirement: Details Pane Works With Dual-Pane Mode
In dual-pane mode, the details-pane reserved width SHALL be applied to the combined viewport, and each pane's content width SHALL be derived from the remaining available column-view width without allowing either pane's content to overlap the details pane.

#### Scenario: Dual-pane content respects the details pane
- **WHEN** both panes are active and the details pane is visible
- **THEN** both panes' content SHALL be clipped to the available column-view width (the viewport minus the details-pane reserved width and pane split), and neither pane SHALL render content underneath the details pane

### Requirement: Details Pane Geometry Handles DPI and Live Resize
The details-pane reserved width and the narrow-width threshold SHALL be expressed in DIPs and scale with the effective DPI, and SHALL be recomputed correctly across live window resizes, pane open/close, and selection changes, without introducing idle repaint loops or animation regressions.

#### Scenario: Correct at 100% and 150% DPI
- **WHEN** the window's effective DPI scale is 100% or 150%
- **THEN** the details-pane reserved width and the narrow-width threshold SHALL be applied at the correct DIP-scaled values, and column content SHALL be clipped to the correctly scaled available width

#### Scenario: Live resize re-lays out without idle repaint
- **WHEN** the user live-resizes the window or toggles the details pane's visibility
- **THEN** the system SHALL recompute the available column-view width and repaint as needed, and SHALL NOT run any animation timer or repaint loop while the geometry and state are idle

### Requirement: Details Pane Layout Preserves Accessibility Behavior
The details-pane responsive layout SHALL preserve the existing high-contrast and reduced-motion behavior defined by the design-token system: under High Contrast the pane SHALL fall back to system colors, and when system animations are disabled the pane SHALL apply any layout/visibility state change instantly with no transition.

#### Scenario: High-Contrast layout falls back to system colors
- **WHEN** Windows High Contrast is active and the details pane is rendered in its reserved or narrow-width state
- **THEN** the details pane SHALL use system colors rather than token overlays, consistent with the rest of the application

#### Scenario: Reduced-motion applies layout state instantly
- **WHEN** the Windows "Show animations" setting is disabled and the details pane collapses, expands, hides, or appears
- **THEN** the layout/visibility change SHALL apply instantly with no animation or active animation timers
