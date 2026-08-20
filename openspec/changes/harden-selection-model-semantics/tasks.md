# Tasks — harden-selection-model-semantics

## 1. Reconcile and inventory

- [ ] 1.1 Fetch/pull `origin/main`; verify the starting SHA and read `AGENTS.md`, `docs/agent/WORKSTATE.md`, this change package, and canonical `multi-selection-and-dragdrop` + `ui-automation` specs before editing.
- [ ] 1.2 Inventory the real selection paths: `SelectionModel.h`, `ColumnView::SelectSingle/ToggleSelection/SelectRange`, `ColumnView::OnKeyDown` Ctrl+A, mouse activation, UIA provider selection actions, current UI tests, and live UIA selection scenario(s).
- [ ] 1.3 Record the exact pre-change gap in durable evidence: existing identity/UIA tests are adjacent coverage, while no dedicated test target directly executes the real `SelectionModel.h` state-transition matrix.
- [ ] 1.4 Confirm the privileged production-validation campaign remains parked and this slice needs no service/signing/MFT/USN work.

## 2. Direct selection-model regression suite

- [ ] 2.1 Add a dedicated headless CTest target (preferred name `ffselection_model_tests`) that includes and executes the production `SelectionModel.h` helpers directly; do not copy/reimplement the model inside tests.
- [ ] 2.2 Prove plain click from empty state selects exactly the clicked item and sets anchor+focus to it.
- [ ] 2.3 Prove plain click from an existing multi-selection replaces the entire selection and re-establishes anchor+focus.
- [ ] 2.4 Prove Ctrl-click on an unselected item adds exactly that item and preserves all unrelated selected members.
- [ ] 2.5 Prove Ctrl-click on a selected item removes exactly that item and preserves all unrelated selected members.
- [ ] 2.6 Prove Ctrl-click as the first interaction from `anchor == -1` establishes deterministic anchor/focus behavior.
- [ ] 2.7 Prove Shift-click forward and backward from a valid anchor produces exactly the inclusive contiguous range and updates focus to the endpoint.
- [ ] 2.8 Prove repeated Shift-click from one established anchor recomputes the range from the same anchor instead of re-anchoring to the previous endpoint.
- [ ] 2.9 Prove Shift-click with no prior anchor deterministically becomes a single-item selection with a valid anchor/focus rather than using the invalid sentinel as a range endpoint.
- [ ] 2.10 Add at least one mixed sequential transition (plain → Ctrl → Shift and/or plain → Shift → Ctrl) that independently asserts membership, anchor, and focus after every step.
- [ ] 2.11 Prove `SelectAllItems` selects exactly `[0..N-1]`, establishes valid anchor/focus for non-empty input, and clears selection/anchor/focus for an empty list.
- [ ] 2.12 Keep caller bounds as the ownership boundary; do not invent unsupported negative/out-of-range helper semantics merely for testing.

## 3. Route production Select All through the tested seam

- [ ] 3.1 Reconcile `ColumnView::OnKeyDown('A')` with `SelectAllItems(...)`; remove the duplicate hand-written mutation so Ctrl+A uses the same tested semantic seam.
- [ ] 3.2 Prove this consolidation is behavior-preserving for empty and non-empty columns and does not change keyboard routing for non-Ctrl `A`.
- [ ] 3.3 Verify no selection mutation introduces synchronous filesystem/index/service work or a new state/cache layer.

## 4. Strengthen UIA selection parity tests

- [ ] 4.1 Extend the existing Column View UIA headless tests rather than creating a fake second provider implementation.
- [ ] 4.2 After `SelectionItem.Select()`, assert `Selection.GetSelection()` returns exactly the target and the previously selected item reports false.
- [ ] 4.3 After `AddToSelection()`, assert the owning column returns the exact multi-selection and unrelated items are unchanged.
- [ ] 4.4 After `RemoveFromSelection()`, assert only the target is removed and remaining selected members stay selected.
- [ ] 4.5 Add/strengthen dual-pane proof so mutating a same-index item in one pane does not alter selection membership in the other pane.
- [ ] 4.6 Preserve existing provider identity/lifetime, stale-element, event, and bounded-lock behavior; do not introduce accessibility-only selection state.

## 5. Live native-input selection scenario

- [ ] 5.1 Add an isolated `windows-ui-automation-validation` scenario (preferred id `selection-model-interactions`) and register it in the capability registry with its own app lifecycle.
- [ ] 5.2 Create/use a test-owned scratch folder with enough uniquely named deterministic rows to exercise single, toggle, range, and select-all behavior; semantically verify the expected rows exist before interaction.
- [ ] 5.3 Plain-click one row using its current UIA bounds and verify via Selection/SelectionItem that exactly that row is selected.
- [ ] 5.4 Ctrl-click another row using real modifier input; verify both expected rows are selected and no others were added.
- [ ] 5.5 Ctrl-click one selected row again; verify only that row is removed.
- [ ] 5.6 Establish a fresh plain-click anchor, Shift-click a farther row, and verify the exact inclusive forward range.
- [ ] 5.7 Perform a second Shift-click to a different endpoint without re-establishing the anchor and verify the range recomputes from the original anchor.
- [ ] 5.8 Execute Ctrl+A through the real keyboard path and verify every listed item in the active pane is selected.
- [ ] 5.9 Enable/use dual-pane mode, establish a known selection in the other pane, mutate/select-all in the active pane, and verify the inactive pane's selection remains exactly unchanged.
- [ ] 5.10 Base pass/fail on bounded semantic state observation. No OCR, screenshot interpretation, fixed row coordinates, arbitrary-sleep-as-proof, or test-only production hooks.
- [ ] 5.11 Export useful diagnostics/tree state on failure without recording sensitive arbitrary filesystem contents.
- [ ] 5.12 Run the isolated scenario 10 consecutive times: 10/10 PASS, zero unexpected FAIL, no retry-until-pass wrapper. Only a genuine failed interactive/UIA capability probe may block it.

## 6. Existing behavior regression checks

- [ ] 6.1 Run focused selection-model, selection-identity, Column View UIA, UIA events, column hit-test, and any directly affected UI tests.
- [ ] 6.2 Confirm selection identity across reorder/replacement remains green; this change must not regress the existing identity-based restore contract.
- [ ] 6.3 Confirm drag source still consumes the active real selection and no OLE/file-operation behavior changed.
- [ ] 6.4 Run the full `windows-ui-automation-validation` capability and require zero unexpected FAIL. Existing environment/privileged SKIPs may remain only when their documented preconditions are genuinely absent.

## 7. Full repository validation

- [ ] 7.1 Run Debug CTest and require all registered tests PASS.
- [ ] 7.2 Run release-test CTest and require all registered tests PASS.
- [ ] 7.3 Run the analyze preset/gate with `/W4 /WX` and require warning-clean success under the repository's supported invocation.
- [ ] 7.4 Run `verify.ps1 build` and `verify.ps1 doctor` successfully.
- [ ] 7.5 Run `check_source_markers.ps1`, `check_diagnostic_policy.ps1`, and any repository doc-drift gate required by current main.
- [ ] 7.6 Run `openspec validate --all --strict --no-interactive` (or the repository-equivalent strict command) and require all changes/specs valid.
- [ ] 7.7 Run `git diff --check` and keep generated build/run artifacts, screenshots, temp fixture data, and DBs out of the commit.

## 8. Evidence, workstate, and follow-up reconciliation

- [ ] 8.1 Add `evidence/implementation-evidence.md` with baseline inventory, exact state-transition truth table, live scenario methodology/results, validation commands/outcomes, and any production fix rationale.
- [ ] 8.2 Update `docs/agent/WORKSTATE.md` truthfully with this change's final state and run identifiers without erasing prior completed history.
- [ ] 8.3 In `openspec/changes/close-independent-validation-gaps/tasks.md`, mark only the historical `selection-model test depth` clause RESOLVED by this change; preserve IPC and any other unresolved follow-ups.
- [ ] 8.4 If a proposed item turned out already covered, document the reconciliation instead of manufacturing duplicate code/tests.

## 9. Commit/push/CI closure

- [ ] 9.1 Review final diff scope and ensure no production signing, trust-store, privileged service, MFT/USN, clean-host, or VM-provisioning work entered the change.
- [ ] 9.2 Commit and push the complete change to `main` under the established repository workflow.
- [ ] 9.3 Verify tracked worktree clean and `HEAD == origin/main` after push.
- [ ] 9.4 Query hosted GitHub Actions for the exact final SHA and record the run id/status/conclusion in the final report. Do not make a recursive evidence commit solely to embed the CI result.

## Completion gate

This change is not COMPLETE until every applicable checkbox above is truthfully satisfied, the direct model suite proves the canonical state transitions, the live native-input scenario is deterministic 10/10, the full UIA capability has zero unexpected FAIL, all repository validation gates are green, durable evidence/workstate are current, the historical follow-up is reconciled, the final SHA is pushed with a clean tree/remote parity, and hosted CI for that SHA has been queried.