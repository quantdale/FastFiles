# Proposal: Certification Truthfulness Repair and Recertification

## Why

Independent repository review identified ten material certification-integrity and fail-open defects in FastFiles' external certification and durable stress evidence systems:

1. **CT-001** — long-run CTest could false-PASS when zero tests matched.
2. **CT-002** — resource soak recorded simulated activity as successful execution.
3. **CT-003** — soak PASS ignored resource instability.
4. **CT-004** — startup samples were incorrectly treated as steady-state baseline.
5. **CT-005** — retained stress evidence was not exact-tree-bound but was summarized too strongly.
6. **CT-006** — required application-lifecycle and rapid-input evidence was missing.
7. **CT-007** — external frontier commands were semantically mismatched: interactive tasks referenced a nonexistent UIA target and environment-provider work was confused with visual-provider certification.
8. **CT-008** — lifecycle completion lacked authoritative git-commit/hosted-run binding.
9. **CT-009** — prose/Markdown diverged from machine-readable JSON.
10. **CT-010** — self-referential two-pass hashing recorded hashes for pre-final bytes.

Owner scope directive (2026-09-04): vendor-specific VM-provider execution/testing is excluded from FastFiles local completion. Generic environment-provider lifecycle and fake-adapter contract tests may remain infrastructure, but no specific VM provider is a completion gate unless the owner explicitly reopens that scope.

## What Changes

* **Long-Run Discovery & Zero-Test Fail-Closed** — enumerate CTest targets before execution, require an exact intended match, prove each iteration executed, and support direct executables where appropriate.
* **UIA Long-Run Adapter & Real Inputs** — run real semantic FastFiles UI scenarios with readiness/focus assertions; retain application-lifecycle and rapid-input evidence.
* **Provider Taxonomy** — keep generic environment-provider infrastructure separate from real visual-provider certification. Vendor-specific VM execution/testing is owner-excluded.
* **Soak Engine Redesign** — separate launch/readiness/warm-up from measured steady state, remove simulated activity, gate resource stability, verify graceful shutdown/residue, and support explicit idle scope.
* **Artifact Integrity & Sanitization** — use external manifests/sidecars or canonical exclusion, sanitize host paths, and derive human summaries from authoritative JSON.
* **Authoritative Recertification** — commit tooling repairs, rebuild from a clean implementation SHA, rerun required stress/soak evidence with exact-tree enforcement, reconcile OpenSpec/lifecycle truth, and obtain hosted exact-SHA certification.

## Impact

* Tooling changes remain concentrated in certification/verification infrastructure unless a real product defect is proven.
* Authoritative evidence lives under this change's `evidence/` directory.
* Product code remains frozen by default.

## Non-Goals

* Vendor-specific VM-provider execution/testing unless explicitly re-enabled by the owner.
* Fabricating passes for unavailable external hardware/services.
* Weakening resource stability or trust policies.
* Opportunistic product refactoring without defect proof.
