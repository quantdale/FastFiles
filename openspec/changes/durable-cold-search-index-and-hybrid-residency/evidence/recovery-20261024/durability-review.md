# Lane B — durability review

## Review

**Reviewed HEAD:** `8c4c8481f312aa4651a5c2bab3cb61e0b0e50af0`  
**Implementation baseline:** `5d7e98ae412cd893e47e7ebc9a9bdea4c609d0ac`

Read-only review; no builds, tests, repository edits, or prohibited evidence directories accessed. Reproduction recipes below were **not executed**.

### Scope and provenance

- `watchdog_diff` confirmed a clean working tree at reviewer-launch HEAD.
- Inspected the parent-generated exact-range artifacts:
  - `build/campaign/recovery-20261024/evidence-range.txt`
  - `build/campaign/recovery-20261024/pr15-executable.diff`
- The supplied `5d7e98a..8c4c848` range contains documentation/evidence changes, not product changes. The product findings below concern PR15’s executable delta from `d3884ee`, as supplied in the second artifact.
- Read applicable root `AGENTS.md`, relevant WORKSTATE checkpoint, durable-cold-search design/tasks/spec, and reconciliation-failure-closure design/tasks. Previous review verdicts were not treated as certification.

## Correct

- **Store batch writes are transactionally coupled.** `Store::ApplyBatch` begins one transaction, updates entries and candidate structures together, rejects failed maintenance, and checks COMMIT before reporting success (`src/indexstore/src/Store.cpp:1327–1456`). The inspected write-error paths do not deliberately advance the projection after failed persistence.
- **Committed-prefix bookkeeping handles ordinary chunk failure correctly.** `FinishReconciliationPass` records IDs only after successful `ApplyBatch`; a later failed chunk retains the obligation, removes only committed IDs from the projection, and advances the searchable epoch for that prefix (`src/engine/src/IndexPipeline.cpp:1093–1143`).
- **The partial-failure regression is discriminating.** `TestReconciliationMassRemovalConvergesAcrossChunks` faults operation 5,501, checks 5,000 committed removals, retained obligation, matching projection state, epoch advancement, and same-process retry (`tests/engine/test_index_pipeline.cpp:2294–2356`). This is not restart or actual disk-full coverage.
- **Read snapshots are established before releasing the pipeline mutex.** `StoreReadSession::Open` forces a read after BEGIN, rather than leaving snapshot acquisition deferred (`Store.cpp:1697–1726`).
- **Normal readiness validation is snapshot-bound and fail-closed.** `CandidateIndexUsable` checks readiness and host signature; candidate delivery is staged before callbacks, avoiding partial-delivery duplication on fallback (`Store.cpp:1791–1816`, `1991–2077`).
- **ANALYZE is no longer invoked per ingestion batch.** Ingestion accumulates debt; maintenance clears it only after successful ANALYZE (`IndexPipeline.cpp:540–567`). This removes the former per-batch full-database work.

## Findings

### P1 — Missing candidate table is silently recreated and accepted as complete

**Location:** `src/indexstore/src/Store.cpp:791`, `904–942`

`Open` runs `CREATE ... IF NOT EXISTS` for candidate structures before checking validity. The subsequent v3 check validates only:

- ready marker;
- host-fold signature;
- map-row count versus entry count.

It does not detect that `entry_name_terms` was just recreated empty. Consequently, a missing derived table can become an apparently ready, empty accelerator, producing successful incomplete searches instead of fallback.

**Minimal reproduction:**

1. Create a v3 store containing one ordinary ASCII name, `durable_hit.txt`; close it.
2. Through raw SQLite, execute only `DROP TABLE entry_name_terms;`.
3. Reopen with `Store::Open`.
4. Query `ForEachEntryWithAsciiNameUntil(volume, "durable_hit", visitor)`.

Source trace: schema initialization recreates empty FTS structures; intact map/meta pass validity checking; admission counts zero candidates; the candidate stream returns success without the canonical row. `ForEachEntryUntil` still finds it.

This contradicts the spec’s **“Generator failure falls back to the canonical scan”** scenario for missing structures. It does **not** establish that ordinary process termination spontaneously drops tables.

**Smallest fix:** Detect missing required candidate structures before creating them, and force rebuild/readiness invalidation. Add a regression dropping the FTS table without also clearing the marker. The existing tamper test clears `state` itself and therefore misses this case (`tests/indexstore/test_store.cpp:1361–1399`).

### P1 — Rebuild treats failed volume enumeration as successful exhaustion

**Location:** `src/indexstore/src/Store.cpp:648–655`, `687–713`

`RebuildCandidateIndex` enumerates volumes using:

```cpp
while (sqlite3_step(volumesStmt) == SQLITE_ROW)
```

but never verifies that termination was `SQLITE_DONE`. An error can therefore supply an empty or partial volume list. The final check merely confirms that `COUNT(entry_name_index_map)` returned a row; it never compares that count with canonical entries before setting `state=1`.

Migration then accepts the rebuild and bumps the schema version without another completeness check (`Store.cpp:827–894`).

**Minimal fault reproduction:**

1. Seed a nonempty store; set `user_version=2`.
2. On the writer connection, inject one `SQLITE_INTERRUPT` specifically into `SELECT id FROM volumes;`, then permit subsequent statements.
3. Call `Open`.
4. Check whether it returns success with `state=1`, fewer map rows than entries, and missing ASCII-query results.

A test can capture the connection through a SQLite auto-extension and use trace/progress callbacks to interrupt only that statement. SQLite resets the interrupt flag when the next statement starts with no other active statement (`third_party/sqlite/sqlite3.c:94482–94488`), so subsequent successful statements are a supported path, not an assumption that errors persist indefinitely.

**Smallest fix:** Require terminal `SQLITE_DONE` before accepting the volume list. Also compare canonical/map coverage before publishing readiness as defense in depth.

**Evidence qualification:** The unchecked error-to-success control flow is verified from source; the injected reproduction was not run.

### P2 — Move assignment preserves readiness from the wrong snapshot

**Location:** `src/indexstore/src/Store.cpp:1679–1692`; `src/indexstore/include/ffindexstore/Store.h:390–391`

The added `candidateReadinessChecked_` and `candidateIndexUsable_` members are not transferred or reset by move assignment. The destination receives another connection/snapshot but retains its old readiness verdict.

**Minimal reproduction:**

1. Reader A executes an eligible query against a ready snapshot, caching `true`.
2. A writer clears the ready marker and deletes candidate terms, simulating the supported rebuild window.
3. Open reader B against that newer snapshot.
4. Execute `*A = std::move(*B)`.
5. Query through A.

A skips revalidation and consults the incomplete index, potentially returning successful missing results.

**Smallest fix:** Reset both readiness-cache members during move assignment, or transfer them with the corresponding snapshot and reset the source. Add a value-move regression.

**Reachability qualification:** This is a public library API regression. Inspected production call sites move `unique_ptr<StoreReadSession>`, not session values; current engine exposure was not found.

## Reconciliation/restart limitation — inherited, not a new PR15 finding

The new comment saying crash/failure leaves the obligation retained should not be interpreted as durable restart bookkeeping (`IndexPipeline.cpp:1088–1092`).

The actual integration behaves as follows:

- `OnScanComplete` clears retry ownership before finishing reconciliation.
- It updates `lastReconciliationTime` unconditionally after `FinishReconciliationPass`, even when the pass retained its failure obligation (`src/engine/src/VolumeSessionManager.cpp:1042–1053`).
- The scheduler uses the timestamp, with a six-hour interval; it does not independently schedule retained pipeline obligations (`VolumeSessionManager.cpp:53–54`, `1567–1586`).
- A fresh process loses `reconciliationSeen_`. `RebuildAll` restores persisted rows and rule-prunes them; it cannot reconstruct which rows the interrupted scan did not observe (`IndexPipeline.cpp:582–651`).

Thus a failed second removal chunk can leave stale rows awaiting ordinary reconciliation after restart. The manager-level false-completion/timestamp problem already existed before chunking, so I am **not attributing it as a newly introduced PR15 defect**.

A Close/Open test on the same pipeline object is insufficient: those methods do not clear `reconciliationSeen_` (`IndexPipeline.cpp:445–468`). Use a fresh object/process and manager-level scheduling assertions.

## Checkpoint and SQLITE_FULL assessment

- `CheckpointPassive` and `CheckpointIfWalExceeds` return SQLite failure to their immediate caller (`Store.cpp:974–1011`).
- Pipeline maintenance and the new chunk loop discard those returns (`IndexPipeline.cpp:529–530`, `1109`).
- A pinned reader can prevent RESTART; the vendored implementation explicitly returns `SQLITE_BUSY` when backfill/restart cannot complete (`third_party/sqlite/sqlite3.c:69871–69898`).
- Therefore **5,000-row transactions do not establish a WAL-size cap**.
- Conversely, checkpoint failure after successful COMMIT does **not itself prove lost removals**: committed state remains in WAL. I found no source justification for claiming canonical data loss solely from ignored checkpoint failure.
- Actual Windows disk-full writes map to `SQLITE_FULL` in the VFS (`sqlite3.c:51717–51726`). Existing maintenance faults bypass operations; they do not exercise this path.

## Coverage gaps and minimal harness

1. **Actual xWrite SQLITE_FULL:** Register a test-only default proxy VFS in an isolated child. Forward all operations, including WAL shared-memory methods; classify main-DB versus WAL files in `xOpen`. Return `SQLITE_FULL` before delegating one armed `xWrite`. This needs no real disk exhaustion or production VFS-selection API.
2. **Exercise distinct boundaries:** WAL spill, COMMIT writes, and main-DB checkpoint writes. The last case must be distinguished from an uncommitted batch failure.
3. **Chunk-two failure:** Reuse the 12,000-row fixture; arm after chunk one commits. Assert durable prefix, projection prefix, epoch, obligation, and retry. Check raw candidate structures before reopening can repair them.
4. **Pinned reader:** Hold a `StoreReadSession` snapshot while removals commit. Assert checkpoint refusal, continued snapshot correctness, and recovery after releasing it—not a fixed physical WAL-size promise.
5. **Real restart:** Terminate a child after an acknowledged chunk commit and during a later uncommitted chunk. Construct fresh pipeline/manager objects and verify repair scheduling, timestamps, final rows, candidate equivalence, and integrity.
6. **Existing kill coverage is narrower:** The store test exits at maintenance operation five; the migration harness exits at operation one (`test_store.cpp:1304–1358`, `1402–1422`; `bench_store_open.cpp:439–446`). These prove process death inside an uncommitted operation, not interruption of an actual VFS commit write.
7. **ANALYZE:** Existing coverage checks deferral and threshold behavior (`tests/engine/test_index_pipeline.cpp:146–214`), but not actual ANALYZE failure/retry. Pending debt resets on Open/Close. Production periodic maintenance is ten-minute cadence, not the 500 ms suggested by a Store comment; no performance certification follows from the unit fixture.

After adding focused regressions and restoring disk headroom, the relevant suites are `ffindexstore_store_tests`, `ffindexstore_read_session_tests`, `ffengine_index_pipeline_tests`, and `ffengine_volume_session_manager_tests`.

## Exact inspected source seams

- `src/indexstore/src/Store.cpp`: candidate maintenance helpers, `RebuildCandidateIndex`, `Open`, `ForgetVolume`, `ApplyBatch`, checkpoint methods, `Analyze`, `ListEntryIds`, read-session lifecycle/move operations, readiness validation, and candidate streaming.
- `src/indexstore/include/ffindexstore/Store.h`: fault seams and read-session state/contracts.
- `src/engine/src/IndexPipeline.{cpp,h}`: ingestion tails, reconciliation lifecycle/chunking, `RebuildAll`, maintenance and ANALYZE accounting.
- `src/engine/src/VolumeSessionManager.cpp`: start/resume, scan/journal apply and completion, disconnect handling, status collection, scheduler.
- `src/engine/src/Main.cpp`: startup recovery, rebuild, idle maintenance wiring.
- Relevant tests and harnesses: `test_store.cpp`, `test_store_read_session.cpp`, `test_index_pipeline.cpp`, `test_volume_session_manager.cpp`, `bench_candidate_index_resources.cpp`, `bench_store_open.cpp`, and component CMake registrations.
- Vendored SQLite: Windows write-error mapping, checkpoint behavior, interrupt reset, and build configuration.

**Fixed:** None; review-only.

**Merge verdict: BLOCK for the scoped PR15 product review**, pending the two P1 fail-open recovery fixes and focused regressions. No P0 found. This is not runtime durability certification or a claim of new product regressions in the documentation-only `5d7e98a..8c4c848` range.