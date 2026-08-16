## Purpose

Delta for `stabilize-live-uia-e2e`: the canonical `ui-automation` spec
defines what the product exposes; this delta defines how the live validation
harness drives it deterministically. It adds requirements for the harness
itself: scenario lifecycle isolation, bounded semantic waits with timeout
diagnostics, targeted semantic queries, structural Explorer-window
discovery, semantic dual-pane setup, and honest failure classification. No
product provider requirement is weakened.

## ADDED Requirements

### Requirement: Interaction-heavy scenarios run in isolated application lifecycles
The live capability SHALL execute the interaction-heavy scenarios —
`search-deep-result-navigation`, `drag-out-to-explorer`,
`drag-in-from-explorer`, `drag-between-panes`, `in-column-error-state-late`
— in fresh, isolated FastFiles lifecycles: deterministic fixture, fresh
launch, semantic ready await, fresh UIA root, scenario execution, evidence
capture, clean application exit, session-owned Explorer window close,
fixture removal. Scenario N SHALL NOT depend on scenario N-1's cleanup.

#### Scenario: Isolated deep-search navigation
- **WHEN** the capability runs only `search-deep-result-navigation` against
  a fresh FastFiles lifecycle
- **THEN** it passes and the activation is verified by targeted
  ColumnView/terminal-column semantics, not a whole-window unbounded walk

#### Scenario: Order independence
- **WHEN** the isolated scenarios run in any order (including randomized
  permutations)
- **THEN** each scenario's result is identical (PASS/FAIL/SKIP and reason)

### Requirement: Bounded semantic waits with timeout diagnostics
All synchronization SHALL be bounded semantic conditions — element exists,
element disappears, element obtains focus, expected tab/path label appears,
expected pane count appears, search result generation changes, Column View
contains an expected item, Explorer window reaches the expected path,
filesystem side effect exists, selection count reaches an expected value —
polled with a bounded deadline and short interval. Correctness SHALL NOT
depend primarily on fixed sleep durations. On timeout the wait SHALL collect
rich diagnostics: current foreground HWND, focused element identity, a
bounded tree excerpt, active owned/popup window state, last exception, and
elapsed time.

#### Scenario: Bounded wait timeout diagnostics
- **WHEN** a semantic wait times out
- **THEN** the failure record includes foreground HWND, focus identity,
  popup state, last exception, and elapsed time

### Requirement: Stale-provider reacquisition convention
After an action that replaces a large semantic subtree (search result
activation, tab replacement, address-bar navigation, pane creation/closure,
Home ↔ filesystem transitions, search open/close), the harness SHALL
re-acquire relevant providers from the root rather than assuming old
`AutomationElement` objects remain valid. A stale object returning
`UIA_E_ELEMENTNOTAVAILABLE` is valid product behavior; the harness SHALL
distinguish an expected stale provider, a missing new provider, and a
traversal timeout.

#### Scenario: Reacquisition after search activation
- **WHEN** a search result is activated and the Column View navigates
- **THEN** the harness re-acquires the main/pane/column providers and finds
  the landing item through the terminal column's ItemContainer pattern or a
  bounded child walk of that column only

### Requirement: Address-bar navigation uses semantic value assignment
The harness SHALL assign the address text through UIA ValuePattern when the
native EDIT proxy supports it, verify the value, and send only Enter as the
commit key. SendInput text entry SHALL remain the recorded fallback.
Correctness SHALL NOT depend on Ctrl+A keyboard selection.

#### Scenario: ValuePattern address assignment
- **WHEN** the harness navigates to a path
- **THEN** the address edit's Value is set and verified semantically before
  Enter commits, and the used path (pattern vs fallback) is recorded

### Requirement: Test-owned Explorer windows are identified structurally
The harness SHALL identify its test-owned Explorer window by class
(`CabinetWClass`), UIA ProcessId (including pre-existing explorer.exe
processes that broker the new window), and the window's address-bar value
matching the exact fixture path — never by window title alone. The harness
SHALL bind only to that window, wait for the folder's list view to be
exposed with valid bounds (and the expected source item when needed) before
dragging, and close session-owned windows without terminating
`explorer.exe`.

#### Scenario: Explorer discovery under broker reuse
- **WHEN** an existing explorer.exe process brokers the test window
- **THEN** the harness still binds to the correct window via class + PID +
  address-bar value and never touches unrelated Explorer windows

### Requirement: Dual-pane setup is semantic
The harness SHALL set up dual pane by inspecting the ColumnView tree,
invoking the real product command when `Pane.1` is absent, waiting
semantically for `Pane.1` to exist, re-acquiring the fragment, and
validating distinct pane identities. No fixed sleep-then-single-search, no
hidden test hook.

#### Scenario: Pane.1 appears deterministically
- **WHEN** the harness invokes dual-pane creation
- **THEN** `Pane.1` appears, both panes have distinct identities, and pane
  ownership is explicit before any per-pane navigation

### Requirement: Drag/drop is verified by filesystem side effects
Each drag scenario SHALL derive all pointer geometry from semantic UIA
BoundingRectangles of identified elements or real HWND/client rectangles,
perform the drag through the driver's SendInput drag sequence, and verify
the resulting filesystem state programmatically. FastFiles → Explorer SHALL
also assert the copy/move semantics on the source; Explorer → FastFiles and
pane → pane SHALL also wait for the FastFiles UIA refresh and verify the
file appears in Column View. A PASS SHALL NOT be claimed merely because
mouse movement occurred.

#### Scenario: Pane-to-pane drag lands in the right pane
- **WHEN** a file is dragged from Pane.0 to Pane.1
- **THEN** the file exists in the destination folder, appears in Pane.1's
  UIA column, and the source pane identity is distinct from the destination
  pane identity

### Requirement: Honest failure classification
The harness SHALL NOT convert legitimate failures into SKIPPED, hide a
product defect as a harness workaround, or introduce pixel/OCR/image
fallbacks, hidden test controls, or private test IPC. A scenario failing in
a fresh isolated lifecycle is a product-defect candidate and SHALL be
reclassified with evidence. Every SKIP SHALL name a real independent
prerequisite.

#### Scenario: No fabricated SKIP
- **WHEN** a scenario fails for a harness-controllable reason
- **THEN** it is recorded FAIL with diagnostics, never SKIPPED
