## Why

FastFiles has reached a mature implementation state, but exact current-tree review found a small set of correctness and assurance gaps that prevent a truthful full-completion claim even though hosted CI is green.

The active rule-pruning durability campaign still contains a false-success rebuild path and false-green/missing regression evidence. Separately, hosted release qualification validates an unsigned candidate but fails to retain the intended manifest/provenance artifacts, and current sanitizer/evidence claims have drifted from the exact hosted coverage.

This change is the queued **completion convergence** campaign after the currently active rule-pruning change is truthfully closed. It is not a broad feature rewrite. It exists to take the repaired product through systematic hardening, regression hunting, release evidence closure, and final exact-SHA certification.

Audit authority: `docs/agent/DEEP_AUDIT_2026-08-28_FASTFILES.md`.

## What Changes

- Require truthful closure of `rule-pruning-durability-and-reconciliation-failure-closure` as a dependency before broad completion certification.
- Repair repository truth surfaces so OpenSpec tasks cannot be treated as complete when requirements were deleted, assertions were relaxed, or evidence was still pending.
- Make hosted unsigned release qualification retain the candidate manifest/provenance evidence it already validates.
- Reconcile the authoritative sanitizer slice with the existing IPC listener lifecycle test and eliminate stale 3/3 vs hosted 2/2 evidence.
- Run a fresh current-tree correctness, data-integrity, security/privilege/IPC, concurrency/lifetime, resource/performance, UI/accessibility, verification, release, and documentation audit after the P1 fixes.
- Open additional bounded implementation OpenSpecs only for newly reproduced material defects or missing intended core functionality.
- Perform final exact-tree and hosted-CI certification, preserving all external-only production prerequisites as external.

## Capabilities

### New Capabilities

- `engineering/completion-integrity`: evidence-driven project-completion gates, exact-SHA assurance, and release-evidence retention.

### Modified Capabilities

- No user-facing protocol or persisted schema is intentionally changed by this umbrella campaign.
- Any product behavior change discovered during execution MUST be implemented through its appropriate bounded capability/OpenSpec rather than hidden inside this orchestration change.

## Impact

- **Correctness:** prevents false completion around durable pruning and future regressions found by the fresh audit.
- **Assurance:** task/evidence state must match executed behavior rather than inferred coverage.
- **CI/CD:** hosted release and sanitizer evidence becomes reproducible and retained.
- **Release:** local completion and external production certification are separated explicitly.
- **Maintainability:** stale historical audits remain non-authoritative; only current-tree evidence drives new work.
- **Product scope:** no speculative feature work and no architecture rewrite.

## Dependencies

1. The currently active `rule-pruning-durability-and-reconciliation-failure-closure` change MUST be repaired and truthfully completed first.
2. Its final implementation SHA MUST pass focused tests, full local-equivalent validation, exact-tree qualification, and hosted CI.
3. Only then does this umbrella campaign move from queued coordination into hardening/certification execution.

## Non-Goals

- Replacing the three-process security architecture.
- Rewriting search/storage/UI without a reproduced defect.
- Editing vendored SQLite absent a dependency-specific defect.
- Manufacturing signer, clean-host, elevated-provider, or physical-display evidence.
- Consuming twelve hours for its own sake.
