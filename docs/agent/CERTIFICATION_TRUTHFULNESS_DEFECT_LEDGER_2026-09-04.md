# FastFiles — Certification Truthfulness Defect Ledger

- Date: 2026-09-04
- Status: historical repair ledger

This ledger summarizes the ten certification-integrity defects identified during the certification truthfulness campaign. Detailed historical diffs and evidence remain available in the corresponding OpenSpec change directories and Git history. Current scope is governed by `docs/agent/WORKSTATE.md`.

Owner scope directive: vendor-specific VM-provider execution/testing is out of scope for FastFiles local completion unless the owner explicitly reopens it. Generic provider-contract and fake-adapter infrastructure may remain, but provider-specific guest execution is not a completion gate.

## Defect summary

| ID | Defect | Resolution contract |
| --- | --- | --- |
| CT-001 | Zero-test CTest match could produce false PASS | Pre-discover exact target, reject zero/ambiguous matches, require execution proof. |
| CT-002 | Simulated UI activity was recorded as successful execution | No synthetic activity may claim PASS; real UIA activity or explicit idle scope only. |
| CT-003 | Resource-soak disposition ignored resource growth | Resource stability is a first-class disposition leg. |
| CT-004 | Startup sample was treated as steady-state baseline | Explicit readiness, warm-up, steady-state baseline, measured soak, shutdown, residue phases. |
| CT-005 | Retained stress evidence was not exact-tree-bound | Authoritative evidence must run on a committed clean tree with exact provenance. |
| CT-006 | Required application-lifecycle and rapid-input artifacts were missing | Execute and retain the required suites or formally amend the requirement. |
| CT-007 | External-frontier commands were semantically mismatched to task requirements | Map UIA, environment, vision, signing, NTFS and clean-host tasks to distinct truthful runners/capabilities. Vendor-specific VM-provider execution is now owner-excluded from local completion. |
| CT-008 | Completed OpenSpec lifecycle lacked authoritative hosted binding | Completed changes require exact git-commit and hosted-run evidence. |
| CT-009 | Human soak summary disagreed with machine-readable JSON | JSON is authoritative; summaries should be generated/validated from it. |
| CT-010 | Naive self-hashing produced a checksum for pre-final file bytes | Use external manifests/sidecars or canonical self-exclusion and independently verify hashes. |

## Key repaired contracts

### CT-001 — test execution proof

`Invoke-LongRunCertification.ps1` must enumerate CTest targets before execution. Zero matches fail as an invalid target; ambiguous matches fail; direct executables are preferred where appropriate; every iteration must contain evidence that the intended test actually ran.

### CT-002 — real vs idle activity

A soak may be a valid idle soak, but it must say so. Any claimed functional activity must be genuinely invoked and semantically observed. No placeholder or simulated action may emit `success=true`.

### CT-003 / CT-004 — steady-state resource stability

The soak lifecycle separates launch/readiness/warm-up from measured steady state. PASS composes process survival, functional activity when requested, resource stability, graceful shutdown, and residue cleanup. Resource measurements are not merely informational.

### CT-005 — provenance

Development evidence with a dirty tree remains development evidence. Authoritative campaign closure evidence must bind to the exact committed implementation source/tree and executable hashes.

### CT-006 — required stress evidence

Task completion must match the task text. Application lifecycle and rapid-input requirements require their own retained evidence rather than substitution by unrelated focus/concurrency runs.

### CT-007 — semantic lane separation

The original frontier collapsed unlike tasks into generic command templates. The corrected architecture separates interactive UIA, environment-provider infrastructure, vision-provider review, production signing, privileged NTFS, and clean-host certification. A command is execution-ready only if its advertised capabilities satisfy the task's actual acceptance requirements.

Vendor-specific VM-provider testing is intentionally excluded from the current local completion scope. Do not reintroduce provider-specific guest-image, credential, channel, or end-to-end VM execution as a FastFiles completion requirement without explicit owner direction.

### CT-008 — lifecycle binding

OpenSpec `completed` is an evidence-bearing state, not an administrative checkbox. The lifecycle record must bind the implementation/final commit and the exact hosted run where required by repository policy.

### CT-009 — JSON/prose consistency

Machine-readable artifacts are authoritative. Markdown must not manually restate dynamic counts/metrics in a way that can drift undetected.

### CT-010 — artifact integrity

A final file cannot truthfully contain an ordinary SHA-256 of its own final bytes without a defined self-exclusion rule. External manifests/sidecars are the preferred durable integrity mechanism and must be independently recalculated during validation.

## Current authority

This file is historical evidence. For current remaining local work and owner scope, use:

- `docs/agent/WORKSTATE.md`
- `docs/agent/FINAL_LOCAL_CERTIFICATION_CLOSURE_LEDGER_2026-09-04.md`
- `openspec/changes/external-certification-execution-engine-and-frontier-truthfulness/`

If historical wording conflicts with current lifecycle/project-status truth, current generated status and the owner scope directive win.
