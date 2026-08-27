# Implementation and validation

## Implementation

The repaired implementation is `f65b3bfc770587b4f28f3fa49f3da68faa4142a3`
(tree `64eb812771966279e332d8ec253394548fd2b7a1`). It adds a test-only,
compile-guarded `SecondaryPruningBoundary` seam for MFT rule pruning, USN rule
pruning, RebuildAll rule pruning, and reconciliation removal. Production
`IndexPipeline` builds do not contain the mutable fault-injection member or
setter.

The four callers now commit the primary durable batch before projection work,
retain durable and projection rows together when the secondary transaction
fails, and suppress success publication. MFT and USN return `false` for retry;
RebuildAll omits the failed volume from its completion callback and rebuild
epoch; reconciliation retains its active pass and does not advance the
searchable epoch. The next retry/rebuild re-evaluates the derived rule
obligation without a schema or protocol change.

## Boundary and recovery evidence

`tests/engine/test_index_pipeline.cpp` independently exercises:

- `TestMftSecondaryFailureIsFailClosedAndRetryable`: one-shot and persistent
  MFT failures, durable row retention, projection/snapshot/aggregate/search
  visibility, epoch stability, and immediate convergence.
- `TestUsnSecondaryFailureIsBoundarySpecificAndRetryable`: USN failure,
  adjacent MFT-boundary non-interference, repeated retention, and retry.
- `TestRebuildSecondaryFailureDoesNotPublishSuccess`: no callback or success
  epoch on failure, retained rebuild obligation, and successful rebuild retry.
- `TestReconciliationRetentionOnSecondaryFailure`: a row seeded before the
  pass, a real Finish removal, active-pass retention, consistent views, epoch
  stability, and successful retry.
- `TestRepeatedSecondaryFailure`: fail/fail/recover reconciliation behavior.
- `TestRulePruningFailureRecoversAfterCloseAndOpen`: failed MFT pruning,
  shutdown, reopen, rebuild, and durable/projection/search/aggregate parity.

The tests deliberately claim Close/Open recovery only; no unrelated search
cancellation test is used as pruning-shutdown evidence.

## Local validation

- `ffengine_index_pipeline_tests`: PASS 1/1, including all new boundary,
  retry, repeated-failure, and restart regressions.
- Neighboring index-store/search/query focused suites: PASS 11/11.
- `cmake --preset debug` and `cmake --build --preset debug`: PASS; final
  `ctest --preset debug --output-on-failure`: PASS 86/86 in 441.23s.
- `cmake --preset release-test` and `cmake --build --preset release-test`:
  PASS; final `ctest --preset release-test --output-on-failure`: PASS 86/86
  in 442.47s.
- `cmake --preset analyze` and `cmake --build --preset analyze`: PASS 88/88,
  warning-clean.
- With `Find-VSToolchain` and `Enter-DevEnvironment -Arch x64`, the ASAN
  slice `ffprotocol_fuzz_tests`, `ffipc_framing_tests`, and
  `ffipc_listener_lifecycle_tests` passed 3/3 in 113.76s.
- `npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate --all
  --strict --no-interactive`: PASS 76/76.
- Source-marker, diagnostic-policy, documentation truth, source provenance,
  project-status, provider, and `pwsh ./verify/verify.ps1 doctor` gates passed.
- `pwsh ./verify/verify.ps1 build`: PASS, 9 passed, 0 failed, 0 skipped.
- `Invoke-ExactTreeQualification.ps1` on the committed SHA above: PASS, nine
  repository-sensitive checks, exact tree bound.
- `git diff --check`: PASS.

## Hosted exact-SHA evidence

Commit `f65b3bfc770587b4f28f3fa49f3da68faa4142a3` was pushed to `main` and
GitHub Actions run `33122565103` completed successfully for that exact SHA.
The run passed repository hygiene, Debug, release-test, Analyze, the aligned
ASAN protocol/IPC/lifecycle 3/3 slice, and unsigned release-candidate
qualification. The run emitted a non-blocking GitHub annotation that
`actions/upload-artifact@v4` targets Node 20; action-runtime maintenance is
tracked by the follow-on campaign.

## Preserved boundaries

No trust store, certificate, signing pin, privileged service, clean-host,
guest/provider, or physical-display state was changed or fabricated. The
unsigned release candidate functionally passed, but its separate hosted
manifest/provenance retention path remains follow-on work and is not claimed
by this change.
