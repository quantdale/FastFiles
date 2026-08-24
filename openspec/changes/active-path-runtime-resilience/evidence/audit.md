# Active-path resilience audit

## Baseline

The campaign baseline is `main` at `178f32e93bbb56a45a5da06efde2b338c8e6287d`,
the fast-forwarded Phase 8–10 train.  It contains the current indexed read
plane, search availability work, degraded browsing recovery, and the PR #6
reconciliation record in `docs/pr6-stabilization-reconciliation.md`.

## Existing protections retained

- `DirectoryWatcher` already bounds active roots, canonicalizes comparisons,
  evicts by LRU, handles zero-byte/`ERROR_NOTIFY_ENUM_DIR` overflow, performs a
  bounded give-up decision, emits terminal-refresh diagnostics, and joins its
  worker before closing handles.
- `SnapshotPublisher` already commits immutable mappings only after the header
  and payload are valid, retains the previous mapping when publication fails,
  and has deterministic mapping-failure hooks.
- `SnapshotFreshness` already distinguishes current, refreshing,
  stale-with-error, and unavailable state and rejects older generations.
- `EngineClient` already bounds reconnect attempts, closes pipe/mapping handles
  during disconnect, and fails pending indexed requests instead of leaving them
  indefinitely pending.
- `SearchPanel` already rejects late completions from superseded query
  generations and the engine read plane already has request cancellation.
- Existing file-operation E2E tests cover operation semantics and disposable
  fixtures, but do not connect those mutations to watcher enumeration and UI
  snapshot publication.

## Reproducible gaps selected for this change

1. The watcher test seam reaches completion errors but not setup failures for
   directory open, stop-event creation, notification-event creation, or the
   initial arm path.  The notification-event setup failure currently emits a
   diagnostic and retires the worker without a final refresh or explicit
   unavailable outcome.
2. `UiServer::HandleRequestDirectory` intentionally ignores the return value of
   `DirectoryWatcher::Watch`.  A successful initial enumeration can therefore
   look current after a watch-open failure, with no later observation path.
3. `SearchPanel::Hide` hides the controls but does not invalidate the active
   request generation or cancel an indexed request.  Destruction is safe, but
   close/reopen can retain an in-flight callback long enough to compete with a
   newer visible request.
4. Snapshot tests cover mapping/size failures and freshness ordering in
   isolation, but lack a serialization-failure injection and a cross-component
   filesystem-mutation-to-published-snapshot convergence battery.
5. The verification registry has resource-leak and filesystem capabilities, but
   no capability that combines marked degraded-path churn, watcher cycling,
   convergence, owned-process cleanup, and bounded resource sampling.

## Implementation disposition

- **Watcher setup and terminal retirement — resolved.** `DirectoryWatcher`
  now has only test-target `TestHooks` for directory/event/arm setup failures.
  Every non-shutdown setup/terminal failure emits structured failure and
  terminal-refresh diagnostics and invokes one final refresh after retired
  worker ownership is released. `ffengine_directory_watcher_tests` covers
  setup failures, synchronous/pending completion, overflow/give-up, case
  variants, delete/recreate, LRU, shutdown suppression, and 100 replacement
  cycles.
- **UiServer watch return — resolved by contract.** The request handler still
  uses the same `OnDirectoryChanged` callback path rather than duplicating a
  second refresh implementation. `DirectoryWatcher::Watch` now guarantees
  that a false setup result has already delivered the final refresh callback;
  the initial enumeration then publishes the current explicit status. The
  watcher diagnostics make the unavailable observation visible even though the
  handler does not need a second branch.
- **Search close/reopen — resolved.** `SearchPanel::Hide`, snapshot refresh,
  and engine lifecycle transitions all advance the pure request/source gate,
  cancel indexed work, and reject late completions with the append-only
  superseded-result diagnostic. `ffasync_result_gate_tests` runs 150
  deterministic supersession/source/cancellation cycles.
- **Snapshot and cross-component convergence — resolved.** A serializer hook
  proves failed publication leaves the last-known-good mapping/generation
  untouched; the fake EngineClient pipe proves 1/2/1/3 notification ordering;
  and the real UiServer/EngineClient fixture battery compares create, rename,
  move, copy, and delete results with independent filesystem enumeration.
- **Verification gap — resolved.** The new
  `windows-active-path-resilience-validation` capability launches the real
  headless harness with a deterministic marker-bound fixture, samples process
  handles/threads/private bytes/working set/CPU/liveness for a configurable
  bounded duration, and cleans the exact owned PID. The short configured run
  and the default 600-second soak passed; the latter completed with bounded
  resource samples, deterministic restart churn, and exact-owned-PID cleanup.
- **Diagnostic UI iterator failure — resolved.** The full diagnostic UIA run
  reproduced a Debug assertion in the only production `std::list` use, the
  icon-cache LRU (`<list>` line 146: value-initialized iterator dereference).
  Recency is now maintained by the pure, mutex-protected `ffui::IconLru`
  timestamp seam, with `ffui_icon_lru_tests` covering promotion, eviction, and
  clear/reuse. A current focused Search UIA run passes after the fix.
- **Interactive dual-pane validation — environment/driver gap remains.** A
  current focused `selection-model-interactions` run fails after 48.9 seconds
  because `Pane.1` does not appear after the real Toggle Dual Pane command;
  this is outside the active-path implementation and is retained as an honest
  UIA failure rather than converted to PASS.

## Focused evidence

After the convergence wait was corrected to predicate on the requested
directory rather than merely a newer global generation, the focused Debug
selection passed 6/6: diagnostics, watcher lifecycle, snapshot publication,
EngineClient convergence/order, active-path churn, and async result identity.
The repository source-marker and diagnostic-policy gates also passed. The
first failed run is retained as a development finding: it exposed a test
synchronization race, not a product failure, and the test now waits on the
published directory contents themselves. The active-path capability's default
600-second run and current focused Search UIA scenario also passed.

## Deliberately not changed

No current evidence requires a protocol or snapshot-format change, a new
privileged command, trust-store work, or reimplementation of the already
validated UI/display-path/Storage Analysis systems.
