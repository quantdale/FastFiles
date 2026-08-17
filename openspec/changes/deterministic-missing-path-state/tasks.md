# Tasks — deterministic-missing-path-state

Baseline when authored: `main` at `2169e858f9cb3e757d613946756481593b188c2e`, Treemap semantic UIA complete, hosted CI green. Live Git state always wins on resume.

Do not mark a task complete without evidence. If the reproduction disproves the expected ordering hypothesis, update this change's design/evidence to the proven root cause before implementing the fix; preserve the requirements that missing paths never masquerade as empty success and that the live state is deterministic.

## 1. Reconcile and reproduce

- [x] 1.1 Fetch `origin`, verify branch `main`, reconcile `HEAD`/`origin/main`, inspect `AGENTS.md`, `docs/agent/WORKSTATE.md`, this OpenSpec change, `recovery-degraded-browsing`, `stabilize-live-uia-e2e`, and the canonical `index-engine`, `column-view-browsing`, and `ui-automation` specs. Start from a clean tree.
- [ ] 1.2 Run the current filtered `in-column-error-state-late` scenario and reproduce its non-determinism without changing product code. Record exact outcomes, diagnostic ordering, snapshot generation/status, and whether prior rows remain visible.
- [x] 1.3 Reproduce manually at the engine/protocol level with a disposable path: enumerate success -> delete directory -> observe watcher/request -> read published snapshot for the exact path. Determine whether the incorrect run is caused by missed watcher termination, stale indexed merge overwrite, status normalization, snapshot lookup, freshness ordering, or another mechanism.
- [x] 1.4 Add pre-fix regression tests that fail for the proven ordering. Do not weaken them to match current behavior.

## 2. Path-observation ordering

- [x] 2.1 Implement a bounded per-path observation/precedence mechanism so a fresh explicit degraded failure (`NotFound`, `AccessDenied`, `IoError`) cannot be overwritten by older indexed `Success` data for the same canonical Windows path.
- [x] 2.2 Preserve complete indexed-export replacement semantics for normal indexed paths and volume cleanup; do not reintroduce stale deleted indexed keys.
- [x] 2.3 Define and test the clearing rule: a later successful explicit re-enumeration of the same path supersedes the failure and publishes current contents.
- [x] 2.4 Ensure path comparison/keys use the repository's Windows-path equality rules so case/spelling variants cannot create conflicting observations.

## 3. Watcher terminal refresh

- [x] 3.1 Audit `DirectoryWatcher` terminal-error behavior for deletion/disconnect/invalid-handle transitions and prove whether directory removal can end the watch without an ordinary callback.
- [x] 3.2 On a terminal watch failure, unless cancellation/shutdown is active, perform exactly one bounded final refresh callback/re-enumeration so the path's current state is published before the watch retires.
- [x] 3.3 Keep `ERROR_NOTIFY_ENUM_DIR` overflow behavior and the existing bounded re-arm policy intact.
- [x] 3.4 Prove explicit shutdown/unwatch never fabricates `NotFound` or another failure observation.
- [x] 3.5 Prove watch-creation failure does not suppress the result of the explicit directory request/enumeration.

## 4. Snapshot and UI correctness

- [x] 4.1 Verify `NotFound` remains `DirectoryEnumerationStatus::NotFound` in the published snapshot and is never encoded as `Success` + empty entries.
- [x] 4.2 Verify a legitimate empty directory remains `Success` + zero entries and renders `This folder is empty`, not the missing-path error.
- [x] 4.3 Verify Column View maps confirmed `NotFound` to `SnapshotFailureReason::NoLongerAvailable`, clears prior rows, and renders exactly the canonical unavailable wording.
- [x] 4.4 Preserve intentional stale-row behavior for temporary/disconnected/publish-failure states; do not clear rows indiscriminately for every failure class.
- [x] 4.5 Verify navigation/re-request after recreating the same directory recovers in place to current `Success` contents without restarting the UI or engine.

## 5. UI Automation and semantic events

- [ ] 5.1 Verify the Column status UIA provider exposes the exact visible missing-path wording and correct `ControlType`/bounds while the unavailable state is active.
- [ ] 5.2 Verify stale item providers from the pre-delete listing fail cleanly with `UIA_E_ELEMENTNOTAVAILABLE` or the existing stale-provider contract and never retarget recreated rows.
- [ ] 5.3 Raise only the existing appropriate semantic structure/property events for the transition; do not add paint-tick or polling events.
- [ ] 5.4 Update the live capability so `in-column-error-state-late` executes deterministically through a normal engine-connected degraded session instead of using `error-state-trigger-not-deterministic` as a SKIP reason.

## 6. Deterministic live scenario

- [ ] 6.1 Build the scenario in an isolated `ScenarioContext` using a disposable profile-root fixture.
- [ ] 6.2 Navigate normally to a folder containing at least one known file; verify the file is semantically visible.
- [ ] 6.3 Delete the current directory externally from the harness (not through FastFiles), then use bounded semantic waits for the missing status. Do not use fixed sleep as the correctness mechanism.
- [ ] 6.4 Assert the prior item is no longer presented as current and the status means `This folder is no longer available.`.
- [ ] 6.5 Recreate the same path with a different sentinel file, trigger a normal re-request/navigation, and assert recovery to current contents while the old sentinel remains absent.
- [ ] 6.6 Run the affected scenario at least 10 consecutive times; target 10/10 PASS with zero retry-until-pass loops and report durations/retries.
- [ ] 6.7 Run the scenario before and after unrelated stable UIA scenarios to prove no order contamination.

## 7. Regression matrix

- [x] 7.1 Unit/integration: existing empty directory -> `Success(0)`.
- [x] 7.2 Unit/integration: missing path -> `NotFound`.
- [x] 7.3 Ordering: indexed success -> explicit `NotFound` -> stale indexed success merge => published state remains `NotFound`.
- [x] 7.4 Ordering: `NotFound` -> recreated path -> explicit success => failure cleared, new entries published.
- [x] 7.5 Watch terminal error -> final refresh -> `NotFound` publication.
- [x] 7.6 Watch cancellation/shutdown -> no synthetic failure publication.
- [x] 7.7 Access denied remains `AccessDenied`; unrelated I/O remains `IoError`.
- [x] 7.8 Multi-client subscribers observe the same resulting generation/state.
- [x] 7.9 Snapshot publication failure preserves the prior valid generation and surfaces the existing publish-failure state; this change must not regress that contract.

## 8. Diagnostics and performance

- [x] 8.1 Add only bounded metadata diagnostics needed to identify observation source/status/precedence. Follow `check_diagnostic_policy.ps1`; no sensitive contents.
- [x] 8.2 Measure added per-path state and verify it is bounded by requested/watched path lifecycle; no unbounded tombstone accumulation.
- [x] 8.3 Confirm no polling thread/timer was added and no filesystem enumeration occurs while holding the shared directory-publication lock.

## 9. Documentation and evidence

- [ ] 9.1 Record pre-fix reproduction, proven root cause, post-fix ordering model, live 10x result, and validation matrix under this change's `evidence/` directory.
- [ ] 9.2 Preserve historical SKIP/evidence in `stabilize-live-uia-e2e`; add a later coordination note if useful rather than rewriting the old result.
- [x] 9.3 Update canonical specs only if required by the normal OpenSpec workflow; do not silently weaken existing recovery/degraded behavior.

## 10. Validation and handoff

- [x] 10.1 Run focused engine watcher/UiServer/snapshot/ColumnView/UIA tests.
- [x] 10.2 `cmake --preset debug && cmake --build --preset debug` and full `ctest --preset debug --output-on-failure` — all discovered tests PASS, none Not Run.
- [x] 10.3 `cmake --preset release-test && cmake --build --preset release-test` and full release-test CTest — PASS.
- [x] 10.4 `cmake --preset analyze && cmake --build --preset analyze` — warning-clean `/W4 /WX`.
- [x] 10.5 `pwsh ./verify/verify.ps1 build` and `pwsh ./verify/verify.ps1 doctor` — PASS.
- [ ] 10.6 Full `windows-ui-automation-validation` — zero unexpected FAIL; `error-state-trigger-not-deterministic` no longer appears for the late missing-path scenario.
- [ ] 10.7 Run source-marker, diagnostic-policy, doc-drift checks; `openspec validate --all --strict --no-interactive`; `git diff --check`.
- [ ] 10.8 Review diff for scope. Do not stage fixture files, diagnostics logs, UIA dumps, JSON API output, local DBs, or build artifacts.
- [ ] 10.9 Commit the completed change, push to `origin/main`, verify `HEAD == origin/main`, and leave `git status --short` empty.
- [ ] 10.10 Query hosted CI for the exact pushed `head_sha`. Record the result in the final handoff, but do not create a recursive follow-up commit solely to store CI state.

## Completion gate

`COMPLETE` requires all of the following:

- confirmed missing path can never be published/rendered as an empty successful directory;
- a newer explicit missing/failure observation cannot be resurrected by stale indexed success;
- terminal watcher deletion is deterministically surfaced;
- recreated path recovers normally;
- `in-column-error-state-late` passes 10/10 live through the ordinary unprivileged engine path;
- full live UIA capability has zero unexpected FAIL;
- all native/build/spec/policy gates pass;
- changes are committed/pushed to `main` with a clean tree.

If the only remaining blocker is a genuinely external prerequisite, report `BLOCKED_EXTERNAL` with evidence. This change is intentionally designed not to require the parked production-signing/privileged-validation campaign.
