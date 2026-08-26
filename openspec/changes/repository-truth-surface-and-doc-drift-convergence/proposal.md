# Proposal: repository-truth-surface-and-doc-drift-convergence

## Why

Current-facing guidance (README/AGENTS/CLAUDE/CODE_INDEX/AUTONOMOUS/verify docs)
is an execution input for planners, executors, and maintainers, but its only
automated guard (`ffdoc_drift_tests`) checks four exact legacy strings plus
mixed line endings. That gate was green while the guidance carried materially
stale current-state claims:

1. README documented the verifier verb set without `status`,
   `release-readiness`, and `release-candidate` (all shipped in
   `verify/verify.ps1`).
2. README presented `interactive-ui-runtime-determinism` as the *current*
   interactive hardening change; CLAUDE.md presented
   `close-independent-validation-gaps` as *current open work* — both superseded
   by later completed campaigns recorded in `openspec/lifecycle.json`.
3. AGENTS.md and CODE_INDEX.md described snapshot publication as a
   "double-buffered" section with an `activeSlot` flip; the implementation has
   not used that mechanic since the immutable per-generation publication model
   landed (generation-named sections committed before handoff).

A stale truth surface can misdirect autonomous work selection even while every
existing gate passes. This change makes machine-derived claims in current-facing
guidance mechanically checkable against their executable sources, and makes
contradictions fail locally and in CI.

## What changes

- Introduce `tests/drift/DocTruth.psm1`: a deterministic, offline contract
  engine that derives expected facts from authoritative seams —
  `verify/verify.ps1` verb/exit-code definitions, `CMakePresets.json`,
  first-party CMake target declarations, `openspec/lifecycle.json`, and the
  snapshot publisher source seam — and validates marker-delimited regions and
  whole-file anti-staleness rules in declared current-facing surfaces.
- Extend `tests/drift/check_doc_drift.ps1` to run those contracts alongside the
  retained banned-phrase and line-ending checks (single entry point,
  `ffdoc_drift_tests`).
- Add adversarial fixture coverage (`ffdoc_truth_fixture_tests`) proving both
  false-negative resistance (missing/extra verbs, stale current-change claim,
  stale snapshot mechanics, malformed authority input) and false-positive
  discipline (historical evidence containing old wording must pass; dynamic
  status counts are never hard-coded).
- Reconcile the drifted guidance surfaces to current truth, preferring stable
  pointers (`openspec/lifecycle.json`, `pwsh ./verify/verify.ps1 status`) over
  copied mutable facts.
- Keep CI executing the drift gate through the existing CTest preset matrices;
  no network dependency is added.

## Capabilities

### New: `repository-truth-drift-guards`

Deterministic semantic/executable consistency contracts over current-facing
guidance, with fail-closed behavior on malformed fencing or unreadable
authority inputs, adversarial fixture proof, and faithful failure propagation
through CTest and hosted CI.

## Out of scope

- No product/runtime/security behavior changes; no UI, index, search, protocol,
  or installer changes beyond a comment-accuracy fix in `UiProtocol.h`.
- No rewriting of historical campaign evidence, OpenSpec archives, audit logs,
  or `.agent` execution history.
- No new hand-maintained truth manifest; no network access during validation.
- No change to lifecycle schema-v2 semantics, status/release-readiness
  projection, production signing gates, or external prerequisite boundaries.
