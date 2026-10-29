# Independent adversarial review wave — campaign section 23 (task 5.10a)

Six READ-ONLY lanes over the finalization candidate (first wave at `97b9ceb`;
the wave's own findings were then remediated in `628de6d`). Full lane reports:
`build/campaign/review-lane-{a..f}.md` (working artifacts) with the finding
digest at `build/campaign/review-findings-digest.txt`. (Operational note: the
first lane run hit the 30-minute subagent timeout mid-investigation and was
recovered by resuming the same contexts under a write-first timebox; every
lane then delivered.)

## Verdicts

| Lane | Verdict | Confirmed findings |
|---|---|---|
| A startup/folding | OK with notes | 1x P1 (test-evidence contradiction), 2x P2, 3x P3 |
| B planner correctness | 1 real P1, rest P3 | P1 readiness gap; 5x P3 |
| C SQLite durability | OK | no defects; verified migration/crash/transactionality |
| D benchmark methodology | OK with notes | 1x P1 (stale cert record), 5x P2 (doc/provenance) |
| E resource scale | OK | doc claims verified byte-for-byte vs raw artifacts |
| F certification truth | doc P1s | 2x P1 (stale cert record/headline caveats), 3x P2, 2x P3 |

## P1s and remediation

1. **Lane B — query-time readiness gap (SILENT TRUNCATION class).** The
   candidate rung consulted the derived index without re-validating readiness
   in the query's snapshot; a session whose snapshot landed mid-rebuild
   (meta state=0, partial content) or on a foreign-fold-signature database
   would serve a truncated set as a successful search. **FIXED in `628de6d`:**
   `StoreReadSession::CandidateIndexUsable()` (snapshot-consistent state +
   fold-signature check, cached per session, fail-closed) gates the rung;
   red-first test `TestCandidateRungDegradesWhenIndexNotReady` simulates a
   real mid-rebuild window (marker cleared + one terms row deleted) and
   proves the pre-gate rung drops a row the gate restores via fallback.
2. **Lane A-1 — the `read-only` campaign cell's evidence contradicted its
   label** (the harness had not actually enforced the read-only attribute in
   the matrix run; a dedicated probe later confirmed the real behavior:
   valid-v3 opens via SQLite read-only degradation, migration-required fails
   closed). **FIXED:** harness sets the attribute per-run (`isReadOnly`
   handling in `RunParent`); the startup evidence states exactly which
   behavior was probed and how.
3. **Lanes D/F — stale certification record + headline claims without
   methodology caveats.** **FIXED at final certification:** the record was
   rewritten against the final candidate with this wave's caveats embedded
   (n, temperature semantics, asserted-vs-verified identity); the PR body was
   rewritten from scratch (not appended to).

## P2s and remediation

- Lane A-2 double PRAGMA execution at open (timings understated): **fixed**.
- Lane A-3 aggregate p50 cold/warm mixing: **documented** in the manifest and
  the startup evidence (per-process cost = cold-sample distribution).
- Lane D/F provenance corrections (baseline flag/SHA discrepancy;
  asserted-vs-verified identity; run-1 contamination rule; run-2/3
  dirty-flag mislabel; temperature-label semantics; bench-candidate-resources
  identity weakness): **recorded** in the provenance manifest
  "Review-wave corrections" section — no historical artifact rewritten.
- Lane F WORKSTATE staleness: **fixed** (current checkpoint rewritten against
  git reality at every frontier change).

## P3 dispositions (all recorded; none requires code change beyond noted)

- Lane A-3b: single-machine scope for the fold-signature number — accepted;
  the caching decision is insensitive to a 2x machine factor.
- Lane A-4 stale schema comment (`entry_name_index_meta` "per-volume"): fixed.
- Lane A-5 seam/sig-memoization ordering warning: documented in
  `FoldClassification.h`.
- Lane B cap-contract comment off-by-one: fixed (==cap admitted, >cap falls
  back — matching `Projection::FilenameCandidateIndices`).
- Lane B cancellation-fault vacuity: fixed (probe assertion added).
- Lane B correlated-oracle expectations: fixed (hard-coded + fail-loud
  premise).
- Lane B cap-override size_t→int narrowing: fixed (clamped).
- Lane B staged early-stop latency: documented on the API (bounded by cap).
- Lane F blob-hash retrieval note: added to the manifest.

## Residual risk (disclosed, not fixed)

- Lane B's readiness gate validates the marker and fold signature but does
  NOT re-derive row-identity equivalence at query time (the open-time
  `mapped == entries` cross-check plus transactional maintenance cover the
  realistic divergence classes; a stronger per-query check would cost a
  table-count scan per session). The gate + transactional coupling make the
  remaining window require two concurrent writers violating the
  one-writer-per-database contract.
- Lane D: `bench_large_index_scalability` identity is asserted+build-bound,
  not exe-hashed (only `bench_store_open` hashes its own binary); disclosed
  in the manifest rather than upgraded mid-campaign.
