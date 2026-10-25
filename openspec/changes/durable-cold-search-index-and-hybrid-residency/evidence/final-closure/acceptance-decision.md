# Owner decision: reconcile task 4.1 without weakening result correctness

**PROPOSED ONLY — governing spec unchanged; task 4.1 remains unchecked.**

## Existing contract and observed production reachability

`specs/index-engine/spec.md`, “Cold-with-index results shall equal hot results”,
requires the same corpus/query set hot and cold at 200k/1M/2M/5M. The same spec
forbids raising the 500 MiB default projection budget. The low watermark is
400 MiB. The final 2M pipeline fixture reopens entirely cold (projection estimate
128 B), and 5M is likewise not a wholly resident production corpus under that
budget. Calling two cold executions “hot/cold” would be vacuous.

- **Production reachable:** hot resident volumes/subtrees; cold oversized volumes;
  mixed-volume searches; transition/reload/fallback, generation changes and
  cancellation. Total corpus size does not imply that every volume is resident.
- **Test-only reachable:** wholly resident 2M/5M projections using an explicit
  larger test budget, if measured machine capacity suffices. This is not a new
  production setting/default, and was not executed in this campaign.
- **Not a supported requirement as worded under the unchanged default:** keeping
  the demonstrated oversized 5M corpus wholly resident. This is a residency
  conflict, not evidence of incorrect cold results or physical impossibility on
  every conceivable machine.
- **Result caps:** production has a bounded response cap. “Uncapped” output beyond
  the API's hard cap is not a production result mode; cap boundaries still require
  exact identities/order/status/truncation, not just matching counts.

## Proposed replacement scenario (requires explicit owner approval)

> For the same deterministic corpus/query matrix at 200k, 1M, 2M and 5M, every
> production-reachable route shall match an independent full canonical scan in
> ordered result identities, volume/file/parent identity, name/folder/metadata,
> cap/truncation, query status and cancellation semantics. The canonical oracle
> shall bypass all candidate filtering. Hot/cold differentials shall additionally
> exercise resident and mixed-residency volumes under the unchanged production
> budget, including route transitions and generation invalidation. Fully resident
> oversized-corpus experiments may supplement this evidence under separately
> labeled test-only budgets; they shall not substitute for default-budget behavior.

This retains exact correctness at **all four scales**, replaces an unreachable
residency premise with an independent oracle, and does not relax Unicode,
ordering, metadata, cancellation, failure, or memory-policy requirements.

## Decision options

1. **Approve the explicit clarification.** Then apply it through OpenSpec and
   assess any remaining resident/mixed query cells against actual evidence.
   Approval alone does not mark unexecuted cells PASS.
2. **Retain the literal contract.** Authorize a bounded test-only high-budget
   2M/5M differential plan with memory preflight; keep task 4.1 open until executed.
3. **Defer integration.** Preserve the active change and current evidence.

Recommendation: option 1. Do not waive equality, silently change the spec, or
raise the production budget. Final 5M extended cold/canonical **23/23** and
2M multi-volume Store boundary **18/18** are evidence for their stated seams,
not a claim of full hot 5M or full engine multi-volume differential coverage.
