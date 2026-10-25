# Independent review record and adjudication

## Identity and execution

Four fresh-context, read-only native `reviewer` lanes actually completed against
implementation `4c1c4501e5b1f0579a2d34f4f7afea5f83708979`, PR15 tip
`2ff3b595940d95c706fa815b72ffee1c8d53fb44`:

- Model: `kilo/nvidia/nemotron-3-ultra-550b-a55b:free` (registered resource,
  no account/billing/permission changes).
- Workflow: `eb3d7aaf-8c93-45fb-bf0d-94531a19828b`.
- A SQLite/data: `ebbbe94e-049e-4186-909f-cfdc4e470015`.
- B projection: `acfc16c8-9d9c-4fa6-ab54-2f057044b2fd`.
- C methodology: `f5ea92ee-4c4e-40ac-bb4b-595398ae1514`.
- D integration/certification: `7a257ee4-1f11-4e9e-bfbc-7b6d72b8aa3f`.

Original reports are preserved in `reviews/`. They are **not authoritative
acceptance verdicts**: several citations/claims are stale, contradictory or
speculative. Source inspection is independent review, not independent runtime
execution. No reviewer executed builds, crash tests or measurements here.

## Dispositions

| Claim | Parent source/test verification | Disposition |
|---|---|---|
| Large subtree eviction still quadratically unlinks doomed siblings | `Projection::EvictSubtree` used `Remove` per descendant; `FlatChildrenMap::remove` linearly searches a sibling slice. New20k flat-subtree regression fails exactly its comparison-count assertion before repair; survivors/links already pass. Bulk identity erasure followed by existing immediate compaction fixes it; focused3/3 and full ladders pass. | **VALID significant performance defect, fixed db7887c1; final independent re-review still pending** |
| `TriggerReconciliation` loses reconciliation intent on failed submission | Actual function calls `ClaimActiveAttemptLocked(...,true)` and `RearmFailedAdmissionLocked(...,true)`; reviewer cited wrong location and historical issue already fixed. | **INVALID/STALE**, withdrawn in A follow-up |
| Move assignment should clear transferred `transactionActive_`/`failed_` | Transaction ownership and failed state belong to the incoming connection. Clearing them would misrepresent it; readiness is separately invalidated. | **INVALID recommendation**; no new product change |
| Whole-volume fallback is a P0 because it evicts a volume | Explicit intended fallback after eight unsuccessful fine-grained passes; Store remains authoritative; no data deletion. Restoring subtree-only fallback reintroduces measured non-convergence. | **INVALID severity/defect**; measured trade-off retained |
| Eight passes necessarily recur indefinitely after correction | Whole-volume fallback removes working set and reaches watermark; final allocator-free5M completes with progress beyond3.1M. | **DISPROVEN as universal current failure**; finite compaction cost remains a known trade-off |
| No mixed-volume/cursor/readiness regression tests exist | `TestAutomaticEvictionPreservesMixedVolumeSearchAndSnapshots`, `TestProjectionEvictionInvalidatesCapturedCursorSafely`, readiness and ordinary storage tests exist and execute in current CTest. | **INVALID** |
| FTS readiness never checked | `StoreReadSession::CandidateIndexUsable` gates candidate route on snapshot readiness/fold signature. | **INVALID** |
| CROSS JOIN requires speculative ANALYZE rewrite | No demonstrated bad plan or wrong result. Explicit SQLite join-order pin plus actual2M local-selective/cap matrix passes. | **SPECULATIVE**, no production change |
| ForgetVolume always invalidates survivor accelerator | Reviewer confused explicit false argument with `RemoveVolume` default true. | **INVALID** |
| Snapshot namespace collision / cached-page-size / test-global concerns | No reachable current regression established; namespace assertion was made without inspecting owning implementation; unsupported page-size mutation and intra-process parallel tests are not product operations. | **Not confirmed P1/P2**; no speculative rewrite |
| Historical dirty migration/downgrade gaps remain current | Reports quote superseded lane-E evidence; clean5d7e98a migration/downgrade evidence and current tests supersede those claims. | **STALE**, history retained, no repeated5M migration demanded |
| Missing acceptance / hosted gaps | Real at review start, but absence is not a P0 corruption bug. New performance/resource/Gallery evidence closes named execution gaps; hot/cold owner decision, final re-review, hosted execution remain open. | **VALID acceptance limits**, updated in owner report/tasks |
| Runtime exe hashes must be computed inside every executable | External runner SHA256 binding plus embedded source identity is explicit. Internal hashing is useful but not required for an already source-bound artifact. | **No demonstrated mismatch**; no unrelated harness rewrite |

A's corrected follow-up `6b66c58f-d9a0-4706-8ba5-3f1aafb7ef9b` is also preserved;
it still contains speculative notes, not newly proven defects. B follow-up
`9e4f306d-b6e5-406e-baf5-4462516abed8` failed before verdict:

`429 PROMOTION_MODEL_LIMIT_REACHED: Sign up for free ... Or come back later.`

No retry of that exhausted route, account sign-up, billing change, alternate CLI,
or screening workaround occurred. Prior default-provider usage-limit failures
remain historical. **Final db7887c1 repair/search-only-harness review is
BLOCKED_EXTERNAL.** Tasks5.10/5.14 remain open. Parent adjudication, tests and
successful benchmarks are not substituted for that independent approval.

## Exact follow-up request when review is available

Review `db7887c1` versus `4c1c450`: bulk subtree identity erasure (both private
callers immediately compact), cycle-safe traversal, survivor child-link rebuild,
20k comparison-count regression, and `--search-acceptance-only` sample scheduling/
JSON semantics. Recheck any data-integrity findings against actual current Store,
IndexPipeline and VolumeSessionManager bodies. Review final artifact/acceptance
claims including latest reaper-fixture failures. Return current findings only,
with exact source proof and a verdict that does not approve unresolved P1/P2.
