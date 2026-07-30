## ADDED Requirements

### Requirement: Volume Capacity Summary
`FastFiles` SHALL display, for each available fixed volume, total capacity, used capacity, free capacity, and used-percentage, sourced from the operating system's own reporting rather than derived solely from summing indexed item sizes.

#### Scenario: Volume list shows capacity for all available volumes
- **WHEN** a user opens the storage overview
- **THEN** the system SHALL display each available fixed volume with its total, used, and free capacity and its used-percentage

#### Scenario: OS-reported usage takes precedence over indexed totals
- **WHEN** the sum of indexed item sizes for a volume differs from the operating system's reported used capacity
- **THEN** the system SHALL display the operating-system-reported total/used/free values as the volume-level figures and SHALL NOT silently substitute the indexed sum for them

### Requirement: Drive Selection for Analysis
The system SHALL allow a user to select a volume from the storage overview to open a detailed hierarchical drill-down and treemap scoped to that volume.

#### Scenario: Selecting a volume opens its drill-down
- **WHEN** a user selects a volume from the storage overview
- **THEN** the system SHALL open the hierarchical drill-down and treemap view scoped to that volume's root

#### Scenario: Volume becomes unavailable after being listed
- **WHEN** a previously visible volume becomes unavailable (for example, an external drive is removed)
- **THEN** the storage overview SHALL mark that volume as unavailable, SHALL retain and clearly label its last-known capacity figures as stale rather than deleting them silently, and SHALL disable opening a live drill-down for it until it reconnects

### Requirement: Degraded-Mode Volume Capacity Availability
The storage overview SHALL display accurate total/used/free/percentage figures for each accessible volume even when `FastFilesIndexSvc` is unavailable, since these figures come directly from the operating system rather than from the filesystem index.

#### Scenario: Volume capacity available without the privileged index service
- **WHEN** `FastFilesIndexSvc` is not installed, not running, or not connected (degraded mode)
- **THEN** the storage overview SHALL still display correct total, used, free, and percentage-used figures for each accessible volume

### Requirement: Indexed Coverage Indicator per Volume
The storage overview SHALL indicate, for each volume, whether whole-volume drill-down and treemap data is available (fully indexed) or limited to browsed/pinned directories (degraded mode or partial index), so a user understands the scope of the analysis before opening it.

#### Scenario: Coverage indicator reflects full indexing
- **WHEN** a volume's filesystem index covers the whole volume
- **THEN** the storage overview SHALL indicate that whole-volume drill-down and treemap analysis is available for that volume

#### Scenario: Coverage indicator reflects degraded or partial coverage
- **WHEN** a volume's index coverage is limited to browsed or pinned directories only, rather than the whole volume
- **THEN** the storage overview SHALL display an explicit indicator that whole-volume analysis is not currently available for that volume, distinguishing it from a fully indexed volume
