# Final engineering closure and owner-controlled integration

## Decision summary

**Performance, ordinary operational characterization and combined Gallery/runtime
qualification are completed. Merge readiness is NOT certified.** Remaining gates:
owner decision on hot/cold acceptance4.1; independent review of the final repair;
latest local process-reaper fixture failure; hosted CI execution. All PRs stay
open/unmerged. No production release is authorized.

## Source and evidence identity

| Item | Binding |
|---|---|
| Starting PR15 tip | `2ff3b595940d95c706fa815b72ffee1c8d53fb44` |
| Final implementation / clean executed source | `db7887c1ec1d3d8f493848a9050240407423e678` |
| PR14 | `d3884eecd03805a91fe5d12d7a90ceb1b5c082f2` |
| PR16 | `e75a230db9ac89f6a638754cb6ad672735e4d644` |
| main at reconciliation/simulation | `a26fe609439a18c128b2a2e557bc7eb2534fa128` |
| Actual combined executed candidate | `e5b006dc9255931a41ff8fa135198f688d093434` |
| Combined Git tree | `2069796b1dbd10cdd5d3558f73d30d2e1b29116e` |
| Evidence/repository tip | Derive after publication; do not embed this document's own commit SHA |

`provenance-manifest.json` hashes available product/test/benchmark binaries by
candidate/preset. `artifact-manifest.json` hashes retained raw artifacts and maps
flattened long Windows paths back to originals. Raw text/JSON/source/runner files
are deterministic `.gz` archives, preserving original bytes across Git's line-
ending conversion; decompress the manifest-mapped artifact to inspect it.
Evidence-only operational driver
and its production-library/executable hashes are explicit. Equivalence verdicts
are generated **after** the final evidence commit, outside this checked tree.
These bindings do not claim bit-for-bit reproducible PE builds.

## Engineering completed

- Reconciled all six initial worktrees, live refs, PR descriptions/reviews and
  processes. Initial trees clean; historical SHAs confirmed, not reset targets.
- Recovered safe capacity using exact manifests and exclusive-open checks for
  campaign CMake intermediates only. First receipt:13,022,874,457 logical bytes,
  11,910,975,488 available bytes reclaimed; later Analyze/diagnostic/completed
  release intermediates have separate receipts. Every executable, fixture,
  source/worktree/Git reference and unique evidence retained. C/D were not
  independent scratch volumes. Later host free-space increases are **not**
  attributed to these receipts or to agent cleanup.
- Obtained four independent final-source reports; adjudicated unsupported/stale
  claims rather than accepting favorable conclusions. One real flat-subtree
  quadratic sibling-unlink defect reproduced red-first and repaired narrowly.
  Whole-volume fallback, 500MiB/400MiB policy and durability logic unchanged.
- Added explicit search-acceptance scheduling to the existing pipeline harness:
  same deterministic fixture and `RunSearch` timing, fewer non-key samples and
  no unrelated churn/contention/snapshot-capacity probes. Small smoke passes.
  Timed-out original all-purpose run is retained as incomplete, not certification.
- Executed final-source performance, extended5M differential, ordinary resource
  matrix and actual three-PR combined builds/runtime qualification.

## Performance — ACCEPTED_WITH_MEASURED_TRADEOFFS

### Current same-pipeline 2M boundary

Two complete fresh-process runs,597.57s/611.48s overall. Each creates exactly2M
rows through production `BuildSyntheticDatabase`,8192-row batches, unchanged
schema/FTS/default memory policy. Reopen is wholly cold (projection estimate128B).
Ten measured warm samples pooled only across these two compatible current runs:

| Query | Warm median ms / n | Forced projection-cold ms / n=2 |
|---|---:|---|
| Selective |4.4724 /10 |5.1846,5.3058 |
| Exact filename |5.7042 /10 |6.2194,6.0871 |
| No match |1.2066 /10 |1.8836,1.8812 |

All12 established classes measured in both runs. Other classes have **one warm
and one forced-cold observation per process**, not a statistically established
tail. Broad warm observations38.735/39.102s; extension49.223/51.159s; Unicode
14.700/15.299s. Broad/ineligible scans remain expensive known trade-offs.
The earlier source4c query log already shows the same broad-scan order of cost;
there is no new source-grounded canonical-scan regression from the subtree fix.
No precise historical speedup ratio, OS-cache flush, p95, or allocator-zero-effect
claim is made. Raw legacy JSON p90 fields are descriptive tiny-sample order
statistics, **not accepted tail estimates**. Store-fixture oracle timings are
not pooled with pipeline timing.

### Allocator-free 5M, final source

| Metric | Observed |
|---|---:|
| Rows completed / batch |5,000,000 /8192 |
| Ingestion elapsed |131.913s |
| Including open/close |142.398s |
| CPU including open/close |122.219s |
| Peak private bytes |1,187,663,872 |
| Peak working set |1,092,804,608 |
| Private before close / after teardown |287,154,176 /7,716,864 |
| Final database bytes |1,037,783,040 |
| Peak sampled WAL / sampling |4,787,316,432B /250ms |
| Minimum available capacity |4,424,540,160B |
| Exit / owned scratch files afterward |0 /0 |

Executable SHA256 `1ae01a92316c9a2fb43f3a343092d24c3ecd8fc0df8f8dc47e390df8d6874866`;
MSVC `_MSC_FULL_VER=194435228`, release-test/RelWithDebInfo; deterministic indexed
fixture, no random seed. CRT new/delete binding is retained; no allocator probe
or per-batch stage timers. Local NTFS storage is identified; physical medium was
not independently established.

Progress windows around the old cliff:3,006,464→3,104,768 in2.04s;
3,104,768→3,203,072 in4.41s; next98,304 rows2.11s. No repeated catastrophic
12-second enforcement loop per8192 rows or late failure. Coarse100k progress
windows are not per-batch latency traces. No new ANALYZE call-count trace was
collected; existing policy/tests remain the evidence for deferred statistics.
The owned ingestion database is closed/deleted normally; reopening this exact
now-deleted artifact was not claimed. Independent existing5M fixture reopens and
queries successfully on final source. An earlier pre-repair5M run134.931s is
separate historical-in-this-session evidence, not pooled.

### Multi-volume/cap and resources

Evidence-only driver links the clean final production Store/SQLite libraries.
Two1M-row volumes, six cells ×three cycles: **18/18 PASS**, exact sorted full
EntryRecord fields versus an independent full1M-row stream per requested volume.
This tests Store admission/materialization, **not full SearchIndexed response
ordering/status/cancellation** (those are covered separately by engine/oracle).

- Locally selective2 matches with50,000 matches elsewhere: candidate route,
 131.117–158.227ms (three observations; no tail claim).
-16,383 and16,384 candidates admitted;16,385 falls back; missing needle admitted
 with0 rows. Broad-volume fallback correct. Full metadata equality in every cell.
- Eight8192-row write/checkpoint cycles while a real reader pins a2M-store
 snapshot: writers progress; all forced checkpoints report BUSY, bounded observed
 calls0.124–0.357ms; WAL reaches14,321,152B. Reader disposal permits full
 checkpoint3476/3476 frames and WAL reuse without file growth. Production
 checkpoint is not promised to physically truncate; prior ordinary TRUNCATE
 probes remain separate evidence.
- Private bytes over pinned cycles8.28–10.36MB, non-monotonic; after cycles5.37MB.
 This is a bounded observation, not an indefinite soak or total-process ceiling.
- Current full CTest includes ordinary injected SQLite FULL/write/commit/rebuild,
 interrupted readiness repair, version-read refusal, committed-prefix, metadata,
 reconciliation enumeration/retry, mixed-volume eviction/search/snapshot tests.

**Operational assumptions:** one serialized writer; synthetic fixtures only;
bounded reader lifetime and retry cadence; capacity for atomic writes plus
reader-retained WAL and OS reserve.500MiB projection estimator/400MiB watermark
are **not RSS/private limits**. Pinned readers can retain WAL indefinitely; no
finite universal WAL reserve/cap exists. No immediate durable retry-after-process-
loss, power-loss guarantee, physical-exhaustion result or new crash certification.

## Exactness and specification

Final-source extended5M read-only oracle **23/23**, including alternate sorts,
cap1/2/17/4096, metadata/identity/order/truncation/status and cancellation;
canonical route bypasses candidate filtering and verifies full5M-row scan.
Original fixture SHA256 `96d353521e2154ce7a60cadaf7fadd70b6494e536aa83e14e023b304862ecb03`
unchanged. Prior invalid oracle v1 remains withdrawn. Historical 2M/200k results
remain labeled by their actual source. No fully-hot5M claim.

[acceptance-decision.md](acceptance-decision.md) proposes a precise clarification
of the oversized-corpus residency premise. It is **not applied**. Task4.1 remains
open pending owner decision and any resulting required coverage.

## Actual combined application qualification

Disposable branch `validation/three-pr-final-20261024` in the existing combined
worktree preserves the old validation branch. Simulated order16→main,14→result,
15→result, then final db7887c1 repair. One textual UI CMake registration conflict
resolved keeping clipboard and theme tests exactly once. No actual PR branch/base
or main mutated; temporary branch not pushed. No duplicate product changes, lost
FTS migrations, overwritten theme helper or lost bulk-removal fix.

| Gate | Actual combined e5b006dc result |
|---|---|
| Debug/release-test/diagnostic actual apps |PASS |
| Initial full Debug/release CTest |131/131 each PASS |
| Later full Debug/release CTest |130/131 each; same reaper fixture FAIL (below) |
| Analyze |Terminal incremental PASS; prior interrupted/link logs retained |
| ASan slice |4/4 PASS |
| strict OpenSpec |93/93 PASS |
| Gallery +core visual catalog |**20/20 PASS**, including all three original Gallery failures |
| Real E2E |8PASS/0FAIL/0SKIP |
| UIA core / visual matrix |25/25 each |
| Full UIA capability |23PASS/5SKIP/0FAIL |

Five UIA skips retain privileged/foreground/Explorer-delivery reason classes.
Gallery covers actual F9 inversion and narrow/wide resize/theme interaction;
unit tests cover all six explicit/Follow-System combinations; settings and
lifecycle suites cover persistence/recovery and normal startup/shutdown. This is
not a physical OS-theme-change/mixed-monitor release campaign. Captures report
**96DPI/100%**; physical125% NOT_EXECUTED. Standalone PR16 source is unchanged.
`PR16_LOCALLY_INTEGRATION_READY` refers to product/runtime compatibility, **not
blanket merge authorization or all current host gates**.

## Latest local certification blocker — do not hide earlier passes or later failures

PR15 clean db7887c1 parity **11/11**, Debug/release130/130 each, Analyze/ASan4/4,
policy/hygiene/strict all passed before the later host change. Combined e5b006dc
initial131/131 suites also passed. The final combined parity attempt is
**8PASS/3FAIL**: release C1041/PDB-open build failure subsequently completed by
serial incremental build, plus both CTests failing only
`ffverify_process_reaper_ps_tests`: “terminated owned process is actually gone”.
The parity report is **not** rewritten into11/11 or passed-on-retry certification.

Read-only observation now shows PowerShell7.6.8: `Get-Command pwsh.exe` yields a
WindowsApps execution alias, while current process/CIM reports the actual
`Program Files/WindowsApps/...7.6.8.../pwsh.exe`. The fixture records launch path
as identity, and the production identity checker refuses path mismatch. This is
a source-grounded explanation consistent with the failure, not a captured trace
of the failed child (that child path was not recorded). Initial process inventory
reported7.6.5. No product-source change explains the transition.

**Boundary disclosure:** default full CTest did execute its pre-existing owned-
process reaper fixture; the live termination behavior was discovered when reading
the failure. No additional direct termination experiment or deliberate-crash
acceptance was dispatched; the fixture was **not rerun after discovery**.
Do not execute it under another name. Owner/environment-qualified validation is
required before treating the latest local gate as green. No source workaround,
security weakening, or unrelated reaper rewrite was folded into PR15.

LSP changed-file probes timed out/inconclusive3/3; no clean-LSP assertion. Strict
MSVC builds and Analyze are the substantive C++ checks.

## OpenSpec closure and acceptance matrix

Lifecycle remains **active**. Checked execution tasks do not certify merge.

| Criterion/task | State | Evidence/disposition |
|---|---|---|
|4.1 full governing hot/cold contract |OWNER_DECISION_REQUIRED |Proposed amendment only; no hot5M default-budget PASS |
|4.2 ordinary resource characterization |COMPLETE_WITH_ASSUMPTIONS |5M ingestion +2M reader/multi-volume +ordinary suites |
|5.7 final performance evidence |COMPLETE |Two pipeline runs, allocator-free5M, cap/volume matrix |
|5.10 independent adversarial sign-off |BLOCKED_EXTERNAL |Four reports obtained; final repair review429 unavailable |
|5.12 combined Gallery integration |COMPLETE |Actual candidate20/20 +E2E/UIA |
|5.14 final repair/harness re-review |BLOCKED_EXTERNAL |New subtree fix lacks final independent approval |
|4.3/5.11 ladder execution |RECORDED_WITH_FAILURE |Latest combined parity8/11, reaper fixture unresolved |
|Source/evidence equivalence |POST-COMMIT_GATE |Run at actual final evidence tip; verdict outside checked tree |
|Hosted certification |NOT_OBTAINED |Inspect final push once; local results are not hosted execution |

## Owner-controlled integration sequence and decisions

Preferred logical order remains16→main,14→main, then reconcile15 onto updated
main without duplicate changes, requalify any executable differences,15→main.
PR15 cannot land independently of14; Gallery fix16 is a tested prerequisite.
Retarget/reconcile only after owner merge decisions; no force push. The actual
simulation and its tree above validate this order, not permission to execute it.

| Decision | Options | Recommendation | Owner approval |
|---|---|---|---|
|PR16 merge |Approve/defer |Product/runtime locally integration-ready; resolve/explicitly disposition host/hosted gates before merge |Yes |
|PR14 merge |Approve/defer |Predecessor locally qualified on its own evidence; hosted limitation requires owner disposition |Yes |
|PR15 merge |Approve/defer |**Defer**: final re-review,4.1 decision and current local fixture gate outstanding |Yes |
|4.1 acceptance clarification |Approve/reject |Approve explicit canonical-all-scales/resident-route clarification, then assess remaining cells |Yes |
|Independent final review |Provide reviewer/defer |Obtain db7887c1 delta and final-claim review; do not substitute self-review |Reviewer resource |
|Local reaper gate |Qualified environment/explicitly authorize fixture validation/defer |Resolve alias/image identity premise in a separate authorized validation slice; no automatic destructive rerun |Yes |
|Hosted CI |Restore/retain blocked |Owner/account administrator restores actual job execution; no rerun loop |Yes |
|External release gates |Qualify/defer |Defer to separately authorized environments |Yes |

## Separate final states

| State | Verdict |
|---|---|
|ENGINEERING_SCOPE_COMPLETE |**NO** — explicit review/spec/local-gate boundaries remain |
|PRODUCT_CORRECTNESS_VERIFIED |TESTED_SCOPE_PASS; not full unsupported-residency certification |
|DATABASE_DURABILITY_VERIFIED |ORDINARY_FAILURE_PATHS_PASS; crash/power-loss not newly certified |
|SEARCH_EQUIVALENCE_VERIFIED |PARTIAL; exact tested differentials pass;4.1 owner decision open |
|RESOURCE_ACCEPTANCE_VERIFIED |PASS_WITH_DOCUMENTED_ASSUMPTIONS |
|PERFORMANCE_ACCEPTANCE_VERIFIED |ACCEPTED_WITH_MEASURED_TRADEOFFS |
|INDEPENDENT_FINAL_REVIEW_COMPLETE |**NO**, final repair review externally blocked |
|GALLERY_INTEGRATION_VERIFIED |**YES**, actual combined product20/20 |
|LOCAL_CERTIFICATION_COMPLETE |**NO**, latest combined reaper fixture FAIL |
|HOSTED_CERTIFICATION_COMPLETE |**NO**, no hosted execution certification |
|PR14_MERGE_READY |OWNER_DISPOSITION_REQUIRED; prior local qualification, hosted blocked |
|PR15_MERGE_READY |**NO** |
|PR16_MERGE_READY |LOCAL_PRODUCT_INTEGRATION_READY; owner/hosted/current-gate disposition required |
|PRODUCTION_RELEASE_AUTHORIZED |**NO** |

Signing/pins, clean-host installation, elevated live MFT/USN, raw-volume completion
bound, physical DPI/contrast/mixed-monitor, guest/provider and Explorer drag-in
remain separate release surfaces. No privileged, trust-store, billing, merge,
force-push or release operation is authorized by this report.
