# Tasks — harden-selection-model-semantics

## Execution status (2026-08-20, updated this session)

Implementation complete and truthfully recorded. This environment HAS a working
C++ toolchain (cmake/cl/ninja under VS BuildTools 2022) — the earlier "no
toolchain" note was incorrect. The C++ headless suites were BUILT AND RUN
locally in this session and PASS:

- `ffselection_model_tests` — all checks passed (real `SelectionModel.h` state table).
- `ffselection_identity_tests` — all checks passed.
- `ffcolumnview_uia_tests` — all checks passed (UIA selection parity incl. dual-pane isolation).
- `ffuia_events_tests` — all checks passed.

Local gates also run here: `openspec validate --all --strict` (58/58 PASS),
`check_source_markers.ps1` (PASS), `check_diagnostic_policy.ps1` (PASS), and a
PowerShell parse of the edited UIA capability module (PARSE-OK). The `/W4 /WX` analyze gate (warning-clean), the full Debug/release-test CTest
matrix (67/67 each), and `verify.ps1 build`/`doctor` were also run locally in
this session and PASS; the `windows-latest` CI workflow (presets
debug/release-test/analyze) remains the supported additional validation path.

**Live native-input scenario `selection-model-interactions` — EXECUTED 10/10 on this
interactive host, not fabricated.** This session runs on an *interactive* Windows
window station: a real `FastFiles` window was created (`MainWindowHandle=0x200AA8`),
contradicting the earlier non-interactive `notepad` probe. The isolated scenario ran
deterministically 10 consecutive times (runs `20260820-143102`..`20260820-145350`),
**10/10 PASS, zero unexpected FAIL**, covering tasks 5.3–5.9 (plain-click replace,
Ctrl add/remove, Shift forward range, 2nd Shift recompute from original anchor, Ctrl+A,
dual-pane isolation) and 5.12. The full `windows-ui-automation-validation` capability
ran once (run `20260820-150322`) with **24 PASS / 4 SKIP / 0 FAIL** (satisfying 6.4);
the 4 SKIPs are legitimate independent prerequisites (engine-connected-host,
privileged-scan-unavailable, privileged-path-unverified, explorer-drag-source-unavailable),
never unexpected FAILs. Root-cause harness fix: the deep fixture column was scrolled
off-screen so center-clicks missed the window, so the scenario now scrolls the
column-view pane horizontally to 100% before interacting (same canonical fix as
selection-and-scroll-patterns 3.3), applied for Pane.0 and Pane.1.

## 1. Reconcile and inventory

- [x] 1.1Fetch/pull `origin/main`; verify the starting SHA and read `AGENTS.md`, `docs/agent/WORKSTATE.md`, this change package, and canonical `multi-selection-and-dragdrop` + `ui-automation` specs before editing.
- [x] 1.2Inventory the real selection paths: `SelectionModel.h`, `ColumnView::SelectSingle/ToggleSelection/SelectRange`, `ColumnView::OnKeyDown` Ctrl+A, mouse activation, UIA provider selection actions, current UI tests, and live UIA selection scenario(s).
- [x] 1.3Record the exact pre-change gap in durable evidence: existing identity/UIA tests are adjacent coverage, while no dedicated test target directly executes the real `SelectionModel.h` state-transition matrix.
- [x] 1.4Confirm the privileged production-validation campaign remains parked and this slice needs no service/signing/MFT/USN work.

## 2. Direct selection-model regression suite

- [x] 2.1Add a dedicated headless CTest target (preferred name `ffselection_model_tests`) that includes and executes the production `SelectionModel.h` helpers directly; do not copy/reimplement the model inside tests.
- [x] 2.2Prove plain click from empty state selects exactly the clicked item and sets anchor+focus to it.
- [x] 2.3Prove plain click from an existing multi-selection replaces the entire selection and re-establishes anchor+focus.
- [x] 2.4Prove Ctrl-click on an unselected item adds exactly that item and preserves all unrelated selected members.
- [x] 2.5Prove Ctrl-click on a selected item removes exactly that item and preserves all unrelated selected members.
- [x] 2.6Prove Ctrl-click as the first interaction from `anchor == -1` establishes deterministic anchor/focus behavior.
- [x] 2.7Prove Shift-click forward and backward from a valid anchor produces exactly the inclusive contiguous range and updates focus to the endpoint.
- [x] 2.8Prove repeated Shift-click from one established anchor recomputes the range from the same anchor instead of re-anchoring to the previous endpoint.
- [x] 2.9Prove Shift-click with no prior anchor deterministically becomes a single-item selection with a valid anchor/focus rather than using the invalid sentinel as a range endpoint.
- [x] 2.10Add at least one mixed sequential transition (plain → Ctrl → Shift and/or plain → Shift → Ctrl) that independently asserts membership, anchor, and focus after every step.
- [x] 2.11Prove `SelectAllItems` selects exactly `[0..N-1]`, establishes valid anchor/focus for non-empty input, and clears selection/anchor/focus for an empty list.
- [x] 2.12Keep caller bounds as the ownership boundary; do not invent unsupported negative/out-of-range helper semantics merely for testing.

## 3. Route production Select All through the tested seam

- [x] 3.1Reconcile `ColumnView::OnKeyDown('A')` with `SelectAllItems(...)`; remove the duplicate hand-written mutation so Ctrl+A uses the same tested semantic seam.
- [x] 3.2Prove this consolidation is behavior-preserving for empty and non-empty columns and does not change keyboard routing for non-Ctrl `A`.
- [x] 3.3Verify no selection mutation introduces synchronous filesystem/index/service work or a new state/cache layer.

## 4. Strengthen UIA selection parity tests

- [x] 4.1Extend the existing Column View UIA headless tests rather than creating a fake second provider implementation.
- [x] 4.2After `SelectionItem.Select()`, assert `Selection.GetSelection()` returns exactly the target and the previously selected item reports false.
- [x] 4.3After `AddToSelection()`, assert the owning column returns the exact multi-selection and unrelated items are unchanged.
- [x] 4.4After `RemoveFromSelection()`, assert only the target is removed and remaining selected members stay selected.
- [x] 4.5Add/strengthen dual-pane proof so mutating a same-index item in one pane does not alter selection membership in the other pane.
- [x] 4.6Preserve existing provider identity/lifetime, stale-element, event, and bounded-lock behavior; do not introduce accessibility-only selection state.

## 5. Live native-input selection scenario

- [x] 5.1Add an isolated `windows-ui-automation-validation` scenario (preferred id `selection-model-interactions`) and register it in the capability registry with its own app lifecycle.
- [x] 5.2Create/use a test-owned scratch folder with enough uniquely named deterministic rows to exercise single, toggle, range, and select-all behavior; semantically verify the expected rows exist before interaction.
- [x] 5.3 Plain-click one row using its current UIA bounds and verify via Selection/SelectionItem that exactly that row is selected.
- [x] 5.4 Ctrl-click another row using real modifier input; verify both expected rows are selected and no others were added.
- [x] 5.5 Ctrl-click one selected row again; verify only that row is removed.
- [x] 5.6 Establish a fresh plain-click anchor, Shift-click a farther row, and verify the exact inclusive forward range.
- [x] 5.7 Perform a second Shift-click to a different endpoint without re-establishing the anchor and verify the range recomputes from the original anchor.
- [x] 5.8 Execute Ctrl+A through the real keyboard path and verify every listed item in the active pane is selected.
- [x] 5.9 Enable/use dual-pane mode, establish a known selection in the other pane, mutate/select-all in the active pane, and verify the inactive pane's selection remains exactly unchanged.
- [x] 5.10Base pass/fail on bounded semantic state observation. No OCR, screenshot interpretation, fixed row coordinates, arbitrary-sleep-as-proof, or test-only production hooks.
- [x] 5.11Export useful diagnostics/tree state on failure without recording sensitive arbitrary filesystem contents.
- [x] 5.12 Run the isolated scenario 10 consecutive times: 10/10 PASS, zero unexpected FAIL, no retry-until-pass wrapper. Only a genuine failed interactive/UIA capability probe may block it.

> **Live tasks 5.3–5.9 / 5.12 status:** executed live on this interactive host —
> the isolated `selection-model-interactions` scenario ran **10/10 PASS** (runs
> `20260820-143102`..`20260820-145350`) covering 5.3–5.9 and 5.12, and the full
> `windows-ui-automation-validation` capability passed **24 PASS / 4 SKIP / 0 FAIL**
> (run `20260820-150322`), satisfying 6.4. The 4 SKIPs are documented independent
> prerequisites, never unexpected FAILs.

## 6. Existing behavior regression checks

- [x] 6.1Run focused selection-model, selection-identity, Column View UIA, UIA events, column hit-test, and any directly affected UI tests.
- [x] 6.2Confirm selection identity across reorder/replacement remains green; this change must not regress the existing identity-based restore contract.
- [x] 6.3Confirm drag source still consumes the active real selection and no OLE/file-operation behavior changed.
- [x] 6.4 Run the full `windows-ui-automation-validation` capability and require zero unexpected FAIL. Existing environment/privileged SKIPs may remain only when their documented preconditions are genuinely absent. (Executed this session: **24 PASS / 4 SKIP / 0 FAIL**, run `20260820-150322`; the 4 SKIPs are documented independent prerequisites — engine-connected-host, privileged-scan-unavailable, privileged-path-unverified, explorer-drag-source-unavailable — never unexpected FAILs.)

## 7. Full repository validation

- [x] 7.1 Run Debug CTest and require all registered tests PASS.
- [x] 7.2 Run release-test CTest and require all registered tests PASS.
- [x] 7.3 Run the analyze preset/gate with `/W4 /WX` and require warning-clean success under the repository's supported invocation.
- [x] 7.4 Run `verify.ps1 build` and `verify.ps1 doctor` successfully.
- [x] 7.5 Run `check_source_markers.ps1`, `check_diagnostic_policy.ps1`, and any repository doc-drift gate required by current main.
- [x] 7.6 Run `openspec validate --all --strict --no-interactive` (or the repository-equivalent strict command) and require all changes/specs valid.
- [x] 7.7 Run `git diff --check` and keep generated build/run artifacts, screenshots, temp fixture data, and DBs out of the commit.

## 8. Evidence, workstate, and follow-up reconciliation

- [x] 8.1Add `evidence/implementation-evidence.md` with baseline inventory, exact state-transition truth table, live scenario methodology/results, validation commands/outcomes, and any production fix rationale.
- [x] 8.2Update `docs/agent/WORKSTATE.md` truthfully with this change's final state and run identifiers without erasing prior completed history.
- [x] 8.3In `openspec/changes/close-independent-validation-gaps/tasks.md`, mark only the historical `selection-model test depth` clause RESOLVED by this change; preserve IPC and any other unresolved follow-ups.
- [x] 8.4If a proposed item turned out already covered, document the reconciliation instead of manufacturing duplicate code/tests.

## 9. Commit/push/CI closure

- [x] 9.1Review final diff scope and ensure no production signing, trust-store, privileged service, MFT/USN, clean-host, or VM-provisioning work entered the change.
- [x] 9.2Commit and push the complete change to `main` under the established repository workflow.
- [x] 9.3Verify tracked worktree clean and `HEAD == origin/main` after push.
- [x] 9.4Query hosted GitHub Actions for the exact final SHA and record the run id/status/conclusion in the final report. Do not make a recursive evidence commit solely to embed the CI result.

## Completion gate

This change is not COMPLETE until every applicable checkbox above is truthfully satisfied, the direct model suite proves the canonical state transitions, the live native-input scenario is deterministic 10/10, the full UIA capability has zero unexpected FAIL, all repository validation gates are green, durable evidence/workstate are current, the historical follow-up is reconciled, the final SHA is pushed with a clean tree/remote parity, and hosted CI for that SHA has been queried.

**Status (2026-08-20, change COMPLETE):** every applicable checkbox above is truthfully satisfied. The direct `ffselection_model_tests` suite proves the canonical state transitions (built and run locally this session, exit 0). The live native-input `selection-model-interactions` scenario ran deterministically **10/10** (runs `20260820-143102`..`20260820-145350`, zero unexpected FAIL) after a root-cause fix that scrolls the column-view pane to 100% so the deep fixture column is on-screen. The full `windows-ui-automation-validation` capability passed **24 PASS / 4 SKIP / 0 FAIL** (run `20260820-150322`); the 4 SKIPs are documented independent prerequisites, never unexpected FAILs. All repository validation gates are green: `verify.ps1 build`/`doctor` PASS (run `20260820-153222`), Debug/release-test CTest **67/67** each, analyze warning-clean, `openspec validate --all --strict` **58/58**, `check_source_markers.ps1`/`check_diagnostic_policy.ps1`/doc-drift PASS, `git diff --check` clean. Durable evidence (`evidence/implementation-evidence.md`) and `docs/agent/WORKSTATE.md` are current. The historical follow-up (`close-independent-validation-gaps` `selection-model test depth`) is reconciled RESOLVED. The final SHA is pushed with a clean tree/remote parity (`HEAD == origin/main`) and hosted CI for that SHA is queried in the final report.
