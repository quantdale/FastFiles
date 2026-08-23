# Memory-Bounded Indexed Scale (Phase 8)

## Why

The durable SQLite Store and the bounded in-memory Projection already exist, but
the *search* path defeats the memory model: `IndexPipeline::SearchIndexed`
calls `Projection::ReloadAllEvictedVolumes(store_)` under the pipeline mutex
before every query (`src/engine/src/IndexPipeline.cpp:793` and `:962`). When
any volume has been subtree-evicted, a keystroke search force-reloads **every**
evicted volume into RAM, then the next ingestion batch's
`EnforceMemoryLimitNow()` evicts again — a reload→evict→reload cycle that can
violate the projection ceiling, churn memory on every query, and (because the
reload result is ignored) silently omit durable rows if a reload fails.
Snapshot publication has the same shape at larger scale:
`ExportDirectorySnapshot` materializes a complete per-volume directory map in
RAM and `UiServer::MergeIndexDirectories` retains it in full forever, so both
transient and published bytes scale with the total indexed tree rather than the
user's active working set.

Invariant this change establishes:

> FastFiles must return semantically complete indexed results even when the
> durable index is larger than the configured in-memory Projection budget.
> Search correctness may never depend on temporarily reconstructing the
> complete durable index in RAM.

## What Changes

- **Cold-search path**: indexed search partitions searchable volumes into hot
  (projection-resident) and cold (`NeedsReload`) sets. Hot volumes enumerate
  from the Projection exactly as today; cold volumes stream rows directly from
  the durable Store (`Store::ForEachEntry`) with bounded per-row transient
  memory. No `ReloadAllEvictedVolumes` call remains in any search path. The
  filename-trigram accelerator stays enabled only when every searched volume is
  hot (postings cover resident rows only); mixed/cold queries use the canonical
  scan over both sources — never an accelerator that could produce false
  negatives.
- **One canonical merge/order/cap contract**: match identity, ordering,
  tie-breaking, scopes, filters, cancellation, supersession, and the 4,096
  engine cap are identical whether a row was hot or cold; deterministic tests
  compare fully-resident reference results against forced-eviction results for
  every supported query class.
- **Honest failure**: a failed cold store-stream fails the query closed
  (`Unavailable`), never silently partial results.
- **Memory invariant hardening**: Projection exposes testable diagnostics
  (configured maximum, estimated bytes, eviction counts, cold-reload counts);
  repeated searches over an evicted index no longer grow or thrash the
  projection.
- **Cold browse/aggregates**: folder aggregates for an evicted volume come from
  the Store's recursive CTE instead of reloading the whole volume; candidate
  folder reconstruction falls back to a new point lookup (`Store::FindEntry`)
  parent-chain walk. No consumer interprets "evicted" as "does not exist".
- **Bounded snapshot publication**: bulk indexed exports (startup rebuild,
  scan-complete) export a bounded top-of-tree prefix of directories instead of
  the complete volume map; `UiServer` additionally caps retained indexed
  directories with a recency-ordered working set. A cold indexed directory
  transparently re-materializes through the existing request/watch path.
  Generation immutability, format-version fail-closed checks, precedence rules
  (explicit filesystem observations beat stale indexed success), and the
  256 MiB publish cap are unchanged.
- **Seeded indexed-mode acceptance**: a deterministic CTest integration suite
  populates the real Store/IndexPipeline with synthetic MFT/USN-equivalent
  records and exercises population → restart → search → aggregates →
  snapshot serialize/publish/parse → mutations → eviction → cancellation.
- **Scale evidence**: the large-index benchmark is refreshed against current
  architecture (projection + cold search + snapshot publication) at 100K–2M+
  rows; measurements are recorded as versioned evidence, never hosted-CI
  wall-clock gates.

## Capabilities

### Modified Capabilities
- `filesystem-index-store`: cold-data completeness requirements (search,
  aggregates, path identity) and projection memory-policy diagnostics.
- `index-engine`: cold/hot partition search semantics, honest failure,
  bounded working-set snapshot publication.

## Impact

- **Code**: `src/indexstore` (`Store.h/.cpp` point lookup, `Projection`
  diagnostics), `src/engine` (`IndexPipeline`, `UiServer`,
  `VolumeSessionManager`, `Main.cpp`), tests under `tests/engine` and
  `tests/indexstore`.
- **No changes** to the wire protocol message set, the privileged service, the
  authentication/signing model, degraded browsing behavior, or the snapshot
  binary format (version 3 layout unchanged).
- **Performance**: searches that previously triggered whole-volume reloads now
  cost one bounded SQLite stream per cold volume; steady-state hot-path
  behavior is byte-for-byte unchanged.
