# Design / decision record: durable-cold-search-index-and-hybrid-residency

Status: **decision record seeded during the PR #14 closure campaign; every
alternative is UNDECIDED pending the measurement phase (task group 1).** No
production implementation exists or is authorized by this document.

## 1. Bounding facts (inherited, measured)

These constrain the design space and come from the completed
`large-index-lifecycle-and-search-scalability` campaign's evidence
(`evidence/memory-cold-decision.md`, `evidence/lifecycle-reload-resources.md`):

- Projection budget: 500 MiB default, 400 MiB early-enforcement watermark;
  `SetProjectionMemoryBudget` has no production caller (tests only). A 1M-row
  projection models ~414 MB (straddles the watermark); 2M models ~839.7 MB
  (always cold under default policy).
- Cold stream floor: ~1.05-1.10 us per scanned row (name-column scan,
  decode, match). The shipped conservative SQL name prefilter
  (`ForEachEntryWithAsciiNameUntil`) removes the survivor-decode share for
  ASCII literals (1.28-1.44x at 1M, 9.8% non-ASCII names) but cannot beat
  the scan floor; non-literal classes are ineligible by design.
- Hot path: filename-trigram accelerator (folded postings + host-derived
  fold-exceptional set + surrogate-conservative set) gives 56-189x on
  eligible literal classes at 1M; candidate cap
  `kMaximumFilenameIndexCandidates = 16,384` is the existing cost-based
  fallback to canonical scan.
- Correctness contract: hot/cold identity+ordering equivalence is pinned by
  differential suites; candidate sets must be conservative supersets under
  `CompareStringOrdinal(..., TRUE)` host semantics (predecessor P2-A repair:
  fold-exceptional classification via a memoized host probe, fail-closed on
  API error).

## 2. Alternatives to measure (all currently UNDECIDED)

| # | Alternative | Sketch | Principal risk |
|---|---|---|---|
| A | SQLite FTS5 candidate table | FTS5 (trigram or unicode61) over names, queried for rowid candidates, verified by the canonical matcher | Tokenizer case-folding is NOT Windows ordinal folding; must prove superset incl. fold-exceptional + surrogate classes, or maintain an exception side-set |
| B | Durable trigram posting table | Persist the in-memory accelerator's structure (folded trigram -> sorted rowid postings + exception set) in SQLite; incremental maintenance per batch | Write amplification on ingestion; crash-consistency of postings vs entries; disk size |
| C | Hybrid hot/cold residency | Keep a compact per-volume candidate subset resident (e.g. folded-name keys or a minimal automaton) while rows stay cold | Memory bound must be strict and provable; consistency with evicted rows; predecessor marked this "separately scoped architecture work" |
| D | Covering ordinal-key index | Sorted index on the matcher's folded name key; range scans for prefix/substring classes | Substring/wildcard classes get little benefit; index size |
| E | Budget policy (adaptive/default raise) | Control arm: serve 2M hot by raising/deriving the budget | Competes with system memory; forbidden without safety proof; does not help 5M |

## 3. Mandatory acceptance dimensions (per alternative)

1. **Completeness:** candidate superset proof under host ordinal semantics,
   with red-first differential tests (fold-exceptional, surrogate, dotted,
   mixed-case, short-name shapes) — the predecessor's
   `TestFilenameCandidatesAreConservativeSuperset` family extended to the
   durable generator.
2. **Equivalence:** cold-with-index results byte-identical to hot results for
   all 12 query classes at 200k/1M/2M (and 5M for the selected candidate).
3. **Migration:** v2 -> v3 cost at 2M-5M; rollback path; crash-consistency
   (kill-during-update recovery; index never disagrees with entries);
   rebuild-on-open as the fallback recovery.
4. **Resources:** planner/index cache memory bounds; disk bytes per 1M rows;
   no change to the default 500 MiB projection budget.
5. **Cost:** ingestion/USN-batch maintenance overhead; reopen/recovery cost;
   measured planner decision quality (when index is consulted vs scan).
6. **Scale:** p50/p90/p95 at 200k/1M/2M/5M per query class, machine-readable
   artifacts with embedded source identity (predecessor lane E E-8 lesson:
   artifacts must embed `--source-sha`).

## 4. Decision log

| Date | Decision | Status |
|---|---|---|
| 2026-10-21 | Proposal + decision record created at PR #14 closure; alternatives A-E recorded; measurement-first mandated; no alternative selected | RECORDED |
| 2026-10-22 | Task group 1 measurements complete: fresh 200k/1M/2M cold baseline reproduced (2M selective 3513 ms / broad 27483 ms / extension 38709 ms); FTS5 candidate prototype (dual side-set arms) 0 false negatives x 3 scales x 10 classes and 41-76x candidate-gen win on target classes at 2M; B/D model bench measured (69-71 MB/1M postings, 42.9 MB/1M sorted-keys); lane-A..F reports landed | COMPLETE |
| 2026-10-22 | **Alternative A (SQLite FTS5 trigram candidate index) SELECTED**; B deferred as fallback; C deferred (out of scope); D rejected (correctness: prefix ranges miss infix matches); E rejected (control arm). Full evidence: `evidence/architecture-decision-record.md` | DECIDED |
| (future) | Phase D implementation + acceptance results | PENDING |

## 5. Explicit non-decisions

- FTS5 is **not** presumed correct: its tokenizer folding must be proven a
  superset of Windows ordinal ignore-case on this host (see dimension 1) or
  supplemented with an exception side-set maintained like the in-memory
  accelerator's.
- Schema v3 is **not** presumed necessary: alternative C (hybrid residency)
  may achieve the latency targets without a schema change, and alternative E
  is retained only as a control arm.
- The 500 MiB default budget is **not** raised by this proposal.
