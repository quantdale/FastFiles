## ADDED Requirements

### Requirement: UIA Element Identity And Tree Traversal
The UIA driver SHALL discover elements through Windows UI Automation identities (Name, AutomationId, ControlType, ClassName, process) and SHALL traverse the UIA tree (Raw and Control views) to locate targets, rather than by screen coordinates or whole-frame pixel matching.

#### Scenario: An element is found by semantic identity
- **WHEN** the driver locates a UI element
- **THEN** it SHALL resolve the element via UIA identity properties (AutomationId/Name/ControlType) and SHALL NOT depend on pixel coordinates as the primary locator

#### Scenario: A missing semantic target fails clearly
- **WHEN** a semantic UIA target cannot be found within the configured timeout
- **THEN** the driver SHALL emit a structured failure with a diagnostic tree dump identifying what was searched, and SHALL NOT silently fall back to a brittle coordinate or pixel assertion

### Requirement: Pattern Selection, Properties, Events, And Input
The UIA driver SHALL invoke UIA control patterns (Invoke, Selection, Scroll, Value, Window, Drag where exposed), SHALL read and assert UIA properties, SHALL subscribe to UIA events, and SHALL drive keyboard and pointer input through UIA patterns with a `SendInput` fallback only where a needed pattern is not exposed by the target's UIA provider.

#### Scenario: A control is driven through its exposed pattern
- **WHEN** a target exposes an Invoke or Selection or Scroll pattern
- **THEN** the driver SHALL drive it through that pattern and SHALL assert the resulting UIA-visible state

#### Scenario: Input falls back only when no pattern is exposed
- **WHEN** a target exposes no UIA pattern for the required interaction
- **THEN** the driver MAY use `SendInput` keyboard/pointer input and SHALL record that the fallback was used, but SHALL NOT use whole-screen pixel comparison to verify the result

### Requirement: Timeouts And Diagnostic Tree Dumps
The UIA driver SHALL enforce per-operation timeouts, SHALL never block indefinitely, and SHALL produce diagnostic UIA tree dumps (machine-readable JSON and indented text) on failure capturing element identities, properties, and hierarchy around the target.

#### Scenario: A hung operation times out with a dump
- **WHEN** a UIA operation exceeds its timeout
- **THEN** the driver SHALL abort it, record a timeout failure, and attach a diagnostic tree dump of the relevant subtree

### Requirement: Headless Unit Tests With Mock Providers
The UIA driver's identity, tree-traversal, pattern-selection, property-validation, and timeout logic SHALL be covered by headless unit tests using mock UIA providers or recorded trees where practical, so the driver logic is verifiable without an interactive desktop.

#### Scenario: Driver logic is unit-tested without a desktop
- **WHEN** the driver unit tests run in a non-interactive context
- **THEN** identity/tree/pattern/property/timeout logic SHALL pass against mock providers without requiring a live UI process