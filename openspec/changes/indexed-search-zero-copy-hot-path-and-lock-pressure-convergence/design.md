# Design — Indexed Search Zero-Copy Hot Path and Lock-Pressure Convergence

## Context

The resident projection search path is deliberately hybrid and generation-validated. A bounded chunk is captured while `IndexPipeline::mutex_` protects mutable projection/configuration state; matcher/sorter/path work then runs outside that critical section. Cold/evicted volumes use an independent SQLite read snapshot. Relevant mutations invalidate the captured query-visible context, causing bounded restarts and ultimately durable fallback.

The current capture representation is heavier than required for most rejected rows: `MakeProjectionCandidate` converts every live projection name from the interned `char16_t` pool into an owned `std::wstring` and builds a full `Candidate` before the query's name/metadata predicates execute.

The repository already exposes `CandidateView`, `Query::MatchesView*`, and `ExecuteSearchViews`, but the current `NamePool` storage is not lifetime-stable across concurrent mutation: `arena_` is a `std::vector<char16_t>` that can reallocate during `Intern`, and `Projection::CompactStorage` constructs and moves in a replacement `NamePool`. Therefore a naked `std::u16string_view` cannot leave the projection lock.

## Design Principles

### D1 — Preserve the indexed-read-plane contract

Long-running matching, sorting, path reconstruction, and response materialization remain outside the mutation/lifecycle critical section. The optimization may add bounded metadata capture work under the lock, but it must not turn the lock into the matcher execution domain.

### D2 — No borrowed pointer without a lifetime owner

Any pointer/string_view used after releasing the projection lock must have an explicit owner/lease whose lifetime covers the use. Epoch validation alone detects semantic staleness; it does not make dangling memory safe.

### D3 — Make rejected rows cheaper than accepted rows

For view-compatible queries, the hot path should carry only the metadata required by `MatchesViewWithoutFolder` / `MatchesView` and materialize owned strings only for rows that survive the cheap predicates and still need result/path processing.

### D4 — Keep canonical semantics singular

Do not fork query semantics. Reuse the existing prepared view predicates and the same `ResultLess`/top-N ordering contract. Where a query is not view-compatible, fall back to the existing owned-candidate path rather than inventing a partial alternate parser/matcher.

### D5 — Stable storage must respect the memory budget

If stable borrowed-name storage is introduced, its retained generations/slabs/leases must be bounded and accounted for. A search must not pin an unbounded historical projection or defeat projection eviction/compaction.

### D6 — Churn remains fail-closed

A rename, move, delete, metadata update, configuration/residency change, compaction, rebuild, or lifecycle transition that is relevant to a search must still produce exactly one of: coherent completion, bounded restart, durable-snapshot fallback, cancellation, or `Unavailable`. Never publish a mixed-generation result to preserve an optimization.

## Required Reconnaissance / Design Spike

Before selecting an implementation, instrument and compare at least these candidate strategies:

1. **Stable interned-name storage + bounded lightweight chunk.** Change `NamePool` so individual name bytes have stable addresses across `Intern`; capture a bounded POD/view chunk under lock; evaluate view predicates after unlock. Prove what happens across `CompactStorage`, rebuild, and pool replacement. If storage generations can be replaced, add an explicit bounded lease/shared-owner mechanism.
2. **Generation-owned immutable name storage.** Capture a shared immutable name-storage generation plus lightweight row metadata. Mutations publish a new generation or append safely; old storage remains alive only while bounded readers hold it. Prove memory reclamation and no whole-index copy per mutation.
3. **Bulk compact capture.** If true zero-copy storage is too invasive, measure a chunk-owned contiguous name buffer plus lightweight metadata that performs one bounded bulk capture rather than one owned `Candidate`/wstring allocation per row. This is acceptable only if measurements show the intended lock/latency win and the campaign accurately calls it copy-reduced rather than zero-copy.

Explicitly reject any strategy that:

- executes the whole query matcher under `IndexPipeline::mutex_`;
- exposes `NamePool::arena_.data()` after a possible reallocation/replacement;
- pins the entire mutable projection indefinitely;
- creates an O(N) owned candidate/name snapshot;
- disables generation validation, compaction, or the projection memory ceiling;
- makes the cold SQLite path depend on projection lifetime.

## Preferred Integration Shape

The implementation should converge toward a bounded resident-row record containing the minimum scalar metadata plus a safe name view/handle. The canonical view matcher runs outside the mutation lock. Only surviving rows are upgraded into owned `Candidate` instances; folder/path materialization remains lazy and occurs from a generation-valid source. Exact details are chosen by the spike above rather than prescribed prematurely.

Where practical, factor the common bounded top-N/result accumulation logic so the owned and view paths cannot drift. Avoid adding a third independent search engine.

## Diagnostics

Preserve existing `SearchTimings` fields and add only narrowly useful counters needed to prove the change, such as:

- hot lightweight rows captured;
- hot rows rejected before owned materialization;
- hot names borrowed vs copied;
- storage-generation/lease count and retained bytes if applicable;
- view-path fallback reason counts;
- time in bounded capture vs view matching vs survivor materialization.

Diagnostics must not emit arbitrary filenames/paths.

## Benchmark Matrix

At minimum use a deterministic 1M-row resident dataset and cover:

- mixed-case ASCII hit and miss;
- lowercase ASCII hit and miss;
- Unicode needle/haystack fallback;
- sparse, medium, and broad match selectivity;
- simple literal with filename accelerator eligible/ineligible;
- `name:`, `ext:`, kind/size/date metadata filters;
- folder/path-dependent queries;
- relevance and each supported explicit sort;
- low and high `maxResults`;
- mixed hot/cold volumes;
- ingestion/churn overlapping search.

Capture before/after wall time, rows examined, name copies, candidate materializations, matcher attribution, max state-lock hold, accumulated lock wait, restarts/fallbacks, and peak transient memory.

## Acceptance Threshold

Correctness and lifetime safety are absolute gates. Performance is evidence-gated: the final selected implementation must show a repeatable material improvement on at least the representative resident selective-search workload and must not introduce a material regression on broad/complex/Unicode/churn cases. Treat approximately 5% or less as noise unless repeated measurements establish otherwise. If the safe implementation cannot produce a meaningful win, revert the invasive optimization and retain only benchmark/diagnostic improvements that are independently useful.

## Compatibility / Migration

No persisted database schema, protocol wire shape, UI contract, search syntax, or release-signing contract should change. Internal structures may change only with direct regression coverage and memory accounting.
