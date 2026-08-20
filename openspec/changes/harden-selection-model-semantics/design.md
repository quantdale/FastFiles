# Design — harden-selection-model-semantics

## Context

`src/ui/src/SelectionModel.h` contains the small, header-only mutation seam used by Column View selection: `ApplySelectionClick(...)` and `SelectAllItems(...)`. `ColumnView::SelectSingle`, `ToggleSelection`, and `SelectRange` already route through `ApplySelectionClick`. The current Ctrl+A branch in `ColumnView::OnKeyDown` duplicates the Select All mutation inline instead of calling `SelectAllItems`.

Current automated coverage is adjacent rather than direct:

- `ffselection_identity_tests` verifies identity-preserving restore after reorder/replacement, not click-modifier state transitions.
- `ffcolumnview_uia_tests` verifies Selection/SelectionItem provider behavior over `FakeUiaSurface`, not the real `SelectionModel` transition table.
- live `windows-ui-automation-validation` verifies selection-pattern discoverability/operation, but does not comprehensively drive real plain/Ctrl/Shift/Ctrl+A input against a deterministic fixture and assert the resulting membership/anchor semantics.

This change closes that exact proof gap without widening product scope.

## Decisions

### D1 — Canonical selection behavior is the oracle

The `multi-selection-and-dragdrop` canonical spec remains authoritative:

- plain click replaces selection with exactly the clicked item;
- Ctrl-click toggles only the clicked item's membership;
- Shift-click selects the contiguous visual range from the current anchor;
- repeated Shift-click recomputes from the same anchor;
- Ctrl+A selects all listed items in the active pane only;
- each pane retains independent selection state.

No new selection UX is invented here.

### D2 — Model state has three distinct concepts

Tests SHALL treat membership (`selectedIndices`), anchor (`selectionAnchor`), and focus (`focusIndex`) as distinct state. A transition is correct only if all applicable fields are correct, not merely if the visible selected count looks plausible.

Plain selection establishes the clicked item as both anchor and focus. Shift range updates focus to the clicked endpoint while preserving the existing anchor. When Shift is used with no valid prior anchor, behavior SHALL deterministically establish a single-item selection at the clicked item rather than constructing a range from an invalid sentinel. Ctrl toggles membership without disturbing unrelated selected items; focus follows the clicked item. Existing supported anchor behavior SHALL be preserved unless a canonical-spec contradiction is demonstrated.

### D3 — The pure helper is the deterministic unit seam

Add a dedicated headless CTest target that includes the real `SelectionModel.h` and exercises table-driven/sequential transitions. Do not create a duplicate model in tests. The suite SHALL cover both individual transitions and multi-step sequences so stale anchor/focus bugs are observable.

At minimum cover:

1. plain click from empty and from a multi-selection;
2. Ctrl add and Ctrl remove;
3. Ctrl as the first interaction when no anchor exists;
4. Shift forward and backward ranges;
5. repeated Shift-click from one stable anchor to different endpoints;
6. Shift with no prior anchor;
7. mixed plain → Ctrl → Shift sequences that distinguish membership from anchor/focus;
8. Select All for N>0 and N=0;
9. invariant checks that selected indices are exactly expected after every transition.

Production callers already bounds-check item indices; this change SHALL NOT invent an out-of-range contract inside the helper.

### D4 — Ctrl+A must share the tested semantic seam

The actual `ColumnView::OnKeyDown('A')` path SHALL call `SelectAllItems(...)` (or be refactored to an equally singular tested seam) rather than maintaining a second hand-written Select All implementation. This is a semantics-preserving consolidation, not a UX change.

### D5 — UIA remains observation/action parity, not a second model

Headless UIA provider tests SHALL continue to use the real provider interfaces and SHALL strengthen assertions that:

- `Selection.GetSelection()` returns exactly the currently selected items;
- `SelectionItem.Select()` replaces selection;
- `AddToSelection()` and `RemoveFromSelection()` mutate only their target;
- same-index items in another pane are unaffected;
- provider state after a mutation agrees with the surface's real selection state.

Do not add accessibility-only caches or state to make tests pass.

### D6 — Live validation must exercise native modifier input

Add an isolated `windows-ui-automation-validation` scenario, e.g. `selection-model-interactions`, using a test-owned scratch directory with deterministic, uniquely named rows. The capability SHALL launch/own the FastFiles lifecycle and navigate to the fixture.

Use real input for the interaction under test (mouse click plus modifier-key state and Ctrl+A keyboard input). Use UIA only to locate rows, obtain their current physical `BoundingRectangle`, and read semantic Selection/SelectionItem state after each action. No OCR, screenshot interpretation, fixed row coordinates, or test-only production hooks.

The scenario SHALL prove at least:

- plain click leaves exactly one expected item selected;
- Ctrl-click adds another item without clearing the first;
- Ctrl-click of a selected item removes only that item;
- plain click establishes an anchor, Shift-click selects a forward range, and a second Shift-click recomputes from that same anchor to a nearer/farther endpoint;
- Ctrl+A selects every listed item in the active pane;
- with dual-pane enabled, a pre-existing selection in the inactive pane survives Ctrl+A or selection mutations in the active pane.

Synchronization SHALL use bounded semantic waits/retries on selection state, not arbitrary sleep as the proof condition. Short UI-settling delays may exist in the driver, but acceptance is based on observed state before the deadline.

### D7 — Stable fixture/order, no accidental filesystem assumptions

Use simple unique names whose visual order is deterministic under the existing Column View ordering. The scenario SHALL assert/discover the expected rows before interaction and fail with diagnostics if the fixture is not represented as expected. It SHALL not rely on unrelated user files or a privileged index.

### D8 — Narrowest fix only

If the new headless/live matrix exposes a discrepancy, patch the smallest production seam necessary. Do not rewrite Column View selection architecture, introduce a new state container, or expand into drag/drop/file operations unless directly required by the canonical selection contract.

### D9 — Repetition is required for the live scenario

Run the isolated selection scenario 10 consecutive times with zero unexpected FAIL and no retry-until-pass wrapper. A genuine UIA/session capability block may be recorded only when the capability probe itself demonstrates the environment cannot provide interactive UIA; ordinary selection assertion failures are FAIL, not SKIP.

### D10 — Historical follow-up reconciliation

On completion, update the historical deferred note in `openspec/changes/close-independent-validation-gaps/tasks.md` so only the `selection-model test depth` clause is marked RESOLVED by this change. Preserve the IPC clause and any other unresolved entries truthfully; do not rewrite historical evidence.

### D11 — Validation and repository closure

Completion requires focused tests, full Debug + release-test CTest, analyze `/W4 /WX`, build/doctor, source-marker and diagnostic-policy checks, strict OpenSpec validation, `git diff --check`, the full UIA capability with zero unexpected FAIL, durable evidence/workstate, clean tracked worktree, `HEAD == origin/main`, and hosted CI queried for the exact final SHA.

### D12 — Privileged campaign stays parked

No production signing, trust-store mutation, service installation, privileged MFT/USN validation, clean-host matrix, or VM provisioning is part of this change.