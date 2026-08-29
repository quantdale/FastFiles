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
