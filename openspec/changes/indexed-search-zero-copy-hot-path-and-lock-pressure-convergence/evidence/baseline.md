# Baseline — Planner Audit

Planned from repository `quantdale/FastFiles` `main` at:

- commit: `a1c8085b51f2594f9bf525414089c01fc43cd6b6`
- commit tree: `a7522a44f8445499b195d3c3e15d1659442b1c40`
- observed hosted CI at planning time: run `32970997929` for the exact SHA above was still `in_progress`; this planner does not convert an in-progress run into a PASS claim

## Preceding optimization evidence

The immediately preceding repository-wide optimization campaign recorded:

- production mixed-case literal hit: 258.6 ms -> 85.9 ms per 1M rows (-67%)
- production mixed-case literal miss: 122.7 ms -> 75.2 ms per 1M rows (-39%)
- final local Debug and release-test suites: 86/86 PASS
- Analyze: warning-clean
- prepared ASCII-fold matching, view-path matcher unification, an anchor-boundary OOB repair, and bulk wchar_t/char16_t conversion

The executor SHALL reproduce a fresh baseline from live HEAD rather than treating these planner numbers as final implementation evidence.

## Repository-state reconciliation found by this planner

The preceding `committed-tree-validation-provenance-and-clean-checkout-parity` campaign is implemented even though current control-plane files had not fully reconciled it:

- all tasks in its `tasks.md` are checked;
- implementation commits are `656acb8b8ebdc69da97501f34917df484213bf49` and corrective `35deafe13e601b82bc88fc2e893f0df0b788a5f4`;
- hosted Actions run `32956387761` completed `success` for exact corrective SHA `35deafe13e601b82bc88fc2e893f0df0b788a5f4`;
- the old `.agent/EXECUTION_PROMPT.md` still said that campaign was ACTIVE;
- `openspec/lifecycle.json` did not yet contain that campaign.

This planning commit reconciles those stale control-plane facts before activating the new search campaign.

## Primary code-path finding

In `IndexPipeline::SearchIndexedReadPlane`, the resident hot-loop currently:

1. holds the engine state mutex;
2. advances the projection cursor or filename candidate slots;
3. calls `MakeProjectionCandidate` for every examined hot row;
4. converts the interned UTF-16 name into an owned `std::wstring`;
5. stores the owned candidate in the bounded chunk;
6. releases the lock;
7. only then calls `SearchAccumulator::Add`, where query predicates reject non-matches.

Existing diagnostics count `projectionRowsExamined`, `candidateNameCopies`, `candidateObjectsMaterialized`, `stateLockWaitMs`, and `maxStateLockHoldMs`, making the remaining cost directly measurable.

## Lifetime constraint discovered

`CandidateView::name` is a `std::u16string_view` into `NamePool`. The current `NamePool` owns a contiguous `std::vector<char16_t> arena_`; `Intern` can grow/reallocate that arena. `Projection::CompactStorage` builds a replacement `NamePool` and moves it into the projection. A naked view therefore cannot safely escape the projection lock. Query epochs protect semantic consistency but do not make invalidated pointers safe.

The new campaign must solve or avoid that lifetime problem explicitly.
