# Post-completion apply audit + P1-2 false-positive repair — 2026-10-29

Session objective: `perform openspec apply` for
`large-index-lifecycle-and-search-scalability`, executed against a **stale
primary worktree** and audited per the goal's requirement-by-requirement
mandate. This document is the audit record.

## 1. Reconciliation (the worktree was a superseded parallel draft)

The primary worktree held a 2026-10-08 snapshot: 4 unpushed commits
(`aee48bb..b77d61e`) plus dirty in-flight files — a parallel draft of this
same change (its own differential bench mode, 30-task ledger, red-first draft
tests). Live reconciliation established:

- The authoritative apply is **complete** at the pushed campaign tip
  (`d3884ee`, 2026-10-22 closure; tasks 27/27, lifecycle `completed`,
  PR #14 OPEN/UNMERGED, review threads resolved).
- The draft chain is superseded (the tip re-implemented and extended the same
  work: `RemoveBatch` bulk removal, planner intersection, six-lane review,
  certification ladder). It is preserved verbatim at
  `archive/large-index-local-draft-20261008` (`358d57f`) and is not for merge.
- The campaign branch was converged to the origin tip; the audit ran on that
  tree.

## 2. Claim-by-claim verification (second host: this audit host)

| Claim (source) | Live verification | Result |
|---|---|---|
| tasks 27/27, 0 open (`tasks.md`) | checkbox recount | PASS |
| strict OpenSpec 93/93 | `openspec validate --all --strict` | PASS 93/93 |
| lifecycle model PASS | `verify/status/Get-ProjectStatus.ps1` | PASS |
| every cited evidence artifact resolves | citation sweep over `tasks.md` | PASS |
| Debug CTest 128/128 | parity `ctest-debug` + post-fix rerun | PASS 128/128 (twice) |
| release-test CTest 128/128 | parity `ctest-release-test` + post-fix rerun | PASS 128/128 (twice) |
| Analyze PASS, 0 warnings | parity `build-analyze` + standalone retry | PASS, 0 warnings (see finding 1) |
| ASan slice PASS | parity `asan-slice` | PASS |
| policy / hygiene / exact-tree / release-candidate | parity steps | PASS |
| local CI parity 11 checks | `verify/Invoke-CiParity.ps1` | 10/1 first run (finding 1), all steps individually PASS |
| E2E 8/0/0 `boundToHead` | `dev.ps1 test-e2e` | PASS 8/0/0, `boundToHead=true`, identity `d3884ee` |
| UIA all-FastFiles-surfaces green; Explorer journey honest | `verify.ps1 run -Capability windows-ui-automation-validation` | run 2 PASS 21 passed / 7 skipped (recorded reasons) / 0 failed; drag-in-from-explorer remains SKIPPED/BLOCKED_ENVIRONMENT, journey UNQUALIFIED (unchanged, not re-qualified); see finding 2 |
| visual 26 captures / 27 skipped-blocked / 0 failed | `verify.ps1 run -Capability windows-ui-visual-validation` | run 2 exactly 26/27/0; run 1 had 1 palette-precondition flake (finding 2) |
| 1M replacement rebuild 158-164 s -> 4.7-5.1 s (31-34x) | artifact cross-check (`memory-report-before/after-fix.json`, `payload_identical: true` both) | VERIFIED |
| removal arm 465x @200k | isolation artifacts + this-host re-run (B1 20.0-21.3 ms vs certified after-fix 21.98 ms) | VERIFIED |
| selective/exact/no_match rows 1 / 1 / 15,625 at 1M; 56-189x | `search-matrix-planner-fix.json` per-run fields | VERIFIED |
| 24/24 exact result-set equality | explore-findings + hot/cold suites in both CTest runs | VERIFIED (evidence + suites green) |
| hosted CI `BLOCKED_EXTERNAL` | one live inspection of runs `39694143188`/`39694150538` at `d3884ee`: six jobs, zero executed steps | VERIFIED once, not rerun |
| PR #14 OPEN / UNMERGED / MERGEABLE | `gh pr view 14` | VERIFIED |
| product tree `43fc3c1..d3884ee` unchanged for src/tests/cmake | `git diff --stat` | VERIFIED |

## 3. Findings

### Finding 1 — Analyze link transient (harness, recovered)
`parity build-analyze` failed on `LNK1104: cannot open file
'...intermediate.manifest'` (AV/filesystem lock on the link manifest) — the
exact transient class documented by the campaign ("plain retry loop succeeds;
do not treat as code failure"). Standalone rebuild of `build/analyze`
completed `exit=0` with **0 warnings**. No code change.

### Finding 2 — interactive flakes, characterized honestly (no product defect)
- `search-deep-result-navigation`: full UIA run 1 failed at a SendInput
  tab-label timing step; solo attempts 2/3 PASS (failures at varying
  sub-steps: tab labels, `Filters` disclosure — both historically documented
  flake families on interactive hosts); full run 2 then passed 21/7/0.
- Visual run 1 failed `theme precondition failed: palette 'settings' missing`
  (palette query input-timing family); run 2 passed 26/27/0 exactly as
  claimed. Retained as observed results; no product change.
- The Explorer drag-in journey remains UNQUALIFIED exactly as the closure
  recorded (second interactive host would be required); nothing in this
  audit re-qualifies or suppresses it.

### Finding 3 — PRODUCT DEFECT (fixed in the new implementation commit)
**The review-lane D P1-2 fit pre-check cold-locked every volume above
819,200 rows at the default budget.** The pre-check predicted the reload size
with a flat 512 bytes/row guess whenever the projection was empty
(`lowWatermark/512 = 819,200` rows at 500 MiB), so a 1M-row volume whose
measured size (~338 MB estimator / 346 MB live) fits under the 400 MiB
watermark was rejected outright — **silently**. Consequences at `d3884ee`:

- Warm startup hydration (`Main.cpp:288` `RebuildAll` -> `RebuildVolumeFromStore`)
  left such volumes' projections **empty forever**; every search fell back to
  the durable SQLite cold stream (the documented 2M cold regime applied at
  ~1M rows).
- Cold reload (`ReloadEvictedVolume`) could never succeed for them.
- The memory-report rebuild cycles at 1M became no-ops (36-52 ms, estimator
  128 bytes) that an artifact could misread as *fast* rebuilds — and
  `payloadIdentical` (pooled-vs-owning export hash) was vacuous against an
  empty projection.
- All "1M reload/rebuild works" evidence (8.1 reload 6,601 ms, "rebuild
  revalidation 28-32x") was measured at binary `b6df9ef`, **before** the P1-2
  pre-check landed in `43fc3c1`; nothing re-measured 1M afterwards.

No row-cost prior is sound (measured row cost ranges from tens of bytes to
several kilobytes with name length), so the guard is now enforced on
**measured growth during the stream** (`ForEachEntryUntil` with a bounded
early stop at the early-enforcement watermark, periodic estimate check plus
final check; entry pre-allocation capped to the watermark envelope). The
P1-2 outcome is preserved: a genuinely oversized volume never streams in
full above the watermark, never thrashes enforcement, and stays cold (the
documented oversized-volume no-reload bound); a fitting volume loads exactly.

**Red-first evidence** (`build/audit/red-run-final.log`, exit=1, six FAILs):
`TestFittingVolumeReloadIsNotRejectedByEmptyProjectionPrior` (new) reproduces
the false positive in both production shapes — cold reload into a
mixed-volume projection (degenerate measured prior) and warm-hydration into
an empty projection (flat prior) — and self-validates that the fixture fits
the watermark. `TestOversizedVolumeReloadFailsFastAndStaysCold` stays green
throughout (oversized rejection pinned). After the fix
(`build/audit/green-run.log`, exit=0, 283 checks) everything passes; Debug
and release-test full CTest 128/128 both.

**Scale re-verification on this host** (release-test, post-fix):
`--memory-report --scales 200000,1000000 --rebuild-cycles 10` now rebuilds
1M rows in 3.55-3.79 s per cycle (`entry_count = 1,000,000`,
`cold_reload_failed_count = 0`), against the pre-fix audit run's 36-52 ms
no-ops; 200k cycles 0.62-0.67 s (certified after-fix 0.82-0.87 s band). The
isolation matrix at 200k is unchanged (removal arm 21.3 ms vs certified
21.98 ms); its 1M fixture genuinely straddles the watermark (documented
D2/D3 straddle) and is now rejected with bounded work (1.55 s guarded abort)
instead of instant-reject — the documented no-reload outcome unchanged.

**Artifact truthfulness companion fix**: memory-report captures now emit
`cold_reload_count` / `cold_reload_failed_count` (plus the existing
`entry_count`), so a rejected rebuild can never masquerade as a fast cycle.

## 4. Identity after the audit

- `implementation_sha` = the audit-fix commit (this change's new
  executable-impacting commit; it **supersedes** `43fc3c1`, whose product
  tree carried finding 3).
- `certified_binary_sha`: identity via `ffbuild_identity_binding_tests`
  (green inside both 128/128 runs), never a bit-for-bit claim.
- The audit-fix's follow-up documentation/evidence commits are proven
  executable-tree-equivalent by
  `verify/qualification/Test-ExecutableTreeEquivalence.ps1
  -ImplementationSha <fix> -EvidenceSha <final tip>`; the live verdict is
  reported in PR #14 / the owner report (kept outside the checked tree).
- Historical measurement artifacts keep their embedded identities
  (`2e0d819`/`b6df9ef` era) and are labeled by their provenance blocks.

## 5. Not reproduced (with reasons)

- 7.1 startup absolute timings: the startup probe script is untracked
  campaign-host tooling and absolute timings are host-specific; the evidence
  documents remain the record (evidence-verified, not re-measured).
- The cold-profile artifacts' asserted provenance (lane E E-8 disclosure)
  is unchanged.
- Privileged/elevated/external boundaries (hosted certification, production
  signing, clean-host install, elevated MFT/USN, raw-volume bound) unchanged.

## 6. Audit ladder (commands actually run on this host)

```
text
openspec validate --all --strict                      # 93/93
verify/status/Get-ProjectStatus.ps1                   # lifecycle PASS
verify/Invoke-CiParity.ps1                            # 10 PASS / 1 transient
cmake --build build/analyze                           # retry exit=0, 0 warnings
dev.ps1 test-e2e                                      # 8/0/0 boundToHead
verify.ps1 run -Capability windows-ui-automation-validation   # run1 flake; run2 21/7/0
verify.ps1 run -Capability windows-ui-visual-validation       # run1 1 flake; run2 26/27/0
ctest --preset debug / --preset release-test          # 128/128 both (post-fix)
ffindexstore_bench_rebuild_isolation --scales 200000,1000000 --arms A,B1,B2,C,D,E1,E2,G1
ffengine_bench_large_index_scalability --memory-report --scales 200000,1000000 --rebuild-cycles 10
```
