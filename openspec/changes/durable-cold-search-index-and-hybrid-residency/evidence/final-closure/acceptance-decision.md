# Applied acceptance clarification: exact equality under production residency

**APPLIED under the owner's 2026-10-25 original-vision recovery directive,
section 9B. Task 4.1 was subsequently completed locally through the actual
2026-10-26 executions described in the final disposition below and
`mixed-scale-closure-20261026.md`.** The earlier open-task inventory is a
historical checkpoint; it does not supersede those results or their limits.

## Existing contract and observed production reachability

The prior `specs/index-engine/spec.md` contract, “Cold-with-index results shall
equal hot results”, required the same corpus/query set wholly hot and cold at
200k/1M/2M/5M. The same spec
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

## Adopted contract

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

The governing delta, proposal and design now carry this contract. The default
500 MiB/400 MiB policy and canonical matcher are unchanged. A changed sentence
does not certify unexecuted routes or scales.

## Historical evidence inventory and then-remaining task 4.1 cells

| Acceptance dimension | Retained evidence | Current disposition |
|---|---|---|
| 200k full canonical differential | `artifacts/canonical-20261025/oracle-200k-extended.json`:23/23 at clean6e060fbb; search/index/protocol/oracle source exactly matches db7887c1 | Verified cold full canonical matrix; supersedes earlier-source200k coverage without relabeling historical runs. |
| 1M full canonical differential | `artifacts/canonical-20261025/oracle-1m-extended.json`:23/23 at the same exact search-source identity; full1M canonical scan per completed cell | Verified cold route with actual admission/caps/metadata/order/status/cancellation premises; no whole-hot1M claim. |
| 2M full canonical differential | `recovery-20261024/final-current/current-2m-oracle-{1,2,3}.json`: 13/13 each, `269d1c3` | Valid earlier-source execution. `git diff 269d1c3..db7887c1 -- src tests/engine/bench_identity_ordering_oracle.cpp` changes only `Projection.cpp`; explicit scope/equivalence determination remains. The final two pipeline benchmark runs are timing evidence, not replacement canonical oracles. |
| 5M full canonical differential | Final `db7887c1` extended oracle: 23/23, full 5M canonical scan, actual candidate admission, metadata/order/caps/status/immediate cancellation; archived final-oracle binding | Verified for the stated cold engine seam; no wholly-hot 5M assertion. |
| Actual resident and mixed engine routes | `TestForcedEvictionSearchEquivalence` asserts resident accelerator use, then one cold and one hot volume; exact ordered metadata/status/truncation comparisons. `TestCappedRootSearchMatchesColdAndRetainsTopN` exercises unload/rebuild cycles | Existing small-fixture regression execution retained in prior current-source CTest evidence. This is not independent-canonical mixed coverage at every scale. |
| Automatic transitions, generation invalidation, cancellation | `TestAutomaticEvictionPreservesMixedVolumeSearchAndSnapshots`, `TestProjectionEvictionInvalidatesCapturedCursorSafely` and related engine tests | Existing regressions cover their named seams. Inventory route assertions and independent-canonical comparison before closing the new scenario; do not infer coverage from test names alone. |
| Large mixed-volume/cap boundary | Final production Store 2M operational matrix: 18/18, three cycles, admission boundaries 16,383/16,384/16,385 | Verified Store seam, not a full engine mixed-residency canonical differential. |

No expensive completed 5M/pipeline/Store matrix was repeated for this planning
clarification. The bounded1M and200k follow-up is now recorded in
`canonical-scale-followup-20261025.md`. The next acceptance slice is explicit
independent-canonical resident/mixed/transition route coverage and a scope
determination for retained2M oracle inputs. Task4.1 stays open until those
cells are resolved; the accepted contract is not a correctness waiver.

## 2026-10-26 scope correction from independent review

Historical 23-cell matrices used numeric sort fields2/3/4 under labels Size/Modified/Created, but those values mean Path/Size/Modified. Ordinary fixture sizes/timestamps tied. Those reports prove their actual numeric routes and tie-break behavior; they do not prove Created or nondegenerate primary sorting. Historical raw files remain unchanged. The earlier2M13 cells cover only default queries/cancellation, not the extended10. Independent source review bounds equivalence269d1c3..db7887c1 over the subtree-only correction; the new root-child path fix is a production change and prevents exact-source inheritance to current HEAD.

New corrected fresh varied20k/200k residency matrices each93/93 at f22f401 verify intended Size/Modified/Created both directions, all-field canonical equality and actual resident/mixed/cold/reloaded/generation routes at the unchanged default budget. See residency-routes-20261026.md. Larger-scale corrected sort evidence remains open; a contract edit or previous mislabeled report cannot close4.1.

## Final local disposition — 2026-10-26

Task4.1 locally COMPLETE through actual corrected executions, not wording:200k/1M93 each resident/mixed/cold/reloaded/restart,2M/5M23 each standalone cold, plus skewed2M/5M70 each mixed/cold/reloaded-mixed/restart under unchanged500MiB maximum. Exact source/input/executable identities and truthful immediate-versus-separate-in-flight cancellation scope are recorded in mixed-scale-closure-20261026.md and current-canonical-20261026.md. The earlier evidence inventory is historical. No wholly-hot oversized corpus, hosted certification, privileged path, release or Explorer replacement claim.
