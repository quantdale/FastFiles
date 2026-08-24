## Why

Phase 9 correctly prevents mixed-generation indexed results, but it still treats
all pipeline mutations as one global invalidation source. Continuous MFT/USN
ingestion can therefore exhaust the bounded restart budget and report
`Unavailable` even when a coherent durable SQLite snapshot is available, while
scan cursors, journal positions, and reconciliation timestamps can restart a
query without changing its answer.

## What Changes

- Define an explicit query-visible epoch contract that advances only for
  searchable row, searchable-volume, inclusion/configuration, or relevant
  projection-residency changes; keep lifecycle cancellation separate.
- Classify and test operational metadata writes so scan cursors, journal
  positions, scan-completion bookkeeping, reconciliation timestamps, and
  maintenance/checkpoint state do not invalidate active indexed queries by
  themselves.
- Track query dependencies by relevant volume where the existing scope/path
  model makes that safe, while preserving all-volume invalidation for global
  searches.
- Retain Phase 9 bounded hot/projection execution and restart attempts, then
  fall back deterministically to one independent durable SQLite snapshot when
  relevant write churn prevents a stable hybrid pass.
- Stream the durable fallback through the bounded `SearchAccumulator`, keeping
  matching, path reconstruction, ordering, cancellation, fail-closed stream
  handling, and response serialization coherent and bounded.
- Add structural regression coverage for sustained MFT/USN churn, metadata-only
  writes, scoped/global invalidation, rule and lifecycle changes, fallback
  cancellation/faults/shutdown, snapshot coherence, and memory/resource bounds.
- Extend bounded diagnostics and the existing large-index scalability harness
  with epoch, avoided-invalidation, fallback, reader, cancellation, and lock
  observations without recording user paths.
- Reproduce the historical local Debug CTest portability failures and harden
  launch/fixture handling only where evidence shows a host-sensitive harness
  defect; leave genuine environment blockers explicit.

## Capabilities

### New Capabilities

- `search-availability-under-index-churn`: query-visible epochs, relevant
  dependency invalidation, and coherent bounded durable fallback under sustained
  index mutation.

### Modified Capabilities

- `index-engine`: indexed search consistency changes from restart-only exhaustion
  to a bounded durable-snapshot fallback, with operational metadata excluded
  from result invalidation.
- `indexed-read-plane`: adds fallback execution, dependency-aware invalidation,
  diagnostics, and the corresponding cancellation/lifecycle guarantees.
- `independent-index-read-sessions`: one query may use its independent snapshot
  as the complete fallback source while the writer continues committing.
- `filesystem-index-store`: clarifies that operational volume metadata writes do
  not change query-visible state and that fallback readers remain query-only.

## Impact

Primary code is in `IndexPipeline`, `VolumeSessionManager` call sites,
`StoreReadSession`, `Projection`, `ffsearch::SearchAccumulator`, and the
existing engine concurrency/UI lifecycle/scalability tests. The change adds no
IPC command, service behavior, database schema migration, privileged access,
second writer, full in-memory index copy, or production-signing dependency.

