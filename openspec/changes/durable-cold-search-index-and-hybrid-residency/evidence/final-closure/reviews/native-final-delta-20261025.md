# Scoped independent review — final subtree repair and benchmark scheduler

- Date: 2026-10-25.
- Reviewer: fresh-context native agent `/root/independent_acceptance_review`.
- Inspected source: `8bffb329779ee0a431aaed37fa92925a4b373d9b`.
- Exact delta: `4c1c4501e5b1f0579a2d34f4f7afea5f83708979` through
  `db7887c1ec1d3d8f493848a9050240407423e678`.
- Independence: fresh-context, read-only native source review; no cross-provider
  independence or independent runtime execution is claimed.
- Activity: source and retained evidence inspection only; no writes, builds,
  benchmarks, UI automation, termination or prohibited-directory reads.

**Verdict: APPROVE for this source delta; no current P1/P2 findings.**

## Subtree eviction

`Projection.cpp:379–423` completes traversal and visited-set cycle detection
before erasure, invalidates the accelerator and dense-order flag, and erases
selected live identities. Both private callers at `:535–536` and `:552–553`
immediately call `CompactStorage`; `:446–482` rebuilds name, LRU and child links
only from surviving identities. Intermediate stale slices do not escape.

`IndexPipeline.cpp:426` and enforcement sites `:812/:824/:965/:972` advance
structural epochs under the engine mutex. Existing concurrency tests at
`:624/:672` inspect cursor restart and exact mixed identities.

The retained red log fails the intended sibling-comparison assertion while
survivor assertions pass. The green log records projection, pipeline and
concurrency passing 3/3.

## Search-only scheduler and evidence

`bench_large_index_scalability.cpp:2248–2292` retains five large-scale warm
samples for selective/exact/no-match, one per other class and one forced-cold
sample each. Ancillary skips are explicit; normal mode is unchanged. JSON mode
and sampling caveats are explicit at `:2427–2431`.

Both final 2M artifacts embed `db7887c1`; sample counts match the declared
schedule. Their 128 B projection estimate, `hot_rows_examined=0` and
`cold_volumes_searched=1` support the disclaimer of fully-hot 2M. “Warm” means
repeated execution, not projection residency.

SHA-256 checks match `artifact-manifest.json` for all four inspected archives:

- `artifacts/final-pipeline/pipeline-2m-1.json.gz`.
- `artifacts/final-pipeline/pipeline-2m-2.json.gz`.
- `artifacts/subtree-red.log.gz`.
- `artifacts/subtree-green.log.gz`.

## Approval limits

This closes the previously unavailable independent final repair/scheduler source
review only. It does not establish full PR #15 merge or runtime certification.
Task 4.1 resident/mixed coverage, the process-reaper failure and hosted execution
remain separate gates. A performance harness exit 0 observes its measurements;
it is not the independent canonical correctness oracle.
