## ADDED Requirements

### Requirement: Filesystem Enumeration And Traversal Validation
The harness SHALL validate filesystem behavior: volume enumeration, NTFS traversal, USN journal handling, incremental indexing, rescans, staleness detection, and correct handling of large directories, symbolic links, junctions, and long (`\\?\`) paths.

#### Scenario: Volumes enumerate and NTFS traverses
- **WHEN** the harness requests volume enumeration and traversal of an NTFS volume
- **THEN** the volumes SHALL be enumerated and the volume traversed, with the result recorded

#### Scenario: Reparse points and long paths are handled correctly
- **WHEN** traversal encounters a symbolic link, a junction, or a path exceeding `MAX_PATH`
- **THEN** the harness SHALL confirm the item is handled per design (followed, skipped, or reported) without failing the enumeration, and SHALL record the behavior

#### Scenario: Staleness detection triggers a rescan
- **WHEN** the underlying filesystem changes in a way the index should detect as stale
- **THEN** the harness SHALL confirm staleness is detected and the appropriate incremental update or rescan occurs

### Requirement: NTFS Metadata, Reparse And Special-File Validation
The harness SHALL validate FastFiles' handling of NTFS-specific and special-file cases: NTFS permissions, symbolic links, junctions, generic reparse points, alternate data streams (ADS), long (`\\?\`) paths, and locked/in-use files — confirming each is indexed or skipped per design without corrupting the listing or crashing.

#### Scenario: Alternate data streams are handled per design
- **WHEN** a file carrying one or more alternate data streams is indexed
- **THEN** the harness SHALL confirm the streams are handled per design (enumerated or deliberately ignored) and SHALL record the observed behavior

#### Scenario: Reparse points are classified, not blindly followed into loops
- **WHEN** traversal encounters a symbolic link, a junction, or another reparse point
- **THEN** the harness SHALL confirm it is classified and handled per design (followed with loop protection, skipped, or reported) without infinite recursion or enumeration failure

#### Scenario: A locked file does not break indexing
- **WHEN** a file is exclusively locked/in use during indexing
- **THEN** the harness SHALL confirm the index records what it can (e.g. metadata) and continues, rather than aborting the directory

#### Scenario: NTFS permissions are respected
- **WHEN** indexing spans directories the current context cannot fully access
- **THEN** the harness SHALL confirm access is governed by the intended NTFS permissions and inaccessible entries are handled per design without failing the whole operation

### Requirement: Index Change And Recovery Validation
The harness SHALL validate the index's response to filesystem mutation and to journal/index disruption: incremental indexing of created/deleted/renamed/moved entries, very large directory trees, USN journal recovery (journal wrap, reset, or ID change), and stale-index recovery.

#### Scenario: Create, delete, rename, and move are reflected incrementally
- **WHEN** files are created, deleted, and directories are renamed or moved under a watched/indexed location
- **THEN** the harness SHALL confirm the index reflects each change incrementally without a full manual rescan, and SHALL record the outcome per mutation type

#### Scenario: USN journal disruption is recovered
- **WHEN** the USN journal wraps, is reset, or changes journal ID
- **THEN** the harness SHALL confirm the index detects the discontinuity and recovers to a correct state (e.g. via rescan or re-baseline) rather than serving silently stale data

#### Scenario: A stale index is detected and recovered
- **WHEN** the index is made stale relative to the real filesystem
- **THEN** the harness SHALL confirm staleness is detected and the appropriate incremental update or rescan restores correctness

#### Scenario: Very large directory trees are handled within limits
- **WHEN** indexing a very large directory tree
- **THEN** the harness SHALL confirm the tree is enumerated/indexed to completion within the run's resource and time limits, and SHALL record throughput and resource usage for the performance baseline

### Requirement: Single-Session Reliability Validation
The harness SHALL validate recovery from unexpected service termination and from engine and UI crashes within a single session, confirming restart behavior, that dependent components reach a defined recovered state, and that resources are cleaned up.

#### Scenario: UI survives the service disappearing mid-session
- **WHEN** `FastFilesIndexSvc` is stopped or killed while the UI is browsing
- **THEN** the harness SHALL confirm the UI does not hang or crash and reaches degraded mode with a clear status, and SHALL record the transition

#### Scenario: A crashed component recovers
- **WHEN** the engine or UI process crashes
- **THEN** the harness SHALL confirm the defined recovery/restart behavior occurs and that the crash dump is captured to the run diagnostics

### Requirement: Resource-Leak Validation
The harness SHALL check for handle, memory, and thread leaks across a start/stop cycle of the FastFiles processes, comparing resource counts before and after and flagging growth beyond a defined tolerance.

#### Scenario: Handle and thread counts return to baseline
- **WHEN** the harness runs a start/exercise/stop cycle and compares process resource counts against the pre-run baseline
- **THEN** it SHALL flag any handle, memory, or thread growth beyond the configured tolerance as a leak finding with supporting diagnostics

### Requirement: Stress Validation
The harness SHALL register a Stress capability covering startup/shutdown/reconnect stress, pipe and snapshot stress, large-filesystem and long-running indexing stress, and memory/CPU pressure; it is Tier-3-gated and SHALL report `SKIPPED(context-absent)` when a Tier-3 execution context (time/scale/disposable host) is unavailable rather than a spurious pass or fail.

#### Scenario: Stress runs when a Tier-3 context is available
- **WHEN** a Tier-3 execution context is available and stress validation is selected
- **THEN** the harness SHALL execute the stress scenarios and record their outcomes and captured resource metrics

#### Scenario: Stress skips with a reason when Tier-3 context is absent
- **WHEN** no Tier-3 execution context is available
- **THEN** the Stress capability SHALL report `SKIPPED` with a machine-readable reason and required-context descriptor, and the archive gate SHALL treat it as required-but-unavailable only for changes whose gate policy requires it

### Requirement: Historical Performance Baselines And Regression Detection
The harness SHALL measure defined performance metrics — startup time, indexing throughput, IPC latency, snapshot generation and publication time, and memory/CPU/handle usage — persist them to a per-environment-fingerprint historical baseline store, and detect regressions against that history using configurable per-metric thresholds.

#### Scenario: Metrics are measured and persisted to the baseline store
- **WHEN** a performance run completes
- **THEN** the harness SHALL record each defined metric into the run artifacts and append it to the historical baseline store keyed by the environment fingerprint, and SHALL produce a performance summary

#### Scenario: Regressions are detected against history using configurable thresholds
- **WHEN** a metric breaches its configured threshold (absolute and/or percentage) relative to the rolling baseline for the same fingerprint
- **THEN** the harness SHALL flag it as a regression in the performance summary, identifying the metric, the baseline, the observed value, and the threshold breached

#### Scenario: Comparisons are like-for-like
- **WHEN** the current fingerprint differs from a stored baseline's fingerprint
- **THEN** the harness SHALL NOT compare across differing fingerprints, avoiding false regressions from hardware/toolset variance

#### Scenario: No prior baseline is handled gracefully
- **WHEN** no history exists for the current fingerprint
- **THEN** the harness SHALL seed the baseline with the current run rather than reporting a spurious regression

#### Scenario: Threshold breaches are advisory unless the gate policy opts in
- **WHEN** a regression is detected and the change's gate policy has not opted performance into gating
- **THEN** the harness SHALL surface the regression as advisory without blocking the archive gate on it
