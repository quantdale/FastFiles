# Many-tabs overflow (tasks 6.1–6.4)

Executable under test: `build/debug-diagnostic/src/ui/FastFiles.exe`, SHA-256
`1a31d192cd16c602e4c5849dd1488d5f0044bde5b5e82c23320ed2e762ff24f0`.
Revision: the change's working tree on top of
`1172c1a1e6aa9c1a726d52abdade0975b6979e4b`.

## Task 6.1 — reproduction at the scenario's narrow width

Scenario `shell-many-tabs-overflow` (action `many-tabs`, width 720). Two
independent probes measured the four quantities separately:

| Quantity | Observed | Where |
| --- | --- | --- |
| Logical tab count | **9** (1 initial tab + 8 new-tab clicks) | `evidence/many-tabs-probe.json` (`logicalTabCount`), and the harness evidence `logicalTabCount` |
| Visible tab-button count | **4** | `many-tabs-probe.json` (`narrowWidth.visibleTabButtons`) and the harness evidence (`visibleTabButtonsAtNarrowWidth`) |
| Overflow affordance | **present** (`AutomationId 8208`), and one of its entries activates a hidden tab | both evidence files (`overflowPresentAtNarrowWidth`, `hiddenTabActivation`) |
| Count the harness asserted before this change | `Get-VisualTabCount >= 8` over the **visible** buttons | `verify/capabilities/windows-ui-visual-validation/VisualValidation.psm1` (pre-change `many-tabs` action) |

So the inherited failure ("tab count 4 < 8") was the visible window, not the
logical set: the product created all 9 tabs, hid the ones that do not fit, and
exposed them through the tab-list affordance. The Control and Raw UIA views
agree (4/4): `SW_HIDE`n tab buttons are not published, which is why a button
count can never be the logical count.

## Task 6.2 — the logical floor stays at 8

The harness now asserts `logicalTabCount >= 8` and reads that count from the
tabs' own labels (each tab's label is `"<one-based index>: <leaf>"`, derived by
the product from that tab's typed location). No expectation was lowered to 4,
and a hidden tab is never treated as deleted: the check requires the hidden
extreme to be *reachable and activating* rather than absent.

## Task 6.3 — harness fixed, product unchanged

The logical count was 9 (>= 8) and a hidden tab activated its own workspace, so
the fix is in the harness (`VisualValidation.psm1`):

- `Get-VisualTabStripState` reads the tab buttons with their own index/leaf
  labels instead of counting only the buttons that fit.
- `Invoke-VisualTabListSelection` opens the real overflow affordance with a
  pointer click (the popup is modal, so a synchronous `BM_CLICK` would block)
  and then drives it by keyboard (`UP`/`DOWN` + `ENTER`).
- The `many-tabs` action now measures and records, separately: the logical
  count, the visible button count, the affordance state, hidden-tab activation,
  a normal width where every logical tab is a visible button, and the restored
  narrow width.

No product code changed for this finding: the responsive overflow layout is
untouched.

## Task 6.4 — proof split

Harness run (visual + UI Automation in one scenario):

```text
pwsh ./build/_run_visual_many_tabs.ps1          # same capability entry point
Status PASS, summary "1 captures passed; 0 skipped/blocked; 0 failed"
```

Recorded in `evidence/many-tabs-harness-scenario.json` and
`evidence/many-tabs-harness-run.log`; the DWM capture is
`build/vis-scenario-shell-many-tabs-overflow/visual-capture/shell-many-tabs-overflow.png`
(untracked execution artifact), and `evidence/many-tabs-release-classification.json`
records `Functional: PASS`, `VisualRegression: PASS` and the two
externally-blocked legs (`Accessibility`, `VisualReview`) that are unchanged.

- **Overflow reachability**: the affordance is present whenever tabs are
  hidden, and selecting its first entry activates the hidden tab (`entry
  "first"`, `activatedIndex 1`, `hiddenBeforeSelection true`,
  `visibleAfterSelection true`).
- **Keyboard activation**: the selection is driven with `UP`/`DOWN` + `ENTER`
  only; no list entry is invoked directly.
- **Correct workspace**: in `many-tabs-probe.json` the activated tab's own
  label is `1: leaf-1` and the active pane's deepest column is
  `…\ff-many-tabs-<guid>\leaf-1` — the workspace the tab names is the workspace
  shown.
- **Close identity**: closing the `leaf-5` tab leaves eight tabs whose labels
  still carry their own leaves (`1: leaf-1`, `2: leaf-2`, `3: leaf-3`,
  `4: leaf-4`, `5: leaf-6`, `6: leaf-7`, `7: leaf-8`, `8: leaf-9`);
  `remainingLeavesIntact: true` and no tab borrowed another tab's leaf.
- **Switch identity**: switching to the tab labelled `6: leaf-7` shows pane
  columns ending in `leaf-7` (`workspaceMatchesLeaf: true`).
- **Narrow width**: 4 of 9 tabs visible, affordance present, hidden tabs
  reachable (above).
- **Normal width**: all 9 tabs are visible buttons and the affordance is gone
  (`normalWidthTabButtons 9`, `normalWidthOverflowPresent false`,
  `normalWidthAfterNarrow` in the probe agrees).

Visual, UI Automation, and semantic outcomes are therefore reported
separately above rather than collapsed into one visible-button count.

## Observation, not a defect

The degraded pane exposes a browsed directory through its *column names*
(`Pane.0.Column.N`) and the tab label, not through a per-file list item in the
probe's window; the probe records the pane columns alongside the label so the
identity readback is explicit about which semantic surface proved it.
