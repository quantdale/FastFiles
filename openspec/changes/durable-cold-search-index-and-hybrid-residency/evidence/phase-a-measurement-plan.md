# Phase A measurement plan + run log — durable-cold-search-index-and-hybrid-residency

Methodology follows the predecessor's corrected discipline
(`large-index-lifecycle-and-search-scalability/evidence/memory-cold-decision.md`
lane-E corrections) plus lane-F audit input:

- release-test binaries from the successor worktree at `d3884ee`
  (PR #14 HEAD; source-equivalent to the certified `43fc3c1` product tree —
  gate-proven), identity passed explicitly
  (`--source-sha d3884eecd03805a91fe5d12d7a90ceb1b5c082f2 --source-state clean
  --preset release-test`).
- 12 query classes (selective, moderate, broad, no_match, prefix, extension,
  worst_substring, exact_name, short_prefix, mixed_case, unicode,
  high_limit_broad), HOT and COLD phases both emitted per class.
- COLD here = cold projection served from the SQLite stream (the production
  fallback path); warm SQLite page cache is a disclosed condition (matches the
  predecessor's "COLD" definition).
- Machine-readable output: harness JSON with per-run counters (rows scanned,
  coldRowsExamined, candidates, results, stream ms, wire bytes, memory).

## Staged scale plan (mission §5: no uncontrolled hours-long matrix)

| Stage | Scales | Status |
|---|---|---|
| smoke | 5,000 | DONE (invocation validated; JSON written) |
| baseline matrix | 200k, 1M, 2M | RUNNING (`build/worktrees/baseline-200k-1m-2m.{json,log}`) |
| baseline 5M | 5M | STAGED: run after the 2M matrix is ingested and the machine is idle; disclosed incomplete coverage is acceptable if impractical |
| repetitions | 200k/1M/2M x3 process repetitions, counterbalanced before/after | required for every wall-clock improvement claim in Phase D |

## Baseline provenance requirements (all satisfied by the harness flags above)

actual source SHA; clean/dirty state; fixture + run configuration (scales,
query classes, preset). Every artifact copied into
`openspec/changes/durable-cold-search-index-and-hybrid-residency/evidence/`
at commit time carries these fields embedded.

## Planned prototype measurements (task 1.2, test-only seams)

Per alternative (A FTS5 / B durable postings / C hybrid residency / D
ordinal-key index / E budget control): candidate-generation latency,
completeness status (superset proof harness), ingestion overhead (rows/sec
before/after), index bytes per 1M rows, bounded-cache memory. Exact commands
to be fixed after the lane-A..F reports land and the decision gate weighs them.
