# Tasks — harden-index-store-concurrent-access

Baseline when authored: `main` at `57b7cd41e36c992fb68cdfb4894df761f3f8a92a`, after `allocated-size-search-filter` completed. Live Git state wins on resume.

Do not mark tasks complete from source inspection alone when executable evidence is required.

## 1. Reconcile and audit

- [x] 1.1 Fetch/prune `origin`, reconcile `main`, verify a clean tracked worktree, read `AGENTS.md`, `docs/agent/WORKSTATE.md`, this change, canonical `filesystem-index-store`, and the current Store/test implementation.
- [x] 1.2 Inventory current concurrency-relevant behavior: `Store::Open`, WAL/synchronous pragmas, `ApplyBatch`, `ForEachEntry`, `CheckpointPassive`, `CheckpointIfWalExceeds`, `Close`, integrity/reopen, and every current same-file/multi-handle test.
- [x] 1.3 Record the pre-change evidence gap precisely: canonical spec promises reader/write overlap, Store enables WAL, but no existing `tests/indexstore` case keeps a read statement active on one SQLite connection while a second `Store` commits to the same file.
- [x] 1.4 Run the existing index-store focused tests before modification and record baseline results.

## 2. Deterministic two-connection fixture

- [x] 2.1 Add a test fixture that creates one existing database, seeds a volume plus enough entries to keep iteration meaningful, and opens two independent `Store` objects on that exact same file.
- [x] 2.2 Ensure each `Store` connection is used by one thread at a time; do not test concurrent method calls on one Store object or imply Store itself is thread-safe.
- [x] 2.3 Implement barrier/condition-variable synchronization that pauses a real `ForEachEntry` callback while its SQLite statement is active. No fixed sleep may establish the overlap.
- [x] 2.4 Add a bounded watchdog so a regression becomes a deterministic FAIL with useful phase diagnostics instead of hanging CTest.

## 3. WAL reader/writer contract

- [x] 3.1 While the reader statement is held open, commit a real multi-record `ApplyBatch` transaction through the second Store connection.
- [x] 3.2 Prove the writer completes within the bounded watchdog without waiting for the reader statement to finish and without surfacing a lock/busy failure for this supported overlap.
- [x] 3.3 Prove the held reader completes against a coherent pre-commit snapshot: no newly committed row appears halfway through the already-started statement and no row is duplicated/torn.
- [x] 3.4 After the held read completes, issue a fresh read on the same reader connection and prove the committed rows are visible.
- [x] 3.5 Include update/remove as well as insert behavior if needed to prove snapshot isolation is not accidentally specific to an append-only fixture.

## 4. Checkpoint and teardown behavior

- [x] 4.1 With a read snapshot active, invoke `CheckpointPassive()` (or the current passive equivalent) on an independent connection and prove it returns in bounded time without invalidating the reader.
- [x] 4.2 Do not require PASSIVE checkpoint to checkpoint frames pinned by the active reader; assert safety/boundedness, not impossible full-drain semantics.
- [x] 4.3 Release the reader, perform a subsequent ordinary checkpoint/read, and verify the database remains usable.
- [x] 4.4 Close both Store instances, reopen the same file, run integrity check, and verify the final durable row set.

## 5. Repetition and contention hygiene

- [x] 5.1 Repeat the supported held-reader + writer-commit sequence deterministically (target at least 20 iterations when runtime is reasonable) with zero retry-until-pass behavior.
- [x] 5.2 Record per-iteration or summarized writer-commit, checkpoint, and total durations sufficient to identify a lock/deadlock plateau.
- [x] 5.3 Verify no iteration leaves stale database/WAL locks that prevent the next supported operation or final reopen.
- [x] 5.4 If the supported overlap exposes a real defect, implement the narrowest bounded Store-layer fix. Do not add unlimited retries/sleeps or broaden the contract to multiple simultaneous writers.

## 6. Regression coverage

- [x] 6.1 Preserve existing schema migration, known-zero/unknown allocated-size, partial-update preservation, batch atomicity, WAL recovery, checkpoint sizing, vacuum gating, UTF conversion, integrity, volume identity, and reopen tests.
- [x] 6.2 Confirm no schema/user_version change and no protocol/projection wire change.
- [x] 6.3 Confirm one-writer/many-independent-reader semantics are documented; do not claim `Store` object thread safety or multi-writer support.
- [x] 6.4 Update `tests/indexstore/CMakeLists.txt` only as needed; prefer extending `ffindexstore_store_tests` unless a separate focused executable materially improves isolation/diagnostics.

## 7. Evidence and repository truth

- [x] 7.1 Add `evidence/implementation-evidence.md` documenting the pre-change gap, synchronization method, snapshot truth table, repeated results, checkpoint behavior, any implementation fix, and full gate results.
- [x] 7.2 Reconcile the old `close-independent-validation-gaps` deferred note that lists same-SQLite-file concurrency coverage as future work only after this executable change is complete; preserve the historical note as historical context rather than rewriting old evidence.
- [x] 7.3 Update `docs/agent/WORKSTATE.md` with a concise current session record and next action based on actual results.
- [x] 7.4 Sync canonical `filesystem-index-store` only through normal OpenSpec workflow; do not weaken its existing concurrent-read guarantee.

## 8. Full validation and handoff

- [x] 8.1 Run the focused index-store test repeatedly and the complete index-store test target.
- [x] 8.2 Configure/build Debug and run all Debug CTest; every discovered test PASS, none Not Run.
- [x] 8.3 Configure/build release-test and run all release-test CTest.
- [x] 8.4 Configure/build analyze and remain warning-clean under `/W4 /WX`.
- [x] 8.5 Run `pwsh ./verify/verify.ps1 build` and `pwsh ./verify/verify.ps1 doctor`.
- [x] 8.6 Run source-marker, diagnostic-policy, doc-drift checks, `openspec validate --all --strict --no-interactive`, and `git diff --check`.
- [x] 8.7 Review complete diff for scope; do not stage DB/WAL/SHM files, build output, run logs, benchmark scratch data, or other generated artifacts.
- [x] 8.8 Commit and push validated changes to `origin/main`; verify `HEAD == origin/main` and a clean tracked worktree.
- [x] 8.9 Query hosted GitHub Actions for the exact final pushed SHA and report the available run/job status without making a recursive CI-recording commit.

## Completion gate

`COMPLETE` requires all of the following:

- a real reader statement on one Store/SQLite connection overlaps a real committed batch on a second connection to the same DB file;
- the writer completes boundedly while the reader remains held;
- the held reader sees a coherent pre-commit snapshot and a fresh subsequent read sees post-commit state;
- passive checkpoint with an active reader is bounded and does not invalidate/corrupt the reader;
- repeated overlap leaves no stale lock/busy state and final integrity/reopen is clean;
- no unsupported multi-writer or Store-object-thread-safety claim is introduced;
- all focused/native/build/spec/policy gates pass;
- final state is pushed to `main`, clean, and hosted CI is queried for that SHA;
- privileged production-signing/MFT-USN/clean-host work remains untouched.
