## 1. Baseline, audit, and contract

- [x] 1.1 Reconcile the Phase 9 branch, `origin/main`, working tree, open task ledgers, current CI evidence, and PR #6 against the live SHA; record discrepancies in WORKSTATE. — live branch/remote reconciliation is recorded in `docs/agent/WORKSTATE.md`; PR inspection/CI refresh remains a final handoff task.
- [x] 1.2 Preserve the generation audit ledger in `design.md` and verify every `IndexPipeline` query-generation mutation has a named Phase 10 disposition before source changes. — D8 covers every current `++generation_` site and the related `VolumeSessionManager` callers.
- [x] 1.3 Validate the proposal, design, delta specs, and task ledger with strict OpenSpec validation. — `openspec validate search-availability-under-index-churn --type change --strict --no-interactive` passes.
- [x] 1.4 Run the focused Phase 9 engine/read-session/lifecycle tests at the unchanged baseline and record the result before implementation. — direct Debug executables for pipeline, concurrency, read sessions, UI lifecycle, and seeded indexed mode all pass; CMake was unavailable on the non-developer shell.

## 2. Explicit query-visible epoch model

- [x] 2.1 Add named query-visible data/configuration/dependency-set epoch state and mutation-reason helpers to `IndexPipeline`; remove direct arbitrary query-generation increments. — `IndexPipeline` now exposes named query-visible data/configuration/dependency epochs and `QueryMutationReason`; direct `++generation_` sites are gone.
- [x] 2.2 Advance per-volume epochs only after successful searchable row, volume visibility/configuration, rebuild/forget, or relevant residency changes; preserve lifecycle cancellation separately. — focused concurrency coverage proves row/configuration/residency/lifecycle invalidation and cancellation behavior.
- [x] 2.3 Make volume resolution and rule/availability updates distinguish new/changed state from repeated/no-op state without adding broad Store synchronization. — `GetOrCreateVolume` and `SetVolumeAvailable` report actual state changes; no-op/failure paths are covered.
- [x] 2.4 Classify scan cursor, journal position, scan-complete, reconciliation timestamp, bulk-scan mode, checkpoint, `ANALYZE`, and maintenance writes as operational metadata and count avoided invalidations. — metadata-only methods use `RecordMetadataOnlyUpdateLocked`; the active-query barrier test proves no restart.
- [x] 2.5 Make reconciliation completion advance query-visible state only when a removal or other searchable mutation was actually committed and applied. — no-removal and rule-aware removal cases are covered by the concurrency and pipeline reconciliation tests.
- [x] 2.6 Add deterministic epoch/metadata-only regression tests covering MFT, USN, cursor, journal, reconciliation, maintenance, no-op, and failure paths. — `ffengine_index_pipeline_concurrency_tests` passes the condition-variable epoch and metadata suite.

## 3. Scope-aware dependency capture

- [x] 3.1 Capture searchable volume prefixes, rules, availability/configuration epochs, and the query's relevant dependency set under the existing short pipeline lock. — `SearchIndexedReadPlane` captures these bounded context fields under `mutex_` and opens the independent reader before releasing it.
- [x] 3.2 Limit current-drive/current-folder/subtree source enumeration and invalidation checks to the matching volume; retain all-volume dependency validation for global search. — scoped and global barrier tests pass, including unrelated-volume mutation avoidance.
- [x] 3.3 Cover unrelated-volume writes, scoped relevant writes, global multi-volume writes, availability transitions, forget, and lifecycle behavior with barrier-based tests. — the concurrency lifecycle, scoped, and global dependency tests pass without sleeps.
- [x] 3.4 Prove projection residency/budget changes invalidate only affected captured volumes and never use stale cursors or freed projection rows. — the forced-eviction barrier test completes coherently and reports no reader leak or stale-cursor result.

## 4. Rule-aware coherent query context

- [x] 4.1 Add an internal bounded candidate-filter seam to `ffsearch` that can require path materialization without changing the wire request or default search semantics. — `SearchRequest::candidateFilter` is internal/default-empty and is applied by the bounded accumulator/view paths.
- [x] 4.2 Apply captured include/exclude rules to hot and durable candidates using canonical reconstructed paths, preserving Unicode/case, scope, filters, ordering, caps, and allocated-size knownness. — captured-rule filters use existing projection/durable path reconstruction; existing equivalence/allocated-size/Unicode tests remain green.
- [x] 4.3 Ensure a rule change during an active query invalidates the old context and cannot publish rows excluded by the newly captured rules; retain deferred reconciliation semantics for durable cleanup. — the active rule-change barrier test proves restart plus no excluded hot publication, while cold filtering and reconciliation cover retained durable rows.
- [x] 4.4 Add include/exclude mutation and reconciliation tests for hot, cold, scoped, global, rename/move, and partial-parent-chain cases. — concurrency coverage passes for global hot, scoped cold, rename/move in/out, rule-aware reconciliation removal, and partial-parent fail-closed behavior.

## 5. Durable fallback execution

- [x] 5.1 Add the durable-snapshot-only fallback after bounded relevant hybrid restarts, with one fresh independent read session captured alongside its searchable context. — fallback follows the fixed three-restart hybrid budget and opens one fresh query-only WAL session per bounded attempt.
- [x] 5.2 Stream every selected durable volume through the existing bounded `SearchAccumulator`, using same-snapshot parent lookup/path reconstruction and no projection reload or mutation-lock hold during long work. — fallback streams selected volumes outside `mutex_`, with durable path lookup on the same reader and no projection hydration.
- [x] 5.3 Bound configuration-consistency fallback retries; treat row commits after snapshot start as valid point-in-time churn while rejecting configuration/lifecycle changes before publication. — fallback retries configuration changes only within the fixed two-attempt bound; snapshot stability and concurrent writer tests pass.
- [x] 5.4 Preserve fail-closed behavior for reader open/begin/busy/stream/path/serialization failure and never publish partial accumulated rows. — read-session/concurrency fault tests pass for open, snapshot, stream, path, and serialization failure with empty responses.
- [x] 5.5 Add fallback diagnostics/timings for attempt, success/failure, snapshot epoch/dependencies, streamed rows, reader lifecycle, and cancellation without user paths. — `SearchTimings`, `ReadPlaneDiagnostics`, and the benchmark report bounded numeric fields only.

## 6. Sustained-churn and lifecycle regression coverage

- [x] 6.1 Add a condition-variable MFT sustained-write test that exceeds the historical restart budget and proves coherent durable fallback completion. — four validation mutations force the old budget to be exceeded; the query completes from fallback with the final coherent row set.
- [x] 6.2 Add equivalent USN rename/move/delete/metadata churn coverage and prove no mixed hot-generation response. — the deterministic USN churn test passes through fallback and verifies rename/move/delete semantics.
- [x] 6.3 Prove one durable SQLite snapshot remains coherent while the writer continues committing, including old/new row visibility and subsequent fresh-reader health. — the reader snapshot is established before streaming; a concurrent commit is absent from the fallback result and visible to the next search.
- [x] 6.4 Cover fallback cancellation, mid-stream/read-session fault, statement/transaction release, subsequent writes, and subsequent searches. — cancellation/fault tests pass with zero partial candidates, zero active readers, and successful recovery.
- [x] 6.5 Cover `IndexPipeline::Close`, `UiServer::Stop`, client disconnect, coordinator queue cancellation/supersession, and no post-cancel publication while fallback is active. — pipeline/UI lifecycle suites pass, including the new active-fallback stop/join test and existing queue/disconnect coverage.
- [x] 6.6 Preserve and re-run hot/cold equivalence, allocated-size, ordering, Unicode/case, filters, scopes, caps, seeded indexed-mode, and UI-server lifecycle suites. — focused pipeline, search, read-session, seeded, concurrency, and UI lifecycle executables all pass.

## 7. Boundedness and scalability evidence

- [x] 7.1 Extend `bench_large_index_scalability` with hot selective/broad, cold selective/broad, sustained-ingestion broad, durable-fallback broad, and fallback-cancellation stages. — final local Debug 1K run includes all requested stages and writes a validated machine-readable report.
- [x] 7.2 Record reader/worker/queue bounds, projection memory, fallback rows, writer progress, lock wait/maximum hold, WAL behavior, and cancellation structurally without noisy wall-clock thresholds. — final report records peak searches/readers, active cleanup, memory/snapshot probes, fallback rows, contention progress, lock timings, and cancellation outcome.
- [x] 7.3 Investigate any repeatable fast-path degradation near 15–20% with stage attribution; record host contention honestly when it explains variance. — no repeatable Phase 10 fast-path regression was established; Phase 9 variance remains documented as host CPU contention and no timing gate was added.
- [x] 7.4 Add a broad evicted-dataset fallback memory regression proving no full owning candidate vector or projection hydration. — the forced-eviction benchmark runs broad fallback with bounded accumulator diagnostics; focused cold-search tests prove no reload/hydration and bounded readers.

## 8. Validation-harness portability investigation

- [x] 8.1 Reproduce the historical local Debug failures for packaged PowerShell child tests and the portable Authenticode fixture with focused commands and preflight diagnostics. — the initial full Debug CTest reproduced the package-path `CreateProcess` failures and AuthentiCode fixture failure; local preflight found PowerShell PKI, certutil, and SDK SignTool availability.
- [x] 8.2 If a repository launcher/fixture defect is proven, harden executable resolution, temporary fixture handling, deterministic cleanup, or host-policy classification without skipping mandatory tests or changing trust stores. — `cmake/FastFilesPowerShell.cmake` now prefers the per-user execution alias when the cached package binary is not CTest-launchable; no trust store was changed.
- [x] 8.3 Re-run the focused portable tests and document any remaining environment-only result as `BLOCKED_ENVIRONMENT`/`REQUIRED-BUT-UNAVAILABLE`. — focused AuthentiCode, PowerShell children, and the full Debug suite pass 78/78 after the resolver; no remaining local portability blocker is open.

## 9. Canonical synchronization and documentation

- [x] 9.1 Sync proven delta requirements into canonical OpenSpec specs and preserve Phase 9 design/benchmark evidence as historical rather than rewriting it. — canonical index-engine, indexed-read-plane, independent-read-session, and filesystem-store specs are synced; the prior Phase 9 delta was reconciled and strict validation passes.
- [x] 9.2 Update `CODE_INDEX.md`, `docs/agent/WORKSTATE.md`, `docs/benchmarks/large-index-scalability.md`, release/verification guidance, and stale Phase 9 comments with the final epoch/fallback contract. — code index, release README, benchmark evidence, WORKSTATE, and historical Phase 9 wording are reconciled.
- [x] 9.3 Run source-marker, diagnostic-policy, documentation-drift, strict OpenSpec, and generated-artifact/hygiene checks after documentation edits. — source markers, diagnostic policy, `ffdoc_drift_tests`, strict OpenSpec **65/65**, `git diff --check`, and tracked/generated-artifact review pass.

## 10. Release-train handoff

- [ ] 10.1 Run Debug configure/build/full CTest, release-test configure/build/full CTest, Analyze, ASAN protocol/IPC fuzz, doctor, verify build, seeded/concurrency/read-session/UI lifecycle tests, and applicable release-candidate gates.
- [ ] 10.2 Inspect PR #6 against the exact Phase 10 baseline; document full supersession or port only a uniquely useful behavior with tests, and close it only if authenticated GitHub write access exists.
- [ ] 10.3 Fetch/prune remotes again, integrate current `main` using the repository's safe history policy, and verify the Phase 8 + Phase 9 + Phase 10 train is intact with no stale PR code.
- [ ] 10.4 Commit logical implementation/spec/validation waves, push normally if authorized, inspect hosted CI for the exact final SHA, and record the live result.
- [ ] 10.5 Run `git diff --check`, leave the working tree clean, and update WORKSTATE with exact branch/SHAs, validation results, external blockers, PR state, and exactly one next recommended frontier.
