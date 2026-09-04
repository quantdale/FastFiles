# NTFS Observer + Visual-Matrix Closure — Defect Ledger (2026-09-04)

Campaign: live MFT/USN observer integration (FC-003/FC-004), visual-matrix
EX-009 closure (FC-007), Hyper-V reconciliation (FC-002/FC-005), soak
gate false-positive (NV-011), post-fix rapid-input closure (FC-001).
Starting SHA `b3dffc5` (= origin/main, clean except pre-existing
unrelated `.opencode/package.json[.lock]` tooling bump, preserved
untouched throughout).

Forensic method first: live-tree topology re-derived before any code
change (no stale `VolumeScanner.*` assumptions). Status values:
CONFIRMED / NOT_REPRODUCED / INVALIDATED / FIXED.

## NV-001 stale NTFS implementation-path assumptions — INVALIDATED

- Symptom risk: an older report referenced NTFS paths that no longer
  matched the tree.
- Live finding: all production paths exist and are wired:
  `src/indexsvc/src/VolumeScanner.cpp` (FSCTL_ENUM_USN_DATA +
  FSCTL_GET_NTFS_FILE_RECORD + MftParser, serving StartVolumeScan),
  `UsnJournalReader.cpp` (FSCTL_QUERY/CREATE/READ_USN_JOURNAL, serving
  OpenUsnJournal), `PrivilegeVerification.cpp`
  (`CaptureCandidateMatrixRow` query+read, `ProbeBackupPrivilegeSufficiency`
  query), `fftest privilege` probe consuming the latter.
- No stale path was relied upon. The real gap was the *certification
  adapter* (NV-002), not production code.

## NV-002 missing production observer connection — CONFIRMED, FIXED

- Root cause: `Invoke-PrivilegedNtfsCertification.ps1` defaulted
  `$MftReader = $null` / `$UsnReader = $null`; only parser unit tests
  (`ffmftparser_tests`, `ffusnrecord_tests`) touched production code.
  Raw-open alone correctly could not PASS, so the lane could never
  PASS even elevated.
- Fix (commit `8b896f0`, rebased as `98375b0`): new `fftest ntfs-observe` probe
  (`src/fftest/src/NtfsObserve.{h,cpp}`, `ffntfsobserve` static lib)
  with `semantics` / `find` / `usn` modes reusing the exact production
  open sequence (GENERIC_READ + backup semantics),
  FSCTL_GET_NTFS_VOLUME_DATA / FSCTL_ENUM_USN_DATA /
  FSCTL_GET_NTFS_FILE_RECORD with production bounds/segment checks,
  production ffmftparser/ffusnparser parsing, and query plus
  non-blocking read-only journal access. Never creates/resizes the
  journal; `mutationOperations` is always 0; RAII handles.
  Targeted FRN fetches (from fsutil FileId truth) avoid full-volume
  scans; FileId is omitted from comparison by the runner's own
  documented rule (Name/Size/ParentPath enforced).
- Runner: `mft/usn-observer-semantics` checks, observer-backed fixture
  confirmation, fail-closed BLOCKED classification preserved;
  `mft/usn-observer-semantics` capabilities added to schema, runner
  manifest, and frontier tasks 4.6/4.7 (FC-006 closure).
- Regression: `ffntfsobserve_tests` (32 checks: classification matrix,
  hex parse/format incl. overflow, enum-walker adversarial buffers) +
  3 new PS contract tests (semantics-only, denied-observer,
  zero-record FAIL). Existing tests 1-6 untouched and green.
- Live proof (non-admin host): real `\\.\C:` open attempted, Win32
  error 5, `BLOCKED` + exit 2, read-only evidence in JSON. Elevated
  PASS remains correctly external.

## NV-003 privilege semantics — CONFIRMED, NO CHANGE NEEDED

- Live proof above: access-denied becomes BLOCKED_NOT_ELEVATED, never
  PASS. Seams distinguish available/elevated/unavailable/unsupported.
  Fail-closed by construction; no implementation gap.

## NV-004 zero-match test execution hazard — NOT_REPRODUCED

- The long-run runner rejects nonexistent targets with INVALID_TARGET
  (contract Test 3), and the NTFS observer path verifies JSON output
  presence (null output = FAIL when a backend resolved, BLOCKED when
  none exists). No silent zero-match PASS exists.

## NV-005/NV-006 missing semantic MFT/USN proof — CONFIRMED, FIXED

- Same fix as NV-002. MFT semantics: NTFS volume + real handle +
  volume metadata + first ENUM buffer walked with production bounds +
  up-to-16 record parses + valid journal identity. USN semantics:
  query + non-blocking read + record decode. Zero records = FAIL.

## NV-007 visual storage semantic context gap (EX-009) — CONFIRMED, FIXED

- Root cause (two layers, both proven live):
  1. `TreemapView::BuildTree` never called `RecomputeTotals`, and
     degraded scopes (volumeRowId == 0) can never receive engine
     folder aggregates, so every share stayed unknown ("--") and no
     percent was ever parseable -- even engine-connected. Debug
     (fail-closed) builds additionally never launch the unsigned dev
     engine, so sandboxes never connected at all.
  2. The matrix never browsed the fixture sub-scope, so its listing
     was absent from the snapshot.
- Fix (commit `4bce753`, rebased as `343d22e`): pure `ResolveDegradedTreemapTotals` helper
  (snapshot-aggregate fallback mirroring the storage list path;
  complete subtrees exact, missing descendants explicitly Partial);
  `BuildTree` resolves degraded dirs + `RecomputeTotals` before layout;
  parent-known gated on partial roots (shares of an incomplete
  denominator stay unknown, never guessed). Matrix warms the Sub scope
  by genuine browsing; runner accepts `-Preset debug-diagnostic` for
  engine-connected local runs (fail-closed builds keep the honest
  BLOCKED skip).
- Regression: fixture-shape snapshot-aggregate test + fallback helper
  matrix test (headless). Full Debug 99/99 x3, release-test 99/99 x2,
  Analyze, ASAN all green.
- Live proof: visual-matrix 2/2 x 14/14 PASS on the diagnostic build
  (top 10/20/30/40 sum 100, drill-down fileD 100%, zero BLOCKED).
  Frontier 5.1 -> READY_EXTERNAL with the engine-connected contract.

## NV-008 stale external commands — REVIEWED, ONE UPDATE APPLIED

- Inventory contract test enforces script/parameter/schema validity
  across all 25 tasks (green). The single stale aspect found: task 5.1
  lacked the engine-connected preset, so external executors would
  always BLOCK. Fixed (`-Preset debug-diagnostic` + reason text).
  No Hyper-V-specific requirement was added (§17).

## NV-009 evidence/report consistency — NOT_REPRODUCED

- Test-EvidenceConsistency green (10/10 suites); machine-readable JSON
  is authoritative; markdown derives from it. No prose/JSON drift
  found in this campaign's artifacts.

## NV-010 exact-tree provenance — DOCUMENTED CAVEAT, NOT A DEFECT

- All long-run artifacts record `exactTreeBound=false` SOLELY because
  of 2 pre-existing unrelated `.opencode` tooling files (plugin
  1.18.11 -> 1.18.27 bump, preserved untouched per repo policy).
  Validated content is commit-bound (`8b896f0`/`98375b0`, `4bce753`/`343d22e`,
  `4d94218`/`a8662a0`; the rebase touched docs only (empty `git diff`
  on src/tests/harness), so all validation evidence transfers exactly.
  `-RequireExactTree` was attempted and correctly
  refused. No unrelated work was staged, stashed, or reset.

## NV-011 soak handle-gate false positive — CONFIRMED, FIXED

- Symptom: interactive 600 s soak FAIL (handle leg +578/slope 0.96;
  one transient palette miss on the first cycle).
- Root cause (NOT a product leak):
  - 20 consecutive settings open/close cycles: handles 1226 -> 1218
    flat (threads decrease). No per-cycle leak.
  - 15 cycles each of search/dual-pane/palette: one-time +536 step on
    the FIRST search-panel open, then flat across 40 further cycles +
    idle. Bounded first-open surface caches (D2D/fonts/imagelists).
  - GDI/USER flat; second-half medians flat (handles +2, WS +0.7 MB
    over ~300 s). Prior PASS baseline ran idle mode (zero transitions),
    so the full-window linear slope gate never faced step growth.
  - The transient palette miss cleared on re-run (12/12 activities).
- Why existing tests missed it: prior soak evidence was idle-only.
- Fix (commit `4d94218`, rebased as `a8662a0`, harness-owned scope): handle verdict gains a
  settled path (second-half median drift <= 25) alongside the unchanged
  fast path; runaway/late-onset shapes still FAIL (pinned). Test 7
  warmup 2 s (was 0 s): a zero-warmup 2 s window samples launch-settling
  noise (~190 KB/s vs ~8 KB/s on the same binary), contradicting the
  runner's own STEADY_STATE definition; duration 4 s keeps it fast.
- Live proof: 600 s interactive soak PASS (12/12, drift=2, graceful
  shutdown, zero orphans); Test-ResourceSoak suite green.

## FC-001 rapid-input focus race — CLOSED

- Post-fix (fix itself in starting SHA `b3dffc5`) stress: rapid-input
  100/100 PASS, 1100/1100 assertions, median ~15.0 s, on commit
  `4bce753`/`343d22e`. Intermittent iteration-22 failure mode not observed.
  Reopen was justified: product code on the exact path changed.

## FC-002/FC-005 Hyper-V channel/readiness — RESOLVED, NO LOCAL WORK

- Verified live: `ESTABLISH_SECURE_GUEST_CHANNEL` fail-closes
  BLOCKED/GUEST_CHANNEL_CREDENTIAL_ABSENT; provider tasks declare
  `requiredCredentials: [provider-api-key-or-guest-credentials]` with
  REQUIRED-BUT-UNAVAILABLE status; Hyper-V PowerShell module absent on
  this host (`verify.ps1 doctor`: SKIPPED). Per campaign §17 no
  Hyper-V credential/channel machinery will be built and no Hyper-V
  requirement added; the fail-closed runner + declared credential
  requirement is the truthful end state. Failure-matrix tests cover
  the channel-failure path (provider suite green).

## FC-008/FC-009 campaign/lifecycle truth — CLOSED BY THIS LEDGER

- Remaining local work after this campaign: none. Tasks 7.1/7.2
  executed (see evidence below); 6.3/7.3 close with the hosted-CI
  binding on the final tip. WORKSTATE head entry supersedes the stale
  `none locally` checkpoint noted in FC-009.
