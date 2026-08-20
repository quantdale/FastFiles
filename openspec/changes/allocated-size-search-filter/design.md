# Design — allocated-size-search-filter

## Context

The completed allocated-size Search path already carries two distinct dimensions on every candidate:

- `sizeBytes`: logical size;
- `allocatedSizeBytes` + `allocatedSizeKnown`: Size on Disk with explicit knownness.

The query registry currently exposes `size:` only. This change adds a query predicate over the second dimension without creating another source of truth.

## D1 — `ondisk` is the single canonical filter key

Register `ondisk` in `FilterRegistry::WithDefaults()` using the same registry mechanism as `size`, `ext`, `modified`, `kind`, `name`, and `folder`.

Do not special-case the tokenizer. Do not add an `allocated` alias in this slice. An unrecognized spelling remains governed by the existing unrecognized-key contract.

## D2 — Reuse the existing binary-size value grammar

`ondisk:` SHALL accept the same numeric syntax as `size:`:

- exact: `ondisk:10MB` / `ondisk:=10MB`;
- comparisons: `>`, `>=`, `<`, `<=`;
- inclusive range: `ondisk:10MB..100MB`;
- units: bytes by default or `B`, `KB`, `MB`, `GB`, using powers of 1024.

Reuse/refactor the existing `SizeFilter` parser rather than maintaining a second subtly different parser.

Malformed values SHALL enter the existing invalid-filter reporting path, not silently become a literal filename term.

## D3 — Explicit knownness gates every numeric predicate

A numeric `ondisk:` predicate first requires `allocatedSizeKnown == true`. Only then does it compare `allocatedSizeBytes`.

Consequences:

- known zero is numeric zero and may match `ondisk:0B`, `ondisk:<=0B`, and ranges containing zero;
- UNKNOWN matches no numeric `ondisk:` predicate in either direction;
- UNKNOWN is never coerced to logical `sizeBytes`, zero, or `UINT64_MAX`;
- a candidate with stray `allocatedSizeBytes` but `allocatedSizeKnown=false` is still UNKNOWN and does not match.

This is the same truth model already used by Search display, UIA, protocol V2, and Size-on-Disk sorting.

## D4 — Owning and projection-view predicates must be equivalent

Add the allocated-size predicate to both `Candidate` and `CandidateView` evaluation so the accelerated projection path and correctness-preserving owning fallback return identical membership.

Add a distinct predicate-kind/diagnostic attribution for allocated-size metadata if the current diagnostics enum requires it; do not misattribute `ondisk:` work as logical `size:` if that would make benchmark/debug evidence ambiguous.

The view predicate uses only the two scalar allocated fields already present in `CandidateView`; it must not force folder/path materialization.

## D5 — Existing `size:` behavior is frozen

`size:` continues to read only `sizeBytes`. Existing request text, parser behavior, tests, result membership, and logical `Size` sorting remain unchanged.

A fixture where logical and allocated sizes deliberately differ SHALL prove the two filters select different rows.

## D6 — Normal AND composition and scope semantics apply

`ondisk:` participates in the existing implicit-AND query model. Examples:

- `ext:iso ondisk:>1GB` requires both predicates;
- `report ondisk:10MB..100MB` requires the name term and the allocated range;
- scope filtering remains independent and unchanged.

No new ordering/ranking rule is introduced. Filtering determines membership; the selected sort mode still determines ordering.

## D7 — Degraded Search stays honest and useful

Degraded/snapshot candidates already set `allocatedSizeKnown=false`. Therefore a numeric `ondisk:` query may legitimately produce zero degraded matches.

Do not probe files with `GetCompressedFileSize`, allocation ranges, file handles, cluster APIs, or any per-result filesystem call to make such queries return data.

The Search UI must treat `ondisk` as a recognized filter key, not report it as unrecognized. The ordinary no-results state is acceptable when every candidate is allocated-unknown; tests/evidence must make clear that this means "cannot prove the numeric predicate", not "known zero".

## D8 — No wire-format change

The existing indexed-search request already carries the raw query string. The engine parses it with the shared search library. `ondisk:` therefore requires no new request field, message ID, struct version, response layout, or compatibility branch.

Protocol byte-pinning tests must remain unchanged except for any test fixture query strings intentionally added outside the pinned legacy cases.

## D9 — Preserve acceleration, cancellation, and memory bounds

The new predicate is metadata-only and SHALL be evaluable before path reconstruction. It must not:

- add a second projection traversal;
- make path reconstruction eager;
- add SQL/filesystem/service/IPC work per candidate;
- weaken chunked cancellation;
- grow result/page caps;
- create per-result UI controls or accessibility state.

Benchmark or diagnostic evidence should show identical candidate/path-materialization behavior for comparable `size:` and `ondisk:` metadata filters, modulo normal timing noise.

## D10 — Live validation proves integration, not privileged known values

Known/zero/UNKNOWN numerical correctness is proven deterministically in search-library and projection/index integration tests.

Live Windows Search validation runs on the ordinary development/degraded path. Because degraded candidates are allocated-unknown, a focused live scenario SHALL demonstrate that:

1. a deterministic fixture is discoverable with a normal free-text or `size:` query;
2. `ondisk:>=0B` is accepted as a recognized structured filter and does not fabricate matches from UNKNOWN values;
3. returning to the normal query restores the fixture results;
4. Search result discovery, structured-filter behavior, cancellation/query replacement, and deep-result navigation remain healthy.

If the current UI exposes an invalid/unrecognized-filter indication, assert `ondisk` does not trigger it. Do not create a test-only production hook merely to inject known allocated values.

## D11 — Repository truth reconciliation is part of closure

The baseline `docs/agent/WORKSTATE.md` top paragraph at `3ad54ae` still says final UIA/build acceptance is pending even though the final live evidence was committed and pushed. The executor SHALL reconcile that stale durable state while implementing this change; historical evidence remains unchanged.

Do not rewrite historical commits. Do not falsely claim a hosted Actions conclusion if the exact final-SHA result cannot be retrieved; report the exact observed state.

## D12 — Privileged production campaign remains parked

This change consumes metadata already present in the unprivileged Search model. It does not expand `FastFilesIndexSvc`, signing, trust, service installation, raw MFT/USN validation, or clean-host requirements.