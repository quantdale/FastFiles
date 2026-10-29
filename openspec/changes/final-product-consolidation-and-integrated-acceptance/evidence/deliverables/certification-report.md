# Certification report — integrated candidate (2026-10-29)

All gates executed on `integration/fastfiles-final-product-20261029` in the
isolated worktree `D:/Documents/tryPython/FastFiles-worktrees/final-product-20261029`,
against the integrated tree containing all 23 PR heads (verified 23/23
reachable) plus the two reconciled local inputs. MSVC 14.50 (VS 2022 Community
17.14), Ninja, x64. Toolchain discovery via `verify/core/Toolchain.psm1`.

## Stage gates (design D2 — progressive)

| Stage | Heads merged | Gate | Result |
| --- | --- | --- | --- |
| Core | #14, #17 (contains #15) | Debug CTest | 131/131 |
| Nav/storage | #19, #20, #29 (contains #18/#26) | Debug CTest | 133/133 |
| Operations | #24, #36, #34 (contains #21/#22/#23/#25/#27/#30/#32/#33) | Debug CTest | 141/141 |
| Verification + theme | #35 (contains #28/#31), #16 | Debug CTest | 143/143 |
| Full composite | all | release-test CTest | 143/143 |

Two lifecycle-state defects found by the stage gates were fixed red-first at
the gate (`fit-native-command-palette-to-results`, `preserve-e2e-failure-evidence`
arrived `active` with zero open tasks; corrected to `completed` — see
`../reconciliation-and-review.md` for the honesty audit).

## Final ladder (all on the integrated tree)

| Gate | Command / scope | Result |
| --- | --- | --- |
| Debug build + CTest | `dev.ps1 test -Preset debug` (all configured tests) | **143/143 PASS** |
| release-test build + CTest | `dev.ps1 test -Preset release-test` | **143/143 PASS** |
| MSVC Analyze | `dev.ps1 build -Preset analyze`, all 507 targets | **0 warnings** (3 C6001 false positives in #36-chain test TUs fixed with documented scoped suppressions per `tests/search/test_ondisk_filter.cpp` precedent) |
| ASan slice | `ci.yml:83-114`: ffprotocol_fuzz_tests, ffipc_framing_tests, ffipc_listener_lifecycle_tests, ffvolumescanner_terminal_tests | **4/4 PASS** |
| Source markers | `verify/check_source_markers.ps1` (248 production files) | PASS |
| Diagnostic policy | `verify/check_diagnostic_policy.ps1` | PASS (release caches do not enable the diagnostic escape hatch) |
| Doc drift + line endings | `tests/drift/check_doc_drift.ps1` | PASS |
| Whitespace | `git diff --check` | PASS |
| Strict OpenSpec | `openspec validate --all --strict` | **106/106 PASS** |
| Lifecycle model | `Get-ProjectStatusLifecycleModel` | PASS (0 structural / 0 evidence / 0 semantic) |
| Native E2E | `dev.ps1 test-e2e` (debug-diagnostic, real exe, UIA+input) | **8/0/0** — launch-ready, navigation-browse, search-user-flow, storage-terminal-geometry, preview-terminal-state, file-operations, cold-restore, process-lifecycle (identity check: PASS, boundToHead) |
| UIA core smoke | `Invoke-UiaLongRunCertification.ps1 -Scenario core-smoke` | **25 iterations PASS / 0 fail** (uia-core-smoke, uia-keyboard-navigation, dual-pane) |
| Visual matrix | `-Scenario visual-matrix` | **PASS — 350 assertions / 0 failed** (storage percentages 10/20/30/40 + drill-down 100%, scope warmup, pane-width regimes, light/dark captures) |
| Application lifecycle | `-Scenario application-lifecycle` | PASS (clean cleanup, no residue) |
| Rapid input | `-Scenario rapid-input` | PASS |
| Search snapshot-refresh probe | `Test-SearchSnapshotRefresh.ps1` (journey B4) | **PASS** (hidden create/delete refreshes an open panel without retype or identity retarget) |
| Search reopen-scope probe | `Test-SearchReopenScope.ps1` (journey B2/B3) | **PASS** (query+scope preserved; exact result reveal; exact folder selection) |

Exclusions (never executed, by policy): the restricted live process-reaper
termination fixture (registered; requires a separately permitted environment);
privileged trust-store integration tests (opt-in `privileged-trust` label only);
production signing gates (no pins configured — Release stays fail-closed by
design).

## Performance preservation (task 10.1/10.2)

Bounded affected re-measurement (`ffengine_bench_large_index_scalability
--scales 200000 --memory-report`, release-test, provenance-embedded report at
`build/perf-check/memory-report-200k-integrated.json`, identity `600ef86e`):

| Metric | Integrated candidate | Certified baseline (post-fix) | Pre-fix pathology |
| --- | --- | --- | --- |
| 200k replacement-rebuild cycles (4) | 896.9 / 782.9 / 902.3 / 855.0 ms | 823–872 ms | 3,856–5,085 ms |
| payload identity across cycles | identical | identical | identical |
| cold reload failures | 0 | 0 | n/a |
| private bytes across cycles | ~129–130 MB flat, teardown ~5 MB | ~flat, teardown ~6 MB | — |

Verdict: within measurement noise of the certified numbers (±8%, shared host);
no material common-path regression. Historical large-scale evidence (1M/2M
matrices, search planner 56–189x selective classes, 31–34x rebuild) remains
applicable by source-equivalence of the affected subsystems (the integration
introduced no changes under `src/indexstore/src/Projection.cpp` hot paths
beyond the audited measured-growth guard that replaced the disproven flat
prior — see `../local-input-reconciliation.md`).

## Known intermittents and preserved first failures (design D3)

- `search-deep-result-navigation` flake family (UIA long-run variance) and the
  palette-precondition visual flake: historical; not observed in this ladder's
  runs (25/25 core, 350/350 visual).
- Historical cold-restore ColumnView omission and preview terminal-state
  failures (recorded on the old simulation branch): this ladder's `cold-restore`
  and `preview-terminal-state` E2E scenarios PASS on the integrated candidate.
  Per design D3 the later passes do not erase the preserved first failures;
  both observations stand in the record.
- Journey I (Explorer interop) remains UNQUALIFIED — see
  `native-acceptance-report.md` for the inherited bounded non-delivery
  diagnosis and cause class.
