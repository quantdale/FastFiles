# Proposal: durable-cold-search-index-and-hybrid-residency

Status: **PROPOSAL / DECISION RECORD ONLY — implementation is explicitly not
started.** This change was prepared during the `large-index-lifecycle-and-
search-scalability` closure campaign (PR #14 handoff) as the separately scoped
successor that campaign deliberately deferred. It must not be folded into PR
#14 and must not begin production implementation without an explicit owner
authorization and a completed measurement phase (task group 1).

## Problem

At 2M+ indexed rows the product enters a measured **cold regime**: the
in-memory projection (~839.7 MB modeled at 2M rows) exceeds the 500 MiB
default budget (`Projection::kDefaultMaximumMemoryBytes`; enforcement
watermark 400 MiB), `SetProjectionMemoryBudget` has no production caller, and
every query then serves from the SQLite cold stream. Measured at 2M
(`evidence/search/search-matrix-before.json`, historical p50): selective
~2.5 s, broad ~27.1 s, extension ~37.6 s per keystroke-triggered query class.

The completed predecessor campaign shipped the safe, schema-free
improvements and stopped here on purpose:

- hot/projection-resident selective search is already accelerated
  (filename-trigram accelerator, 56-189x on eligible literal classes at 1M,
  rows examined 1M -> 1);
- the cold path gained a conservative SQL name prefilter
  (`ForEachEntryWithAsciiNameUntil`, 1.28-1.44x on cold literal classes at
  1M with 9.8% non-ASCII names), but the **hard floor is the name-column
  scan itself (~1.05-1.10 us/scanned-row)** — a zero-survivor literal is
  still ~1.1 s at 1M and proportionally worse at 2M-5M;
- non-literal classes (broad, extension, short_prefix, unicode) are
  prefilter-ineligible by design and remain O(N) scans on the cold path.

The largest remaining user-relevant cost at production scale is therefore
**cold candidate generation at 2M-5M+ rows**: no durable structure exists
that can answer "which rows could match this needle?" without scanning and
decoding every row of the volume.

## What this proposal investigates (measure first, decide second)

The proposal does **not** assume FTS, schema v3, or hybrid residency is the
answer. It defines a measurement campaign over alternatives, with a decision
record per alternative:

1. **Durable candidate-index alternatives** (schema v3 or equivalent):
   - SQLite FTS5 (unicode61/trigram tokenizer) as a candidate generator;
   - a durable trigram posting table mirroring the in-memory accelerator's
     semantics (folded-trigram postings + exception set);
   - covering-index / sorted-name strategies (ordinal-key prefixes);
   - hybrid hot/cold residency (Option C of the predecessor's memory
     decision: a compact, always-resident candidate subset per volume) as a
     non-schema alternative;
   - budget-policy alternatives (adaptive budget) as the control arm.
2. **Candidate completeness under Windows ordinal comparison.** Every
   candidate generator must be a **conservative superset** of the canonical
   matcher's result set under `CompareStringOrdinal(..., TRUE)` semantics on
   the host table — including the fold-exceptional class (units the host
   uppercase table equates with ASCII, e.g. U+017F on some tables) and
   surrogate names, per the predecessor's P2-A repair
   (`Projection::SetTestingFoldExceptionalUnit`, host-derived memoized
   probe, fail-closed). An FTS tokenizer that drops such rows is not
   acceptable; the differential suites (hot/cold identity + ordering,
   `TestFilenameCandidatesAreConservativeSuperset` and descendants) are the
   acceptance oracle.
3. **Query planner selection.** When does the planner consult the durable
   index vs. stream the cold path? Candidate caps, eligibility gates (single
   literal, folded length, residency), and fallback-to-scan behavior must be
   cost-based and measured at 200k/1M/2M/5M.
4. **Schema migration and rollback.** Any schema v3 must be additive and
   versioned: migration cost at 2M-5M rows, crash-consistent index updates
   (the index must never disagree with the entries table after a crash —
   transactional coupling or rebuild-on-open), a documented,
   data-preserving downgrade policy (FAIL_CLOSED_DOWNGRADE refusal +
   operator-invoked rollback that drops only derived structures — never an
   automatic in-binary downgrade, which is not supported), and disk-size
   tradeoffs (measured index bytes per 1M rows per alternative).
5. **Bounded memory footprint.** The durable index is a disk structure, but
   caches/planners built on it must stay within documented bounds; no
   alternative may silently raise the default 500 MiB projection budget.
6. **Cold/hot result equivalence.** Cold (durable-index-assisted) and hot
   (projection-accelerated) results must remain byte-identical in identity
   and ordering for every query class — the pinned contract from the
   predecessor spec delta.
7. **Startup/rebuild cost.** Index build during scan/ingestion, incremental
   maintenance per USN batch, and reopen/recovery cost must be measured;
   a durable index that doubles ingestion time is a regression.

## Performance targets (to be validated, not promised)

Measure at 200k / 1M / 2M / 5M rows, same discipline as the predecessor
campaign (idle host gating, medians for hot, disclosed single samples for
cold, machine-readable artifacts with embedded identity):

- cold selective/exact/no-match at 2M: from seconds to the sub-200 ms class
  if a durable candidate generator is adopted;
- cold non-literal classes: measured improvement or an honest documented
  floor;
- ingestion and rebuild overhead: no material regression vs. the certified
  31-34x replacement-rebuild behavior at 1M.

## Impact

- **Affected components (if adopted later):** `ffindexstore` (schema,
  migration, candidate generation), `ffengine` (planner selection, budget
  policy), `ffsearch` (query classification), tests (differential +
  crash-consistency + migration suites). No protocol, service, installer, or
  UI change is implied.
- **Risk:** schema migration on multi-million-row user indexes is the
  highest-risk operation this product would yet ship; crash-consistency and
  a data-preserving downgrade policy (fail-closed refusal + documented
  operator rollback) are mandatory acceptance items, not stretch goals.
- **Non-goals:** raising default memory consumption; changing canonical match
  semantics; hot-path redesign; any change to the certified PR #14 behavior.
- **Relationship to PR #14:** none — PR #14 remains a self-contained,
  certified candidate; this successor starts from its landed state.

## Decision record pointer

`design.md` in this directory is the living decision record: alternatives,
measurement plan, acceptance gates, and the retain/reject decision per
alternative (to be filled by the future campaign — current entries record
the deferred predecessor decisions that bound this work).
