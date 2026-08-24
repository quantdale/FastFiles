## Context

See `proposal.md` for the motivation and behavior contract. The live Phase 9
baseline is `f332f222c73297d00ae36cc36c8a9ee5ffb375ae` on the Phase 10 branch,
with `origin/main` at `d8d52abec4ad5e5b49c82fc1e9a63637c6ea5dd1`.

Phase 9's read plane already has the necessary mechanical foundations: a
single mutation mutex, an unsynchronized one-writer `Store`, move-only
query-only `StoreReadSession`s, bounded hot chunks, a bounded
`SearchAccumulator`, bounded UI admission, lifecycle cancellation, and
fail-closed stream/serialization handling. `IndexPipeline::SearchIndexedReadPlane`
currently captures one global `generation_`, opens a reader, combines hot
projection rows with cold durable rows, and restarts up to three times whenever
that integer changes. The same integer is advanced by row mutations, volume
configuration, projection residency, lifecycle operations, and writes that
only update scan/journal/reconciliation bookkeeping.

The authoritative writer remains connection-owned. The privileged service and
both existing named-pipe seams are outside this change; no service command,
wire format, or security boundary is needed.

## Goals / Non-Goals

**Goals:**

- Make the invalidation contract explicit and difficult to misuse.
- Preserve a coherent hybrid fast path while avoiding invalidation for
  proven metadata-only writes.
- Limit invalidation to volumes that can contribute to a scoped query where
  the existing path model permits that dependency calculation.
- Provide a bounded, durable-snapshot-only fallback after relevant hybrid
  churn, with no projection reload and no writer lock held during read work.
- Keep cancellation, lifecycle shutdown, reader ownership, top-N memory,
  response fail-closed behavior, and deterministic ordering unchanged.
- Make the result understandable through bounded counters/timings and the
  existing scalability harness.

**Non-Goals:**

- No multi-version in-memory projection, persistent search index, reader pool,
  second writer, SQLite schema migration, or distributed MVCC system.
- No attempt to make a point-in-time durable fallback include commits that occur
  after its snapshot begins; coherence is the guarantee, not zero staleness.
- No synchronous full-volume rule reconciliation on a settings update.
- No production signing, privileged trust-store, clean-host, guest, or
  physical-display qualification.
- No change to degraded browsing or to the privileged MFT/USN acquisition path.

## Decisions

### D1 — Replace the overloaded generation with explicit query-visible epochs

`IndexPipeline` will retain lifecycle cancellation as the already-existing
`lifecycleEpoch_`, but replace direct `++generation_` calls with named helpers
for query-visible state. The state will contain:

- a monotonic diagnostic `queryVisibleEpoch_`;
- per-volume searchable-data epochs;
- per-volume searchable-configuration epochs; and
- a searchable-volume/dependency-set epoch for all-volume queries.

The mutation helpers take an explicit reason (entry mutation, volume
visibility/configuration, rebuild/forget, or projection residency). They
advance only after a successful operation has changed state. A query captures
the epochs for its dependency set. Hybrid validation compares both the
per-volume data and configuration epochs and, for an all-volume query, the
dependency-set epoch. The search timing field historically named
`searchGeneration` remains populated with the captured query-visible epoch for
compatibility, while new fields identify fallback and dependency observations.

Why: a named mutation API makes accidental invalidation reviewable, while
separate data/configuration epochs allow a durable fallback to tolerate row
churn but reject a rule/availability change that would alter which rows are
searchable. A single global counter would continue to invalidate a narrow query
for unrelated volumes.

Alternatives considered:

- **Keep one global integer and increase the restart budget:** rejected because
  it preserves avoidable invalidation and makes continuous writes unbounded
  work rather than solving consistency.
- **Use a full MVCC projection copy:** rejected by the Phase 8 memory ceiling
  and the no-O(N)-owning-candidate contract.
- **Introduce a general distributed MVCC layer:** rejected as unnecessary for
  the existing per-volume scope and SQLite snapshot seams.

### D2 — Use a dependency set derived from the existing search scope

During the short locked capture, the pipeline will build the same searchable
volume prefixes it uses today, then select dependencies as follows:

- `AllIndexedLocations` depends on every currently searchable volume and the
  searchable-volume set epoch.
- `CurrentDrive`, `CurrentFolder`, and
  `CurrentFolderAndSubfolders` depend on the searchable volume whose prefix
  matches the requested scope path. The source enumeration is narrowed to that
  volume, so an unrelated volume mutation cannot restart the query.
- A missing/invalid dependency or configuration state remains fail-closed
  according to the existing indexed-search contract; the implementation will
  not invent a drive identity for an unresolved scope.

Each dependency captures its prefix and the `VolumeSetting` needed for the
query. The query does not depend on path strings in diagnostics; the set is an
internal identity/epoch map only.

Why: the current scope matcher already has a canonical volume-prefix model,
and this dependency calculation is bounded by the number of indexed volumes.
It avoids a complex range-index or path-MVCC implementation.

### D3 — Treat operational metadata as a separate persistence concern

The following operations will remain serialized through the writer mutex and
will continue to report their existing success/failure results, but will not
advance query-visible epochs by themselves: `SetScanCursor`,
`SetJournalPosition`, `MarkScanComplete`, `SetLastReconciliationTime`,
`SetBulkScanActive`, passive/forced checkpoint, `ANALYZE`, and maintenance
timestamps. `SetVolumeAvailable` will compare the prior availability and
advance only when the visible availability value changes; refreshing
`last_seen_time` alone is metadata-only.

`BeginReconciliationPass` is bookkeeping-only. `FinishReconciliationPass`
advances the affected volume only when a successful removal actually changes
the searchable rows. MFT/USN batches advance after a successful non-empty
durable commit and projection application. Empty/no-op/failing operations do
not advance.

Why: the search answer depends on rows and searchable configuration, not the
resume cursor or the timestamp used to schedule the next sweep. This preserves
the one-writer ordering without making metadata writes invisible to the
diagnostics that count avoided invalidations.

### D4 — Make include/exclude rules part of the captured search context

Changing a `VolumeSetting` remains a query-visible configuration mutation and
invalidates dependent queries. Because existing rows may remain durable until
the next reconciliation pass, the query context will also carry the captured
rules as a bounded in-memory map. A search candidate filter will evaluate the
candidate's reconstructed canonical path against those captured rules. When
rules require a path, the existing bounded projection or same-snapshot durable
path materializer is invoked before the filter; no full index path cache is
created.

This prevents an active query from publishing a row that the newly captured
rules exclude, without turning a settings update into a synchronous full-store
rewrite. If a rule changes during a fallback, the configuration epoch check
discards the old fallback before publication and retries from a fresh snapshot
within a fixed fallback-attempt bound.

Why: the repository's subtree-gating contract intentionally reconciles old
rows away on the next pass, so invalidation alone would still allow stale rows
to leak into a query captured after a rule change. A context filter closes that
gap while retaining the existing reconciliation lifecycle.

### D5 — Preserve the hybrid hot/cold fast path and add a durable fallback

The existing Phase 9 loop remains the first execution mode:

1. Capture dependencies, prefixes, rules, residency, and one reader under the
   short pipeline lock.
2. Copy bounded hot chunks and stream cold volumes through the same reader.
3. Match, reconstruct paths, accumulate bounded top-N, and sort outside the
   lock.
4. Validate relevant epochs before each next chunk and before hybrid
   publication; discard stale partial results and restart up to the existing
   fixed limit.

When a relevant stale result reaches that limit, the query enters a separate
`durableFallback` execution mode. It captures a fresh dependency context and
opens exactly one independent read session while holding the short pipeline
lock. It then enumerates every selected volume through that session, using the
same `SearchAccumulator`, durable point lookups for paths, captured rules, and
the existing cancellation checks. The projection cursor and trigram postings
are not used in this mode, and no evicted volume is reloaded.

The fallback treats row epochs differently from configuration epochs: the
SQLite transaction is already a coherent point-in-time answer, so later row
commits do not force another restart. Before publication it validates the
captured configuration/dependency-set epochs. A configuration change discards
the response and permits only a fixed small number of fresh fallback attempts;
continued inability to capture a safe configuration is `Unavailable`.

Why: a durable snapshot is the existing authoritative source and already has
the required read-only WAL semantics. This solves sustained write churn without
retaining a second index or blocking the writer.

### D6 — Keep failure, cancellation, and shutdown fail-closed

Fallback enumeration distinguishes normal visitor cancellation from a failed
statement/session. A failed stream, failed path lookup, reader-open failure,
serialization failure, closed pipeline, or unresolvable configuration clears
the accumulator and reports `Unavailable`; cancellation/supersession/lifecycle
epoch changes report `Cancelled`. The response is not published until the
fallback is complete, its session is healthy, and configuration validation has
passed.

`StoreReadSession` remains move-only, read-only, query-only, non-migrating, and
non-maintenance-owning. Fallback attempts close the old session before opening
the next one. Existing `UiServer` coordinator limits and connection-scoped
tokens are unchanged; `UiServer::Stop`, disconnect, and `IndexPipeline::Close`
continue to win over fallback publication.

### D7 — Add bounded diagnostics at both mutation and query boundaries

`ReadPlaneDiagnostics` will add monotonic counters for the captured/current
query-visible epoch, query-visible invalidations, metadata-only updates whose
invalidation was avoided, fallback attempts, successful/failed fallbacks,
fallback rows streamed, and reader acquisition/open failures (the existing
reader failure counter remains). `SearchTimings` will add the captured
dependency epoch/summary, relevant invalidation count, fallback attempt/use/
success flags, and fallback rows. Existing active/peak reader/search counts,
restart counts, cancellation, and lock wait/hold fields remain.

Counters contain no paths, names, or candidate data. The scalability harness
will serialize these bounded numeric fields beside the existing hot/cold and
lock observations; it will not introduce wall-clock pass/fail thresholds.

### D8 — Generation audit ledger and supersession record

The following ledger was produced from every `++generation_` site in the live
Phase 9 `IndexPipeline.cpp` and the associated `VolumeSessionManager` callers.
The historical Phase 9 design's restart-exhaustion behavior is intentionally
superseded by D5 only in this new change; the Phase 9 artifacts remain
historical evidence.

| Site / operation | Classification | Phase 10 action |
| --- | --- | --- |
| `Open` / `Close` | lifecycle-invalidating | Keep `lifecycleEpoch_` cancellation; route query state through named lifecycle helpers and retain fail-closed close behavior. |
| `SetBulkScanActive` | operational metadata only | Remove query-visible bump; projection accelerator/eviction mode is not a row answer change. |
| `RebuildAll` | query-result-affecting | Advance each rebuilt volume after the durable/projection rebuild; validate as relevant dependency state. |
| `ResolveVolume` when a new durable volume is created | query-visibility/configuration-affecting | Advance the new volume and searchable-set epoch; do not bump for an existing lookup. |
| `SetVolumeRules` | query-visibility/configuration-affecting | Advance only on an actual setting change; capture rules and filter candidates. |
| `ApplyMftBatchChanges` | query-result-affecting | Advance once after a successful non-empty commit/projection update; no bump for empty/failing batches. |
| `ApplyUsnBatchChanges` | query-result-affecting | Same as MFT; metadata-only persistence is still a row mutation only when fields/entry state can affect a query. |
| `SetVolumeAvailable` | query-visibility/configuration-affecting when the boolean changes; operational timestamp otherwise | Compare prior visibility; advance only on a real transition. |
| `SetScanCursor` | operational metadata only | Do not advance; add structural regression coverage. |
| `MarkScanComplete` | operational scan bookkeeping only | Do not advance; its status path remains independent. |
| `SetJournalPosition` | operational metadata only | Do not advance; journal identity/USN resume is not a searchable row. |
| `SetLastReconciliationTime` | operational metadata only | Do not advance. |
| `ForgetVolume` | query-result/configuration/lifecycle visibility-affecting | Advance the affected volume and searchable-set epoch only after successful deletion. |
| `BeginReconciliationPass` | operational reconciliation bookkeeping only | Do not advance. |
| `FinishReconciliationPass` with successful removals | query-result-affecting | Advance only when removals were committed/applied; no-op completion does not invalidate. |
| `SetProjectionMemoryBudget` | query-result-affecting only when residency changes | Compare per-volume residency before/after and advance changed volumes; a budget write with no eviction is metadata-only for search. |
| `UnloadVolumeFromProjection` | query-result-affecting for that volume's captured hot state | Advance only when the operation changes residency; scoped queries ignore other volumes. |
| `SearchIndexedReadPlane` validation | uncertain until regression | Replace raw global comparison with dependency-aware validation and fallback tests. |

This ledger deliberately marks the USN and projection cases as result-affecting
even when an individual record may be semantically redundant: the safe default
is to invalidate after a committed row mutation, while tests prove no-op and
bookkeeping cases do not churn.

## Risks / Trade-offs

- **[Risk]** A durable fallback can return a point-in-time result that does not
  include the newest commits. → It is explicitly one coherent snapshot; later
  searches observe newer commits, while hybrid attempts still prefer a stable
  current projection.
- **[Risk]** A rule-aware candidate filter requires more path reconstruction for
  configured volumes. → It is enabled only for volumes with rules, remains
  bounded by existing path-depth limits, and is measured in the benchmark.
- **[Risk]** Per-volume dependency selection could miss an unusual scope/path
  identity. → Reuse the existing canonical volume-prefix matcher, keep
  unresolved scope state fail-closed, and retain global dependency checks for
  all-volume requests.
- **[Risk]** Configuration churn during fallback could cause repeated snapshots.
  → Bound fallback configuration retries; repeated inability to capture a safe
  context remains `Unavailable`.
- **[Risk]** Opening a reader for every hot query may be measurable. → Retain
  the Phase 9 behavior until benchmark evidence justifies lazy creation; do
  not trade away snapshot/path consistency for an assumed micro-optimization.
- **[Risk]** SQLite WAL frames can remain pinned by fallback. → One reader per
  active query, existing coordinator limits, deterministic close, passive
  checkpointing, and reader/WAL evidence remain in force.
- **[Risk]** Existing direct Debug CTest failures may be host policy rather than
  product defects. → Reproduce with focused diagnostics, classify the result,
  and harden only the launcher/fixture seam without skipping mandatory tests or
  changing trust stores.

## Migration Plan

1. Land this change's artifacts and the generation audit before source edits.
2. Add explicit epoch/dependency state and mutation helpers; run focused
   metadata/invalidation tests.
3. Add the rule-aware filter and durable fallback; run fallback/fault/cancel
   tests before changing benchmarks.
4. Extend structural concurrency, UI lifecycle, seeded, and store read-session
   tests; update tasks with exact evidence.
5. Extend the existing scalability harness and reproduce the local portability
   failures. Record environment-only blockers without changing the ordinary
   non-admin suite.
6. Sync canonical specs/docs after behavior is proven, then run the full
   Debug/release-test/analyze/ASAN/OpenSpec/verifier ladder.
7. Reconcile `origin/main`, commit logical waves, push normally if authorized,
   inspect hosted CI for the exact final SHA, and leave one clean reviewable
   Phase 8 + Phase 9 + Phase 10 branch.

Rollback is a source-level revert of the Phase 10 commits. No database schema,
wire protocol, service contract, or trust-store state changes.

## Open Questions

None. The scope dependency model, epoch classes, fallback retry bound, and
failure semantics are resolved above; chunk and fallback-attempt constants are
implementation seams that may be tuned only from structural evidence.

