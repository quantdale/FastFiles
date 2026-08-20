# Proposal — allocated-size-search-filter

## Summary

Add a first-class `ondisk:` structured Search filter that evaluates the Size on Disk / allocated-byte metadata already carried by indexed Search, without changing the established meaning of `size:`.

`indexed-search-allocated-size` deliberately stopped at propagation, presentation, UIA, and sorting. FastFiles can now display and sort by allocated size, but the query language still cannot express queries such as `ondisk:>100MB` or `ondisk:0B`.

## Goals

- Register `ondisk` as a structured numeric filter in the existing extensible Search filter registry.
- Reuse the existing binary-size grammar and operators: `=`, `>`, `>=`, `<`, `<=`, and inclusive `..` ranges with `B`, `KB`, `MB`, and `GB` units.
- Evaluate `ondisk:` only when `allocatedSizeKnown=true`.
- Preserve known zero as a real value; `ondisk:0B` must match known-zero candidates.
- Exclude UNKNOWN allocated-size candidates from every numeric `ondisk:` predicate; UNKNOWN must never behave like zero, infinity, or logical size.
- Keep `size:` exactly logical-size semantics and preserve all existing query membership.
- Execute against the existing `CandidateView`/projection metadata with no database query, filesystem probe, service call, or additional IPC per candidate.
- Preserve query cancellation, scope behavior, acceleration/lazy path reconstruction, result ordering, and Search UI virtualization.
- Prove the new filter through deterministic headless tests and a focused live Search regression on the normal degraded development path.

## Non-goals

- No `allocated:` alias in this change; `ondisk:` is the single canonical key.
- No `ondisk:known` / `ondisk:unknown` keywords in this change; this slice is numeric comparison/range filtering only.
- No change to `size:` semantics.
- No protocol message or structure change: the query text already crosses the existing indexed-search request.
- No new allocated-size collection path and no per-result filesystem probing.
- No Search UI redesign.
- No production signing, trust-store mutation, service-install campaign, real MFT/USN production validation, clean-host matrix, or other parked privileged production work.

## Success criteria

The change is complete when `ondisk:` is recognized by the registry and evaluated identically in owning and projection-view query paths; known nonzero/zero/range/comparison cases are correct; UNKNOWN candidates match no numeric `ondisk:` predicate; `size:` remains logical and unchanged; combined AND/scope/cancellation behavior remains correct; no new I/O or eager path materialization is introduced; focused live Search validation shows the new key is accepted without breaking the current degraded/UIA workflow; all native/build/spec/policy gates pass; and the final validated state is pushed to `main` with repository truth reconciled.