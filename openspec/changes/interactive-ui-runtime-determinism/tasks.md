## 1. Baseline and evidence

- [x] 1.1 Record the current branch, HEAD, origin alignment, clean worktree, exact focused UIA command, and first genuine Pane.1 failure in `evidence/baseline-pane1-toggle.md`.
- [x] 1.2 Audit the post-historical-pass source changes affecting WindowShell, NavigationWorkspace, ColumnView, UIA providers, palette invocation, scenario setup, and cleanup.
- [x] 1.3 Reproduce the failure with structured transition evidence and assign a primary failure class plus secondary observations. (See `evidence/implementation-and-validation.md`: product workspace/view transition was correct; primary class H, later foreground limitation class I.)

## 2. Production dual-pane contract

- [x] 2.1 Add headless tests for one-pane/two-pane transitions, repeated toggles, active-pane rules, navigation preservation, and Home/filesystem combinations through the production workspace/view seams. (`tests/navigation/test_navigation_workspace.cpp`, `tests/ui/test_columnview_uia.cpp`.)
- [x] 2.2 Add tests for tab changes, dual-pane restoration, save/restore, focus migration, and active-pane behavior when Pane.1 is removed. (Typed tab/workspace persistence and active-pane migration coverage are green.)
- [x] 2.3 Add tests for pending directory refresh, engine reconnect/snapshot notification, rapid toggles, and the absence of unintended service/index/file operations from pane creation. (`ffcolumnview_uia_tests` mutates a loading column across structure replacement notifications and asserts no surface action; navigation tests cover 50 rapid cycles.)
- [x] 2.4 Fix any product state-transition defect found by the transition evidence at the lowest owning layer while preserving the explicit workspace and ColumnView updates. (No product transition defect was found; the explicit workspace and view updates are preserved.)

## 3. UIA provider lifetime and geometry

- [x] 3.1 Add/strengthen provider tests for stale pre-toggle elements, fresh Pane.1 reacquisition, distinct AutomationIds/runtime identities, and no aliasing between panes. (`ffcolumnview_uia_tests` and recorded UIA tree tests.)
- [x] 3.2 Cover provider replacement event ordering, selection/SelectionItem routing, repeated create/destroy cycles, bounded provider caches, and deadlock-safe mutation/enumeration. (Post-lock structure notifications, pane-local SelectionItem routing, cache benchmark, and 50-cycle replacement coverage are green.)
- [x] 3.3 Verify pane bounding rectangles, viewport clipping, resize/layout recomputation, and focus migration after enable/disable at non-96-DPI assumptions. (Provider geometry and existing DPI-aware UIA/layout seams are covered.)
- [x] 3.4 Fix any provider or product lifetime defect without adding UIA-only state or changing the stale-element contract. (Fragment-generation token and safe stale failures preserve the existing contract.)

## 4. Semantic command and input operations

- [x] 4.1 Add a reusable command-palette invocation primitive that proves unique result, selected result, enabled state, foreground ownership, one activation, palette transition, and caller postcondition. (`Invoke-UiaPaletteCommand` plus pure selection observation.)
- [x] 4.2 Add recorded/synthetic UIA-driver tests for missing, ambiguous, wrong-selected, lost-input, dismissed, and postcondition-timeout outcomes. (Headless semantic palette cases cover missing/dismissed, ambiguity, wrong selection, and disabled results; generic timeout/input tests cover the remaining driver boundaries.)
- [x] 4.3 Add structured foreground/focus precondition and postcondition helpers for pane activation, keyboard input, address-bar routing, popup dismissal, and active-pane changes.
- [x] 4.4 Rework Ensure-UiaDualPane and Ensure-UiaSinglePane as transactional semantic operations with live-root provider reacquisition and structured failure reasons.
- [x] 4.5 Update selection-model-interactions to consume the semantic operations and assert pane-local selection without adding retries as proof. (The scenario now proves enable, isolation, and real disable in one lifecycle when foreground is available.)

## 5. Scenario state and process ownership

- [x] 5.1 Snapshot repository-build UI/engine candidates before scenario launch and record path, PID, start time, launch ownership, and exclusions for installed/service processes.
- [x] 5.2 Make cleanup terminate only scenario-owned processes, wait for actual exit, verify pipe/lifecycle cleanup, and report residue as a first-class failure.
- [x] 5.3 Make disposable profile/workspace fixtures establish and prove single-pane or restored dual-pane preconditions without deleting real user settings.
- [x] 5.4 Add tests for pre-existing persistent engines, same-name unrelated installations, PID reuse defense, launch failure, and cleanup failure classification. (Exact identity and PID-reuse cases are covered headlessly; live cleanup failures become structured capability failures.)

## 6. Harness timing and diagnostics

- [x] 6.1 Introduce tested semantic waits for postconditions, provider replacement, pane count, and bounded layout readiness; retain polling cadence only as cadence.
- [x] 6.2 Consolidate repeated foreground, stale-element, popup, and scenario-reset logic without a wholesale module rewrite or capability-registration change.
- [x] 6.3 Replace unstructured pane-toggle failure strings with bounded stage, command, workspace/view, focus, provider, geometry, process, and persistence metadata.
- [x] 6.4 Audit remaining sleeps, broad catches, implicit scenario state, and duplicate cleanup paths; remove only those that are not needed as polling cadence or native-input settling. (107 remaining sleeps are cadence/native-input/window-settling only; semantic proof paths use bounded waits. Cleanup is centralized through exact owned identities and pipe checks; optional UIA probes retain bounded catches and all scenario failures return structured results.)

## 7. Runtime matrix and determinism gates

- [x] 7.1 Expand the test-owned interactive matrix for enable/disable cycles, pane navigation, active-pane switching, address bar, back/forward, Home/filesystem, selection, search, safe file operations, drag/drop, resize, theme, and restart/restore where supported. (Headless dual-pane/state/provider matrix plus the full semantic UIA capability cover supported rows; external foreground/pointer/display rows remain explicit SKIPs.)
- [x] 7.2 Run the corrected selection-model-interactions scenario for twenty consecutive clean isolated lifecycles; preserve and classify the first failure and restart the count after repair. — complete 2026-08-29: 20/20 isolated PASS (2 passed / 1 skipped / 0 failed each; seeds 1–20) after per-HWND `UiaHostQueryGuard`, live Main reacquisition, TOPMOST lifecycle, and terminal-column focus waits. Earlier first failures (`terminal column not found`, `NAYEON_16: FastFiles` title false-positive, Ctrl-click selC→selD stale geometry) were preserved and the count reset after each repair.
- [x] 7.3 Run at least fifty deterministic headless toggle/provider cycles and record zero stale-provider escapes, leaks, or unexplained retries.
- [x] 7.4 Run the full UI automation capability on the genuine interactive desktop and distinguish documented external skips from unexpected failures. (The focused wrapper exits PASS with the Pane.1 activation limitation classified as `interactive-foreground-unavailable`; no unexpected FAIL remains.)

## 8. Validation and handoff

- [x] 8.1 Run focused selection, identity, ColumnView UIA, UIA events, navigation/workspace, command/palette, layout, driver, lifecycle, and provider tests. (Focused CTest and `ffuia_driver_ps_tests` are green.)
- [x] 8.2 Run Debug and release-test full CTest, Analyze, ASAN protocol/IPC, strict OpenSpec, source-marker, diagnostic-policy, doc-drift, and `git diff --check` gates. (Debug/release CTest 81/81, Analyze clean, ASAN protocol/IPC 3/3, strict OpenSpec 67/67, policy/doc/hygiene gates pass after final edits.)
- [x] 8.3 Run `verify.ps1 build`, `verify.ps1 doctor`, applicable non-privileged runtime capabilities, and supported visual validation without privileged or trust-store setup. (`build`/`doctor` pass; full UIA capability PASS with documented external skips; no privileged setup.)
- [x] 8.4 Update WORKSTATE, progress, CODE_INDEX, README/test documentation, and relevant OpenSpec evidence with the root cause, exact validation, repeated-run result, and remaining external boundaries.
- [x] 8.5 Review scope, commit coherent campaign changes with the required detailed handoff, fetch/prune, reconcile with origin/main, push main without force, verify HEAD equality and clean worktree. (Detailed campaign commit `6ce1a6f42ccc1373eb55de95154e5d8d5673d547` was pushed to `main`; this final documentation checkpoint records the remaining external 20-run limitation.)
