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

**Run 10 recorded `search-allocated-size = SKIPPED:engine-connect-unavailable`.**
That skip was recorded here as an unexplained ~1-in-20 "UI/Engine startup
handshake miss". It has since been root-caused, and the original reading was
wrong. See *Root cause of the run-10 skip* below. The corrected statement is:
the `search` group reached 20/20 with one lifecycle whose readiness signal was
destroyed by a defect in the diagnostics log writer, not by a failed handshake.

The claim previously recorded here -

> the retained diagnostics log for that lifecycle contains zero
> `FFDIAG_ENGINE_CONNECT_SUCCEEDED` records ... so the UI/Engine handshake
> genuinely never completed rather than merely being observed late

- was **not supported by the data it cited**. It is a process-attribution
conclusion drawn from a log that carried no process attribution. The absence of
a record in a shared, multi-process, non-atomically-appended file does not
establish that the event never occurred. Retained for the record because this
campaign's subject is exactly this class of overstated evidence.

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

## Root cause of the run-10 skip - the diagnostics log lost the record

Date: 2026-08-30. This supersedes the "unexplained handshake miss" reading above.

### What the evidence actually shows

Each UIA lifecycle runs in its own disposable sandbox with its own
`LOCALAPPDATA`, so a lifecycle's `diagnostics.log` is written only by the
processes that lifecycle launched (`New-UiaSessionSandbox` creates a fresh
GUID-named root per scenario). A straggler from the previous lifecycle writes
to its own, already-deleted, sandbox - it cannot appear here.

Run 10's `search-allocated-size` log contains
`FFDIAG_UI_SNAPSHOT_READ_FAILED state=snapshot-generation-remap-failed`. That
state has exactly one emitter, `EngineClient::ReaderLoop`, and `ReaderLoop`
runs only after `ConnectAndSubscribe` has published the pipe - which happens
immediately *before* `AppendDiagnostic(FFDIAG_ENGINE_CONNECT_SUCCEEDED)`, on
the same thread.

So the handshake completed and the record was **written and then destroyed**.

### The defect

`ffprotocol::AppendDiagnostic` appended through `std::wofstream` opened with
`std::ios::app`, which appends by seeking to end-of-file and writing at that
offset. That is atomic only *within* one process, where `LogMutex` serialises
it. The UI, the engine it launches, and the service all append to the same
file. Two processes that resolve the same end offset overwrite each other.

Direct physical evidence, measured across the 121 retained lifecycle logs in
`verify/runs/`:

| Measurement | Result |
| --- | ---: |
| Records written | 25,927 |
| Orphaned record tails (a record's head overwritten) | 35 |
| Lifecycles missing `FFDIAG_ENGINE_CONNECT_SUCCEEDED` | 1 of 121 |

Every orphan has the same shape: a UI record's head clobbered by an engine
record written at the same offset, leaving the UI record's tail stranded as its
own line. For example, a `FFDIAG_UI_DIRECTORY_REQUEST_SENT` record (UI)
partially overwritten by a `FFDIAG_ENGINE_DIRECTORY_REQUEST_RECEIVED` record
(engine):

```
1788033665 ... codeName=FFDIAG_ENGINE_DIRECTORY_REQUEST_RECEIVED path=<sandbox>
-directory-request-sent outcome= account= sid= privilegeHeld=0 ... items=0
```

The single lifecycle missing `CONNECT_SUCCEEDED` is run 10 - the only one of
121 - and its loss window is the moment the just-launched engine emits its
first record, which is exactly when the UI writes `CONNECT_SUCCEEDED`.

The harness uses that one record as its engine-readiness signal, so it
correctly reported `engine-connect-not-observed` for a handshake that had in
fact completed. The observation was accurate; the record it depended on had
been destroyed by another process's concurrent append.

### Fixes

* `AppendDiagnostic` now opens with `FILE_APPEND_DATA` (deliberately *without*
  `FILE_WRITE_DATA`) and writes each whole record with a single `WriteFile`.
  The filesystem then chooses the offset under its own lock, so an append is
  atomic against every other process. `LogMutex` is retained for intra-process
  ordering.
* Because a record must reach the file in one `WriteFile` for the append to
  stay atomic, an over-long record is dropped. The cap is set above the
  largest legitimate record (a `kMaxPathLengthChars` path costs up to ~98 KB
  in UTF-8, ~197 KB JSON-escaped; the cap is 1 MiB), so the drop path is a
  backstop that no real diagnostic can reach. An earlier 60 KB cap in this
  same change would have silently discarded long-path diagnostics -- the very
  failure shape being removed. `ffprotocol_diagnostics_tests` now appends a
  maximum-length path in both formats and fails against that smaller cap.
* Records are encoded explicitly as UTF-8. The previous wide stream narrowed
  through the "C" locale, which fails the entire record as soon as a path
  contains a non-ASCII character - silently, because every call site discards
  `AppendDiagnostic`'s return value. `ExportDiagnosticBundle` decodes with the
  matching code page.
* Every record now carries `pid`, `tid` and `epochMs` (append-only trailing
  fields in text, new keys in JSON). A shared multi-process log with no process
  attribution and one-second resolution is why this took two sessions to
  root-cause: the decisive question was always "which process wrote this", and
  the log could not answer it.
* `EngineClient::ConnectAndSubscribe`'s one silent `return false` - taken when
  a completed handshake is superseded or shutdown has begun - now records
  `engine-connect-superseded` / `engine-connect-stopping`, logged outside the
  writer critical section. A silent return there was indistinguishable in the
  log from a connect that never finished.

### Coverage

`ffprotocol_diagnostics_concurrency_tests` spawns four real writer processes
appending 250 varying-length records each. A single-process thread test cannot
reproduce the defect, because `LogMutex` already serialises threads.

Reverting `AppendDiagnostic` to the previous implementation and rebuilding
fails three of its checks - torn records, lost records, and per-writer record
loss - so the coverage pins the defect rather than describing the fix.

### What was *not* changed

The harness's 20 s readiness budget was left exactly as it was. Widening it
would have produced a green 20-run that proved nothing.

### Post-fix certification — 20 consecutive isolated lifecycles

Raw per-run records: `uia-search-20run-postfix.jsonl`. Run against the
`debug-diagnostic` tree at commit `51a594a`.

| Metric | Pre-fix batch | Post-fix batch |
| --- | --- | --- |
| Top-level status | 20/20 PASS | 20/20 PASS |
| Exit codes | 20/20 zero | 20/20 zero |
| Distinct scenario-status sets | 2 | **1** |
| Non-PASS scenario results | 1 (`search-allocated-size` SKIPPED) | **0** |
| Cleanup / residue | clean / empty | clean / empty |
| Duration | 64-77s | 60-96s (96s is the first, cold-cache run) |

The drifting SKIP set is gone: all twenty lifecycles produced the identical
seven-scenario all-PASS set, so the count is not resting on a green top-line
result hiding a varying skip.

**The load-bearing evidence is not the 20/20.** A 1-in-121 event failing to
recur in 20 runs would be weak on its own. The direct measurement is the
orphaned-record signature in the logs those runs produced:

| Measurement | Pre-fix corpus | Post-fix batch |
| --- | ---: | ---: |
| Lifecycle logs | 121 | 60 |
| Records written | 25,927 | 12,285 |
| Orphaned record tails | 35 (0.135%) | **0** |
| Lifecycles missing `FFDIAG_ENGINE_CONNECT_SUCCEEDED` | 1 | **0** |
| Records carrying `pid`/`tid`/`epochMs` | 0 | 12,285 (100%) |
| Distinct writer pids attributed | not recorded | 116 |

At the pre-fix rate, 12,285 records would be expected to contain ~16.6 orphans.
Observing zero has probability ~6.3e-8 if the mechanism were unchanged. That,
not the run count, is what establishes the repair.

### Invalidated records

An operator error ran two batches concurrently against the same desktop. The
capability correctly refused 18 runs with
`HARNESS_ERROR / stale-build-process-before-run` because a previous lifecycle's
UI process was still alive. Those 18 records measured nothing and are excluded
from every count above; they are preserved unmodified in
`build/uia-20run.polluted.jsonl` rather than deleted.
