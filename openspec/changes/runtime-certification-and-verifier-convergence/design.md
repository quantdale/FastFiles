## Context

See `proposal.md` for the motivation. The current mainline verifier has a useful but incomplete envelope in `verify/core/RunTree.psm1`: it identifies a capability and timestamps, but not an invocation UUID, scenario records, cleanup outcome, or a validated cardinality/precedence model. `verify/core/CapabilityRunner.psm1` calls a capability directly in the parent PowerShell pipeline, while `verify/verify.ps1` allows provider teardown and report-generation exceptions to escape from a `finally` block. `Get-VerbExitCode` then decides only from capability status objects that survived that path.

The current UIA capability has exact process/PID/start-time cleanup and semantic waits inside `ScenarioContext`, but its result is assembled locally from a mutable child list. Its shared and isolated paths have different cleanup/error handling, and a blocking UIA/COM operation can keep the supported wrapper alive without a terminal envelope. The historical release-readiness ledger records three post-fix full-wrapper failures with an opaque `scenario-failed` despite an exported 23 PASS / 6 SKIP / 0 FAIL result. This session reproduced the current boundary again: the full supported invocation launched the diagnostic UI and engine, ran beyond a bounded live-validation window without a terminal result, and was stopped only after verifying and terminating the exact run-owned PowerShell/UI/engine identities; a focused `UiaScenario=selection-model-interactions` wrapper then returned PASS with exit code 0.

No existing product process boundary, IPC command, authentication rule, parser rule, or privileged path is changed. The verifier remains a Windows PowerShell harness over the existing local provider and UIA driver.

## Goals / Non-Goals

**Goals:**

- Make one invocation's final result authoritative, schema-validated, identity-bound, and compositional.
- Preserve primary assertion failures, harness/schema failures, timeouts, cancellation, provider blocks, and cleanup failures as separate evidence with deterministic precedence.
- Put a bounded outer execution boundary around capabilities whose UIA/COM work can stall, while preserving exact owned-resource cleanup and first-failure evidence.
- Make UIA scenario selection, lifecycle ownership, order permutations, repeatability seeds, and external interactive boundaries machine-visible.
- Make reports, project status, release-readiness, task ledgers, and handoff evidence agree with current source and exact hosted SHA.

**Non-Goals:**

- Rewriting UIA provider semantics, product navigation, engine/index behavior, or the three-process architecture without a separately reproduced product defect.
- Making the current interactive desktop satisfy foreground, Explorer drag-source, elevation, physical display, guest, clean-host, or production-signing prerequisites.
- Adding whole-scenario retries, arbitrary sleeps, pixel/OCR fallback, unsigned production escape hatches, trust-store mutations, or process-name-wide termination.
- Retroactively changing prior campaign evidence or closing an administrative PR without authenticated repository write access.

## Decisions

### D1. One final envelope per invocation, with normalized child records

The core will normalize capability output into one envelope containing `runId`, `invocationId`, capability identity/version/tier, start/end/duration, status, classification, reason code, bounded summary, provider identity, artifacts, child scenario records, and a `lifecycle` block. Each child record will contain `scenarioId`, `runId`, `invocationId`, lifecycle mode, timestamps, status/classification/reason, bounded detail, evidence paths, and cleanup outcome. The existing PASS/FAIL/SKIPPED status vocabulary remains compatible; `classification` carries `BLOCKED_ENVIRONMENT`, `REQUIRED-BUT-UNAVAILABLE`, `ASSERTION_FAILURE`, `HARNESS_ERROR`, `TIMEOUT`, `CANCELLED`, and `CLEANUP_INCOMPLETE` without conflating a truthful skip with a pass.

Why: the existing envelope is the established report/archive seam, and extending it preserves capability extensibility while making the missing identity and lifecycle state explicit. A new parallel result format would recreate the drift this change is meant to remove.

Alternative rejected: infer scenario state from capability-specific payloads or console counters. That is the direct path to the observed 23/6/0-versus-`scenario-failed` contradiction.

### D2. Centralize result cardinality and aggregation

`Invoke-Capability` will capture the run entry point as a bounded, explicit result channel. It will reject zero or multiple authoritative outputs, normalize property names and arrays, validate the child record schema, and persist the envelope exactly once. A capability-specific module may provide internal diagnostic output only through its declared artifacts; internal pipeline leakage cannot become a second result.

Aggregation precedence will be deterministic:

1. Contract/schema/cardinality/run-identity violations are `FAIL/HARNESS_ERROR`.
2. An executed assertion or product check failure is `FAIL/ASSERTION_FAILURE`.
3. Timeout is `FAIL/TIMEOUT`; explicit cancellation is `FAIL/CANCELLED` unless the capability had not begun required execution.
4. Cleanup/provider/report failures are retained as distinct lifecycle failures; any incomplete cleanup prevents PASS. They do not erase a primary assertion or harness cause.
5. If no hard failure exists, an unavailable optional/external prerequisite yields `SKIPPED` with its stable blocked classification; otherwise the result is PASS.

The envelope will include the primary failure and an array of secondary failures so a cleanup failure after an assertion failure is not overwritten. The process exit code is derived from the normalized envelope (`0` PASS, `1` product/assertion/timeout/cancel/cleanup FAIL, `2` no required execution due to skip/block, `3` harness/contract/report failure); it is never derived from a raw child counter.

When a capability declares `FAIL` without a child, lifecycle, provider, or report failure that explains it, the normalizer creates a capability-level `HARNESS_ERROR` with the canonical reason `capability-failed-without-attributed-cause`. The raw declared reason is retained as `declaredReason` and in the bounded summary for diagnosis; an opaque declaration such as `scenario-failed` is never the authoritative reason by itself. An explicitly classified capability-level failure remains distinct and preserves its declared classification/reason.

### D3. Isolate the capability execution boundary

The parent verifier will run each selected capability through a small PowerShell worker process using a run-local JSON request and result path. The worker will resolve the registered capability, execute availability and run entry points, and write the candidate envelope under the invocation-owned artifact directory. The parent will own the worker process identity, enforce the manifest-declared timeout, validate the worker result and run identity, and classify missing/late/multiple output as a harness timeout or contract failure.

Why: UIA/COM calls can block the parent PowerShell process; a thread/job timeout cannot safely abort arbitrary COM work or guarantee child-process cleanup. A worker gives the parent a killable boundary without changing the product's process split.

The parent will snapshot exact repository-build process identities before worker launch and after worker termination. On timeout/cancellation it will terminate only newly adopted identities whose path and PID/start-time match the invocation ledger, then verify absence. The worker and UIA capability will use the invocation/run ID in the sandbox marker and environment so fixture cleanup remains scoped. No executable-name-wide kill is allowed.

Alternative rejected: a fixed sleep followed by a status guess, or a PowerShell thread job without a process boundary. Both can leave UI/engine/pipe residue and cannot prove a terminal result.

### D4. Treat lifecycle as a first-class result

Scenario invocation will go through a common lifecycle wrapper: create identity/ledger, capture baseline, execute, collect bounded evidence, run cleanup in `finally`, validate cleanup, then return exactly one normalized record. UIA's existing `ScenarioContext` remains the owner of its window, engine, pipe, fixture, Explorer, and provider resources; it will publish its cleanup record to the common wrapper. Parent timeout recovery will use the worker ledger and exact process snapshot as a recovery path, not as a replacement for scenario cleanup.

Cleanup status is independently persisted even when execution throws. A successful stop command is insufficient; process exit, pipe absence, fixture removal, Explorer-window disappearance, and UIA lifetime release must be observed or explicitly marked unverifiable. A cleanup exception is never allowed to replace the only result with an opaque PowerShell exception.

### D5. Bind artifacts to an invocation, never to "latest"

`New-VerificationRun` will generate a collision-resistant invocation identity in addition to the human-readable timestamp, write it before capability execution, and record the allowed artifact root. Reports and `Build-RunIndex` will consume only declared `result.json` files under that context. `Get-ExistingRunContext` may select a run only by explicit ID/timestamp and will reject ambiguity; no capability result is recovered from a sibling run. All declared paths are resolved beneath the run root and all result identities must match the manifest.

This keeps the existing ignored `verify/runs/` layout while closing the stale-previous-run and same-second collision classes. It also makes interrupted-run recovery explicit: a new run does not silently reuse an incomplete old result; a repair/report operation names the old run deliberately.

### D6. Keep UIA lifecycle modes and order evidence explicit

The UIA registry remains the source of stable scenario IDs and shared/isolated lifecycle modes. The capability will record selection expression, resolved IDs, seed, order, executable identity, provider identity, and per-scenario cleanup. Repeatability coverage will invoke the supported wrapper for focused scenarios, groups, complete capability, deterministic permutations, back-to-back runs, and interrupted-run recovery where the host permits. The first failing order/scenario is retained.

Existing semantic reacquisition, fragment-generation retirement, foreground guards, and exact process ownership remain intact. A foreground or Explorer limitation is converted only at the documented precondition boundary; downstream assertions are not attempted. Product defects found in this work will be fixed at their owning seam with focused regression tests; unverifiable environment behavior remains a truthful block/skip.

### D7. Make report/status composition validation executable

`Build-RunIndex`, report generation, and release/status generators will validate the envelope and child-state invariants before projecting counts. They will surface capability-level failures and cleanup/timeout classifications, and a fidelity check will compare report totals to the normalized current run. Project status will continue deriving current Git/CTest/OpenSpec/release evidence at invocation time; handoff documents will record evidence references and blockers, not duplicate mutable counts.

### D8. Preserve security and release boundaries

This change does not alter FastFiles' three-process privilege architecture, named-pipe ACLs, closed service command surface, symmetric Authenticode pinning, parser/frame hardening, or DLL search hardening. The worker runs with the same unprivileged token as the verifier and never forwards file content or gains service access. Production signing, trust-store, clean-host, elevated raw MFT/USN, guest, and physical-display evidence remain external and are represented as such.

## Risks / Trade-offs

- [Risk] A worker boundary adds serialization and process startup overhead. → Keep the request/result schema small, record the worker identity, and use it only around selected capabilities; focused CTest remains in-process.
- [Risk] A forced worker termination can leave a product child or fixture behind. → Persist invocation ownership, compare exact path/PID/start time, perform bounded recovery, and fail cleanup visibly if proof is incomplete.
- [Risk] Some existing capability modules emit incidental pipeline output. → Add cardinality tests and normalize only the declared result channel; malformed output fails closed.
- [Risk] Full UIA repeatability depends on the current desktop. → Record deterministic seeds and stable interactive/external reason codes; do not claim 20/20 or production coverage when the host cannot supply it.
- [Risk] Historical ledgers contain stale pass counts. → Preserve them as historical evidence and make the new generated status/release projection the current source of truth; no retroactive rewrite.
- [Risk] A report/schema change can affect downstream scripts. → Keep existing status values and exit codes compatible, add fields additively, validate fixtures, and update the schema/tests together.

## Migration Plan

1. Add the result/lifecycle schemas, normalizer, worker boundary, and focused PowerShell fixtures alongside the existing runner.
2. Adapt the central runner and UIA capability to emit the common record; keep old capability payloads opaque and preserve current external skips.
3. Regenerate reports/status only from the current run tree; add deterministic tests and run the focused wrapper matrix.
4. Run the full local validation ladder and a bounded repeatability session. Record any environment/provider limitation as evidence.
5. Commit and push on `main`, inspect hosted CI for the exact SHA, and update the new change evidence plus `WORKSTATE.md` with the final handoff. If the change must be rolled back, revert the implementation commits; prior run artifacts and historical campaign ledgers remain untouched.

## Open Questions

- The exact UIA scenario timeout budget should be selected from measured clean runs after the worker boundary is in place; it must be long enough for the supported full matrix but finite enough to diagnose a hang.
- Whether to expose worker-level timeout as a new CLI option or keep it manifest-defined can be decided after the first compatibility test; the envelope and exit semantics do not depend on that surface choice.
