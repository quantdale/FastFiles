# UIA repeatability evidence — 2026-08-23

The raw per-attempt UIA trees, screenshots, run logs, and foreground diagnostics are retained as local execution evidence under the ignored `verify/runs/phase7-certification/` directory. This committed summary intentionally contains no desktop screenshots or host-user paths.

| Scenario/group | Result | Evidence conclusion |
| --- | --- | --- |
| `treemap-readability-click-precision` isolated | 3/3 PASS | Proportional tiles, click precision, folder invoke, and no filesystem mutation all passed. |
| `treemap` group | PASS; 3 passed, 0 skipped, 0 failed | No unexplained failure after the missing-return harness fix. |
| `drag-between-panes` isolated after fix | 1/1 PASS | Pane ownership and Pane.1 refresh passed after live-provider reacquisition and semantic activation. |
| `drag` group | PASS; product drag-out PASS, Explorer-source drag SKIPPED, pane drag SKIPPED | Explorer synthetic OLE source and a competing host foreground are explicit environment boundaries; no blind drag assertion is claimed. |
| `selection-model-interactions` final isolated/group attempts | PASS capability result with scenario `SKIPPED/interactive-foreground-unavailable` | The host foreground (`browser-blackhole`/`clone-MC_v3`) could not be held for the pane transition; no selection assertion was attempted. Earlier failures retained diagnostics and led to the foreground guard, unassociated `.ffsel` fixtures, double-click guard, and pane/column/row focus projection fix. |
| full `windows-ui-automation-validation` via `verify.ps1` | PASS | Full repository capability completed with no unexplained FAIL result and cleaned all FastFiles processes. |

The registry no longer labels these scenarios `Flaky=true`; they use a fail-fast diagnostic wrapper (`FailFast=true`) with zero retries by default. A transient retry is available only through the explicit investigation option and never changes a failed assertion into a pass.

Root causes fixed in this campaign:

- stale ColumnView provider was used after the dual-pane fragment was replaced;
- palette/address input could be dispatched while another window owned the foreground;
- pane activation was inferred from a click instead of a live focus projection;
- selection checks ignored a failed anchor transition;
- repeated `.txt` synthetic clicks could launch Notepad and contaminate later runs;
- `Pane.HasKeyboardFocus` was treated as true even when a focused row correctly owned focus.
