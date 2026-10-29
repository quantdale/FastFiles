## ADDED Requirements

### Requirement: Result viewport fits complete rows
The native palette SHALL size sparse and no-match results to their actual rows and cap long results to complete rows within available client space.

#### Scenario: Single or no matching command
- **WHEN** filtering leaves one command or the inert no-match row
- **THEN** the list has one complete row when space permits and does not reserve a large empty result area

#### Scenario: Many commands
- **WHEN** all results cannot fit
- **THEN** the list remains bounded and native keyboard scrolling reaches the remaining commands

### Requirement: Query position remains stable
The query field SHALL retain its top/width across filtering within a fixed viewport and density.

#### Scenario: Filter and clear
- **WHEN** the user changes from many to one to no results and back
- **THEN** the edit remains anchored and retains ordinary typing/caret focus behavior

### Requirement: Native behavior and accessibility are preserved
The palette SHALL retain real native controls, existing command availability/execution, safe z-order/show/hide and live context updates without resetting query/selection/scroll.

#### Scenario: Operation availability changes
- **WHEN** a visible command becomes unavailable
- **THEN** its state updates without resetting the query or native list selection/scroll

#### Scenario: Closed palette receives layout
- **WHEN** the main window resizes while the palette is closed
- **THEN** the edit/list remain hidden and do not steal focus
