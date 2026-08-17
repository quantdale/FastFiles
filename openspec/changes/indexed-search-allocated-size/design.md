# Design — indexed-search-allocated-size

## Context and current data path

Allocated size is already authoritative before Search sees it:

`MFT/USN metadata -> EntryRecord -> Store -> ProjectionEntry{sizeBytes, allocatedSizeBytes, allocatedSizeKnown}`.

Storage Analysis consumes those fields. Indexed Search currently narrows the model to logical size:

`ProjectionEntry -> ffsearch::CandidateView/Candidate -> IndexedSearchCandidate -> indexed-search wire -> SearchResultRow -> UiaSearchResultInfo`.

The change extends that existing chain. It must not introduce a second source of allocated-size truth.

## D1 — Logical size and allocated size remain separate dimensions

Every search candidate/result that can carry metadata SHALL represent:

- `sizeBytes`: logical file size, existing semantics;
- `allocatedSizeBytes`: on-disk allocation when known;
- `allocatedSizeKnown`: explicit knownness; zero is a valid known value and is never the absence sentinel.

Unknown allocated size MUST NOT be synthesized from logical size.

## D2 — Search filters keep their existing meaning

The registered `size:` filter continues to evaluate logical `sizeBytes` exactly as before. This change does not add `allocated:`, `ondisk:`, or another query token.

Adding on-disk metadata must therefore not change query parsing, `PredicateKind::Size`, existing filter tests, or existing result membership for any query.

## D3 — Projection-backed Search copies allocated metadata without new I/O

`IndexPipeline::SearchIndexed` SHALL copy `ProjectionEntry::allocatedSizeBytes` and `allocatedSizeKnown` into the lightweight `CandidateView`/materialized `Candidate` path while it already reads projection metadata.

No SQLite query, filesystem probe, service call, or additional IPC request is permitted per candidate/result.

The projection lock and lazy path-materialization strategy remain as they are: the new scalar fields travel with the existing lightweight candidate record and do not make path reconstruction eager.

## D4 — Degraded Search remains explicitly unknown

Search results originating from degraded directory enumeration/snapshot fallback do not have trustworthy allocated size. Such rows SHALL set `allocatedSizeKnown=false` regardless of logical size.

Do not call `GetCompressedFileSize`, open file handles, query allocation ranges, enumerate clusters, or issue one filesystem query per result merely to populate this field.

## D5 — Engine/UI protocol evolves additively

The existing `IndexedSearchResult` response and its fixed candidate header are historical wire contracts. Their exact bytes/layout MUST remain unchanged and covered by regression tests.

Introduce an additive, versioned indexed-search result representation using the repository's existing additive-message convention (as used by `FolderAggregateResultV2`) rather than enlarging the legacy fixed header in place. The extended candidate representation SHALL carry allocated bytes plus explicit knownness.

Decoder rules:

- an extended result preserves known/zero/unknown exactly;
- a legacy result maps to `allocatedSizeKnown=false` without guessing;
- malformed flags/reserved bits/counts/lengths remain fail-closed and bounds checked;
- frame-size truncation remains deterministic and honest.

The implementing session SHALL inspect the current protocol numbering/version policy and select the next valid message/layout rather than colliding with an existing message.

If the Size-on-Disk sort requires extending the existing sort enum, old sort values and their exact request bytes remain unchanged. The new value must be validated explicitly; do not reinterpret a previous value.

## D6 — Size on Disk sorting is a first-class sort, not a UI-only approximation

Add `SortField::AllocatedSize` (display label `Size on Disk`) through the search library, request mapping, engine execution, and Search UI.

For KNOWN values:

- ascending: smaller allocated byte count first;
- descending: larger allocated byte count first.

UNKNOWN values SHALL sort after all known values in BOTH directions. This prevents `Unknown` from behaving like zero or like `UINT64_MAX` depending on direction.

Ties among known allocated sizes use the same deterministic alphabetical secondary ordering as other explicit sorts. Ties among unknown rows are also deterministic.

The existing `SortField::Size` remains logical size.

## D7 — Search result presentation distinguishes the values

The virtualized Search row SHALL show both metrics without redesigning the result surface. The exact typography may follow existing Search row conventions, but the semantics must be unambiguous, for example:

- known: `12 MB • 8 MB on disk • <modified date>`;
- known zero: `0 B • 0 B on disk • <modified date>`;
- unknown: `12 MB • Size on disk unknown • <modified date>`.

Do not render `Unknown` as `0 B` and do not label logical bytes as on-disk bytes.

If the row is a directory and the current indexed Search candidate represents only the directory entry's own allocation rather than a recursive subtree aggregate, the UI must not imply that the value is a recursive folder total. Preserve the semantics of the stored candidate field exactly.

## D8 — Search result model owns the metadata once

Extend `SearchResultRow` and the search semantic projection with the same fields. Rendering, sort-state behavior, and UIA description must consume those fields rather than independently re-querying the engine/filesystem.

Do not create an accessibility-only allocated-size model.

## D9 — Microsoft UIA reports the same metadata

`Search.Results` keeps the existing fragment tree, virtualization, selection, invoke, scroll, runtime-ID, and stale-provider contracts.

Each Search result ListItem SHALL expose logical size and Size on Disk consistently with the visible row. Extend the existing `UiaSearchResultInfo` projection with allocated bytes/knownness and expose a concise accessible description/property using existing provider conventions.

Requirements:

- known zero is distinguishable from unknown;
- stale providers never retarget after query replacement;
- provider reads do not perform filesystem I/O or engine IPC;
- cache cap remains bounded;
- no duplicate semantic child is introduced solely for the second size metric.

## D10 — Cancellation, paging/truncation, and broad-search performance stay intact

The added scalar metadata must not weaken:

- cancellation of superseded indexed searches;
- bounded maximum result counts;
- protocol frame-size truncation;
- projection candidate acceleration/lazy path reconstruction;
- owner-data ListView virtualization;
- UI responsiveness.

The implementing session SHALL benchmark the existing indexed-search scalability target or equivalent focused benchmark before/after. A small fixed increase in candidate/result width is expected; algorithmic complexity and path-materialization counts must not regress materially.

## D11 — Known-value testing does not require production privileged runtime

Core correctness for known allocated sizes SHALL be proven with deterministic headless/integration fixtures that seed or construct projection/index records with:

- allocated known and different from logical;
- allocated known zero;
- allocated unknown.

Live UIA can run against the ordinary development/degraded path and is expected to prove the UNKNOWN presentation if no real indexed allocated metadata is available in that environment. Do not reopen production signing or real MFT/USN validation merely to obtain a known live value.

## D12 — Compatibility and byte-pinning are acceptance requirements

Protocol tests SHALL pin:

- the legacy indexed-search request/result bytes for existing fields/sorts;
- legacy response decode -> allocated unknown;
- extended response known/zero/unknown round trips;
- malformed knownness/flags/reserved fields rejected;
- truncation at frame limits;
- existing message IDs/layouts unchanged.

Search tests SHALL pin that existing `size:` filters and logical `Size` sorts produce unchanged results after the model grows.

## D13 — No privileged-boundary expansion

Nothing in this change expands what `FastFilesIndexSvc` reads or transmits. It consumes metadata already present in the unprivileged engine's projection. The `$DATA` prohibition remains untouched.

The parked production-validation campaign remains out of scope.
