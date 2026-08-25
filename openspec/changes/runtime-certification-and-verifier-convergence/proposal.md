## Why

FastFiles' supported Windows UIA wrapper can disagree with the capability evidence it is meant to report: the current campaign records an exported result equivalent to 23 PASS, 6 truthful SKIP, and 0 FAIL while repeated `verify.ps1 run -Capability windows-ui-automation-validation` invocations have returned an opaque `scenario-failed`; the latest full invocation also stalled beyond a bounded live-validation window without a terminal result. The verifier therefore cannot yet provide release-truth semantics for PASS, SKIP, FAIL, timeout, cleanup, or external blocking states.

This change establishes one authoritative, identity-bound result contract and lifecycle for capability runs, then applies it to UIA repeatability and release-status composition. Historical evidence remains immutable; new evidence will distinguish the reproduced wrapper mismatch, the current hang/timeout boundary, and any remaining interactive or production prerequisites.

## What Changes

- Add a central result-composition contract for capability, scenario, provider, cleanup, timeout, cancellation, and report outcomes.
- Give every run and executed scenario stable invocation identity, timestamps, reason codes, bounded diagnostics, evidence paths, provider identity, and explicit cleanup outcome.
- Make top-level status and process exit code deterministic projections of validated child results, with explicit error precedence; a zero-failure child set cannot become an unexplained failure, and a lifecycle/schema/harness failure cannot be hidden by a zero counter.
- Bound capability execution and preserve timeout/cancellation/harness exceptions as attributable results while cleaning only exact owned processes, pipes, fixtures, Explorer windows, and UIA lifetimes.
- Reject stale, ambiguous, malformed, missing, duplicate, or cross-run artifacts instead of discovering the latest unrelated result.
- Add verifier-level regression coverage for result composition, pipeline cardinality, malformed output, stale artifacts, cleanup precedence, timeout/cancellation, provider preflight, and interrupted-run recovery.
- Add deterministic seeded UIA wrapper repeatability and order/partition coverage; retain truthful `interactive-foreground-unavailable`, Explorer-source, privilege, signing, clean-host, guest, and physical-display boundaries.
- Reconcile project status, release-readiness, OpenSpec task ledgers, and campaign evidence so locally implemented, locally validated, hosted-CI validated, environment-blocked, and externally unavailable states are mechanically distinct.

## Capabilities

### New Capabilities

- `runtime-verification-certification`: authoritative result envelopes, deterministic aggregation, run/scenario identity, lifecycle ownership, cleanup/error precedence, bounded execution, stale-artifact rejection, repeatability, and release-truth evidence for the PowerShell verification harness.

### Modified Capabilities

- None. Existing product and UIA capabilities retain their product/security requirements; this change hardens the verifier contract that composes their results.

## Impact

- Affected verifier code: `verify/verify.ps1`, `verify/core/CapabilityRunner.psm1`, `verify/core/RunTree.psm1`, `verify/core/Reporting.psm1`, schemas, provider lifecycle handling, and the UIA capability/context cleanup boundary.
- Affected tests: PowerShell verifier/provider/status/UIA-driver suites and any CTest registration needed for deterministic core coverage.
- Affected evidence: a new OpenSpec evidence ledger, bounded live/repeatability matrix, generated release-status projections, and final hosted-CI handoff.
- No changes to the three-process privilege split, named-pipe security, Authenticode pins, privileged service command surface, parser validation, trust stores, or production-signing claims.
