# Evidence plan — allocated-size-search-filter

## Authorized baseline

- Baseline SHA: `3ad54ae01bb664623d16511bfb5e99761e431743`.
- `indexed-search-allocated-size` has already propagated allocated bytes + explicit knownness through candidate/view, engine, V2 response, Search row, Size-on-Disk sort, and UIA.
- Live closure evidence for that prior change is committed: focused `search-allocated-size` 10/10 PASS and full UIA 23 PASS / 4 legitimate SKIP / 0 FAIL, per its tasks/evidence.
- The baseline WORKSTATE top paragraph is stale: it still says final UIA/build acceptance is pending despite the committed final evidence. This change must reconcile that durable state without rewriting history.
- Hosted Actions status for the baseline final SHA was not retrievable in the executing agent's environment. Do not convert that limitation into a fabricated success/failure claim.

## Current code facts to verify on resume

1. `ffsearch::Candidate` and `CandidateView` already contain `allocatedSizeBytes` and `allocatedSizeKnown`.
2. `FilterRegistry::WithDefaults()` currently registers `size` but not `ondisk`.
3. Existing `ParseSizeFilter` implements the numeric grammar that should be shared.
4. Logical `size:` predicates use `sizeBytes`; this behavior must not change.
5. Indexed Search's view path already has the allocated fields, so `ondisk:` can be evaluated before path materialization.
6. Degraded/snapshot candidates already mark allocated size unknown; no additional probing is permitted.
7. The indexed-search request carries raw query text, so no wire-format change is expected.

## Required deterministic evidence

Record a candidate truth table with at least:

| Candidate | Logical | Allocated | Known | Expected `ondisk:` behavior |
|---|---:|---:|---|---|
| known-large | deliberately different | 8 MiB | true | numeric comparisons/ranges use 8 MiB |
| known-zero | nonzero logical allowed | 0 | true | exact/range zero semantics work |
| unknown-zero-storage | any | 0 | false | no numeric predicate matches |
| unknown-stray | any | nonzero sentinel fixture | false | no numeric predicate matches |

Prove:

- exact, comparison, and inclusive range syntax;
- overflow/malformed unit rejection;
- `ondisk` recognized vs malformed-value reporting;
- `size:` and `ondisk:` select different membership when the two dimensions differ;
- owning and view paths return identical membership;
- free-text/`ext:` implicit AND;
- scope and cancellation unchanged;
- no new path lookups merely from the allocated predicate.

## Engine/performance evidence

Use the existing Search benchmark/diagnostic path or a focused equivalent. For comparable metadata-only `size:` and `ondisk:` queries, capture candidate count, result count, path-needed/path-lookups, and timing. The acceptance criterion is architectural: no extra traversal, SQL/filesystem/service/IPC, or eager path reconstruction. Normal timing noise is not a regression.

No protocol width/ID/version change should occur. Existing byte-pinning tests must remain green.

## Live evidence

Use the actual executable selected by `windows-ui-automation-validation`; rebuild it before running.

On the normal degraded development path, create a deterministic fixture that is discoverable by a normal/free-text or logical `size:` query. Then:

1. prove the normal query discovers the fixture;
2. enter `ondisk:>=0B` and prove UNKNOWN rows are not fabricated into matches;
3. if the UI exposes query-feedback semantics, prove `ondisk` is recognized and not labeled unknown;
4. restore the normal query and prove the fixture returns under current-query generation rules;
5. preserve the established structured-filter and deep-result navigation scenarios;
6. run the focused scenario repeatedly (10/10 target if isolated) and then the full UIA capability with zero unexpected FAIL.

Do not inject known allocated values through a test-only production hook. Known/zero correctness belongs in deterministic headless/integration fixtures.

## Final validation

Record:

- focused query/search/engine/UI tests;
- Debug CTest all PASS;
- release-test CTest all PASS;
- analyze `/W4 /WX` warning-clean;
- `verify.ps1 build` + `doctor`;
- source-marker, diagnostic-policy, doc-drift checks;
- strict OpenSpec validation;
- `git diff --check`;
- final `HEAD == origin/main`, tracked tree clean;
- exact hosted CI observation for the final SHA if retrievable, otherwise the exact retrieval limitation without inventing a result.

Do not stage `verify/runs`, build outputs, DBs, logs, screenshots, or fixture data.