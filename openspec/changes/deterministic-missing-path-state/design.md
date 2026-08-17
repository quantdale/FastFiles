# Design — deterministic-missing-path-state

## Context

The current architecture has multiple producers that can affect the same path in the UI snapshot:

1. `UiServer::HandleRequestDirectory` performs/arranges degraded enumeration for an explicitly browsed path and starts a watcher.
2. `DirectoryWatcher` re-enumerates watched paths after changes/overflows.
3. `IndexPipeline` periodically exports indexed directory state and `UiServer::MergeIndexDirectories` merges that state into the published directory map.
4. `SnapshotPublisher` publishes the merged map as immutable generations.
5. `ColumnView` maps `DirectoryEnumerationStatus` into freshness/error state.

The primitives already distinguish `Success`, `AccessDenied`, `NotFound`, and `IoError`. The defect is therefore expected to be in observation ordering, merge precedence, or watcher terminal handling rather than in the display vocabulary itself. The implementation MUST confirm the real transition before patching.

## D1 — Missing is a first-class terminal observation, never an empty success

A directory is `Success` with zero entries only when enumeration successfully opened the directory and observed zero children. Failure to open the path because it no longer exists MUST remain `NotFound` through publication and UI consumption.

No layer may normalize `NotFound` to `Success`, erase the status while preserving an empty entry vector, or substitute stale entries while claiming current success.

## D2 — Fresh explicit filesystem observation outranks stale indexed success for the same browsed path

For a path that the UI explicitly requested or is actively watching, a fresh degraded/filesystem observation is the immediate authority for current accessibility/existence. If that observation is `NotFound`, `AccessDenied`, or `IoError`, a later merge of indexed data that predates the observation MUST NOT silently replace the failure with `Success` for the same path.

Implementation may use an observation overlay, tombstone/failure map, per-path epoch, or another bounded representation. The important contract is ordering, not the container type.

A failure observation is cleared only by a later observation that is demonstrably newer for that same path, such as:

- a successful explicit degraded re-enumeration after recreation/recovery; or
- an indexed observation with a trustworthy ordering relation proving it post-dates the failure.

If the current architecture has no cross-source ordering primitive, prefer the safe rule: explicit degraded failure remains authoritative until explicit degraded success re-observes the path.

## D3 — Indexed merge may remove stale indexed paths but may not resurrect a path over a newer failure observation

`MergeIndexDirectories` can continue replacing a complete indexed export for a volume. It MUST still remove stale indexed keys when they disappear from a complete export.

However, if a current path has a newer explicit failure observation, inserting an older indexed `Success` entry at the same normalized path MUST NOT overwrite that observation.

Path equality uses the repository's canonical Windows-path comparison/normalization rules. No case-sensitive duplicate state.

## D4 — Watcher terminal transitions perform a final refresh

A directory watch can terminate because the directory was removed, disconnected, access changed, the handle became invalid, or another non-overflow error occurred. Before abandoning a watched path after a terminal `ReadDirectoryChangesW`/watch error, the engine MUST make one bounded final re-enumeration attempt (unless shutdown/cancellation is in progress) and publish the resulting path status.

The final refresh is not a polling loop. It occurs only on a terminal watcher transition.

If final re-enumeration returns `NotFound`, the UI receives the missing-path state even when no ordinary change notification was delivered before the directory handle became invalid.

Watcher shutdown requested by FastFiles itself MUST NOT manufacture a missing/error state.

## D5 — Initial watch startup failure does not suppress the explicit enumeration result

Opening a watch handle and enumerating a directory are separate concerns. If watch registration fails but explicit enumeration can still classify the path, the classification MUST be published. A missing path should become `NotFound` even when no watch can be created.

No request may wait indefinitely for a watcher.

## D6 — Confirmed path disappearance clears rows; transient refresh errors may retain stale rows only when labeled stale

Column View keeps its existing freshness model, but the distinction becomes explicit:

- `NotFound` / `NoLongerAvailable`: confirmed terminal path disappearance; clear the unavailable column's rows and show `This folder is no longer available.`
- `AccessDenied`: current path cannot be accessed; clear rows and show the permission error.
- `IoError` / publication failure / temporary disconnect: preserve existing stale-content behavior only where the freshness policy intentionally permits it, and label the content as previous/stale rather than current.
- `Success`: accept only according to generation/observation ordering and populate the actual entries, including a legitimate empty directory.

A confirmed `NotFound` MUST NOT be rendered as `This folder is empty`.

## D7 — Recovery is explicit and monotonic

If the path is recreated after a `NotFound` state, the next normal re-request/retry/navigation that successfully enumerates it clears the failure observation and publishes `Success` with its actual contents.

The UI transitions from unavailable to current without process restart and without retaining old row identity as though it were current.

Old UIA status/item providers follow the existing stale-element contract: removed items fail cleanly and never retarget a new occupant.

## D8 — Diagnostics describe state transitions, not sensitive contents

Add or refine diagnostics only where needed to make the ordering observable. Diagnostics SHOULD include:

- path-state source (`request`, `watch-refresh`, `watch-terminal-refresh`, `indexed-merge`);
- status enum;
- observation/generation identifier if available;
- whether an indexed overwrite was suppressed by a newer explicit failure.

Do not log file contents, raw MFT/USN bytes, credentials, signing material, or unbounded directory listings.

Existing diagnostic policy and path-sanitization rules remain authoritative.

## D9 — Live validation uses normal UI/engine behavior

The `windows-ui-automation-validation` capability SHALL create a disposable directory, navigate to it through the normal UI, confirm real contents, delete that directory externally from the harness, and wait for the product to publish the missing state.

The assertion is semantic:

- the Column View status element appears;
- exact user-visible meaning is `This folder is no longer available.`;
- prior file rows are not presented as current;
- the result is reached without private product hooks, OCR, screenshots, or retry-until-pass loops.

The scenario then recreates the same path, performs a normal re-request/navigation, and verifies recovery to `Success` with new contents.

## D10 — Determinism is proven under ordering stress

Regression coverage MUST exercise the dangerous orderings, including:

1. indexed `Success` exists -> degraded `NotFound` -> stale indexed `Success` merge;
2. degraded `NotFound` -> recreate -> degraded `Success`;
3. watcher terminal error -> final re-enumeration `NotFound`;
4. watcher shutdown/cancel -> no fabricated failure;
5. empty existing directory -> `Success` with zero entries, never `NotFound`;
6. access denied and generic I/O failures remain distinct from missing.

At least one stress/repeatability test must run the live missing/recreate scenario repeatedly without sleeps as the correctness mechanism. Bounded semantic waits are allowed.

## D11 — No privileged-path dependency

This change MUST work with the ordinary unprivileged/degraded engine path and MUST NOT require FastFilesIndexSvc, administrator elevation, trust-store mutation, production signing, or real MFT/USN access.

The parked production-validation campaign remains parked.

## D12 — Performance and lifecycle

No background polling thread is introduced. Additional per-path state is bounded by the same requested/watched directory lifecycle and should be removed when no longer needed or when the existing watcher/directory cache evicts it.

Locks remain short; filesystem enumeration is not performed while holding the publication map lock. Snapshot publication remains generation-based and immutable for readers.
