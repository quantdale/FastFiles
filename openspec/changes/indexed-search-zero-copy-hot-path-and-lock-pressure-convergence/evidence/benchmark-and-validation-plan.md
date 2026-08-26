# Benchmark and Validation Plan

## Principle

Measure the post-ASCII-fold production path first. Do not compare the new design only against historical pre-optimization numbers. Use the same host/build/preset for paired before/after data and record the exact commit for each measurement.

## Required resident-index datasets

Use deterministic datasets large enough to expose per-row costs; retain the existing 1M-row scale where practical.

1. mixed-case ASCII names, selective hit;
2. mixed-case ASCII names, miss;
3. lowercase ASCII hit/miss;
4. Unicode names and non-ASCII needles;
5. duplicate/common basenames across many folders;
6. long-name and boundary-length corpus;
7. sparse (~0.01-0.1%), medium (~5-20%), and broad (>50%) match rates;
8. filename-index eligible and forced canonical-scan cases;
9. metadata filters (`ext:`, kind, size/allocated-size, modified) and compound predicates;
10. folder/path-dependent scope/filter cases.

## Required source-state combinations

- all-hot resident projection;
- mixed hot/cold volumes;
- forced projection eviction/reload;
- active ingestion/rename/move/delete while search runs;
- repeated relevant invalidation until restart/fallback boundary;
- cancellation during capture, predicate execution, survivor materialization, and durable streaming.

## Measurements

For every representative run capture:

- end-to-end search duration;
- projection rows examined / filename candidates enumerated;
- lightweight rows/views captured;
- rows rejected before owned materialization;
- `candidateNameCopies`;
- `candidateObjectsMaterialized`;
- path lookups/materializations;
- predicate/substring/glob timing counters already exposed;
- state-lock wait and max hold;
- capture time vs matching time vs survivor materialization time;
- restarts, relevant invalidations, durable fallback attempts/rows/success;
- result count/truncation;
- peak transient memory and retained name-storage bytes/generations if the selected design introduces them.

Run enough repetitions to distinguish noise. Report median and a tail statistic (p95 or maximum of a fixed small repeat set) rather than selecting a single favorable run.

## Correctness oracle

Keep a reference path capable of producing canonical owned-candidate results for deterministic test corpora. Compare:

- status (`Completed`/`Cancelled`/`Unavailable`);
- exact candidate identities;
- exact ordering for every sort direction;
- `MatchTier` behavior/relevance order;
- truncation/top-N boundary behavior;
- scope and persistent-only semantics;
- allocated-size known/unknown ordering;
- rule-filter behavior;
- mixed hot/cold result composition.

The reference path is a test oracle, not a permanently duplicated production engine.

## Lifetime / concurrency proof

Whichever storage strategy is chosen must have explicit tests for:

- name insertion that would have reallocated the old vector arena;
- rename generating a new interned name while an earlier search is active;
- projection `CompactStorage` while a search holds its permitted read state;
- volume eviction/reload;
- query-visible epoch advance and restart;
- search cancellation releasing any storage lease;
- shutdown releasing all readers before projection destruction;
- bounded number/bytes of retired storage generations;
- no stale slot-index dereference after compaction.

Prefer ASAN-capable focused tests where the existing Windows toolchain supports them, but do not rely solely on a sanitizer for lifetime correctness.

## Performance gate

A product-path change lands only if correctness/lifetime gates pass and paired measurements show a material improvement in the targeted resident workload. As a default interpretation, <=5% movement is noise unless repeated data demonstrates otherwise. The campaign should aim to structurally eliminate owned names/candidates for rejected eligible rows; if that structural win does not translate to useful latency/lock/memory behavior, do not retain an invasive storage redesign merely because a counter improved.

## Validation ladder

1. new NamePool/projection lifetime unit tests;
2. search-query and ASCII-fold tests;
3. indexed-read-plane/index-pipeline focused tests;
4. index-pipeline concurrency/churn tests;
5. large-index and filename-index benchmarks;
6. strict OpenSpec validation;
7. source provenance/doc truth/policy fixtures;
8. `verify.ps1 status` + `doctor` and focused release-frontier generation;
9. official verifier build gate;
10. Debug full CTest;
11. release-test full CTest;
12. Analyze warning-clean;
13. required ASAN protocol/IPC slice plus any new sanitizer-capable lifetime slice;
14. exact committed-tree qualification;
15. `git diff --check` and complete diff review;
16. push without force, then inspect hosted CI for the exact pushed implementation SHA.

External production signer/pins, privileged MFT/USN execution, provider/guest, clean-host, physical-display, and foreground-owned interactive UIA remain external unless the implementation actually changes those boundaries.
