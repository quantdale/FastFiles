# Evidence plan — indexed-search-allocated-size

Do not convert this plan into PASS claims until the executing session records observed results.

## Authored baseline

- Baseline product commit: `abe3a67204b2a804d80c8f9f65eaab0d297a0287`.
- `deterministic-missing-path-state` is complete at that baseline.
- `allocated-size-storage-accuracy` previously established authoritative allocated bytes + explicit knownness in Store/Projection/Storage Analysis and deliberately left global indexed Search as a separate later change.
- Current `ProjectionEntry` contains `sizeBytes`, `allocatedSizeBytes`, and `allocatedSizeKnown`.
- Current `ffsearch::CandidateView`, `ffsearch::Candidate`, indexed-search wire candidate, Search row, and `UiaSearchResultInfo` carry logical size but not allocated size.
- Current `size:` filter and existing `SortField::Size` are logical-size semantics and must remain so.

## Required implementation evidence

Record the exact changed symbols/files and prove the metadata path:

`ProjectionEntry -> CandidateView/Candidate -> SearchResult -> protocol response -> EngineClient/UI response -> SearchResultRow -> visible row + UiaSearchResultInfo -> SearchUiaProvider`.

For every boundary record:

- known nonzero allocated value;
- known zero allocated value;
- unknown allocated value;
- logical value independently.

## Required protocol evidence

Record:

1. current legacy request/result header sizes and message IDs;
2. exact pre-change legacy byte fixture(s);
3. additive extended message/layout selected and why it does not reinterpret the legacy layout;
4. legacy decode -> allocated unknown;
5. extended known nonzero round trip;
6. extended known zero round trip;
7. extended unknown round trip;
8. malformed flag/reserved/length/count/trailing-data rejection;
9. frame-limit truncation behavior;
10. old sort values/bytes unchanged and new Size-on-Disk sort value explicitly validated.

## Required search semantics evidence

### Filters

Prove existing `size:` queries have unchanged membership when logical and allocated values differ.

### Sorting

Use a deterministic fixture containing at least:

| Row | Logical | Allocated state |
|---|---:|---|
| A | 100 | known 80 |
| B | 50 | known 0 |
| C | 75 | known 40 |
| D | 25 | unknown |
| E | 90 | known 80 |

Record:

- logical Size ascending/descending;
- Size on Disk ascending/descending;
- unknown last in both directions;
- known zero participates numerically;
- deterministic tie break for A/E;
- logical and allocated ordering can differ without either contaminating the other.

## Required UI evidence

Record visible row examples for:

- known logical/allocated divergence;
- known zero on disk;
- allocated unknown.

Confirm:

- logical Size label/meaning unchanged;
- `Size on Disk` sort control exists and visibly distinguishes itself from `Size`;
- degraded row says Unknown rather than repeating logical bytes;
- search cancellation/query replacement cannot leak old allocated metadata;
- virtualization remains owner-data/bounded.

## Required UIA evidence

Headless/provider tests SHALL prove known, zero, unknown, and stale-result semantics.

Live UIA SHALL prove at minimum on the normal development path:

- `Search.Results` result remains semantically discoverable;
- accessible metadata contains logical size plus explicit Size-on-Disk unknown where the live degraded source has no allocation metadata;
- sort-control interaction does not break Search UIA/navigation;
- existing search-result discovery, structured filter, and deep-result navigation remain green.

A host without real privileged allocated values is not a blocker for this change. Do not reopen the production-signing campaign for live known-value evidence.

## Required performance evidence

Use the repository's indexed-search benchmark/diagnostic path or closest canonical equivalent. Record before/after:

- candidate count;
- total time;
- candidate/lightweight materialization time;
- lazy path materialization count/time;
- result materialization time;
- protocol result width / maximum bounded result behavior.

State explicitly that no database, filesystem, service, or extra IPC query was added per result.

## Required validation matrix

Record exact outcomes for:

- focused protocol/search/engine/UI/UIA tests;
- Debug configure/build/CTest;
- release-test configure/build/CTest;
- Analyze build;
- `verify.ps1 build`;
- `verify.ps1 doctor`;
- relevant filtered Search UIA scenarios;
- full `windows-ui-automation-validation`;
- source-marker check;
- diagnostic-policy check;
- doc-drift check;
- strict OpenSpec validation;
- `git diff --check`;
- final commit/push/clean-tree state;
- hosted GitHub Actions run bound to the final pushed SHA.

## Historical-evidence rule

Do not rewrite `allocated-size-storage-accuracy` to pretend Search was included there. Its explicit Search non-goal is historical evidence for why this change exists. Add a coordination/resolution note only if normal OpenSpec workflow calls for one.

## Security/privacy

No evidence may contain file contents, credentials, tokens, private signing material, raw MFT/USN bytes, or unnecessarily identifying user paths. Use synthetic/deterministic fixtures and sanitized diagnostics.
