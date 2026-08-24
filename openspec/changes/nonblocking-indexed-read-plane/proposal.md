## Why

Phase 8 made indexed search correct under projection eviction, but the current
`IndexPipeline::SearchIndexed` still holds the pipeline mutex across projection
enumeration, cold SQLite streaming, path reconstruction, matching, and sorting.
At large scale that turns an expensive query into a stop-the-world event for
ingestion, lifecycle transitions, aggregates, snapshot publication, and
cancellation. The read plane now needs an explicit concurrency contract before
large-index use expands.

## What Changes

- Add a bounded indexed-read coordinator so UI clients can run independently
  without creating unbounded query threads, reader connections, CPU, or memory
  fan-out.
- Give durable search/aggregate reads independent SQLite read sessions while
  keeping `Store` connection-owned, one-writer semantics and WAL snapshot
  behavior explicit.
- Refactor indexed search so the mutation/lifecycle critical section is used
  only for bounded state capture and short validation, never for the full
  matcher, sorter, cold stream, or path-materialization lifetime.
- Define and test an immutable query-generation consistency model, including
  concurrent insert, rename/move, metadata update, delete, reconciliation,
  volume availability, and forget behavior.
- Preserve Phase-8 memory bounds, hot/cold result equivalence, deterministic
  ordering, result caps/truncation truthfulness, allocated-size knownness,
  fail-closed durable-stream errors, per-client supersession, and degraded mode.
- Add structured read-plane diagnostics, deterministic contention and fault
  injection seams, real multi-client lifecycle coverage, and extensions to the
  existing large-index benchmark.

## Capabilities

### New Capabilities

- `indexed-read-plane`: bounded, non-blocking indexed query execution,
  consistency, cancellation, lifecycle, backpressure, observability, and
  shutdown behavior.
- `independent-index-read-sessions`: independent SQLite reader ownership,
  WAL snapshot semantics, bounded reader resources, and fail-closed read
  failure behavior.

### Modified Capabilities

- `index-engine`: indexed search and lifecycle requirements change to require
  bounded mutation blocking and explicit concurrent-query consistency.
- `filesystem-index-store`: durable read requirements change to distinguish
  independent read sessions from the unsynchronized writer `Store` object
  while preserving one-writer/WAL semantics.

## Impact

The primary implementation spans `src/engine` (`IndexPipeline`, `UiServer`,
and the bounded query coordinator), `src/indexstore` (read-session ownership),
and the existing `ffsearch` integration. Engine, index-store, UI-server,
benchmark, and fault-injection tests will gain deterministic barriers and
bounded-watchdog coverage. No service command, privileged boundary, protocol
frame limit, MFT/USN parser behavior, schema migration, trust-store behavior,
or degraded-mode dependency is introduced by this change.
