# Proposal — indexed-search-allocated-size

## Summary

Carry the already-indexed allocated/on-disk size metadata through global Search without changing the meaning of existing logical-size search behavior.

FastFiles already persists and projects `allocatedSizeBytes` with an explicit `allocatedSizeKnown` bit, and Storage Analysis already presents that distinction honestly. Indexed Search currently drops those fields when it materializes `ffsearch::Candidate`, serializes `IndexedSearchResult`, builds `SearchResultRow`, and projects `UiaSearchResultInfo`. As a result, Search can only show logical size even when the index knows the on-disk value.

This change closes that deliberate follow-up from `allocated-size-storage-accuracy`.

## Goals

- Preserve allocated size and its known/unknown state from `ProjectionEntry` through indexed-search execution, engine→UI protocol, Search rows, sorting, and semantic Microsoft UI Automation.
- Display logical size and Size on Disk as distinct values in Search results.
- Add a distinct `Size on Disk` sort mode with deterministic treatment of unknown values.
- Preserve exact legacy indexed-search response bytes/layout and decode them as allocated-size unknown.
- Use an additive response layout/message for the extended metadata rather than changing the legacy fixed candidate header in place.
- Keep `size:` filters and the existing `Size` sort defined as logical size.
- Keep degraded/non-indexed fallback results useful while reporting Size on Disk as unknown instead of cloning logical size or probing every result from the filesystem.
- Preserve Search virtualization, cancellation, stale-result rejection, navigation, UIA provider lifetime, and bounded result behavior.

## Non-goals

- No new `allocated:` / `ondisk:` query-filter grammar in this change.
- No change to Treemap area semantics or Storage Analysis behavior.
- No filesystem-per-result allocated-size probes.
- No production signing, trust-store work, privileged service installation, real MFT/USN validation, or other parked production-validation work.
- No redesign of the Search UI or unrelated correction of existing Search scope/sort feature debt.

## Why now

The source value already exists in the in-memory projection (`allocatedSizeBytes` + `allocatedSizeKnown`), so the remaining inconsistency is a bounded data-contract/UI problem rather than a new indexing capability. `allocated-size-storage-accuracy` explicitly left indexed Search as a later change and required any future Search support to preserve unknownness rather than treating logical size as on-disk size.

## Success criteria

The change is complete when known, zero, and unknown allocated sizes survive indexed Search end-to-end; Search visibly distinguishes logical size from Size on Disk; Size on Disk sorting is deterministic; legacy response compatibility is pinned; degraded results remain unknown; semantic UIA reports the same metadata; relevant live Search validation has zero unexpected failures; all native/build/spec/policy gates pass; and the final validated state is committed/pushed to `main` with hosted CI queried by final SHA.
