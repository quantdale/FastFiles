## Context

See `proposal.md` for the motivation and behavior surface. The Phase-8
implementation is correct under projection eviction, but `IndexPipeline` still
uses one mutex around both the authoritative writer and all projection/store
reads. The view-compatible and owning `SearchIndexed` branches retain that
mutex through cold `Store` streaming, candidate/path work, matching, and
sorting. `Store` is explicitly connection-owned and unsynchronized, so the
writer cannot simply be called from new query threads. `UiServer` currently
owns one search worker per client and has no engine-global admission bound.

The repository already has a tested one-writer/independent-reader WAL contract
from `harden-index-store-concurrent-access`; Phase 9 extends that contract to a
long-lived read-session abstraction without changing the SQLite schema or
privileged process boundary.

## Goals / Non-Goals

**Goals:**

- Bound mutation/lifecycle critical-section ownership to state capture and
  generation checks rather than query duration.
- Keep a query coherent while the durable store and projection change, with a
  deterministic restart and fail-closed exhaustion policy.
- Preserve hot/cold equivalence, Phase-8 memory limits, top-N ordering,
  cancellation, degraded operation, and writer durability.
- Bound active/queued UI searches and durable reader sessions, with deterministic
  cancellation and shutdown.
- Make contention, resource ownership, and fault outcomes observable in tests
  and benchmark records without noisy filename/path logging.

**Non-Goals:**

- No second writer, multi-process database ownership, SQLite schema migration,
  persistent secondary search index, or full immutable copy of the index.
- No service command, MFT/USN parser, Authenticode, installer, trust-store,
  degraded-enumerator, or snapshot wire-format change.
- No promise that a broad cold query itself becomes sub-second; responsiveness
  means unrelated engine work is no longer serialized behind it.

## Decisions

### D1 — Use bounded chunk capture plus generation restart

`IndexPipeline` will maintain a monotonic search generation under its existing
mutex. Every mutation or lifecycle operation that can change searchable rows,
volume availability, inclusion rules, or residency advances the generation.

At query start, the pipeline captures the searchable-volume metadata, the
generation, and (when a cold volume is involved) starts an independent durable
read snapshot while holding the mutex. Hot projection rows are copied into a
fixed-size owning candidate chunk while the mutex is held. Matching, path work,
sorting, and top-N accumulation happen after that lock is released. Before the
next chunk and before publication, the query validates the generation. A
change discards the partial response, closes the read session, and restarts
from a new capture, up to a small fixed limit; repeated churn returns
`Unavailable`.

This is a deterministic-restart consistency model rather than an accidental
mixed-generation stream. It avoids copying the complete projection and avoids
holding a shared/read lock for seconds. A query that reaches a stable final
generation is coherent; a mutation that races it either precedes the query's
captured generation or causes a whole-query restart.

Alternatives rejected:

- `std::shared_mutex` alone still lets one broad reader hold the read lock for
  the entire cold stream and blocks the writer.
- A full immutable projection copy per query defeats the Phase-8 memory
  ceiling and makes keystroke search O(N) in owning memory.
- Holding the current mutex while matching is the defect being removed.

### D2 — Add a connection-owned `StoreReadSession`

`ffindexstore` will expose a move-only read-session type created from the
writer store's database path. It opens a separate read-only SQLite connection,
enables query-only behavior and a bounded busy timeout, and begins one
transaction for the query snapshot. It implements cancellable entry streaming,
point lookup, and aggregate reads using that connection. It never runs schema
migrations, writes, vacuum, or checkpoint maintenance.

The writer `Store` remains unsynchronized and is used only by the pipeline's
short mutation critical sections. No new code will concurrently call methods
on the same `Store` object. Session destruction finalizes statements and rolls
back the read transaction deterministically. SQLite WAL permits the writer to
commit while the session pins an older snapshot.

### D3 — Keep the Phase-8 hot/cold partition and path semantics

Search still classifies volumes by residency. Hot rows are captured from the
projection in bounded chunks. Cold rows stream from the read session, with
parent-chain reconstruction using the same session's point lookups. The
filename-trigram accelerator remains eligible only when all searchable
volumes are hot; if its postings are too large for a bounded capture, the
canonical hot scan is used. No read path reloads a cold volume.

An internal bounded search accumulator will own at most the protocol result
cap of `SearchResult` values and will apply the existing query predicates,
relevance, deterministic total order, and top-N truncation as rows arrive.
This keeps broad-query memory at one capture chunk plus bounded results while
preserving the Phase-8 order/cap contract across hot and cold sources.

### D4 — Use FIFO global query admission

`UiServer` will retain the existing per-client supersession/cancellation seam,
but its client workers must acquire a global `IndexedSearchCoordinator` permit
before invoking the pipeline handler. The production coordinator allows two
active indexed searches and eight queued waiters. Queue admission is FIFO;
cancelled waiters are removed/skipped, and a full queue produces a fail-closed
`Unavailable` response with diagnostics. The coordinator exposes active,
queued, peak, wait, rejection, and shutdown counters. It does not create
threads; the existing per-connection worker threads remain lifecycle-owned,
while expensive query/read work is globally bounded.

Two active queries are enough to preserve independent client progress while
leaving ingestion and lifecycle work outside the query permit. The constants
are centralized and test-overridable; no unbounded reader cache or thread-per-
result behavior is introduced.

### D5 — Make fault and test seams explicit

The pipeline will expose test-only hooks for search phases and injectable read
fault points. A phase hook is invoked while query work is deliberately parked
outside the mutation lock in the fixed design. The deterministic reproduction
first uses the same hook to show the old implementation parked a writer behind
the search; the final regression asserts that ingestion, aggregate, and
publication progress while the hook remains held.

Fault points cover reader open/begin, stream step, point lookup, busy/read
failure, generation churn, cancellation, session exhaustion, and response
serialization. They are inert unless a test installs them and never weaken
production fail-closed behavior.

### D6 — Treat shutdown as cancellation and ownership drain

`UiServer::Stop` stops new coordinator admission, signals all client
cancellation tokens, wakes queued waiters, stops the pipe listener, waits for
connection callbacks, and joins workers before releasing server-owned state.
`IndexPipeline::Close` advances the generation and closes only its writer
after its short mutex section; a query holding an independent reader is
cancelled by its owner and releases that reader before completion. No callback
may publish after a client/server stop state is observed.

### D7 — Add bounded diagnostics, not production tracing

`SearchTimings` gains generation/restart, state-lock wait/hold, hot/cold, and
reader attribution for benchmark/test callers. Pipeline and coordinator
diagnostics expose bounded counters for active/peak searches/readers, queued
work, cancellation, failures, and restarts. No diagnostic record includes
candidate names or paths. Existing redaction and diagnostic append policies
remain unchanged.

### D8 — Preserve the privileged/degraded boundary

The read plane lives entirely in the unprivileged engine and index-store
library. It adds no UI↔service or engine↔service command, does not read file
content, does not alter MFT/USN parsing, and never makes degraded browsing
depend on the service or the indexed database.

## Risks / Trade-offs

- **[Risk]** Frequent ingestion can cause repeated query restarts and make a
  broad search expensive. → Restarts are bounded and observable; cancellation
  wins immediately; a query that cannot stabilize fails closed instead of
  returning mixed or partial data.
- **[Risk]** Copying a bounded hot chunk adds per-row name/path ownership to
  selective searches. → Keep the chunk small, retain the bounded trigram fast
  path for genuinely selective all-hot terms, measure established benchmarks,
  and use view-level filtering before path materialization where profiling
  proves necessary.
- **[Risk]** A pinned SQLite snapshot can retain WAL frames. → Reader count and
  lifetime are bounded, passive checkpoints remain writer-owned, cancellation
  finalizes sessions, and WAL size is measured in the scalability evidence.
- **[Risk]** Per-client worker threads still exist for pipe lifecycle while the
  coordinator bounds query work. → The coordinator caps active/queued query
  execution; future connection architecture can remove idle workers without
  changing the read contract.
- **[Risk]** A malformed or forgotten volume can invalidate parent paths. →
  Point lookup/path walks remain bounded and defensive; any incomplete durable
  stream fails closed, and generation/lifecycle validation prevents stale
  publication.
- **[Risk]** Tests can accidentally prove timing instead of ownership. → Use
  barriers/condition variables and explicit phase hooks; wall-clock values are
  evidence only, with bounded watchdogs for hangs.

## Migration Plan

1. Add the OpenSpec contract and deterministic pre-refactor contention seam.
2. Add and test independent read sessions without changing the writer schema.
3. Add generation/chunked search and the bounded accumulator; retain the old
   result semantics as regression references.
4. Add coordinator admission, lifecycle/cancellation, metrics, and fault tests.
5. Run focused, full, analyze, ASAN, verifier, OpenSpec, policy, and benchmark
   gates. Rollback is a source-level revert of the Phase-9 commits; databases
   remain schema-compatible because no migration is introduced.

## Open Questions

None. Chunk size, restart count, coordinator limits, and diagnostic fields are
implementation constants/seams covered by the tasks and may be tuned from
benchmark evidence without changing the behavior contract.
