# PR #15 final-acceptance owner report

Implementation baseline: **`5d7e98ae412cd893e47e7ebc9a9bdea4c609d0ac`**.
This is a commit-bound implementation record, not a claim about the SHA of
its containing evidence commit. Derive live refs/hosted results externally.

**Disposition: REVIEWABLE WITH OPEN ACCEPTANCE, NOT UNQUALIFIED CERTIFICATION.**
The search/ingestion fixes are tested locally; independent final review,
unintegrated Gallery remediation, and the remaining WAL operational cells
are explicitly outstanding. Neither PR #14 nor #15 is merged.

## A. Repository and PR state

- Repository `quantdale/FastFiles`; existing PR #15 branch
  `feat/durable-cold-search-and-hybrid-residency`, worktree
  `build/worktrees/durable-cold-search`. Stacked base remains PR #14's branch
  `feat/large-index-lifecycle-and-search-scalability-20261008`.
- PR #14 baseline `d3884eecd03805a91fe5d12d7a90ceb1b5c082f2` and main
  `a26fe609439a18c128b2a2e557bc7eb2534fa128` were fetched/reconciled;
  untouched by this campaign. No rebase, merge, force-push or branch deletion.
- Separate UI worktree `build/worktrees/f9-toggle-fix`, PR #16,
  `fix/f9-theme-toggle-follow-system` at
  `e75a230db9ac89f6a638754cb6ad672735e4d644` (based on main).
- Final pushed evidence SHA and live CI state are reported in PR #15/the
  session response, not embedded self-referentially in tracked workstate.

## B. Gallery regression

Controlled A/B: fresh debug-diagnostic builds of PR #14 `d3884eec` versus
PR #15 `b68767a`; identical harness, host/session, window sizes, capture
method and measured **100% DPI**. Both arms: 16 core surfaces PASS,
Gallery light PASS, dark/narrow/wide FAIL, **repeated 2/2 per arm**.

Root cause is **INHERITED_PRODUCT_UI_DEFECT**: F9 used
`preference == Light ? Dark : Light`, while the default is Follow-System.
On a light system the first press changes Follow-System to Light without
changing rendered appearance. The next press makes the supposed light
resize captures dark. Resize-only, forced repaint, direct posted-key and
persisted-settings probes discriminate this from rendering/input theories.
The harness's hardcoded "125% DPI" coverage line is stale; manifests show
96 DPI. Live 125% qualification was NOT_EXECUTED, not inferred.

PR #16 resolves the rendered theme before toggling, with red-first tests;
its Gallery catalog passes **20/20 twice**. It is NOT integrated into PR #15.
Final PR #15 visual run still reports **17/20, three FAILs**, exactly named:
`gallery-dark`, `gallery-narrow`, `gallery-wide`. Task 5.12 remains open for
compatible integration or explicit owner exclusion. No failure concealed.
See `gallery-ab-attribution.md` and `artifacts/gallery-ab/`.

## C. Five-million-entry correctness

Oracle v1 (`0019b37`) is INVALIDATED: reopening repaired its tampered marker,
so its reference was not independent. No v1 PASS is used for acceptance.

Corrected oracle v2 takes a read-only-source SQLite backup into its own
scratch directory, unloads projection residency, and compares normal
production search with a test-hook-forced **full entries scan**, bypassing
both FTS and LIKE/GLOB. Premises assert Completed/Cancelled status, exactly
5,000,000 canonical rows examined, no hot rows, candidate-route admission
for the three selective classes and nonvacuous matching results. Exact
ordered vectors compare identity, parent, folder/name, sizes/knownness,
timestamps and flags — not counts or aggregate hashes.

At clean `5d7e98a`: **all 12 established 5M classes + cancellation PASS**:
selective, exact, no-match, broad, extension, Unicode, moderate, prefix,
worst-substring, high-limit broad, mixed case and short prefix.
`IDENTITIES_EQUAL`, `ORDERING_EQUAL`, `CAPS_EQUAL`,
`NO_SILENT_TRUNCATION` apply to the **returned capped sequences**.
Selective/exact/no-match examine 1/1/0 indexed rows versus 5M canonical.
The 50,000 request is clamped to the product's 4,096 publication cap, with
equal truncation flags; no claim of 50,000 delivered identities.

200k extended oracle: **23/23**, including size/modified/created direction
sweeps, caps 1/2/17/4096 and cancellation. Small unit fixtures cover roots,
multi-volume, exceptional/surrogate names, repeated trigrams and mutation
seams. Dedicated 5M targeted/uncapped cells are NOT_EXECUTED. Fully-hot 5M
residency is not claimed. This uses the existing open/migration fixture,
not the discarded historical pipeline-ingest fixture; do not pool timings
between those different layouts. See `exact-order-oracle.md` and preserved
`artifacts/final-acceptance/oracle-*-final.json`.

## D. Ingestion profile and fixes

Two separate causes were found:

1. Full-database ANALYZE every ~10k accumulated records created quadratic
   scan work. It is now deferred to maintenance cadence, with pending debt
   retained on failure. Two stats-dependent plans were pinned: candidate
   stream drives from FTS/side-set via map/entries keys; aggregate CTE uses
   `idx_entries_parent`. Red-first scheduling test and semantic suites pass.
2. ANALYZE deferral alone did NOT fix the 3.1M cliff: a clean controlled run
   reproduced it and was stopped at 900s. Stage timers isolated the actual
   cliff: **~0.17s Store write versus ~12.8s projection enforcement per
   8,192-row batch**. Eight tiny subtree evictions repeatedly compacted the
   whole projection without reaching the low watermark. A tiny-victim
   regression fails before the correction. After eight inadequate subtree
   passes, eviction now falls back to whole-volume **cold residency**, with
   canonical durable search intact and no budget increase.

Final ordinary benchmark ingestion of 5M: **132,367.8954ms (132.37s)**,
versus the historical 5,266s observation. The exact same fixture builder is
used; the historical host had concurrent certification work, so this is not
a controlled 40x statistical guarantee. The cliff's stage attribution is
controlled: instrumented before/after work shifts from 12–13s enforcement
per batch to approximately 0.001ms steady state after coarse eviction.
Coarser hot residency is the explicit trade-off. Decision: **RETAIN pending
independent review**, not blanket final sign-off.

See `ingestion-memory-cliff.md`, `ingestion-analyze-ab.md`, and the raw
before/after profiles preserved under `artifacts/final-acceptance/`.

## E. WAL investigation

Historical 960MB was a whole-lifecycle watcher peak with explicit checks
only at phase boundaries; the bench's 32MiB literal was not production.
Production threshold is **4000*4096 = 15.625MiB**, called by scheduled/event
maintenance, not an invariant bounding every transaction/file allocation.

Bench per-batch checks reduce the 2M aggregate peak from **959,807,592B** to
**273,629,832B** (n=1 each), with +21%/+35%/+48% rename/removal/reinsert
elapsed differences. The watcher includes atomic ForgetVolume, so residual
peak attribution by phase is UNPROVEN. Production reconciliation now commits
5,000-row chunks and checkpoints between chunks. Injected failure in chunk
two proves committed-prefix durability/projection agreement, retained
obligation, query-visible epoch advancement and retry convergence.

**No universal WAL upper bound or sufficient 1GB reserve is claimed.**
A terminated profile's owned WAL was ~2.497GB when cleaned up. Pinned
readers can prevent RESTART; migration/ForgetVolume remain atomic. A watched
5M migration parent (preparation + one migration, sampled every100ms) sees
**648,352,072B** peak, a sampled lower bound, not an absolute guarantee.

Large-reader/busy checkpoint, kill-before/after large removal and safe
SQLITE_FULL operational matrices remain NOT_EXECUTED. No real disk-full
experiment occurred: a <4GiB free-space preflight refused a run, only exact
campaign-owned disposable copies/profile files were removed, and the watcher
then ran. Original fixtures/user data were not deleted. See `wal-lifecycle.md`.

## F. Migration and rollback

Historical 1M/2M: 11.2s/23.5s (older source/harness qualifications retained).
Clean current 5M entries-only fixture, two fresh migrations:

| | run1 | run2 |
|---|---:|---:|
| total open | 67,564.1713ms | 66,881.7371ms |
| candidate rebuild | 59,075.5419ms | 56,349.9312ms |
| map | 11,440.9559ms | 10,853.2204ms |
| FTS terms | 39,261.3501ms | 37,238.8078ms |
| exceptional side-set | 3,548.3812ms | 3,359.1227ms |
| peak working set | 12,980,224B | 13,295,616B |

A REAL interrupted 5M migration child followed by recovery succeeds:
62,739.927ms total, 54,116.5726ms rebuild, no integrity failure. Separate
post-run integrity check returns `ok`; entries/map/terms counts are all5M.
A further watched migration supplies the sampled WAL figure in section E.

Policy is explicitly **FAIL_CLOSED_DOWNGRADE**, not automatic reversibility.
The documented offline backup-first SQL rollback is transactional and drops
only derived candidate tables. A probe built from PR #14's actual older
Store.cpp + its SQLite library rejects v3 (`opened=0 integrity_failed=0`);
after rollback it opens v2 with all5M entries. Direct ordered full-tuple
comparison proves **5,000,000 entry tuples preserved**, then integrity `ok`.
This is older STORE compatibility, not a full installed-app downgrade test.
Task3.3c can close. No production/user database was used.

## G. Performance repeatability

Two COMPLETE final-SHA production benchmark runs at200k/1M (runs1 and3):

| scale | selective cold | exact-name cold | no-match cold |
|---|---:|---:|---:|
| 200k | 3.7891 / 1.8520ms | 4.7888 / 2.1353ms | 0.9130 / 0.8442ms |
| 1M | 3.8419 / 3.4477ms | 4.4327 / 2.9219ms | 1.2523 / 0.8580ms |

Cold n=2 means first query of a measurement block, NOT an OS-cache flush.
No p90/p95 is inferred from two samples. Warm labels retain the harness's
residency caveat (1M here is cold stream); multiple warm blocks are not
independent host repetitions. The runs show material host variance
(1M ingestion56.1s versus22.1s), not controlled idle-host guarantees.
Run2 timed out after recorded query blocks and is EXCLUDED from completed
JSON distributions; its log is preserved. No incomplete output is invented.

Historical 2M three-order-of-magnitude eligible-query gains remain historical
`628de6d` evidence, not re-certified timings for new source. Final-SHA2M
same-harness repetitions remain NOT_EXECUTED; task5.7 stays qualified/open
for that remaining cell. No new performance architecture is proposed.

## H. Resource bounds

- Default projection budget remains500MiB, low watermark400MiB.
- Ordinary5M ingestion: database artifact **1,037,783,040B**; after fixture
  teardown private8.17MB; process peak private **1,368,276,992B**, peak
  working set **1,262,170,112B**. The budget is a projection estimator/
  residency policy, NOT a hard process-RSS ceiling. Temporary compaction/
  allocator overhead is disclosed, not hidden behind500MiB.
- Instrumented corrected ingestion final projection estimate234,213,198B;
  entries-only diagnostic5M116.45s versus instrumented FTS-on136.26s
  (n=1, same fixture/stage diagnostic, approximate17% overhead; no claim
  about individual internal FTS merge costs).
- Five-million-row startup rebuild stays cold: fail-fast rebuild observation
  200.3ms, not fully resident hydration. Mapping/snapshot product limits
  disclosed previously are unchanged.
- WAL is workload/reader dependent (section E), not a guaranteed file cap.

## I. Independent review

Four initial read-only investigation lanes (B/C/D/E) are complete and
preserved. They are NOT final reviews of subsequent fixes. The final
three-lane adversarial wave `09243bbc-ea8a-4f23-9c2f-9a4b423b1cdb` failed
with provider402 quota/balance exhaustion, no final verdicts. Primary
self-review found and repaired the oracle independence flaw and disproved
the ANALYZE-only cliff hypothesis; that is NOT independent sign-off.
**INDEPENDENT_REVIEW_COMPLETE is not claimed.** Tasks5.10/5.14 stay open.
No billing/account changes or silent alternate-provider launch occurred.

## J. Final local certification

At implementation `5d7e98a`, own runs (not predecessor inheritance):

| gate | result |
|---|---|
| Debug build + full CTest | PASS **129/129** |
| release-test build + full CTest | PASS **129/129** |
| Analyze | clean |
| ASan slice | PASS4/4 via parity |
| local CI parity | **11PASS / 0FAIL / 0SKIP** |
| strict OpenSpec / governance / lifecycle / markers / diagnostics / drift / hygiene | PASS via parity and suites |
| real E2E | **8PASS / 0FAIL / 0SKIP** |
| UIA core | **25/25** |
| UIA visual matrix | **25/25** |
| full UIA capability | **23PASS / 5SKIP / 0FAIL** |
| final visual catalog with Gallery | **17PASS / 3FAIL**, no skips |
| exact5M oracle | **12 classes + cancellation PASS** |
| extended200k oracle | **23/23 PASS** |
| migration/recovery | measured at5M; integrity and real older-store rollback verified |

Full-UIA skips: privileged storage mid-scan and service-toggle scenarios;
drag-between-panes and selection interaction foreground limitations;
Explorer drag-source unavailable. These are recorded scenario outcomes,
not proof of missing functionality being correct. Physical125/150/200%
DPI, production signing/privileged MFT and clean-host install are not
obtained here. Transient LNK1104 during the earlier build was resolved by
one incremental retry; it is not counted as a product test pass-on-retry.

## K. Provenance

- Product/test/benchmark source baseline is `5d7e98a`. Final artifacts carry
  that embedded build identity where supported; caller-supplied clean/source
  labels remain labeled as assertions, not cryptographic source attestation.
- `bench-identity-reconciliation.md` distinguishes628de6d product,
  80a6f11 bench-only maintenance-off addition, b68767a docs tip. Historical
  dirty-harness outputs are retained with qualifications, never relabeled.
- The old oracle-v1 and pre-fallback/timeout measurements are excluded from
  final success pools. Current clean artifacts supersede corresponding
  dirty exploratory cells.
- `artifacts/final-acceptance/summary.json` records raw artifact hashes and
  repeatability calculations. Executable-tree equivalence from5d7e98a to
  the evidence tip must PASS; its final verdict is recorded externally
  after committing this report (no self-referential SHA loop).

## L. OpenSpec

The change remains **active**. 3.3c closes with current5M migration/recovery
and downgrade policy evidence. Base5M capped identity/order acceptance is
verified; dedicated targeted5M/uncapped cells stay disclosed. 200k/1M repeats
are complete, but final2M repeats remain open under5.7. Review5.10/5.14,
Gallery integration/exclusion5.12 and remaining WAL operational bounds
remain open. Full-ladder execution is recorded with failures/skips, not
misrepresented as all-green certification.

## M. Hosted CI

Local parity is NOT hosted certification. Inspect one corresponding run
AFTER final push and record its URL/head/jobs/steps/artifacts/conclusion in
PR #15 and the session response. If six jobs again fail with zero steps,
`HOSTED_CI=BLOCKED_EXTERNAL`, `HOSTED_CERTIFICATION=NOT_OBTAINED`.
No billing adjustment, security bypass or repeated workflow triggering.

## N. Owner decision boundary

**Completed:** Gallery attribution and isolated fix submission; exact capped
5M publication comparison; staged ingestion root cause plus red-first fix;
clean5M ingestion/migration/recovery; real older-store rollback proof;
200k/1M repetitions; final local ladder with honest outcomes.

**Externally blocked:** independent final review provider quota; hosted CI
if the post-push check confirms the historical zero-step failure.

**Open / unproven, NOT external by fiat:** final2M same-harness repetitions;
large-reader/SQLITE_FULL/large-removal crash WAL matrix; absolute disk-space
bounds; dedicated targeted/uncapped5M cells; physical125% A/B.

**Merge decision:** owner only. Gallery fix integration or explicit accepted
exclusion and independent review of new eviction/ingestion/reconciliation
code remain material acceptance boundaries. No auto-merge, no false PASS.
