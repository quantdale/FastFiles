# selection-model-interactions — 20 consecutive isolated lifecycles

Date: 2026-08-29
HEAD at execution: `9fd67ea` plus uncommitted UIA host-query / harness repairs.
Exe: `build\debug-diagnostic\src\ui\FastFiles.exe`
Command: isolated `UiaScenario=selection-model-interactions` via `verify.ps1 run`.

Result: **20/20 consecutive clean PASS** (`consecutiveClean=20`, wrapper exit 0).
Each run: 2 passed, 1 skipped (documented foreground/Explorer skip), 0 failed.

| run | seed | durationMs | exit | status |
| --- | --- | --- | --- | --- |
| 1 | 1 | 93493 | 0 | PASS |
| 2 | 2 | 60139 | 0 | PASS |
| 3 | 3 | 83310 | 0 | PASS |
| 4 | 4 | 95323 | 0 | PASS |
| 5 | 5 | 69480 | 0 | PASS |
| 6 | 6 | 77381 | 0 | PASS |
| 7 | 7 | 57689 | 0 | PASS |
| 8 | 8 | 60371 | 0 | PASS |
| 9 | 9 | 66688 | 0 | PASS |
| 10 | 10 | 67805 | 0 | PASS |
| 11 | 11 | 61786 | 0 | PASS |
| 12 | 12 | 58470 | 0 | PASS |
| 13 | 13 | 66782 | 0 | PASS |
| 14 | 14 | 57124 | 0 | PASS |
| 15 | 15 | 57136 | 0 | PASS |
| 16 | 16 | 58125 | 0 | PASS |
| 17 | 17 | 58954 | 0 | PASS |
| 18 | 18 | 56842 | 0 | PASS |
| 19 | 19 | 61168 | 0 | PASS |
| 20 | 20 | 58336 | 0 | PASS |

Run trees `20260829-043516` … `20260829-045630` under
`verify/runs/completion-integrity-hardening-and-release-certification/`
(untracked execution evidence; not copied here).

First failures that reset the count (not part of the 20):

- `20260829-035556` — `terminal column not found for Pane.0` while the
  fixture rows were visible. Cause: process-wide `UiaHostQueryGuard`.
- Palette edit without keyboard focus while `RootWebArea` owned focus.
- `Pane.1 activation did not establish HasKeyboardFocus` with
  foreground title `NAYEON_16: FastFiles` (browser tab, not the product).
- Ctrl-click `selC` selected `selD` (stale post-ScrollIntoView geometry).
