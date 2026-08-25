## Purpose

Define a deterministic, evidence-producing contract for interactive dual-pane UI behavior so product regressions, stale UIA state, input failures, contaminated lifecycles, and unavailable desktop prerequisites are classified separately.

## ADDED Requirements

### Requirement: Dual-pane changes are observable as one semantic operation
The product and its supported UI automation surface SHALL expose a dual-pane enable or disable operation as successful only after the requested workspace state, rendered pane state, active-pane state, and current accessibility tree agree.

#### Scenario: Enable dual pane from a single-pane tab
- **WHEN** a real Toggle Dual Pane command is activated while the current tab has one pane
- **THEN** the operation reports success only when the tab has two panes, both panes are reachable from the current ColumnView tree, their identities are distinct, and the active pane is known

#### Scenario: Disable dual pane while the second pane is active
- **WHEN** a real Toggle Dual Pane command is activated while Pane.1 is active
- **THEN** the operation reports success only when one pane remains, its navigation location is the active location defined by the workspace contract, focus has migrated to it, and no live provider still presents Pane.1

#### Scenario: Product and accessibility state disagree
- **WHEN** the workspace changes but the current accessibility tree does not expose the corresponding pane set within the bounded semantic wait
- **THEN** the result is a classified provider or readiness failure and is not accepted as a successful toggle

### Requirement: Interactive failures retain a precise failure class
Each failed semantic operation SHALL identify the earliest supported failure class from this set: product state-transition defect, command dispatch or palette defect, active-pane/focus/input-routing defect, UIA provider reconstruction or stale-element defect, layout/animation/readiness race, workspace persistence or prior-run contamination, test-owned process contamination, harness semantic-discovery defect, or genuine external interactive-desktop limitation.

#### Scenario: A toggle fails before product state changes
- **WHEN** the command is accepted by the palette but the workspace state remains unchanged
- **THEN** diagnostics classify the failure as a product state-transition defect rather than a provider or timing failure

#### Scenario: A provider is replaced during a toggle
- **WHEN** a pre-toggle element becomes unavailable and a fresh element cannot be reacquired from the current main-window tree
- **THEN** diagnostics classify the failure as a UIA provider reconstruction or stale-element defect and include both the old and new identity observations

#### Scenario: The desktop cannot provide foreground input
- **WHEN** bounded foreground and focus preconditions cannot be established because the documented interactive desktop prerequisite is unavailable
- **THEN** diagnostics classify the result as an external limitation only after product and harness ownership checks have passed

### Requirement: Isolated scenarios own their runtime state
An interactive scenario requiring a known startup state SHALL use disposable, test-owned workspace and fixture state and SHALL distinguish pre-existing processes from processes launched or adopted by that scenario.

#### Scenario: A persistent engine predates the scenario
- **WHEN** an engine with the repository build identity exists before UI launch
- **THEN** the scenario records it as pre-existing and does not terminate it unless ownership is subsequently proven by an explicit scenario adoption contract

#### Scenario: A prior workspace contains dual-pane state
- **WHEN** a scenario requires a single-pane precondition
- **THEN** it establishes that state through a supported isolated fixture or product operation and reports contamination if the precondition cannot be proven

### Requirement: Determinism gates count only clean executions
The dual-pane runtime gate SHALL count an execution only when it completes without an unexplained retry, stale-provider escape, leaked scenario-owned process, or unclassified failure.

#### Scenario: Twenty consecutive isolated executions
- **WHEN** the supported interactive desktop is available and the selection dual-pane scenario is run repeatedly
- **THEN** the gate requires twenty consecutive clean passes and resets the count after the first failure

#### Scenario: External prerequisite is unavailable
- **WHEN** the desktop prerequisite is genuinely absent and the product and harness evidence is otherwise complete
- **THEN** the gate records a structured external skip with the prerequisite evidence and does not convert the known dual-pane failure into a skip

### Requirement: Diagnostics exclude sensitive user data
Runtime evidence SHALL describe command, pane, focus, provider, process-ownership, and readiness state without recording arbitrary user file contents, credentials, or sensitive authentication material.

#### Scenario: A test uses filesystem fixtures
- **WHEN** navigation or selection evidence is emitted
- **THEN** it identifies only the test-owned fixture and stable item metadata needed for diagnosis, not arbitrary user filesystem contents
