## ADDED Requirements

### Requirement: Dual-pane Home and filesystem locations are independently typed
When dual pane is enabled, each pane SHALL retain a typed Home or filesystem location, and Home SHALL never be passed to filesystem APIs as an empty path.

#### Scenario: Home is cloned into the second pane
- **WHEN** dual pane is enabled while the active pane is Home
- **THEN** both panes expose Home semantics until one is explicitly navigated, with no filesystem enumeration caused by the Home path representation

#### Scenario: One pane is Home and the other is a filesystem location
- **WHEN** only Pane.1 is navigated to a test-owned filesystem fixture
- **THEN** Pane.0 remains Home and pane-local navigation, focus, and selection state remain distinguishable

### Requirement: Home and pane layout restore deterministically
Workspace restoration SHALL preserve the supported per-tab pane count, typed locations, and active pane, or report a bounded restoration failure rather than silently presenting a different layout.

#### Scenario: A dual-pane tab is restored
- **WHEN** a disposable workspace containing Home/filesystem dual-pane state is reopened
- **THEN** the current UIA tree exposes the restored pane count and typed locations with the restored active pane
