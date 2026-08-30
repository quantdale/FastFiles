# Current manifest audit — refreshed 2026-08-30

The lifecycle inventory is reconciled against the current task ledgers and
evidence. Fully checked changes do not need an open-task entry; every change
with an unchecked task is represented in `openspec/lifecycle.json`.

| Entry | State | Current disposition |
| --- | --- | --- |
| `autonomous-runtime-verification` | external-evidence-only | Provider/guest tasks 2.3 and 2.4 are explicit external overrides. |
| `close-independent-validation-gaps` | external-evidence-only | Task 4.2 has local negative-test coverage plus an external signing remainder; 4.2.2, 4.5–4.8, and 5.1 are explicit external overrides. |
| `interactive-ui-runtime-determinism` | completed | Task 7.2 is complete; the recorded 20/20 isolated selection run is current completion evidence, not an external blocker. |
| `nonblocking-indexed-read-plane` | completed | Historical delivery evidence is complete. |
| `phase8-release-readiness-convergence` | external-evidence-only | Tasks 5.1–5.3 are explicit provider/signing/clean-host/interactive overrides. |
| `real-user-dogfooding-release-engineering` | completed | Historical delivery evidence is complete. |
| `runtime-certification-and-verifier-convergence` | external-evidence-only | Tasks 1.3, 5.3, 7.2, 8.2, and 8.3 are explicit external overrides; checked task 6.3 has no stale override. |
| `ship-ready-ui-ux-hardening-convergence` | external-evidence-only | Task 6.2 remains the sole external prerequisite; checked tasks 4.3 and 6.1 have no stale overrides. |
| `zero-touch-autonomous-engineering` | external-evidence-only | Tasks 6.3, 6.4, and 6.6 are explicit provider-gated overrides. |
| `openspec-frontier-lifecycle-truthfulness` | completed | Lifecycle/status delivery is complete. |
| `stabilize-live-uia-e2e` | superseded | Superseded by `interactive-ui-runtime-determinism`. |
| `openspec-lifecycle-evidence-integrity-and-semantic-convergence` | completed | Typed evidence and semantic lifecycle validation are complete. |
| `repository-truth-surface-and-doc-drift-convergence` | completed | Truth-surface and drift delivery is complete. |
| `committed-tree-validation-provenance-and-clean-checkout-parity` | completed | Exact-tree/provenance delivery is complete. |
| `indexed-search-zero-copy-hot-path-and-lock-pressure-convergence` | completed | Indexed-search delivery is complete. |
| `rule-pruning-durability-and-reconciliation-failure-closure` | completed | Durability/reconciliation delivery is complete. |
| `final-product-completion-and-production-readiness` | completed | Local product completion evidence is complete; production prerequisites remain represented by the external-only changes. |
| `completion-integrity-hardening-and-release-certification` | external-evidence-only | Tasks 4.5.1–4.5.3 have explicit mixed local/external dispositions; 6.15 remains explicit interactive external evidence. |

## Open-task coverage

The status collector recognizes dotted numeric task identities at any depth,
including the previously invisible `close-independent-validation-gaps/4.2.2`
and `completion-integrity-hardening-and-release-certification/4.5.1–4.5.3`.
The current raw inventory is 25 unchecked tasks across seven changes. All 25
have a task-level lifecycle disposition and authoritative evidence:

- `autonomous-runtime-verification`: 2.3, 2.4
- `close-independent-validation-gaps`: 4.2, 4.2.2, 4.5, 4.6, 4.7, 4.8, 5.1
- `completion-integrity-hardening-and-release-certification`: 4.5.1, 4.5.2, 4.5.3, 6.15
- `phase8-release-readiness-convergence`: 5.1, 5.2, 5.3
- `runtime-certification-and-verifier-convergence`: 1.3, 5.3, 7.2, 8.2, 8.3
- `ship-ready-ui-ux-hardening-convergence`: 6.2
- `zero-touch-autonomous-engineering`: 6.3, 6.4, 6.6

The lifecycle model has no `active` entries and the frontier has zero
active-local tasks. The three completion-integrity UI/UX tasks remain visible
as `mixed` rather than being silently hidden or checked: their local component
coverage is recorded, and their continuous foreground/display closure remains
an explicit interactive environment boundary.
