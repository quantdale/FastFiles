# Tasks — deterministic-missing-path-state

Baseline when authored: `main` at `2169e858f9cb3e757d613946756481593b188c2e`, Treemap semantic UIA complete, hosted CI green. Live Git state always wins on resume.

Do not mark a task complete without evidence. If the reproduction disproves the expected ordering hypothesis, update this change's design/evidence to the proven root cause before implementing the fix; preserve the requirements that missing paths never masquerade as empty success and that the live state is deterministic.

## Continuation checkpoint — after `eea692287153ba3374098b8a76bf16761875cd66`

The engine/product half of this change is implemented and pushed. Hosted CI run `32000506341` for `eea692287153ba3374098b8a76bf16761875cd66` completed `success`. Debug CTest 64/64, release-test CTest 64/64, analyze, `verify.ps1 build`, and `verify.ps1 doctor` were reported PASS.

This change is **PARTIAL, not COMPLETE**. The remaining acceptance work is intentionally the live semantic/UIA half: tasks 1.2, 5.x, 6.x, 9.1-9.2, and 10.6-10.10.

Important execution rule for the continuation session:

- A pre-existing running FastFiles desktop process is **not** a prerequisite. `windows-ui-automation-validation` owns process launch through its scenario lifecycle.
- First run the repository's normal UIA availability probe through `verify.ps1`. If it reports an interactive Windows/UIA context as available, the live tasks MUST be executed in this session; they are not `BLOCKED_EXTERNAL` merely because FastFiles was not already running.
- Only an actual availability result such as session 0/services, `[Environment]::UserInteractive == false`, or an unavailable UIA driver can justify `BLOCKED_ENVIRONMENT`. Record the exact probe result if that happens.
- The parked production-signing / privileged MFT-USN campaign remains out of scope. The missing-path live scenario must use the ordinary per-user engine/degraded path.
- The current `Invoke-UiaScenarioErrorStateLate` still contains historical pre-fix logic that accepts multiple broad error strings and returns `SKIPPED(error-state-trigger-not-deterministic)` when the deleted path appears empty. That fallback is now obsolete and must be replaced by exact post-fix assertions; do not leave it as a permanent SKIP escape hatch.

## 1. Reconcile and reproduce

- [x] 1.1 Fetch `origin`, verify branch `main`, reconcile `HEAD`/`origin/main`, inspect `AGENTS.md`, `docs/agent/WORKSTATE.md`, this OpenSpec change, `recovery-degraded-browsing`, `stabilize-live-uia-e2e`, and the canonical `index-engine`, `column-view-browsing`, and `ui-automation` specs. Start from a clean tree.
- [ ] 1.2 Preserve real pre-fix live evidence. Prefer running the historical filtered `in-column-error-state-late` scenario from a detached temporary worktree at pre-fix commit `5286cda96a681ec46c7459be405c0fe045d8921f` (or another exact pre-fix parent before `eea6922`) without modifying `main`; record the old `error-state-trigger-not-deterministic` / empty-success behavior and relevant diagnostics. If prior retained live evidence already proves the same race, cite that evidence precisely instead of fabricating a new reproduction. Do not weaken this task into source inspection only.
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

- [ ] 5.1 Verify the Column status UIA provider exposes **exactly** `This folder is no longer available.` as a `Text` element with a non-zero, in-column `BoundingRectangle` while the unavailable state is active. Do not accept unrelated strings such as `Showing previous results`, `refresh failed`, generic `unable to load`, or `engine is unavailable` as proof of the missing-path state.
- [ ] 5.2 Before deletion, retain a UIA provider for a known sentinel row. After confirmed `NotFound`, prove that provider fails under the established stale-element contract (`UIA_E_ELEMENTNOTAVAILABLE` or the driver's exact equivalent) and never retargets after recreation. Also prove the old sentinel is absent from the current semantic children.
- [ ] 5.3 Verify the transition uses only the existing appropriate semantic structure/property events. Add/adjust provider tests if necessary. Do not add paint-tick, polling, hover, or timer-driven UIA events merely to make the harness pass.
- [ ] 5.4 Rewrite `Invoke-UiaScenarioErrorStateLate` to validate the post-fix contract. Remove the `error-state-trigger-not-deterministic` success escape path for an otherwise available interactive environment. The scenario may report `SKIPPED/BLOCKED_ENVIRONMENT` only when the capability availability probe itself proves the interactive/UIA prerequisite is unavailable.

## 6. Deterministic live scenario

- [ ] 6.1 Keep `in-column-error-state-late` as an isolated `ScenarioContext` using a disposable profile-root fixture. The scenario lifecycle launches/tears down its own repo-built FastFiles UI/engine; do not depend on an already-running app.
- [ ] 6.2 Create the path with a sentinel such as `before-delete.txt`, navigate normally through FastFiles, reacquire the terminal Column View semantically, verify the sentinel is visible, and retain its provider for the stale-provider assertion.
- [ ] 6.3 Delete the **currently displayed directory** externally from the harness (not through FastFiles). Prefer observing the watcher-driven current-view transition directly. Use bounded semantic waits for exact missing status; do not use a fixed sleep or navigation race as the correctness mechanism.
- [ ] 6.4 Assert all of the following in the missing state: exact status text; `ControlType=Text`; valid bounds; pre-delete sentinel absent from current children; held sentinel provider stale; no `This folder is empty` false positive.
- [ ] 6.5 Recreate the same path with a different sentinel such as `after-recreate.txt`, then trigger a normal supported re-request/navigation. Verify recovery to current `Success`: new sentinel visible, old sentinel absent, status removed, and the old provider still cannot retarget.
- [ ] 6.6 Run the affected scenario at least 10 consecutive times. COMPLETE target is 10/10 PASS, zero `error-state-trigger-not-deterministic`, zero retry-until-pass loops. Record each duration plus any bounded low-level retry/fallback count.
- [ ] 6.7 Run contamination/order checks: at minimum stable scenario -> missing-path -> stable scenario, and missing-path -> unrelated isolated scenario -> missing-path. Results must not depend on prior focus/search/pane state.
- [ ] 6.8 If the first live run fails after the engine fixes, diagnose the exact product-vs-harness cause. Fix a genuine product defect if required; otherwise harden only the relevant harness interaction. Do not convert a reproducible FAIL into SKIP.

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

- [ ] 9.1 Extend `evidence/implementation-evidence.md` (or a clearly linked live-evidence file) with: pre-fix live evidence or exact historical citation; proven four-defect root cause; post-fix ordering model; exact live scenario behavior; stale-provider proof; recreate/recovery proof; 10-run table; contamination results; full capability counts; all validation gates.
- [ ] 9.2 Preserve the historical `stabilize-live-uia-e2e` SKIP as history. Add a later resolution/coordination note if useful, but never rewrite the prior run to look as though the race never existed.
- [x] 9.3 Update canonical specs only if required by the normal OpenSpec workflow; do not silently weaken existing recovery/degraded behavior.

## 10. Validation and handoff

- [x] 10.1 Run focused engine watcher/UiServer/snapshot/ColumnView/UIA tests.
- [x] 10.2 `cmake --preset debug && cmake --build --preset debug` and full `ctest --preset debug --output-on-failure` — all discovered tests PASS, none Not Run.
- [x] 10.3 `cmake --preset release-test && cmake --build --preset release-test` and full release-test CTest — PASS.
- [x] 10.4 `cmake --preset analyze && cmake --build --preset analyze` — warning-clean `/W4 /WX`.
- [x] 10.5 `pwsh ./verify/verify.ps1 build` and `pwsh ./verify/verify.ps1 doctor` — PASS.
- [ ] 10.6 Run the filtered `in-column-error-state-late` scenario, its 10x repeatability gate, contamination checks, and then the full `windows-ui-automation-validation`. COMPLETE requires zero unexpected FAIL and no `error-state-trigger-not-deterministic` result. The early `in-column-error-state` scenario may remain a legitimate conditional SKIP on an engine-connected host if its documented engine-less condition does not apply.
- [ ] 10.7 Run `verify/check_source_markers.ps1`, `verify/check_diagnostic_policy.ps1`, the repository doc-drift check, `openspec validate --all --strict --no-interactive`, and `git diff --check`; all applicable gates PASS.
- [ ] 10.8 Review diff for scope. Do not stage fixture files, diagnostics logs, UIA dumps, JSON API output, local DBs, screenshots, or build artifacts unless the repository explicitly tracks a small sanitized evidence file.
- [ ] 10.9 Mark tasks complete only after evidence exists; update `docs/agent/WORKSTATE.md` to the true final state; commit all validated task-owned changes and push `main`; verify `HEAD == origin/main` and `git status --short` empty.
- [ ] 10.10 Query hosted CI for the exact final pushed `head_sha`. Record the live run id/status/conclusion in the final handoff. Do not create another commit solely to recursively store the CI result.

## Completion gate

`COMPLETE` requires all of the following:

- confirmed missing path can never be published/rendered as an empty successful directory;
- a newer explicit missing/failure observation cannot be resurrected by stale indexed success;
- terminal watcher deletion is deterministically surfaced;
- recreated path recovers normally;
- exact missing-path UIA status + stale-provider behavior are proven live;
- `in-column-error-state-late` passes 10/10 live through the ordinary unprivileged engine path;
- full live UIA capability has zero unexpected FAIL;
- `error-state-trigger-not-deterministic` is absent from the late scenario;
- all native/build/spec/policy gates pass;
- changes are committed/pushed to `main` with a clean tree and hosted CI is queried for the final SHA.

If the UIA availability probe proves the current process is session 0/non-interactive or the driver is genuinely unavailable, report `BLOCKED_ENVIRONMENT` with the exact probe evidence. Do **not** report `BLOCKED_EXTERNAL` merely because no FastFiles desktop process was already running. This change intentionally requires no parked privileged-production prerequisite.
