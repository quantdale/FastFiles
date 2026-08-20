# Design — harden-index-store-concurrent-access

## Context

`filesystem-index-store` requires a single-file SQLite source of truth in WAL mode and explicitly states that an ingestion write must not block a concurrent reconciliation/diagnostic read and vice versa. `Store::Open` currently applies `PRAGMA journal_mode=WAL` and `synchronous=NORMAL`. Existing store tests cover transactions, persistence, WAL recovery/checkpoint behavior, migration, integrity, allocation metadata, and reopen paths, but not two independent connections to the same database file.

The purpose of this change is to turn that architectural promise into deterministic executable evidence and harden only defects that evidence exposes.

## Decisions

### D1 — Test independent connections, never concurrent calls on one `Store`

A `Store` object remains connection-owned and is not declared thread-safe. Concurrency tests SHALL create at least two independent `Store` instances pointed at the same existing database file. Each instance is owned by one execution thread while an operation is active.

This isolates SQLite/WAL concurrency from accidental C++ object data races and matches the canonical requirement: independent read activity may overlap the authoritative writer.

### D2 — Use a real held read snapshot as the synchronization point

The primary fixture SHALL seed multiple rows, then start `reader.ForEachEntry(...)` on one connection. The callback for the first observed row signals a barrier and remains blocked on a condition variable while its SQLite statement is still active. Only after that signal may the writer execute a real `ApplyBatch` transaction on the second connection.

No fixed sleep may be used as the correctness oracle. Bounded waits are allowed only around the barrier/deadlock watchdog and must fail with diagnostics if exceeded.

### D3 — Snapshot semantics are part of acceptance

While the reader statement is held open, the writer SHALL be able to commit. The already-started reader SHALL finish against a self-consistent pre-commit snapshot: it must not see a torn mixture or the newly committed row halfway through the same statement.

After that reader completes, a fresh read on the same reader connection SHALL observe the writer's committed row. This distinguishes valid WAL snapshot isolation from stale-forever caching.

### D4 — One writer is the supported contract

This change SHALL NOT promise simultaneous independent writers. The production architecture has one authoritative engine/store writer. Tests may verify a second connection can open/read/checkpoint, but SHALL NOT force `ApplyBatch` from two writer connections concurrently or add retry policy for a product mode that is not supported.

If implementation work is needed, it must preserve one-writer/many-reader semantics rather than silently broadening ownership.

### D5 — Busy handling must be bounded and specific

A normal supported reader/writer overlap must not surface `SQLITE_BUSY` merely because the reader has an active statement under WAL. If a real supported overlap exposes transient busy behavior during open/checkpoint/commit, use the smallest bounded solution consistent with SQLite semantics.

Do not add unbounded retry loops, arbitrary sleeps, or blanket error suppression. Persistent lock/configuration failures must still fail closed and remain diagnosable.

### D6 — Passive checkpoint is maintenance, not a reader-killing barrier

With an independent reader snapshot active, `CheckpointPassive()` (or the repository's current passive checkpoint equivalent) SHALL return in a bounded interval and SHALL NOT invalidate or corrupt the reader. SQLite is allowed to leave WAL frames uncheckpointed while a reader pins an older snapshot; the test must not require an impossible full/restart checkpoint.

After the reader releases, ordinary subsequent checkpoint/read/reopen operations SHALL continue to work.

### D7 — Repetition proves teardown hygiene

Run the held-reader/commit sequence repeatedly on fresh or reset fixture state. At minimum cover multiple iterations sufficient to expose stale locks/event ordering; prefer 20+ iterations if runtime remains small. Every iteration must use deterministic synchronization and finish without retry-until-pass.

Close both stores, reopen the database, run integrity check, and verify the final durable row set. No connection may leave the file permanently busy after teardown.

### D8 — Preserve current durable semantics

The test/fix must preserve:
- WAL mode and current durability policy;
- batched transaction atomicity;
- schema version 2 and nullable allocated-size semantics;
- volume/FRN identity;
- integrity-check behavior;
- projection and engine APIs;
- checkpoint and vacuum policy unless a directly evidenced concurrency defect requires a narrowly justified adjustment.

No schema migration is authorized by this change.

### D9 — Separate concurrency evidence from performance claims

Record bounded durations for writer commit while the reader is held, passive checkpoint while held, and complete repeated fixture runtime. The gate is absence of lock/deadlock/regression, not a microbenchmark target. Do not weaken unrelated performance thresholds or claim throughput improvements without measurements.

### D10 — Repository scope and privileged boundary

Expected product/test scope is `src/indexstore`, `tests/indexstore`, OpenSpec evidence/tasks, and workstate. Touch engine code only if the store contract cannot otherwise be satisfied and the need is demonstrated.

Do not touch production signing, trust stores, `FastFilesIndexSvc`, raw MFT/USN parsing/scanning, clean-host validation, or VM provisioning.

### D11 — Hosted CI is closure evidence, not a reason for a recursive commit

After the final implementation/evidence commit is pushed, query Actions for that exact SHA and report status/conclusion available at handoff. Never create another commit solely to record the CI result.
