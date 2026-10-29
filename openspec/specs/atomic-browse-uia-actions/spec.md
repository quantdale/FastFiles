# Atomic Browse UIA Actions

## Purpose

Preserve original filesystem identity and visible native navigation when accessibility clients interact with Column View.

## Requirements
### Requirement: Stable identity at action execution
Column View accessibility actions SHALL preserve the provider's original column and item identity through owner-thread dispatch. Identity validation SHALL share the model lock with mutation or activation-target capture. Hidden, retired or replaced elements SHALL return element-not-available without acting on a replacement.

#### Scenario: Directory refresh replaces a resolved row
- **WHEN** a directory refresh replaces a row after an accessibility provider resolves it but before its action executes
- **THEN** selection, membership changes, focus, invocation and scroll actions reject the original provider without changing the replacement

#### Scenario: Membership changes before dispatch
- **WHEN** AddToSelection or RemoveFromSelection executes for the same original item
- **THEN** current membership is evaluated atomically and repeated operations remain idempotent

#### Scenario: Fragment retires before dispatch
- **WHEN** the browse workspace is hidden or its owner or fragment lifetime retires
- **THEN** the queued action fails without invoking navigation or mutating selection

### Requirement: Visible navigation after accessibility actions
Accepted Browse accessibility actions SHALL reconcile active-pane location and selection presentation through the same native shell boundary as ordinary input. Focused items SHALL be revealed within their live pane. ScrollIntoView SHALL reveal its requested column without activating another pane. A pane narrower than one column SHALL preserve the filename's leading edge.

#### Scenario: Focus a file in a newly opened child column
- **WHEN** an accessibility client invokes a folder and focuses its child file
- **THEN** the original child row receives keyboard focus, its leading edge and vertical bounds are within the live pane, and location chrome reflects the actual folder

#### Scenario: Narrow dual-pane column
- **WHEN** a focused column is wider than the available pane
- **THEN** the native reveal calculation aligns the column's leading content rather than showing only its trailing edge

#### Scenario: Scroll an inactive pane item
- **WHEN** ScrollIntoView targets a valid item in the inactive pane
- **THEN** that pane reveals the column while the current active pane remains unchanged

### Requirement: Pane paint isolation and geometry consistency
Browse rendering SHALL clip every pane's columns, backgrounds and focus treatment to that pane's live viewport. UIA geometry SHALL use the same frame scroll offsets as rendering.

#### Scenario: Partially scrolled inactive pane
- **WHEN** an inactive pane's parent column partly overlaps its left viewport edge
- **THEN** its paint remains inside that pane and cannot cover the other pane's focused row
