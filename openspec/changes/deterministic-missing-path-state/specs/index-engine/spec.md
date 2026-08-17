# deterministic-missing-path-state — index-engine delta

## Purpose

This delta makes path-state publication deterministic when an explicitly browsed or watched directory disappears. It preserves the existing unprivileged degraded-enumeration architecture while defining ordering between fresh filesystem observations and indexed snapshot merges so a confirmed missing/error state cannot be replaced by stale successful data.

## ADDED Requirements

### Requirement: Explicit path observations preserve the real enumeration status
For a directory explicitly requested by the UI or refreshed by its active watcher, the engine SHALL publish the actual enumeration status (`Success`, `AccessDenied`, `NotFound`, or `IoError`) for that path. `NotFound` SHALL NOT be normalized to `Success` with an empty entry set.

#### Scenario: Missing directory is not an empty directory
- **WHEN** the engine enumerates a requested path and the operating system reports that the path no longer exists
- **THEN** the published snapshot entry for that path has status `NotFound`, regardless of whether the previous generation contained entries for the path

#### Scenario: Existing empty directory remains successful
- **WHEN** the engine successfully opens an existing requested directory and observes zero children
- **THEN** the published snapshot entry has status `Success` and zero entries, not `NotFound`

### Requirement: A newer explicit failure observation outranks stale indexed success for the same path
If a requested/watched path has a fresh explicit observation of `NotFound`, `AccessDenied`, or `IoError`, an indexed snapshot merge that does not demonstrably post-date that observation SHALL NOT overwrite the path with `Success`. The implementation SHALL retain bounded ordering/provenance sufficient to enforce this rule.

#### Scenario: Stale indexed state cannot resurrect a deleted directory
- **GIVEN** an indexed export contains a successful directory snapshot for path P
- **WHEN** a later explicit degraded enumeration observes P as `NotFound`
- **AND** an older/stale indexed success for P is merged afterward
- **THEN** the next published UI snapshot still reports P as `NotFound`

#### Scenario: Recreation clears the failure
- **GIVEN** path P is currently published as `NotFound`
- **WHEN** P is recreated and a later explicit enumeration succeeds
- **THEN** the failure observation is superseded and the next published snapshot reports the newly observed successful contents

### Requirement: Terminal watcher failure performs one final state refresh
When a watched directory's notification loop terminates because of a non-cancellation error, the engine SHALL perform one final bounded path refresh/re-enumeration before retiring the watch, unless engine shutdown/unwatch cancellation is already in progress. The final result SHALL be published through the normal snapshot-generation path.

#### Scenario: Deleted watched directory closes without an ordinary notification
- **WHEN** a watched directory is removed and the watch terminates with a path/handle error before delivering a normal change callback
- **THEN** the engine performs a final refresh and publishes `NotFound` when the directory is absent

#### Scenario: Intentional shutdown does not fabricate an error
- **WHEN** FastFiles cancels or stops a watch as part of normal shutdown or eviction
- **THEN** no synthetic `NotFound`, `AccessDenied`, or `IoError` observation is created solely because of that cancellation

### Requirement: Watch startup and directory enumeration are independently fail-safe
Failure to create a directory watch SHALL NOT prevent an explicit request from publishing the directory's enumeration result. Watch registration SHALL remain bounded and optional to serving the immediate request.

#### Scenario: Missing path cannot be watched
- **WHEN** a requested path no longer exists and the watch handle cannot be created
- **THEN** the explicit enumeration still publishes `NotFound`

### Requirement: Path-state precedence is bounded and canonical
Any observation/provenance state introduced to enforce ordering SHALL use the repository's canonical Windows path equality semantics, SHALL be bounded by requested/watched path lifecycle, and SHALL NOT grow indefinitely as a permanent tombstone database.

#### Scenario: Case variants share one observation state
- **WHEN** the same Windows path is referenced with case-only spelling differences
- **THEN** the engine applies one path-state ordering decision rather than publishing conflicting states
