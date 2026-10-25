# Applied acceptance clarification: exact equality under production residency

**APPLIED under the owner's 2026-10-25 original-vision recovery directive,
section 9B. Task 4.1 remains unchecked.** This supersedes the prior proposed-only
decision status, not the provenance or limitations of historical executions.

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

## Evidence inventory and remaining task 4.1 cells

| Acceptance dimension | Retained evidence | Current disposition |
|---|---|---|
| 200k full canonical differential | `artifacts/final-acceptance/oracle-200k-extended-final.json`: 23/23, clean `5d7e98a`, route/row premises | Historical valid execution; not relabeled as `db7887c1`. Final-source coverage/equivalence still needs an explicit determination. |
| 1M full canonical differential | Prototype/benchmark and general hot/cold evidence exist; they are not the independent full canonical oracle | Final-source route-asserting oracle matrix remains unverified. |
| 2M full canonical differential | `recovery-20261024/final-current/current-2m-oracle-{1,2,3}.json`: 13/13 each, `269d1c3` | Valid earlier-source execution. `git diff 269d1c3..db7887c1 -- src tests/engine/bench_identity_ordering_oracle.cpp` changes only `Projection.cpp`; explicit scope/equivalence determination remains. The final two pipeline benchmark runs are timing evidence, not replacement canonical oracles. |
| 5M full canonical differential | Final `db7887c1` extended oracle: 23/23, full 5M canonical scan, actual candidate admission, metadata/order/caps/status/immediate cancellation; archived final-oracle binding | Verified for the stated cold engine seam; no wholly-hot 5M assertion. |
| Actual resident and mixed engine routes | `TestForcedEvictionSearchEquivalence` asserts resident accelerator use, then one cold and one hot volume; exact ordered metadata/status/truncation comparisons. `TestCappedRootSearchMatchesColdAndRetainsTopN` exercises unload/rebuild cycles | Existing small-fixture regression execution retained in prior current-source CTest evidence. This is not independent-canonical mixed coverage at every scale. |
| Automatic transitions, generation invalidation, cancellation | `TestAutomaticEvictionPreservesMixedVolumeSearchAndSnapshots`, `TestProjectionEvictionInvalidatesCapturedCursorSafely` and related engine tests | Existing regressions cover their named seams. Inventory route assertions and independent-canonical comparison before closing the new scenario; do not infer coverage from test names alone. |
| Large mixed-volume/cap boundary | Final production Store 2M operational matrix: 18/18, three cycles, admission boundaries 16,383/16,384/16,385 | Verified Store seam, not a full engine mixed-residency canonical differential. |

No expensive completed 5M/pipeline/Store matrix was repeated for this planning
clarification. The next executable acceptance slice is the bounded 1M independent
oracle plus explicit resident/mixed/transition route coverage and an equivalence
determination for retained 200k/2M oracle inputs. Task 4.1 stays open until those
cells are resolved; the accepted contract is not a correctness waiver.
