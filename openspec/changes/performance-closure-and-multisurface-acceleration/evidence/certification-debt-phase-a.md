# Phase A — predecessor certification debt closed on the pre-optimization candidate

Candidate: `feat/performance-closure-and-multisurface-acceleration-20261001` at
`8c8aa0b` (planning repair + reconciliation + evidence/gate repair; no product
source changed vs `origin/main` `bdd7d7b`). All runs 2026-10-02, this session,
after the established idle gate opened (median CPU load over 3 samples <= 25;
load at release-test CTest start = 19). Historical results were not reused.
Every count below is the actual registered total from the run named.

## Task 2.1 — idle gate and process hygiene

- Dominating load at apply start was two live Playwright `chrome-headless-shell`
  processes (user/other-session browser automation, started 19:22/19:46 local,
  swiftshader rendering). Not campaign-owned; NOT killed per policy.
- No campaign-owned bench/test processes were running; the installed
  `FastFilesIndexSvc` service is untouched.
- The gate opened when the Playwright session ended (~20:15). Load samples at
  gate time: 39/17/24 then 17/45/21 (median 24 then 21, threshold 25).

## Task 2.2 — full release-test CTest (idle host)

`ctest --test-dir build/release-test --output-on-failure -C RelWithDebInfo`
at `8c8aa0b`: **124 passed / 0 failed / 0 skipped**, total 232.58 s.

- `ffengine_volume_session_manager_tests` passed in **13.99 s** — the existing
  30-second assertion in `TestStopReturnsWithoutWaitingForTheSchedulerInterval`
  is unchanged and passed with margin. No raise, no skip, no retry-until-green;
  this was run 1 of at most three allowed idle runs and needed no repeat.
- The historical 123/124 loaded result is superseded by this idle-host run.

## Task 2.3 — full Debug CTest

`ctest --test-dir build/debug --output-on-failure` at `8c8aa0b`:
**124 passed / 0 failed / 0 skipped**, total 221.20 s.

## Task 2.4 — Analyze

`cmake --build build\analyze` at `8c8aa0b`: rc=0, **0 warnings** (warning-clean).
Current-session result; no historical Analyze pass inherited.

## Task 2.5 — authoritative ASan slice

`ctest --test-dir build\asan` for the four hosted-parity tests, each run
explicitly (a combined `-R` regex was mangled by cmd pipe quoting and matched
only 2; rerun as four invocations to be exact):

| test | result |
|---|---|
| `ffprotocol_fuzz_tests` | PASS (1/1) |
| `ffipc_framing_tests` | PASS (1/1) |
| `ffipc_listener_lifecycle_tests` | PASS (1/1) |
| `ffvolumescanner_terminal_tests` | PASS (1/1) |

**Total: 4/4 passed, 0 failed.**

## Task 2.6 — user-facing validation (same candidate)

| gate | result | classification |
|---|---|---|
| E2E (`dev.ps1 test-e2e`, all scenarios) | **8 passed / 0 failed / 0 skipped** | `boundToHead=true` |
| UIA core (`UiaScenario=core`) | **16 passed / 0 skipped / 0 failed** | PASS |
| UIA full (`UiaScenario=all`, final aggregate) | **23 passed / 6 skipped / 0 failed** | PASS |
| Visual catalog (`Invoke-VisualCatalog.ps1 -Gallery`) | **20 recorded / 20 passed / 0 failed / 0 skipped** | PASS |
| Focused many-tabs (`VisualScenario=shell-many-tabs-overflow`) | **1 passed / 0 skipped / 0 failed** | PASS |

UIA full aggregate instability during this session (recorded, not hidden):

1. Aggregate run #1: FAIL — `search-deep-result-navigation`, the exact
   historical flake (`search query 'alpha' was not accepted on AutomationId
   7101; observed=''`). See section 9 evidence below.
2. Aggregate runs #2/#3: `HARNESS_ERROR capability-worker-exited-without-result`
   (worker exited non-zero after module import with empty stderr and no
   candidate result). Free RAM at the time was ~0.5 GB (user work: browsers,
   qemu, etc.) — environmental memory pressure, not a product failure. Not
   retried beyond the attempts needed for the flake matrix.
3. Aggregate run #4 (current): PASS 23/6/0, matching the historical full-run
   shape (6 conditional skips: privileged 2, selection 1, drag 3).
4. Per-group aggregates (equivalent coverage, better isolation): core 16/0/0,
   search 7/0/0, privileged 2/2/0, treemap 3/0/0, selection 2/1/0, error 3/0/0,
   drag 2/3/0 — all PASS.

## Search-input flake matrix (tasks 9.2–9.4)

| trial set | runs | outcome |
|---|---|---|
| aggregate `all` run #1 | 1 | **REPRODUCED once** (`alpha` not accepted, observed='') |
| focused isolated `search-deep-result-navigation` | 5 | 5 PASS |
| `search` group aggregate | 1 | PASS (7/0/0) |
| six other group aggregates | 6 | all PASS |
| aggregate `all` run #4 | 1 | PASS (23/6/0) |
| focused trial after instrumentation | 1 | PASS (3 sub-results) |

Investigation (9.3): the harness sets queries by foreground + focus + typed
keystrokes into the query combo (AutomationId 7101) with read-back verification
(native window text, then UIA text) and one re-focus/re-type retry. The failure
mode (observed='' despite retry) is consistent with focus transfer or typed
input landing elsewhere during aggregate runs, but no controlled trial
reproduced it, so no deterministic harness or product defect was established.
Bounded failure-path diagnostics were added to `Set-UiaSearchQuery`
(edit-child discovery, combo hwnd at start/end, native vs UIA read-back split,
retry state) so a future failure self-identifies its mechanism. They do not
alter test behavior.

Classification per the repaired contract: **`UNREPRODUCED / OBSERVED FLAKE`** —
with the fresh aggregate reproduction retained above and the two historical
failures kept visible. No deterministic fix was fabricated.

## Task 2.7 — predecessor closure

Predecessor `measured-performance-acceleration-and-exact-certification` tasks:

- 6.3 (authoritative ASan slice + Analyze for the final candidate): **checked**
  — current results above (ASan 4/4, Analyze 0 warnings).
- 6.4 (E2E, UIA core/full, visual catalog for the final candidate): **checked**
  — current results above (E2E 8/0/0, UIA core 16/0/0, UIA full 23/6/0,
  visual 20/20).

Preceding conditions from the repaired contract are satisfied: stale evidence
was corrected (allocation-probe interpretation v2, equivalence-gate validation
v2 — both in this change's evidence dir, historical files byte-identical);
repository truth was reconciled (`apply-live-reconciliation.md`); current-state
validation is this record. The predecessor is therefore marked `completed` in
`openspec/lifecycle.json` (not by `supersededBy`). Raw-volume tasks 5.4 and 2.3
are NOT touched: they remain unchecked and external (`elevated`/`clean-host`).

## Task 9.1 — user-visible checks after the retained changes

`pwsh ./dev.ps1 test-e2e` on the retained candidate (pooled name path +
search admission + capped-truncation fix), debug-diagnostic rebuilt at the
current HEAD: **8 passed / 0 failed / 0 skipped**, provenance
`boundToHead=true`. The scenario details cover Home, This PC terminal state,
nested browse, Back/Forward/Up, direct path, stale-column and terminal-item
behaviour, plus exact/partial/no-result/superseded-query/selection/result-open
search checks — the practical navigation-to-ready and
search-input-to-visible-results paths. These checks are not new wall-clock
gates; no timing threshold was added. UIA core/full and the visual catalog
are re-run in the final certification ladder (task 11.3).
