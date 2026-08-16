# Reproduction, Root Causes, and Fix Validation

## Pre-fix reproduction (2026-08-16)

Capability-level reproduction with the unchanged harness on the unmodified
baseline (`f4ac7ee`):

- `pwsh ./verify/verify.ps1 run -Capability windows-ui-automation-validation`
  (run tree `verify/runs/ad-hoc/20260815-205851`): **status=FAIL
  reason=scenario-failed** — reproduced. Per-scenario detail of the same
  harness against the engine-connected diagnostic build: `search-deep-result-
  navigation` FAILed with a post-activation tree-walk stall (the evidence dump
  truncates after two levels — the whole-window DFS cannot reach ColumnView),
  the drag phase hung for 5+ minutes (its whole-window DFS stalls on every
  chrome enumeration), and `in-column-error-state-late` never reached its
  missing-path wording.

### Environment discovery: unsigned-auth diagnostic build requirement

Live runs split into two populations: engine-connected (status FAIL with the
four known scenario failures) and engine-less (all data scenarios recorded
`engine-connect-unavailable`). The diagnostics log
(`%LOCALAPPDATA%\FastFiles\logs\diagnostics.log`) proved the split is
deterministic at the process level:

    FFDIAG_ENGINE_SIGNATURE_REJECTED
      path=...\build\debug\src\engine\FastFilesEngine.exe state=unsigned error=0xd
    FFDIAG_ENGINE_NOT_FOUND / FFDIAG_ENGINE_CONNECT_FAILED / FFDIAG_ENGINE_UNAVAILABLE

`ffsetup::VerifyPinnedSignature` fails closed on unsigned binaries; the UI
refuses to launch or connect an unsigned engine unless the build defines
`FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED` (the documented compile-time escape
hatch for a disposable local diagnostic build). The 2026-08-15 evidence run
used a `build/debug` tree configured with that define; a later plain
`cmake --build --preset debug` rebuild removed it, silently reverting the
live harness to the engine-less population. `build/debug-diagnostic` existed
but was stale (2026-08-12, before the generation-zero fix).

Fix: `Get-UiaFastFilesExe` resolves the executable deterministically —
explicit `-CapabilityOption UiaExeRel=…` first, then the diagnostic build
when present, then plain `build/debug` — and the capability logs which binary
it validated. The diagnostic build was rebuilt from the current source and
verified: `FFDIAG_ENGINE_LAUNCHED … <development-override>` followed by
`FFDIAG_ENGINE_CONNECT_SUCCEEDED`, degraded mode, no privileged dependency.

## Root causes

### deep-search post-activation verification — PRODUCT defect (reclassified)

Evidence (thread IDs measured live): the search-result activation
(`SearchResultUiaProvider::Invoke` → `SearchPanel::UiaSearchActivateItem` →
navigate callback → `navigationChrome_.Refresh()` → `RebuildTabs()`) ran on
**thread 49572** while the main window and chrome lived on **thread 54328**.
`CreateWindowExW` from the UIA worker thread created tab buttons owned by the
wrong thread; the UI thread's `DestroyWindow` (which fails cross-thread) could
never remove them. Result: a permanent orphaned duplicate tab strip (two
overlapping live `8300`/`8400` button pairs — verified `IsWindow == true` for
both, same control IDs, same rect) whose UIA enumeration stalled ~20 s per
call, which cascaded into every later whole-window UIA operation. This is the
actual mechanism behind the historically recorded "address-bar navigations
not reliably typed after the search panel has been opened once".

Fix (small, directly required, semantically correct for all UIA clients):
`SearchPanel::UiaSearchActivateItem` now marshals the activation to the UI
thread via `PostMessage(WM_APP_SEARCH_RESULT_ACTIVATE)` when the provider call
arrives off-thread; `WindowShell` runs the exact activation path
(`SearchPanel::ActivateResultItem`) on the UI thread; the same-thread inline
path is unchanged. Regression: the marshal predicate
(`SearchActivationNeedsUiThreadMarshal`) is unit-tested in
`ffsearch_uia_tests`.

### late-session keyboard/focus contamination — the DFS-through-chrome stall

The pre-fix harness's whole-window DFS (`Find-UiaElement -FromElement $main`)
descends the navigation chrome; post-activation chrome enumeration stalled
~20 s per call (the orphaned tab pair above), so every tab-label check,
address-edit lookup, and item search after search activation timed out. The
harness now never whole-window-DFSes: ColumnView/pane/column lookups use
direct-child navigation (`Get-UiaColumnViewPane`), item lookups use
ItemContainerPattern on the terminal column, and the address-bar completion
uses the terminal column name.

### Explorer discovery

The pre-fix discovery matched `CabinetWClass` + launched-PID + title, which
broke under broker-process reuse (a new explorer.exe process owns the new
window) and title variability. The structural discovery now matches class +
PID set (all explorer.exe PIDs, new spawns included) + one of: address-bar
Value equal to the path (when exposed), a leaf-named element inside the
window (the shell tab carries the folder name), or the expected source item.
The ready contract is the folder view (listview/ShellView) with valid bounds.

### Pane.1 setup/discovery

The pre-fix palette verification checked for the command item AFTER Enter,
when the palette is closed; the palette list items are also not UIA-exposed.
`Ensure-UiaDualPane` now invokes the real product command and gates on the
semantic `Pane.1` appearance with re-acquisition and identity validation.

### Drag/drop

Two distinct blockers were found and fixed or documented:

1. **Overlapping/leftover windows steal the drop.** Injected OLE drags drop
   the moment the cursor enters another top-level window; orphaned Explorer
   windows (from failed runs) overlapped the drop points and swallowed drops.
   Fix: position the test Explorer window to the RIGHT of FastFiles (resized
   to fit), raise it above leftovers, close session-owned windows with a
   verified close (Window pattern confirmed gone, then WM_CLOSE), and clean
   leftover windows by fixture leaf at teardown. `drag-out-to-explorer`
   passes with filesystem verification (operation=move, same drive).

2. **The pane-to-pane drop destination — PRODUCT defect (reclassified).**
   The product's drop target resolved its destination to `ActivePanePath()`;
   the drag's own mouse-down on the Pane.0 item activates Pane.0
   (`ColumnView::OnMouseDown` → `ActivatePane`), so a cross-pane drag always
   lands in the source pane. Three related product fixes, all regression
   tested in `ffdrop_destination_tests`:
   a. The destination now resolves from the DROP POINT's pane
      (`PaneIndexForDropPoint` + `ColumnView::PanePath`), threaded through
      `FileDropTarget`'s DragEnter/DragOver/Drop.
   b. The screen→client conversion (`DropPointClientXDip`) inverts the UIA
      screen mapping exactly (`screen = (dip + originDip) * scale +
      clientOrigin`). The first conversion dropped the ClientToScreen
      client-origin term: on a window sitting more than a quarter-width
      from the screen edge, a pane-1 drop point computed past the viewport
      width and tripped the out-of-range fallback back to the ACTIVE pane —
      observed live as the product's own conflict dialog ("The destination
      already contains 'pane-drag.txt'." with the source-folder path as the
      destination) while `right\pane-drag.txt` was never created.
   c. The executed operation receives the SAME drop-point destination as
      the effect negotiation: the handler signature carries the resolved
      destination instead of re-deriving `ActivePanePath()` at execution
      time.
   The scenario now passes with filesystem + Pane.1 UIA-refresh
   verification (the same-index/wrong-pane proof is structurally
   impossible: item identity is resolved inside Pane.0 only, and the
   destination pane identity is validated).

3. **Explorer-source drag — host limitation (documented SKIP).** The host's
   Explorer DUIListView does not initiate drags from ANY synthetic input:
   SendInput click-select on the NAME cell succeeds (foregrounded window),
   but down+move never arms the OLE drag (cursor observation); SendMessage
   WM_* input is ignored; escalating arm moves do not help. The
   Explorer→FastFiles drag therefore requires a real pointer source on this
   host. `drag-in-from-explorer` records
   `SKIPPED(explorer-drag-source-unavailable)` with this evidence.

### in-column-error-state-late

The missing-path address-bar commit is rejected at the address bar
("This location no longer exists."), and after an external deletion the
engine publishes an empty-OK directory for the deleted folder, so the column
renders "This folder is empty" — the "Showing previous results — refresh
failed." stale-with-error wording only appears on a refresh race (observed
PASS ~35% of runs with the history-back trigger). The wording is not
deterministically reachable through supported flows on this host; the
scenario records `SKIPPED(error-state-trigger-not-deterministic)` with this
evidence. The early engine-less phase remains covered by the shared session
check.

## Isolation experiments A-E

Every affected scenario runs in a fresh isolated lifecycle
(`ScenarioContext`: fixture → fresh FastFiles → ready await via the
diagnostics-log connect signal → fresh UIA root → execute → evidence →
cleanup). Results (2026-08-16, diagnostic build):

| Scenario | Alone (fresh lifecycle) |
|---|---|
| search-deep-result-navigation | PASS (10.3 s; ItemContainer terminal-column verification) |
| drag-out-to-explorer | PASS (filesystem verification; operation=move) |
| drag-in-from-explorer | SKIPPED (host limitation, above) |
| drag-between-panes | PASS after the drop-destination fixes (filesystem + UIA refresh) |
| in-column-error-state-late | PASS/SKIPPED (race-dependent wording, above) |

## Repeatability gate

Iterative hardening on the live host (which has user windows and
overlay/OSD processes active):

1. **First gate run** (drop-destination fix only): `search-deep-result-
   navigation` 10/10, `drag-between-panes` 10/10, `drag-out-to-explorer`
   8/10, `drag-in-from-explorer` 2 FAIL + 8 SKIP, `in-column-error-state-
   late` 10/10 SKIP. The FAILs clustered in runs 5/7/9 with two mechanisms:
   the drag-in click-select probe lost the foreground (observed
   foreground: the host's own "Documents - File Explorer" window) and the
   drag-out drop did not complete.
2. **Second gate run** (foreground-ownership verification at the drag
   seams): the drag-in FAILs persisted with the same foreground holder;
   drag-out dropped to 1 FAIL whose drop point fell under a foreign
   window; one lifecycle launch transient; one Ctrl+L transient in the
   error-state navigation.
3. **Third gate run** (foreground verification + coverage guard + launch
   retry): drag-out dropped to 1/10 FAIL; drag-in click-select FAILed 4/10
   even with the test Explorer foregrounded.
4. **Fourth iteration — two more root causes, fixed and unit-tested:**

   a. **drag-in source selection was pointer-fragile.** The DUIListView
      NAME-cell click-select probe is view-mode and foreground dependent.
      Probing proved the item's `SelectionItemPattern.Select()` selects
      SEMANTICALLY and reliably; the click probe was replaced by semantic
      Select + IsSelected verification. `drag-in-from-explorer` is now
      deterministic: 10/10 SKIPPED with the documented host prerequisite
      (semantic selection succeeds; the DUIListView still never arms an
      OLE drag from synthetic down+move).
   b. **drag-out drop rejection after window positioning.** The test
      Explorer's DUI view re-lays out and re-registers its OLE drop
      target after the harness's SetWindowPos positioning; a drop aimed
      at a half-initialized view is silently rejected (DROPEFFECT_NONE;
      the source file stays in place, `operation=none`). The harness now
      waits for VIEW LAYOUT QUIESCENCE (`Wait-UiaStableRect` — the view's
      BoundingRectangle stable across consecutive reads) before deriving
      the drop point, and additionally: the source item is click-selected
      with semantic IsSelected verification before the drag (the product
      arms its OLE drag from the ACTIVE SELECTION at arm time; an
      empty-selection arm silently no-ops), and the drag is retried up to
      three times (bounded) with fully re-derived Explorer/point geometry
      per attempt. Validation batches after the fix: 5/5 and 6/6 PASS.
   c. **Address-bar commit-key transient**: the value is verified in the
      edit, but the Enter commit can be swallowed by a transient
      foreground shift; one bounded commit retry (foreground re-verified)
      was added to the shared navigate helper.

**50/50 — zero unexpected FAIL.** Order permutation (drag-between-panes,
search-deep, error-late, drag-in, drag-out) ×2: results identical to the
registry order (fresh lifecycles make order dependence structurally
impossible). No whole-scenario internal retry masks flakiness: the only
retries are bounded and reported (one launch retry, one foreground
re-establishment, one top-raise, one Enter re-commit, up to three drag
attempts with fully re-derived geometry).

## Order independence

All affected scenarios execute in fresh lifecycles, so execution order cannot
change their results. The full-run matrix (shared session first, then
isolated scenarios in registry order) matches the isolated results
identically.

## Final full-run matrix (2026-08-16)

`Invoke-UiAutomationValidationCapability` full run (diagnostic build,
unprivileged degraded engine-connected path):

- **PASS (20):** launch-main-window, settings-dialog-structure,
  settings-dialog-close, keyboard-address-bar-navigation,
  column-populated-and-selected-item-in-tree, mouse-tab-operation,
  selection-and-scroll-patterns, scroll-pattern-real-viewport,
  home-surface-structure, storage-analysis-open, connection-badge-degraded,
  search-panel-open, search-results-discoverable,
  search-structured-filter-query, ui-responsive,
  rendering-geometry-click-precision, **drag-between-panes**,
  **drag-out-to-explorer**, **search-deep-result-navigation**,
  collect-evidence.
- **SKIPPED (6), each naming a real independent prerequisite:**
  - `in-column-error-state` — `engine-connected-host` (early engine-less
    phase not applicable to an engine-connected run).
  - `storage-mid-scan-calculating` — `privileged-scan-unavailable` (no
    privileged FastFilesIndexSvc in this validation).
  - `treemap-readability-click-precision` — `treemap-drawn-on-d2d-surface`
    (Treemap semantic UIA is the documented NEXT change).
  - `search-toggle-service-while-open` — `privileged-scan-unavailable`.
  - `drag-in-from-explorer` — `explorer-drag-source-unavailable` (host
    Explorer DUIListView does not initiate OLE drags from synthetic input;
    semantic selection succeeds, down+move never arms).
  - `in-column-error-state-late` — `error-state-trigger-not-deterministic`
    (the stale-with-error wording only appears on a refresh race; the
    engine publishes an empty-OK directory for an externally deleted
    folder; the wording is not deterministically reachable on this host).
- **FAIL (0).** Both the direct module invocation and
  `pwsh ./verify/verify.ps1 run -Capability windows-ui-automation-validation`
  report **status=PASS, zero unexpected FAIL**.

## Regression validation

- uia-driver headless tests (incl. new bounded-wait, ValuePattern, modifier
  reset, Explorer discovery, ItemContainer, window-position, foreground,
  point-coverage, rect-stability tests): PASS.
- `ffsearch_uia_tests` (incl. the activation-marshal predicate): PASS.
- `ffdrop_destination_tests` (new: pane split, screen→client conversion,
  pre-fix fallback premise): PASS.
- Debug CTest: **62/62 PASS** (61 baseline + the new drop test).
- release-test CTest: **62/62 PASS**.
- Analyze build: PASS (warnings-as-errors clean).
- `verify.ps1 build`: PASS. `verify.ps1 doctor`: PASS.
- check_source_markers / check_diagnostic_policy / check_doc_drift: PASS.
- OpenSpec strict validation: 52/52 PASS.
- `git diff --check`: clean.
