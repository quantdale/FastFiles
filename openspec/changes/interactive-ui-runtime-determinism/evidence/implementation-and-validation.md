# Implementation and validation evidence

## Root-cause reproduction

The baseline command in `baseline-pane1-toggle.md` was rerun against the current
debug-diagnostic interactive build with opt-in `FASTFILES_UIA_DIAGNOSTICS=1`.
The palette contained exactly one `Toggle Dual Pane` result, that result was the
selected result, and the helper sent exactly one Enter. The product diagnostic
recorded:

```text
command=navigation.toggle-dual-pane
accepted=true
workspacePaneCountBefore=1
workspacePaneCountAfter=2
activePaneBefore=0
activePaneAfter=0
viewDualBefore=false
viewDualAfter=true
```

The first live UIA query sequence observed two direct ColumnView children:

```text
group-first-child: paneCount=2
pane-next-sibling: paneCount=2
pane-next-sibling-returned: paneCount=2
pane1-automation-id: paneCount=2
Pane 0: AutomationId=Pane.0
Pane 1: AutomationId=Pane.1
```

The equivalent recursive `Find-UiaElement -AutomationId Pane.1` operation still
timed out. The direct child walk succeeded because it re-read the current
ColumnView fragment; the recursive operation spent one short whole-tree budget
walking a fragment that was being replaced/requeried. This proves **Class H:
harness semantic-discovery defect** as the primary cause. It does not support a
product state-transition defect, command-dispatch defect, persistence
contamination, or product Pane.1 absence. The later activation failure is a
separate **Class I** condition: the host foreground was owned by the unrelated
interactive window `clone-MC_v3`, so Pane.1 keyboard focus could not be proven.

## Implemented boundaries

- `WindowShell::InvokeCommand` emits opt-in, bounded command-dispatch metadata;
  it records the stable command ID, acceptance, workspace pane counts, active
  pane, and ColumnView dual-pane state.
- `ColumnViewUiaProvider` now uses a fragment-generation owner token distinct
  from the long-lived root token. A structure replacement retires every old
  descendant; held old group/pane providers fail with
  `UIA_E_ELEMENTNOTAVAILABLE`, and a root walk creates fresh providers.
- `ColumnView` emits `StructureBulkAdded` on enable and `StructureBulkRemoved`
  on disable. The provider tests cover pane-local `SelectionItem` routing,
  distinct pane-scoped RuntimeIds, positive non-overlapping geometry, stale
  providers, fresh Pane.1 reacquisition, and 50 enable/disable replacement
  cycles.
- The UIA driver has direct-child and parent-reacquisition waits, semantic
  command palette result/selection/foreground/accepted-dispatch checks,
  RuntimeId capture, exact process identity checks, PID-reuse defense, bounded
  engine-pipe cleanup, and structured cleanup failure propagation.
- `Ensure-UiaDualPane` and `Ensure-UiaSinglePane` reacquire from the current
  main-window tree and require pane count, identity, RuntimeId, geometry, and
  bounded stability postconditions. Sending a command is not success.
- The surrounding live scenarios exposed three additional harness-owned
  variants of the same lifecycle problem and were hardened at their owning
  seams: missing-path status is found from the live terminal-column children,
  Search.Results rows are reacquired from the current result list instead of a
  cached fragment, and Explorer top-level windows are enumerated directly from
  the UIA root after repositioning. Drag-out now checks for a non-overlapping
  physical destination before sending input and reports a documented external
  layout skip when the desktop cannot provide one.

## Focused validation

All commands below were run from the repository checkout on the Windows
debug-diagnostic build:

| Check | Result |
| --- | --- |
| `ffnavigation_tests` | PASS |
| `ffcolumnview_uia_tests` | PASS, including 50 fragment cycles |
| `ffuia_events_tests` | PASS |
| `ffuia_driver_ps_tests` | PASS, including direct-child, RuntimeId, and process-ownership cases |
| `pwsh ./verify/uia-driver/tests/run-tests.ps1` | PASS, all recorded-tree and semantic-helper checks |
| `verify.ps1 run ... UiaScenario=selection-model-interactions` | exit 0 / overall PASS; later Pane.1 activation explicitly SKIPPED for foreign foreground |
| final direct selection scenario result | `2 passed, 1 skipped, 0 failed`; latest foreground was foreign `FocusTube`, so no pane-local input assertion ran |
| `verify.ps1 run -Capability windows-ui-automation-validation` | PASS; zero unexpected scenario failures; documented external skips only |
| `in-column-error-state-late` isolated live scenario | PASS after direct terminal-column status discovery |
| `search-allocated-size` isolated live scenario | PASS after live Search.Results reacquisition |
| `drag-out-to-explorer` isolated live scenario | PASS after direct Explorer-window rediscovery/layout hardening |
| Debug full CTest | PASS, 81/81 |
| release-test full CTest | PASS, 81/81 |
| Analyze build | PASS, warning-clean |
| ASAN protocol/IPC (`ffprotocol_fuzz_tests`, `ffipc_framing_tests`, `ffipc_listener_lifecycle_tests`) | PASS, 3/3 |
| `verify.ps1 build` / `verify.ps1 doctor` | PASS / PASS; doctor has only documented optional-tool skips |
| source markers / diagnostic policy / doc drift | PASS / PASS (0 failures) / PASS |
| `git diff --check` | PASS |
| `openspec validate --all --strict --no-interactive` | PASS, 67/67 items after final task/evidence edits |

The live selection scenario was not counted as a clean repeat: its wrapper
result has passing setup/isolation checks and one explicit external skip, with
no downstream input/selection assertion attempted while a foreign interactive
window owns the foreground (`FocusTube` in the latest run; `clone-MC_v3` in an
earlier run). The 20-consecutive-
run gate is therefore **not satisfied on this desktop** and must not be
represented as 20/20. The headless production/provider stress gate is 50/50
clean. The complete UIA capability is nevertheless PASS because its remaining
SKIPs are documented external prerequisites rather than unexpected failures.

## Timing, cleanup, and ownership audit

The final `UiAutomationValidation.psm1` audit found 107 `Start-Sleep` calls and
179 catch blocks (114 intentionally empty probe/fallback catches). The sleeps
remaining in the module are bounded polling cadence, native-input/key-repeat
settling, or Win32/Explorer layout settling; the dual-pane and single-pane
postconditions themselves use `Wait-UiaCondition` and fresh provider reads.
Palette activation no longer uses a fixed sleep as proof. Scenario teardown is
centralized through exact repository-build process identities, PID/start-time
checks, bounded exit waits, and named-pipe absence; installed copies,
services, and unrelated same-name processes are excluded. Optional UIA probes
retain catches because unavailable providers are an expected diagnostic branch,
while scenario/action failures return structured result reasons rather than
being swallowed.

## Boundaries deliberately not exercised

No certificates, trust stores, administrator execution, privileged service
qualification, raw MFT/USN validation, production signing, physical-display
matrix, guest/provider evidence, or production engine-lifetime change was
attempted. These remain external prerequisites under repository policy. The
live foreground/physical-layout skips are evidence boundaries, not product
passes manufactured by retrying or by replacing UIA with OCR/pixels.

## Subsequent completion evidence — 2026-08-29/30

The earlier selection-run limitation above was superseded by the corrected
isolated campaign recorded in
`openspec/changes/completion-integrity-hardening-and-release-certification/evidence/uia-search-20run-postfix.jsonl`:
`selection-model-interactions` completed 20/20 consecutive isolated lifecycles
with 2 PASS, 1 explicit environment SKIP, and 0 FAIL per lifecycle. The
current task ledger and lifecycle therefore treat task 7.2 as completed. The
remaining full foreground/physical-display limitations are retained as
external evidence boundaries and do not reopen this completed change.
