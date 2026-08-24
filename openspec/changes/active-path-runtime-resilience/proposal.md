## Why

The degraded/unprivileged browsing path is the supported runtime when the
privileged service is unavailable, but its existing unit seams mostly validate
individual watcher, snapshot, and search operations. Cross-lifecycle failures
can still combine a filesystem change, a failed publication, a disconnect, or
a superseded request in ways that are difficult to diagnose and that risk
leaving old UI state looking current. The Phase 8–10 mainline convergence made
the indexed read plane bounded and coherent; this change hardens the remaining
active-path lifecycle edges without replacing that architecture.

## What Changes

- Add deterministic, test-only lifecycle fault seams for degraded directory
  observation, snapshot publication, and engine/UI request coordination where
  native failure paths cannot otherwise be reached reliably.
- Make watch retirement, publication failure, reconnect, and cleanup outcomes
  explicit through bounded, metadata-only diagnostics and recovery/unavailable
  state transitions.
- Exercise snapshot-generation ordering, last-known-good freshness, engine
  disconnect/reconnect, search cancellation/supersession, and file-operation
  observation convergence with condition-variable or generation-based tests.
- Add a bounded active-path resilience verification capability over marked,
  disposable fixtures, with honest PASS/FAIL/SKIPPED results and cleanup
  evidence; add bounded resource observations when the environment supports
  process execution.
- Reconcile current project-status documentation and workstate with the
  converged `main` state while preserving historical evidence as history.

This is hardening of implemented behavior, not a replacement architecture. It
does not change the privileged command surface, authentication policy, wire or
snapshot binary layouts, or production signing requirements.

## Capabilities

### New Capabilities

- `active-path-runtime-resilience`: cross-component degraded-runtime
  observation, publication, reconnect, supersession, diagnostics, and
  verification invariants.

### Modified Capabilities

None. Existing degraded browsing, search, snapshot, and file-operation
requirements remain authoritative; this change adds a focused cross-lifecycle
contract and regression coverage around them.

## Impact

- `src/engine`: `DirectoryWatcher`, `UiServer`, `IndexPipeline`, and
  `SnapshotPublisher` lifecycle/error seams.
- `src/ui` and `src/search`: snapshot freshness, engine reconnect, and stale
  search-result gating where an actual gap is reproduced.
- `tests/engine`, `tests/search`, `tests/fileoperations`, and relevant UI
  headless tests.
- `verify/capabilities` and capability registration for the marked-fixture
  resilience run.
- `openspec/changes`, `progress.md`, `CODE_INDEX.md`, and
  `docs/agent/WORKSTATE.md`.

All new fault injection is test-only, deterministic, and inaccessible in
release-test/production configurations. The existing three-process security
boundary remains unchanged.
