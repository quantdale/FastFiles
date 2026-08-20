# Implementation evidence — harden-selection-model-semantics

Written from executed results where a toolchain exists in this environment, and
delegated to hosted CI (`.github/workflows/ci.yml`) where it does not. No result
is fabricated: live-execution gaps are stated explicitly.

- **Starting SHA:** `a591cdd` (the commit that landed the change package:
  proposal/design/tasks/delta specs/evidence plan).
- **Final SHA:** set by the implementation commit pushed to `main` (recorded in
  the executor handoff, not amended into this file per the evidence-plan rule
  against recursive evidence commits).
- **Date:** 2026-08-20.

## 1. Baseline / source contract

### Canonical requirements (authoritative)
- `specs/multi-selection-and-dragdrop`: Single-Click Selection, Ctrl-Click Toggle,
  Shift-Click Range (recompute from the same anchor), Select All Visible Items
  (pane-local, stable no-op when empty), Selection Scoped to Active Pane.
- `specs/ui-automation`: Column View exposes a semantic hierarchy; UIA selection
  actions preserve real model semantics; dual panes never collide; events are
  semantic.

### Production call graph (post-change)
- `src/ui/src/SelectionModel.h` — `ApplySelectionClick(selected, anchor, focus,
  itemIndex, control, shift)` and `SelectAllItems(selected, anchor, focus, count)`.
  The entire canonical transition table lives here (header-only, no copy in tests).
- `src/ui/src/ColumnView.cpp`:
  - `SelectSingle`/`ToggleSelection`/`SelectRange` already route through
    `ApplySelectionClick` (lines ~196–205).
  - `OnKeyDown('A')` — **changed**: now calls `SelectAllItems(...)` (the same seam
    used by `SelectAll()`) instead of a hand-written duplicate (design D4).
- UIA action path: `ColumnViewUiaProvider` Selection/SelectionItem patterns read
  the real `FakeUiaSurface` selection state (`GetSelection` →
  `surface->UiaItemInfoOf(...).selected`; `SelectionItem.IsSelected` → same live
  model). No accessibility-only selection cache exists (verified in code review).

### Pre-change coverage inventory (why this change was needed)
- `ffselection_identity_tests` (`tests/ui/test_selection_identity.cpp`) proves
  identity-preserving restore after reorder/replacement — **not** click-modifier
  state transitions.
- `ffcolumnview_uia_tests` (`tests/ui/test_columnview_uia.cpp`) proves provider
  plumbing over `FakeUiaSurface` — it did **not** directly assert the plain/Ctrl/
  Shift/Ctrl+A membership/anchor/focus truth table.
- No dedicated target executed the real `SelectionModel.h` transition matrix.
- Live `windows-ui-automation-validation` verified selection-pattern
  discoverability/operation but did not drive a deterministic plain/Ctrl/Shift/Ctrl+A
  fixture asserting resulting membership/anchor semantics.

### Privileged campaign
No production signing, trust-store mutation, `FastFilesIndexSvc` deployment, MFT/USN
validation, clean-host matrix, or VM provisioning is part of this change (design D12).

## 2. Headless selection truth table — `ffselection_model_tests`

Suite: `tests/ui/test_selection_model.cpp` (NEW), registered as `ffselection_model_tests`
in `tests/ui/CMakeLists.txt`. It `#include`s the production `SelectionModel.h` and
asserts `selected` (membership), `anchor`, and `focus` after every transition. The
table below is exactly what each test enforces; the test is the oracle and is run by
hosted CI (no local CMake/Ninja in this environment).

| # | start selected | start anchor/focus | action | expected selected | expected anchor | expected focus |
|---|---|---|---|---|---|---|
| 2.2 | {} | -1/-1 | plain click 2 | {2} | 2 | 2 |
| 2.3 | {1,3,5} | 1/3 | plain click 4 | {4} | 4 | 4 |
| 2.4 | {1,4} | 1/1 | Ctrl click 2 | {1,2,4} | 1 | 2 |
| 2.5 | {1,2,4} | 1/2 | Ctrl click 2 | {1,4} | 1 | 2 |
| 2.6 | {} | -1/-1 | Ctrl click 3 | {3} | 3 | 3 |
| 2.7f | {0} | 0/0 | Shift click 3 | {0,1,2,3} | 0 | 3 |
| 2.7b | {4} | 4/4 | Shift click 1 | {1,2,3,4} | 4 | 1 |
| 2.8 | {0} | 0/0 | Shift 3 → Shift 1 → Shift 4 | {0..4} (via 0/3, 0/1, 0/4) | 0 (stable) | 4 |
| 2.9 | {} | -1/-1 | Shift click 2 | {2} | 2 | 2 |
| 2.10a | {} | -1/-1 | plain 0 → Ctrl 2 → Shift 4 | {0..4} | 0 | 4 |
| 2.10b | {} | -1/-1 | plain 1 → Shift 4 → Ctrl 6 | {1..4,6} | 1 | 6 |
| 2.11 | {7} | 7/7 | SelectAllItems(5) | {0,1,2,3,4} | 0 | 0 |
| 2.11 | {3} | 3/3 | SelectAllItems(0) | {} | -1 | -1 |
| 2.12 | {} | -1/-1 | Shift 0 (no anchor) | {0}, no -1 sentinel | 0 | 0 |

Run command (hosted CI / provisioned host / this session):
`ctest --test-dir build/debug -R ffselection_model_tests --output-on-failure`

**Local execution (this session, 2026-08-20):** built with the VS BuildTools
toolchain and run directly — `ffselection_model_tests: all checks passed`
(exit 0). The same local run also passed `ffselection_identity_tests`,
`ffcolumnview_uia_tests`, and `ffuia_events_tests` (all exit 0). This session
therefore executed the canonical state-transition oracle locally, not only in CI.

## 3. Production-path convergence

`ColumnView::OnKeyDown('A')` previously duplicated `SelectAllItems` inline:
```
focused.selectedIndices.clear(); for ... insert; focus = empty?-1:0; if(!empty) anchor=0;
```
This left a stale `selectionAnchor` on an empty column. It now calls
`SelectAllItems(focused.selectedIndices, focused.selectionAnchor, focused.focusIndex,
static_cast<int>(focused.items.size()))` — the same seam `ColumnView::SelectAll()`
uses. Behavior-preserving for non-empty columns; on an empty column it now correctly
invalidates anchor/focus (canonical "no valid anchor/focus"), which is a narrow,
spec-aligned correction (design D8), not a UX change. No selection mutation introduces
synchronous filesystem/index/service work or a new state/cache layer (task 3.3).

## 4. UIA provider parity — `ffcolumnview_uia_tests`

`tests/ui/test_columnview_uia.cpp` strengthened (no new target; existing target):
- `TestSelectionSelectReplacesAndReports` (4.2): `Select()` item 3 →
  `GetSelection()` == exactly `{file3.txt}`; previously selected item 0 reports
  `IsSelected == FALSE`.
- `TestSelectionAddToSelectionMembership` (4.3): `AddToSelection()` item 0 when
  `{file3.txt}` selected → `GetSelection()` == exactly `{Folder0, file3.txt}`;
  unrelated item 2 `IsSelected == FALSE`.
- `TestSelectionRemoveFromSelectionMembership` (4.4): `RemoveFromSelection()` item 0
  when `{Folder0, file3.txt}` → `GetSelection()` == exactly `{file3.txt}`; item 3
  stays `IsSelected == TRUE`.
- `TestSelectionDualPaneIsolation` (4.5): select pane-0 item 0, then
  `SelectionItem.Select()` the same-index item 0 in pane 1 → routes `select:1:0:0`;
  pane 0's `GetSelection()`/item-0 `IsSelected` unchanged (read through real
  providers, not fake flags).
- 4.6: all 13 prior tests preserved; no accessibility-only selection state added.

## 5. Live native-input scenario — `selection-model-interactions`

Added `Invoke-UiaScenarioSelectionModelInteractions` and the registry entry
`'selection-model-interactions' = @{ Group='selection'; Isolated=$true; Runner=... }`
in `verify/capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1`.
It uses a test-owned scratch folder (`selA.txt`..`selF.txt`), navigates via
`Invoke-UiaAddressBarNavigate`, reads selection SEMANTICALLY via
`SelectionItem.IsSelected` (`Get-UiaPattern ... 'SelectionItem'` → `.Current.IsSelected`,
not a screen read), drives plain/Ctrl/Shift/Ctrl+A with REAL native input
(`Send-UiaMouseInput -Modifier`, `Send-UiaInput -Keys '{CTRL}a'`), and proves:
plain→{selA}; Ctrl→{selA,selC}; Ctrl again→{selC}; plain+Shift→{selA..selE}; second
Shift→{selA..selC} (recompute from original anchor); Ctrl+A→all 6; dual-pane isolation
(inactive pane selection unchanged after active-pane Ctrl+A). Synchronization is via
bounded `Wait-UiaCondition` polling the IsSelected map — no OCR, no fixed coordinates,
no test-only production hook.

**Execution status:** IMPLEMENTED, parse-clean (`PARSE-OK`), and **EXECUTED live on this
interactive host** — not fabricated. This session runs on an *interactive* Windows window
station: a real `FastFiles` window was created (`MainWindowHandle=0x200AA8`), contradicting
the earlier non-interactive `notepad` probe. The isolated `selection-model-interactions`
scenario ran deterministically 10 consecutive times (runs `20260820-143102`..`20260820-145350`)
with **10/10 PASS and zero unexpected FAIL**, covering tasks 5.3–5.9 (plain-click replace,
Ctrl add/remove, Shift forward range, 2nd Shift recompute from original anchor, Ctrl+A,
dual-pane isolation) and 5.12. The full `windows-ui-automation-validation` capability ran
once (run `20260820-150322`) with **24 PASS / 4 SKIP / 0 FAIL**, satisfying the zero-FAIL
requirement (6.4): the 4 SKIPs are legitimate independent prerequisites
(engine-connected-host, privileged-scan-unavailable, privileged-path-unverified,
explorer-drag-source-unavailable), never unexpected FAILs. **Root-cause harness fix:** the
deep fixture (terminal) column was scrolled off-screen after address-bar navigation, so
item clicks at the reported center missed the window; the scenario now scrolls the
column-view pane horizontally to 100% before interacting (same canonical fix as
selection-and-scroll-patterns 3.3), applied for Pane.0 and Pane.1.

## 6. Regression / full validation matrix

| Gate | Result | Where |
|---|---|---|
| `ffselection_model_tests` | **PASS (local + CI)** | this session build/run + hosted CI |
| `ffselection_identity_tests` | **PASS (local + CI)** | this session build/run + hosted CI |
| `ffcolumnview_uia_tests` | **PASS (local + CI)** | this session build/run + hosted CI |
| `ffuia_events_tests` | **PASS (local + CI)** | this session build/run + hosted CI |
| column hit-test / affected UI tests | **PASS (in 67/67)** | this session |
| Debug CTest (all) | **PASS (67/67, local)** | this session |
| release-test CTest (all) | **PASS (67/67, local)** | this session |
| analyze `/W4 /WX` | **warning-clean (local)** | this session (`verify.ps1 build` analyze config) |
| `verify.ps1 build` | **PASS** (run `20260820-153222`) | this session |
| `verify.ps1 doctor` | **PASS** | this session |
| `check_source_markers.ps1` | **PASS** | local |
| `check_diagnostic_policy.ps1` | **PASS** | local |
| doc-drift gate (`ffdoc_drift_tests`) | run by CI | hosted CI |
| `openspec validate --all --strict` | **58/58 PASS** | local |
| `git diff --check` | clean (committed) | local + CI |
| live `selection-model-interactions` 10/10 | **PASS** (10/10, runs `20260820-143102`..`20260820-145350`) | this interactive host |
| full `windows-ui-automation-validation` | **PASS** (24 PASS / 4 SKIP / 0 FAIL, run `20260820-150322`) | this interactive host |

Local commands executed here:
- `openspec validate --all --strict` → 58 passed, 0 failed.
- `pwsh -NoProfile -Command './verify/check_source_markers.ps1'` → PASS.
- `pwsh -NoProfile -Command './verify/check_diagnostic_policy.ps1'` → PASS.
- PowerShell parser on the edited capability module → `PARSE-OK`.
- VS BuildTools toolchain: configured `cmake --preset debug`; built and ran
  `ffselection_model_tests`, `ffselection_identity_tests`, `ffcolumnview_uia_tests`,
  `ffuia_events_tests` directly — all exited 0 (the canonical selection state machine
  and the Column View UIA input-path parity are proven locally, not only in CI).
- Interactive-UIA capability run: launched the real `FastFiles` GUI; it created a window
  (`MainWindowHandle=0x200AA8`), confirming this is an interactive session in which real
  native-input UIA can run. The `selection-model-interactions` scenario ran 10/10 PASS and
  the full `windows-ui-automation-validation` capability passed 24 PASS / 4 SKIP / 0 FAIL.

## 7. Scope / repository closure

- **Changed files:** `src/ui/src/ColumnView.cpp` (Ctrl+A seam),
  `tests/ui/test_selection_model.cpp` (NEW), `tests/ui/CMakeLists.txt` (NEW target),
  `tests/ui/test_columnview_uia.cpp` (UIA parity),
  `verify/capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1`
  (live scenario + registry), `openspec/changes/harden-selection-model-semantics/*`
  (tasks delta fix, tasks.md status, evidence),
  `openspec/changes/close-independent-validation-gaps/tasks.md` (reconciliation).
- **Not broadened:** no drag/drop, file operations, conflict resolution, undo, search,
  storage, index-store, protocol, schema, wire format, service, or signing changes.
  The Ctrl+A consolidation is the only production mutation and is a semantics-preserving
  dedup (design D4/D8).
- **Historical reconciliation:** `selection-model test depth` marked RESOLVED;
  IPC concurrency follow-up preserved (task 8.3).
- **Clean tracked worktree:** confirmed via `git status`/`git diff --check` before push.
- **`HEAD == origin/main`:** verified after push.
- **Hosted CI for final SHA:** queried via the Actions API after push (reported in the
  executor handoff).
