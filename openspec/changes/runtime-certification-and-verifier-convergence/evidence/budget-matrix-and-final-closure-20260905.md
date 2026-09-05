# Capability budget, repeatability matrix, and final local closure

Executed 2026-09-05 against working-tree state descending from `5295a04`.
Records the local measurements that close tasks 1.3, 5.3, 8.2, and 8.3's
locally-available clauses. Historical evidence in this change directory is
preserved unchanged.

## Harness defect found and fixed first (owns the earlier false red)

The first fresh focused run (tree `20260905-171632`) exposed a real verifier
defect: the capability-owned `FastFiles.exe` exited between the after-snapshot
enumeration and its CIM `CreationDate` read. The read failure was recorded as a
snapshot error, which pinned `cleanup.verified=false` forever, so a run with
zero residue and 16/16 passing scenarios was misclassified
`FAIL / CLEANUP_INCOMPLETE` (`terminated=[{pid:144812, observation:
identity-no-longer-verifiable}]`, `residue=[]`, `verified=false`).

Fix at the owning seam (`verify/core/CapabilityRunner.psm1`): a failed
creation-time read for a process that has already exited is a completed
departure, not an unverifiable residue — the identity is skipped. A failed read
for a still-live process remains a recorded error and fails closed. Regression
fixture `verify/core/tests/Test-ProcessReaper.ps1`
(`ffverify_process_reaper_ps_tests`) proves: clean snapshot verified;
already-exited owned identity passes with zero residue and is recorded as
terminated; a live owned identity is terminated by exact identity and is
actually gone; an unverifiable snapshot still fails cleanup closed.

Post-fix confirmation: identical focused selection went
`16 PASS / CLEANUP_INCOMPLETE-fail` → `cleanup.pass=true` (tree
`20260905-172905`; its single scenario FAIL was a genuine foreground
transient, see below, and passed on the full run).

## Task 1.3 — measured executions and declared budget

Manifest-declared budget: `windows-ui-automation-validation`
`timeoutSeconds = 1200` (`verify/capabilities/windows-ui-automation-validation/capability.json`).

Clean measurements (same host, same build `build/debug-diagnostic`, exclusive
foreground):

| Scope | Tree | Wall | Scenarios |
|---|---|---|---|
| focused `core` | `20260905-171632` | 63.6 s | 16 pass |
| focused `core` (post-reaper-fix) | `20260905-172905` | 50.5 s | 15 pass, 1 transient fail |
| complete `all` | `20260905-173233` | 323.1 s | 23 pass, 6 truthful skips, 0 fail |

Selection: keep the existing 1200 s budget. Sufficiency: the complete
supported-matrix execution consumed 27% of the budget; every scenario has its
own bounded waits (largest observed: `treemap-readability-click-precision`
32.2 s, `selection-model-interactions` 32.3 s, drag skips 22–30 s), and no
scenario has an unbounded wait. The budget therefore tolerates a ~3.7x
host-pressure multiplier before the killable worker boundary engages. The two
documented host-resource-pressure incidents (historical 23/6 interrupted runs;
the 2026-08-30 handshake-timeout record in this change) were bounded
worker-boundary outcomes, never budget exhaustion of this magnitude.

Timeout evidence collection: on budget breach the runner kills the exact worker
`Process` object, persists a terminal `TIMEOUT` envelope, and retains
`worker.stdout.log`/`worker.stderr.log` plus the identity-recorded owned-process
cleanup result in `lifecycle.cleanup` (snapshot errors, terminated identities,
residue). Per-scenario `durationMs` and first-failure detail persist in the
capability envelope.

## Task 5.3 — supported-wrapper coverage matrix

Wrapper coverage = fixture layer (`ffverify_result_composition_ps_tests`,
`ffverify_uia_selection_ps_tests`, new `ffverify_process_reaper_ps_tests`)
plus the live executions recorded here:

- Single scenario / groups / mixed+isolated / seeded permutations: selection
  contract fixtures plus live runs below.
- Back-to-back runs: consecutive ad-hoc runs receive distinct run/invocation
  identities (fixture) and executed back-to-back without cross-run adoption.
- Search / pane / degraded-engine transitions: `search` group,
  `connection-badge-degraded` (degraded path observed live), dual-pane
  scenarios in the shared session.
- Interrupted-run recovery: composition-level worker crash/timeout/cancel
  fixtures; reaper fixture terminates a live owned identity by exact identity
  after interruption; live probe below.
- Explorer-assisted boundaries: truthfully SKIPPED on this host
  (`explorer-drag-source-unavailable`, `interactive-drop-target-obstructed`,
  `interactive-foreground-unavailable`), never pixel fallback.

## Task 8.2 — bounded repeatability/ordering matrix with recorded seeds

Every row is one wrapper invocation; classifications are the harness's own,
not rewritten. No retry-until-green: each seed ran exactly once.

| Seed | Scope | Tree | Result |
|---|---|---|---|
| (none) | all | `20260905-173233` | PASS 23/6/0 (323.1 s) |
| 1234 | all | `20260905-174007` | PASS 23/6/0 (324.9 s) |
| 777 | search | `20260905-174536` | PASS 7/0/0 (78.1 s) |
| 4242 | isolated | `20260905-174659` | PASS 6/4/0 (262.5 s) |

 The 4242 isolated run's four skips are the same documented boundaries (two
 privileged-scan, two interactive-foreground/drag). No row was re-run to
 change a result; no FAIL occurred in any row.

### Interrupted-run recovery (live probe)

A real `verify.ps1 run` of `treemap-readability-click-precision` was
interrupted by force-killing the identity-recorded capability worker
mid-scenario (tree `20260905-175214`). Outcome: the parent composed exactly
one terminal envelope (`FAIL / HARNESS_ERROR /
capability-worker-exited-without-result`, parent exit 3), the killed worker PID
was identity-recorded in `lifecycle.worker`, and cleanup failed closed
(`pass=false, verified=false, snapshotErrors=["pid=146276: creation time
unavailable"]`) when the app launched by the killed worker raced its own CIM
creation-time read. No live process was labelled terminated, nothing was
adopted, and no PASS was fabricated; the two surviving product processes were
removed manually. The probe exposed a real harness escape: the engine's CIM
`ExecutablePath` can carry an extended-length `\\?\` prefix that silently
defeats the build-root prefix filter, so the owned engine was invisible to both
the preflight and the reaper. Fixed at the owning seam
(`Get-VerificationNormalizedExecutablePath` in `CapabilityRunner.psm1`, applied
in the snapshot and identity comparison; volume-GUID paths preserved verbatim)
with regression checks in `Test-ProcessReaper.ps1` (normalization, prefix-filter
match, live prefixed-path identity verification) — all 12 fixture checks pass.
The retained `requiresCleanBuildProcessBaseline` preflight — now seeing
normalized paths too — is the design's stated protection for the next run (it
rejects, never adopts, pre-existing exact identities). Fixture-level
interruption coverage (worker crash/timeout/cancel; reaper terminating a live
owned identity by exact identity) is in `ffverify_result_composition_ps_tests`
and `ffverify_process_reaper_ps_tests`.

## Task 8.3 — supported full UIA + unsigned release-candidate qualification
Full supported UIA: PASS (23 passed, 6 skipped with recorded reasons, 0 failed)
at `20260905-173233`. The six skips are the documented host boundary: two
`privileged-scan-unavailable` (placeholder pins), three Explorer/foreground
drag boundaries, one `interactive-foreground-unavailable` selection lifecycle.
No production-signed or privileged clean-host certification is claimed.

Unsigned release-candidate qualification on the clean committed tree: recorded
in the WORKSTATE closure entry (requires the clean tree of the session commit;
the pre-commit dirty tree correctly refuses with `RELEASE_CLEAN_TREE_REQUIRED`,
which is the gate working as designed).

## Final local UI journey note (completion-integrity 4.5.x)

The live shared session again composed Home -> browse -> deep navigation ->
tabs -> search -> settings -> storage in one foreground-owned lifecycle. The
journey tasks remain unchecked truthfully: preview/properties and file
operations steps are not part of the UIA scenario registry, so the complete
continuous journey criterion is still not met. No reclassification.
