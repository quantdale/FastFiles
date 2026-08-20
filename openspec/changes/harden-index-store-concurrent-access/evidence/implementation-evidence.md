# Implementation Evidence — harden-index-store-concurrent-access

Status at completion: **COMPLETE**. The one-writer + independent-reader WAL
contract that the canonical `filesystem-index-store` specification already
promises is now proven by an executable, deterministic, repeated
two-connection fixture. No Store-layer fix was required: the existing
`Store::Open` WAL configuration already satisfies the contract, so this change
is evidence + hardening of the test surface only. The `Store` object remains
connection-owned and is **not** claimed thread-safe; the contract proven is
one authoritative writer with independent readers, never multi-writer.

## 1. Baseline gap (pre-change)

- **`Store::Open` concurrency configuration** (`src/indexstore/src/Store.cpp:240-248`):
  applies `PRAGMA journal_mode=WAL;`, `PRAGMA synchronous=NORMAL;`, and
  `PRAGMA foreign_keys=ON;` on open. SQLite WAL mode is what allows a read
  snapshot and a write transaction to overlap on independent connections.
- **Canonical requirement** (`openspec/specs/filesystem-index-store/spec.md`,
  "Batched Transactional Writes Under WAL"): ingestion writes "SHALL permit
  concurrent readers (reconciliation sweeps, diagnostics) to proceed without
  being blocked by an in-progress ingestion transaction."
- **Pre-change `tests/indexstore` coverage**: every existing test in
  `test_store.cpp` drives a single `Store` per scope. Two connections to the
  same file were only ever used **sequentially** (e.g.
  `TestWalReplayRecoversCommittedDataWithoutAnExplicitCheckpoint` opens a writer
  `Store`, lets it destruct, then reopens with a second `Store`). No test kept
  a read statement active on one SQLite connection while a second `Store`
  committed a batch to the same file. The promised reader/write overlap was
  therefore unproven by executable evidence — exactly the gap this change
  closes.
- **Baseline focused result (before modification)**: existing
  `ffindexstore_store_tests` passed (Debug, 1.08s including the new suite added
  afterward; the prior suite alone passed in 0.22s).

## 2. Two-connection fixture

Implemented in `TestConcurrentReaderWriterWalContract()`
(`tests/indexstore/test_store.cpp`):

- **Database**: a fresh DB under the test temp space per iteration, via
  `FreshDbPath("ffindexstore_test_concurrent_<i>.db")` so each iteration opens
  a genuinely fresh file (the existing `FreshDbPath` also clears `-wal`/`-shm`
  sidecars).
- **Seed**: one volume (`VolumeKey` with a per-iteration `serialNumber`) plus
  **64 entries** (ids 1..64, distinct names `entry-<id>`), committed as one
  batch and then `CheckpointPassive()` so the reader starts from a clean
  committed base.
- **Two independent `Store` instances**: `writer` (authoritative writer, owned
  by the main/test thread) and `reader` (opened on the **same existing file**,
  owned exclusively by a spawned reader thread). Each `Store` is used by one
  thread at a time — no concurrent calls on one `Store` object, and no claim
  that `Store` itself is thread-safe.
- **Barrier / condition-variable protocol** (`HeldReadState` +
  `HeldReadWorker`): the reader thread calls `reader.ForEachEntry(vol, ...)`.
  The visitor records each `(id, name)` and, on the **first** observed row,
  sets `holding=true`, signals the barrier, and blocks on a `condition_variable`
  while its SQLite statement is still active. The writer action runs only after
  the barrier proves the read statement is live. **No fixed `sleep` is used to
  establish the overlap** — synchronization is entirely through the barrier and
  a bounded deadlock watchdog.
- **Watchdog bounds**: `kBarrierTimeout = 10s` (read statement must become live)
  and `kDoneTimeout = 10s` (reader must finish after release). On timeout the
  test records a deterministic `FAIL` with diagnostics and force-releases/joins
  the reader thread so CTest never hangs; it does not spin or retry-until-pass.

## 3. Snapshot truth table (executable observations)

For every iteration the held reader and a fresh read on the same reader
connection observed:

| Phase | Held reader (statement active during writer action) | Fresh read (after release, same connection) |
|---|---|---|
| Insert while held | sees exactly the 64 pre-commit seed rows; does **not** observe the newly committed row (id `1000+i`) | sees 65 rows including the committed insert |
| Update while held (id 7) | sees the **pre-update** name (`entry-7`) | sees the **post-update** name (`updated-7`); row count unchanged (65) |
| Remove while held (id 13) | still sees id 13 and the full 65-row pre-remove snapshot | does **not** see id 13; row count drops to 64 |
| Passive checkpoint while held | completes against a coherent 64-row snapshot; not invalidated | subsequent checkpoint + fresh read succeed (64 rows) |

The update/remove variants (D3.5) confirm snapshot isolation is not an
artifact of an append-only fixture.

## 4. Writer and checkpoint boundedness

- **Writer `ApplyBatch` while reader held**: every insert/update/remove batch
  committed and returned `true` (no `SQLITE_BUSY`). `writer.CountEntries` on the
  writer connection immediately observed the committed row. Measured commit
  durations (reader held): `min=0ms, median=0.0ms, max=1ms` across all
  iterations — bounded and effectively instantaneous, proving the writer is not
  blocked by the active reader and the reader does not surface a busy failure
  for this supported overlap (D5).
- **Passive checkpoint while reader held** (`writer.CheckpointPassive()`, an
  independent connection): returned `true` every time, bounded
  (`min=0ms, median=0.0ms, max=0ms`). SQLite is permitted to leave WAL frames
  uncheckpointed while the reader pins an older snapshot; the test asserts
  safety/boundedness, not an impossible full-drain (D6). After release, a
  subsequent `CheckpointPassive()` and a fresh `ForEachEntry` both succeeded,
  confirming the checkpoint did not invalidate or corrupt the reader.
- **No Store-layer fix was needed**: the supported overlap behaved correctly on
  the first run, so D5/D7.4's "narrowest bounded fix" path was not triggered.
  The contract was already correctly implemented; this change only adds
  executable proof.

## 5. Repetition / teardown hygiene

- **Iterations**: `kIterations = 24` (>= 20 target from D7), all passing
  deterministically with **zero retry-until-pass** (a single `Check` at the end
  asserts `passedIterations == kIterations`).
- **No stale lock between iterations**: each iteration uses a fresh DB file and
  the final phase closes both `Store` instances, reopens the same file with a
  third `Store`, runs `RunIntegrityCheck()` (pass), and verifies the durable
  row count is exactly 64. A leftover permanent busy/lock state would fail the
  reopen/integrity/row-count checks — none did.
- **Total deterministic fixture runtime**: ~1131ms for all 24 iterations
  (including 24 reopen + integrity checks), i.e. ~47ms/iteration. The gate is
  absence of lock/deadlock/regression, not a throughput target (D9).

## 6. Scope proof

- **No schema / `user_version` change**: `Store::Open` and the schema SQL are
  untouched; `kCurrentSchemaVersion` remains 2.
- **No protocol or snapshot wire change**: only `tests/indexstore/test_store.cpp`
  was modified; no `ffprotocol`/`ffipc`/`SnapshotFormat` edits.
- **No production signing / trust-store change**, no `FastFilesIndexSvc`, raw
  MFT/USN, clean-host, or VM provisioning change. The change is confined to
  `tests/indexstore` and OpenSpec evidence/tasks (D10).
- **No multi-writer product promise, no `Store`-object thread-safety claim**:
  the test creates two connections but only ever commits from the single
  authoritative writer connection; comments and evidence state the
  one-writer/many-reader contract explicitly.
- **No generated DB/WAL/SHM/build/run artifacts committed**: only the source
  test edit and OpenSpec text/evidence are staged; `verify/runs/` and
  `build/` remain untracked.

## 7. Full validation matrix

| Gate | Result |
|---|---|
| Focused `ffindexstore_store_tests` (Debug) | PASS (1.08s; new concurrency suite 24/24 iters) |
| Full Debug CTest | 66/66 PASS |
| Full release-test CTest | 66/66 PASS |
| Analyze build (`/W4 /WX` + `/analyze`) | warning-clean, links |
| `pwsh ./verify/verify.ps1 build` (windows-build-validation) | PASS (exit 0) |
| `pwsh ./verify/verify.ps1 doctor` | PASS (exit 0; environmental tool SKIPs only) |
| source-marker policy (`check_source_markers.ps1`) | PASS |
| diagnostic policy (`check_diagnostic_policy.ps1`) | PASS (0 failures) |
| doc drift (`ffdoc_drift_tests`) | PASS (within CTest) |
| `openspec validate --all --strict --no-interactive` | 57 passed, 0 failed |
| `git diff --check` | clean (exit 0) |

All results are from the actual local run for the final pushed SHA and are
preserved truthfully; no run was rewritten to look cleaner.

## Deferred-note reconciliation

The stale `close-independent-validation-gaps` deferred note that listed
"same-SQLite-file concurrency coverage" as future follow-up work (its
`tasks.md` line 63) was reconciled **after** this executable evidence landed:
the coverage now exists (`TestConcurrentReaderWriterWalContract`), so the note
is preserved as historical context and annotated as resolved rather than
deleted. No old evidence was rewritten.
