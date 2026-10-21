# Design: large-index-lifecycle-and-search-scalability

## D1. Bulk removal, not double-buffer replacement

The measured defect is incremental destruction cost, not construction cost. The fix
therefore removes the wasted per-entry work instead of restructuring ownership:

- `Projection::RemoveBatch(volumeRowId, frns, keepFilenameIndexCurrent)` above
  `kPerEntryFilenameRemovalThreshold = 256` drops the filename accelerator wholesale
  (`InvalidateFilenameIndex`, O(1)) and rebuilds it exactly once at the end
  (`RebuildFilenameIndex`, O(N)); below the threshold the per-entry path keeps the
  accelerator hot without a full rebuild. Child unlink, id-map erase, and free-list
  maintenance stay per entry: they are O(1) each (measured residual < 0.7 us/row).
- `RemoveVolume(volumeRowId, keepFilenameIndexCurrent = true)` routes through
  `RemoveBatch`; the default preserves the external contract (accelerator current
  afterwards). `RebuildVolumeFromStore` passes `false` because its own end-of-rebuild
  `RebuildFilenameIndex` covers the accelerator (the measured double-rebuild waste).
- `EvictSubtree`/`EvictVolume` skip per-entry erases above the threshold; their
  trailing `CompactStorage` invalidates the accelerator anyway, and the
  memory-pressure path must not allocate a rebuild.

Rejected alternatives: fresh-`Projection`-and-swap (D-arm measurement shows a
double-buffer peak of ~1 GB private at 1M rows -- unacceptable at the default budget,
see D2); per-entry removal retained (the measured 40x pathology); unconditional
rebuild-on-every-small-batch (O(N) regression for single USN deletes).

## D2. Memory semantics preserved by construction

The fix is in-place: no second projection, no peak change (after-fix memory report:
private ~461-477 MB, live requested 345.9/349.9 MB -- identical to before). The
memory-limit policy, LRU timestamps, eviction, and `EnforceMemoryLimit` watermark
semantics are untouched. Finding recorded for Phase 8: a 1M-row projection straddles
the default 500 MB budget (modeled ~414 MB vs the 400 MB low watermark in the
raw-projection harness), so rebuilds can deterministically end in eviction +
accelerator invalidation; `SetProjectionMemoryBudget` has no production caller. That
is a product memory-policy question, deliberately NOT bundled into this fix.

## D3. Benchmark-only seams stay out of production

`FF_INDEXSTORE_TESTING` compiles the `RemovalMaintenanceMode` attribution seam into
the isolation benchmark target's `Projection.cpp` only; production builds contain no
seam (the mode checks compile to constant false). The isolation matrix is
informational and never a CTest gate.

## D4. Search planner: conservative candidates without the false-negative risk

`FilenameCandidateIndices` currently unions every non-ASCII-named row into every
candidate set. Since the fixture's 10% Unicode population alone exceeds
`kMaximumFilenameIndexCandidates` (16,384) at 200k+ rows, every lookup returns
nullopt and the accelerator is dead code at scale.

Correctness argument for removing the unconditional union (to be proven by the
mandatory differential suite before it lands):

- `FoldAscii` is length-preserving (one output unit per input unit; only ASCII
  A-Z is lowered), so trigram windows align between a name and its fold.
- Non-ASCII names' folded trigrams ARE indexed (`AddFilenameIndexEntry` indexes the
  fold of every name; the non-ASCII list is a separate structure).
- A pure-ASCII needle of length >= 3 that occurs in a name (fold-matched for ASCII
  names, ordinally for non-ASCII names) has all its folded trigrams present in that
  name's folded trigram multiset, so the posting intersection can never drop a true
  match. The non-ASCII union can only add false positives.

The candidate set stays conservative; the canonical matcher still verifies every
candidate; hot/cold candidate identity and ordering equivalence (the pinned spec
contract) is unaffected because both paths verify through the same matcher.

Eligibility/cost-based selection: keep the existing gates (simple single literal,
folded length >= 3, every searched volume resident) and the existing cap
(`kMaximumFilenameIndexCandidates`): queries whose smallest posting already exceeds
the cap correctly fall back to the canonical scan (broad classes) -- the planner
gains come from enabling the clearly selective classes (selective, exact, no-match),
not from forcing the index where candidate sets are broad.

## D5. Differential testing is mandatory (spec contract)

Every accelerated path must reproduce the reference result set exactly across the
full matrix: sort fields, directions, capped/unbounded, volume roots, Unicode,
mixed case, empty term, dotted filenames, extension, ties, path sort, cancellation,
stale query replacement. The superset property is tested directly against a
brute-force canonical scan over Unicode-heavy fixtures; end-to-end equivalence
reuses the hot/cold differential harness (`TestForcedEvictionSearchEquivalence`
family) plus a focused ASCII-needle-on-Unicode-fixture case.

## D6. Evidence and identity discipline

Before/after use the same harness, fixture, and command line for exact
comparability. Report rows/second and operation counts alongside wall time. Keep
`implementation_sha`, `certified_binary_sha`, `evidence_sha`, and
`repository_tip_sha` separate; regenerate final artifacts at a clean embedded
identity; never claim reproducible-binary equivalence. Benchmark claims are
single-machine wall-clock; comparisons are strongest within one report.
