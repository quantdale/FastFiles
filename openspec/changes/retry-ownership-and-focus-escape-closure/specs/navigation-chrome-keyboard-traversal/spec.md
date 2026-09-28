## Purpose

Keyboard users can move through navigation chrome and return to the shell content surface without becoming trapped in a chrome-only Tab cycle.

## ADDED Requirements

### Requirement: Chrome traversal is enterable and escapable

Tab traversal SHALL be able to enter navigation chrome from the shell content surface and SHALL be able to leave it again in both directions. Forward Tab from the last focusable chrome control SHALL move focus to the shell owner that routes content keys. Reverse Tab from the first focusable chrome control SHALL move focus to that same shell owner. Neither boundary SHALL wrap to the opposite chrome control. The traversal SHALL NOT form a cycle whose only stops are chrome controls. Address-bar editing, tab buttons, the overflow affordance, settings, and search SHALL remain reachable when they are visible and enabled. Disabled Back, Forward, or Up controls SHALL NOT be Tab stops. A hidden overflow control SHALL NOT be a Tab stop. Mouse behavior, existing keyboard shortcuts, and UI Automation exposure of the same controls SHALL remain intact.

#### Scenario: Forward Tab leaves the last chrome control

- **WHEN** focus is on the shell content surface, Tab moves through the visible enabled chrome controls, and Tab is pressed on the last of those controls
- **THEN** focus SHALL return to the shell content surface
- **AND** SHALL NOT wrap to the first chrome control

#### Scenario: Reverse Tab leaves the first chrome control

- **WHEN** focus is on the shell content surface, Shift+Tab enters chrome at its last control, and Shift+Tab is pressed on the first focusable chrome control
- **THEN** focus SHALL return to the shell content surface
- **AND** SHALL NOT wrap to the last chrome control

#### Scenario: Entry from content still reaches chrome

- **WHEN** the shell content surface has focus and the user presses Tab or Shift+Tab
- **THEN** focus SHALL enter the first focusable chrome control or the last focusable chrome control, respectively
- **AND** a later boundary press SHALL be able to return to the shell content surface

#### Scenario: Overflow present and absent

- **WHEN** the tab strip shows an overflow affordance, and again when that affordance is hidden because every tab fits
- **THEN** the visible enabled overflow control SHALL be in the forward and reverse traversal only while it is shown
- **AND** leaving the last or first focusable control SHALL still return to the shell content surface

#### Scenario: Narrow and normal layouts with several tabs

- **WHEN** several tabs are open at a narrow width that hides some tabs, and again at a normal width that shows them
- **THEN** visible enabled tab controls SHALL be reachable from chrome traversal
- **AND** hidden tab buttons SHALL NOT be required Tab stops
- **AND** the boundary SHALL still escape to the shell content surface in both directions

#### Scenario: Address editing is not swallowed by chrome traversal

- **WHEN** the address editor is open and has focus
- **THEN** chrome traversal SHALL NOT steal that edit
- **AND** closing the edit SHALL still return focus to the shell owner that routes content keys

#### Scenario: Settings, search, and disabled navigation buttons

- **WHEN** settings and search are visible and enabled, and Back, Forward, or Up is disabled
- **THEN** settings and search SHALL be reachable in the chrome traversal
- **AND** each disabled navigation button SHALL be skipped
- **AND** the first and last remaining focusable controls SHALL still escape to the shell content surface

#### Scenario: Content re-entry after overflow

- **WHEN** the user opens the overflow menu, activates a hidden tab, and then continues Tab or Shift+Tab from chrome
- **THEN** focus SHALL be able to return to the shell content surface
- **AND** the activated tab SHALL remain the active workspace
