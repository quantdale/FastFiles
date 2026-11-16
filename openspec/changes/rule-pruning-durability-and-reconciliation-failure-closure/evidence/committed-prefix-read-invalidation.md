# Committed-prefix read invalidation correction — 2026-11-17

## Reproduction and root cause

The repository audit reproduced a current-tree consistency defect after the historical pruning-failure campaign. `ApplyMftBatchChanges` and `ApplyUsnBatchChanges` commit their primary batch and mutate the projection, then return false when secondary pruning fails **without invalidating search dependencies**. Returning failure does not roll back the already committed primary transaction.

A deterministic test runs through the actual `SearchIndexed` read plane. Its Matching hook commits an included parent rename (`before` -> `after`), changes an included file's size (41 -> 82), and inserts a separately excluded row. The named secondary-prune fault then fires. Search had captured the old file metadata, but later resolves its parent through the new projection. Both MFT and USN arms returned Completed with **size 41 / folder C:\after**, belonging to neither generation, and reported zero generation restarts.

Evidence lives under `D:/Documents/tryPython/_AgentArtifacts/FastFiles/sessions/20261117-pi-production-readiness/`:

- `debug-prune-generation-red-test.log`: initial MFT counterexample.
- `debug-prune-both-red-test.log`: both MFT and USN fail the exact consistency and invalidation assertions (four failures); primary rollback controls pass.
- `debug-prune-both-green-test.log`: corrected source passes the full engine executable including all prior pruning/retention/retry/rebuild/reconciliation checks.
- `debug-prune-valid-parent-red-test.log`: final fixture explicitly preserves the renamed directory attribute; temporarily removing only the two session-owned invalidation additions reproduces both size-41/new-path failures (plus four corrected epoch-contract failures). Both production additions were restored before final qualification.

The existing epoch-equality assertions incorrectly treated a query dependency invalidation as a successful ingestion acknowledgement. Those four MFT/USN assertions now require invalidation of changed state. They still independently require false return, retained durable/resident rows and successful later retry. Rebuild and failed reconciliation completion/epoch assertions are unchanged.

## Fix and preserved behavior

Advance the affected volume's EntryMutation invalidation epoch under the mutation lock on the two failed-secondary return paths. This makes active searches recapture through the existing bounded restart/durable-fallback mechanism. No new state, wire protocol, callback or publication is introduced.

A rolled-back primary transaction does not mutate the projection or advance its epoch. The new regression verifies that negative control. Secondary failure still returns false, preserves excluded rows consistently in Store and projection, and retains the ordinary retry obligation. Ordinary successful ingestion still invalidates once. Unrelated scoped volumes retain their existing independence.

This correction addresses MFT/USN committed-prefix failure only. It does not certify allocation-exception recovery, all rebuild layouts, crash/power-loss behavior, production signed-service operation, or every possible interleaving.

## Validation frontier

Final corrected source: Debug **146/146** (298.23s), optimized **146/146** (255.05s), affected engine ASan **1/1** (35.13s), full incremental MSVC `/analyze` build warning-clean. Logs: `debug-prune-final-{build,test}.log`, `release-test-prune-final-{build,test}.log`, `asan-prune-final-{build,test}.log`, `analyze-prune-final-build.log`. The earlier verbose green trace observed size 82 / C:\\after and a generation restart in both arms. No external privileged/signing qualification was run.

Source/diagnostic policy and strict OpenSpec pass; full portable suites include doc/lifecycle/status gates. Active C++ LSP was inconclusive; its missing aggregate-initializer warning is already present in the parent implementation (line 1301 of ae0e6fb), and the two typo suggestions are unrelated old text. Native MSVC compilation/analysis, not an empty cache, is the gate.

**Correction COMPLETE:** implementation `da4111e7905b1b5f7094f584f65e84a006303177` was pushed normally. Post-commit Debug and optimized identity/package-trust/reaper/fixture/model/engine gates each passed **6/6** (`*-prune-postcommit-{build,test}.log`). Hosted [run 43947668140](https://github.com/quantdale/FastFiles/actions/runs/43947668140) for that exact SHA completed **SUCCESS, all six jobs**: Debug, release-test, analyze, ASan protocol/IPC/lifecycle, repository hygiene/contracts, and unsigned candidate qualification. `gh run watch --interval 60 --exit-status` returned 0; final headSha/status/job conclusions were independently queried. Task 8.4 is satisfied.

The implementation SHA is an immutable validated baseline, not the SHA of this evidence document. Later documentation-only closure must remain executable-input equivalent; derive live HEAD/remote/CI on resume. Neither this closure nor unsigned candidate qualification authorizes a production signed release.
