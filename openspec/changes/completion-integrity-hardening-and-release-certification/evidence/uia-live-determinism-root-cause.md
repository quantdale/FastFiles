# Live UIA determinism — root cause, measurements, and fixes

Date: 2026-08-30
Host: Windows 11 Pro 10.0.26200, interactive desktop session
Capability: `windows-ui-automation-validation` via `verify/verify.ps1 run`

## Summary

The live UIA capability was not merely flaky. It was failing for four
independent, separately-fixed reasons, three in the harness and one in the
product. None of them was the "Filters disclosure" defect the campaign
inherited as the presumed blocker.

Everything below is measured from retained run artifacts, not inferred.

## Defect 1 — unsatisfiable pre-search guard (harness)

The shared-session guard probed the terminal column for `alpha.txt`
immediately after navigating to `$scratch`. `alpha.txt` is created one level
deeper, in `$scratch\SubFolder`, so that column never held it and the guard
could not pass on any host.

It used `throw`, which aborts the whole capability as `HARNESS_ERROR /
driver-exception` before any of the 20 scenarios run. **This, not the Filters
disclosure, is why the campaign observed the Filters scenarios failing** —
they never executed.

Fixed by probing for `SubFolder`, which is what that column actually holds.
`alpha.txt` is still verified after the re-navigation into `SubFolder`.

## Defect 2 — wrong scope for the fixture (harness)

`Set-UiaSearchFixtureScope` selects `Current Folder`, which the product
defines as **non-recursive** (`Current Folder + Subfolders` is a separate
scope — `SearchPanel.cpp` `RefreshScopeLabels`). The browsed folder was
`$scratch` while `alpha.txt` lives in `$scratch\SubFolder`, so an empty result
set was the product behaving correctly.

Fixed by navigating to `$folder` before the palette sequence, as the isolated
`search-deep-result-navigation` scenario already did, and reacquiring the main
window element afterwards.

## Defect 3 — bogus combo control id (harness)

`Set-UiaComboSelectionElement` derived the combo's control id from
`SendMessage(cb, 0x0129)`, commented `WM_GETCONTROLID`. No such message
exists: **0x0129 is `WM_QUERYUISTATE`**, so the value was UI state flags.

Consequences, all three real:

* the `WM_COMMAND`/`CBN_SELCHANGE` posted to the parent named the wrong
  control, so the app never learned the scope changed;
* only the raw `CB_SETCURSEL` took effect, which a later re-sync from the
  product's own unchanged scope state could silently undo;
* the function still returned `true`, so the caller recorded the selection as
  applied and never reached the working `ControlId` fallback.

Fixed with `GetDlgCtrlID`, failing closed when it yields no id.

## Defect 4 — stranded coalesced snapshot refresh (product)

`SearchPanel::OnSnapshotGenerationChanged` rate limits re-dispatches to one per
`kSnapshotRefreshIntervalMs` (2000ms). A generation arriving inside that window
set `snapshotRefreshPending_` and did nothing else.

That flag is consumed **only** by `ShowAndFocus`, which does not run again
while the panel stays open. So a visible Search panel kept showing results from
before the generation, for a query the user had already typed, until they
retyped or reopened the panel.

The window is easy to hit because `lastSnapshotRefresh_` is stamped by *every*
dispatch including the debounce dispatch from typing — so the generation that
first carries newly indexed files very often lands inside the interval the
user's own keystroke just started.

Fixed by arming a timer for the remainder of the interval and servicing it in
`HandleTimer`. The decision moved to `ffui/SearchSnapshotRefresh.h` and is
covered by `ffsearch_snapshot_refresh_tests`; reverting the header to the old
"mark pending, schedule nothing" behaviour fails 4 checks, so the coverage pins
the defect rather than describing the fix.

**This is a genuine user-visible staleness bug and the fix stands on its own
merits. It did not resolve the live flake** — the measured failure rate was
unchanged afterwards. Recorded here so that is not misread later.

## Root cause of the residual flake — fixture under the user's `%TEMP%`

The column view browses the entire path chain down to the fixture. The session
sandbox was rooted at `%TEMP%`, so that chain included the user's real temp
directory — **25,555 entries on this host** — and re-enumerated it on every
snapshot refresh.

Retaining the product's `diagnostics.log` into the run artifacts (previously
deleted with the sandbox at teardown, making any live failure undiagnosable)
made the cost measurable:

| Metric (`search-allocated-size`) | Failing run | Passing run in same batch |
| --- | ---: | ---: |
| `FFDIAG_UI_SNAPSHOT_COLUMN_REFRESH` records | 440 | 101 |
| Refreshes of the ~25k temp column | 49 | 11 |
| Directory entries re-enumerated | 1,273,036 | 286,090 |

The constant column rebuilds retire UIA elements mid-query, which surfaced as
`FindAll` throwing `UIA_E_ELEMENTNOTAVAILABLE` with an empty message at stage
`locate-sort-combo`.

Fixed by rooting the sandbox at `%LOCALAPPDATA%\FastFilesUiaSessions`
(overridable via `FASTFILES_UIA_SESSIONS_ROOT`). The browsed chain drops from
~25,963 entries to ~408. The teardown guard moved with it and still refuses to
remove anything outside the sessions root lacking the session marker.

This also satisfies the campaign's requirement that fixtures be disposable and
sanitized: 25,555 entries of the user's personal temp directory are no longer
enumerated into every evidence tree.

### Effect

| Metric | Before | After |
| --- | ---: | ---: |
| Entries re-enumerated per run | 1,273,036 | 633 |
| Column refreshes | 440 | 13 |
| Refreshes of a 25k column | 49 | 0 |
| Wall clock per `search` group run | ~180s | ~65s |
| `search` group, 5 consecutive runs | 1 FAIL | 5 PASS |

The 2.8x wall-clock drop is independent corroboration of the mechanism: the
capability was spending most of its time re-enumerating the user's temp
directory.

## Measured history

Each row is 5 consecutive runs of the `search` scenario group. Recorded so the
sequence is not retold as a straight line — three fixes produced no measurable
improvement before the actual cause was found.

| Tree state | Result |
| --- | --- |
| After combo control-id fix | 1 FAIL |
| After product coalescing fix | 3 FAIL |
| After foreground/diagnostics instrumentation | 1 FAIL |
| After shallow sandbox root | **5 PASS** |

1 FAIL vs 3 FAIL at n=5 is noise; no claim of regression is made for the
product fix. The final row is the first change with a mechanism, a
corroborating performance signal, and a clean batch.

## Supporting harness corrections

* Address-bar navigation failures (`address bar edit did not appear after
  Ctrl+L`, `tab labels did not reflect ...`) recorded no foreground evidence, so
  `Convert-UiaForegroundFailureToSkip` could not distinguish a stolen
  foreground from a product defect and both were reported as
  `ASSERTION_FAILURE`. They now capture `Get-UiaSafeInteractiveState`. The
  converter still reclassifies only when the recorded foreground was actually
  foreign, so this cannot turn a genuine failure green.
* The fixture-navigation precondition initially used `throw`, reintroducing the
  Defect 1 anti-pattern. It now fails only the two dependent scenarios, under a
  distinct `search-fixture-precondition-unavailable` reason so an input
  precondition is not reported as a product search-results defect.

## Known remaining gap

`Test-UiaEngineServingData` reports the skip reason
`engine-connect-unavailable`, but the predicate only checks whether any column
currently exposes `ListItem` children. A transient UIA read failure therefore
skips a scenario on a fully engine-connected host under a reason that is not
what was observed. Not yet fixed.

## Repeatability measurement (2026-08-30)

Raw per-run records: `uia-search-20run.jsonl`, `uia-full-capability-runs.jsonl`.
Each record carries the per-scenario status set, per-lifecycle cleanup/residue,
duration, and run tree, so a drifting SKIP set is visible rather than hidden
behind a green top-line result.

### `search` scenario group — 20 consecutive isolated lifecycles

| Metric | Result |
| --- | --- |
| Top-level status | 20/20 PASS, 0 FAIL |
| Exit codes | 20/20 zero |
| Cleanup | clean on every lifecycle, residue empty |
| Duration | 64-77s |
| Distinct scenario-status sets | 2 |

**This does not yet satisfy the 20-consecutive-clean requirement.** Run 10
recorded `search-allocated-size = SKIPPED:engine-connect-unavailable`, and that
skip is currently unexplained at root-cause level:

* the reason is accurate this time — it came from `$Context.ReadyReason`
  (`engine-connect-not-observed`), not from the `Test-UiaEngineServingData`
  predicate criticised above;
* the retained diagnostics log for that lifecycle contains **zero**
  `FFDIAG_ENGINE_CONNECT_SUCCEEDED` records across its full 21s span, where a
  passing lifecycle logs exactly one — so the UI/Engine handshake genuinely
  never completed rather than merely being observed late;
* the lifecycle logged `FFDIAG_ENGINE_STATUS_CHANGED engine-started`,
  `FFDIAG_ENGINE_LAUNCHED`, and then
  `FFDIAG_UI_SNAPSHOT_READ_FAILED state=snapshot-generation-remap-failed`;
* the interleaved `FFDIAG_ENGINE_CONNECT_FAILED
  privileged-connection-disconnected/service-not-running` records are the
  expected degraded-mode privileged path (pins are placeholders) and are
  present in passing runs too, so they are not the discriminator.

A ~1-in-20 UI/Engine startup handshake miss accompanied by
`snapshot-generation-remap-failed` is a real anomaly. It is recorded here
rather than waved through: the acceptance criterion is twenty consecutive
lifecycles with no *unexplained* product or harness result, and this one is
unexplained. The count is not claimed.

### Full capability — 3 consecutive lifecycles

| Run | Result | Failing scenarios |
| --- | --- | --- |
| 1 | PASS | — |
| 2 | FAIL | `drag-out-to-explorer` |
| 3 | FAIL | `drag-out-to-explorer`, `treemap-readability-click-precision` |

Both failures are in the pointer-input family (synthetic mouse against a live
interactive desktop). The search family passed in all three.

The obvious harness explanation was checked and does **not** hold: the drop
point is taken from Explorer's `listview`/`ShellView` centre after waiting for
a stable rect, not from the window rect, and the reported geometry is
internally consistent (`explorerRect` is `x,y,width,height`, and the drop point
falls inside it). What the diagnostics show is a `Static` covering the view at
drop time — once titled with the target path, once `Cut && Paste` — with
`fg='FastFiles'`, i.e. no foreground theft. That is Explorer chrome or a menu
overlaying the drop target, not a mis-computed point.

n=3 is too thin to classify the pointer-input family as the
`ship-ready-ui-ux-hardening-convergence` 6.2 "Explorer-assisted boundary where
actually available" external boundary, and it is deliberately **not** so
classified here. Excluding it from the required set on this evidence would be
defining the requirement around its failure.

### Invalidated records

An operator error ran two batches concurrently against the same desktop. The
capability correctly refused 18 runs with
`HARNESS_ERROR / stale-build-process-before-run` because a previous lifecycle's
UI process was still alive. Those 18 records measured nothing and are excluded
from every count above; they are preserved unmodified in
`build/uia-20run.polluted.jsonl` rather than deleted.
