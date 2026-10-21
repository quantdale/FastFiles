# Independent adversarial review (9.1) — six lanes, 2026-10-21

Six parallel read-only review lanes attacked the committed seams at `b6df9ef`
(Unicode/search completeness, clipboard/test integrity, rebuild correctness,
memory/cold integrity, performance/statistics, certification truth). Full
lane reports are preserved in the session run artifacts
(`build/campaign/review-task-{1..6}.md` mirrors). All confirmed material
findings were fixed in `43fc3c1` and re-tested; the lane-suggested validation
commands were run by the primary agent.

## Confirmed findings and remediation (fix commit `43fc3c1`)

| Lane | Finding | Severity | Remediation | Regression test |
|---|---|---|---|---|
| D P1-1 / C F1 | Memory-pressure enforcement evicts+compacts across volumes with no ProjectionResidency epoch bump; in-flight searches serve silently incomplete results as Completed and walk repacked layouts | P1 (results integrity) | `EnforceMemoryLimitNow` returns whether eviction ran; every enforcement/reload site bumps residency epochs via the existing restart machinery (scoped unrelated-mutation semantics preserved) | `TestEnforcementEvictionAdvancesQueryVisibleEpochs` |
| D P1-2 | Over-watermark reload streams the whole volume under the pipeline mutex (unbounded transient; at 2M ~839 MB) then re-evicts -> empty export, repeatedly | P1 (availability + memory bound) | fail-fast fit estimate vs the early-enforcement watermark before streaming; reload of an oversized volume returns false immediately and stays cold | `TestOversizedVolumeReloadFailsFastAndStaysCold` |
| D P2-2 | Allocation failure mid-reload-stream left a volume silently partial-but-"hot" | P2 | stream wrapped in catch-all -> existing failed-read cleanup (remove partials, re-mark cold) | covered by the reload test + existing failure-injection tests |
| C F2 | `ProjectionEntryCursor` holds raw map/array positions; growth/compaction scrambles them -> silent truncation | P2 | layout markers + `Stale()`; hot loops treat staleness as a stale generation (restart + re-capture), never "exhausted" | `TestCursorDetectsDisturbedLayout` |
| C F3 | Accelerator dead after every bulk scan (nothing rebuilt it) | P2 (silent perf loss) | `SetBulkScanActive(false)` rebuilds the accelerator once | `TestBulkScanCycleRevivesFilenameAccelerator` |
| C F4 | Store INSERT arm ignored the field mask: partial upsert of a missing row persisted wire values while the projection zeroed them (hot/cold divergence) | P2 | INSERT binds zero/NULL for absent fields (matches `UpsertPartial` contract) | `TestPartialUpsertOfMissingRowZeroesAbsentFields` |
| C F5 | `RemoveBatch(keep=false)` left small batches' accelerator valid (contract violation) | P3 | keep=false invalidates regardless of batch size; contract comment corrected | existing removal tests + comment |
| C F6 | Cold reload re-enabled the accelerator mid-bulk-scan | P3 | rebuild gated on `!bulkScanActive_` | covered by bulk-scan test |
| C F7 | Missing-trigram branch returned the wrong (too large) candidate set via dead intent | P3 (safe over-approximation) | returns the exceptional merge of an empty intersection | existing superset tests |
| D P2-1 | Single-pass enforcement could free O(1) rows per batch boundary (never converges in flat corpora) | P2 | bounded multi-pass loop (8 passes, no-progress guard) | `TestProjectionMemoryLimitRunsAtBatchBoundary` (semantics preserved) |
| B P2-1 | Interception SKIP evaluated before the sequence-change guard (external change + all-null probes could skip) | P2 (skip integrity) | sequence-change evaluated first; counterexample is case 7b | `ffclipboard_classifier_tests` case 7b |
| B P2-2 | `writePublished=false -> SKIP` conflated session refusal with product write regression | P2 | readable evidence + failed write = FAIL(undetermined) | case 8b |
| B P2-3 | Interception gate required a CF_UNICODETEXT witness the product never publishes (skip unreachable; comment overclaimed evidence) | P2 | gate over the product's actual write set + no readable handle anywhere; comment cites the real late-delivery evidence | case 7 (updated) |
| B P3-4/5/6 | Probe bookkeeping invariant; observation-identical controls; window diagnostics | P3 | bookkeeping fixed; direction-distinct controls; attempts-before-exact recorded | controls 3-6 updated |
| E E-1/E-2 | Evidence noise-floor claim off ~an order of magnitude; cold "medians" were n=1 | P1 (evidence truth) | `memory-cold-decision.md` corrected (same-block control -9%..+6%; n=1 samples; hot medians 10/5 runs) | n/a (documentation) |
| E E-3..E-8 | Scan-floor number, rebuild band phrasing, 189x scope conditions, attribution tautology, 1M-cold context, artifact provenance | P2/P3 | all corrected in the evidence docs | n/a |

## Honest open items (not fixed here, no material risk claimed)

- Lane D P3-3: `Store::ForEachEntry` and `ReadEntryRecord` duplicate the
  column decode (currently identical); consolidation deferred.
- Lane C F6 second half: `RebuildAll` pays O(V*N) full-projection
  compaction/rebuild across volumes; batching deferred (correctness fine).
- Lane E E-8: future cold-profile artifacts should embed `--source-sha`
  (current pairing is asserted provenance, now disclosed).
- Lane D P3-2: a direct same-visitor A/B prefilter test would close the last
  equivalence-test indirection (semantics currently covered transitively by
  the store exact-set tests + hot/cold signature equality).

## Validation after remediation (`43fc3c1`)

- `ffindexstore_projection_tests`, `ffindexstore_store_tests`,
  `ffindexstore_read_session_tests`, `ffengine_index_pipeline_tests`,
  `ffengine_index_pipeline_concurrency_tests`, `ffclipboard_classifier_tests`,
  `ffquick_actions_tests`: all PASS (Debug, MSVC `/W4 /WX`).
- Full CTest suites at the certified tip: see `certification-ladder.md`.
