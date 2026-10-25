# PR15 ordinary final-acceptance recovery — historical owner report

**Checkpoint superseded by [final closure](final-closure/owner-report.md).**
All measurements and qualifications below retain their original commit binding;
they are not the latest campaign status. No earlier failure is silently erased.

**Qualified local engineering checkpoint; campaign ACTIVE, NOT merge-ready.**
No pull request was merged. Main, PR14 and PR16 were preserved.

## A. Session recovery and identities

- Intake PR15: `8c4c8481f312aa4651a5c2bab3cb61e0b0e50af0`, stacked on PR14.
- Final executable/test/benchmark implementation:
  **`4c1c4501e5b1f0579a2d34f4f7afea5f83708979`**.
- Clean executed validation and query-measurement source:
  **`269d1c36036cb84b40c3ef01baee3c2b6c990579`**; executable inputs equal4c1c450.
- Qualified tested UI binary SHA256:
  `ee46d8702a52425b25f143444ee76b20c5799969fb5cc87ff1c26755f5df1f9a`.
- Query/oracle executable SHA256:
  `0b9ec97ec7b0e1cdc2af67641f265aa0161a136759155286eca96a7d84ed5ca3`.
- Current allocator-free ingestion executable SHA256:
  `52fe83b9aaecfd113ec68af134c8e826e86ffc52d549e8bcfc4bbe61453f997d`
  (**preflight only**, not a completed5M measurement).
- Evidence tip/repository tip and final hosted result are derived after the
  evidence commit and reported in PR15/session handoff; no self-referential SHA.
- Preserved refs, re-fetched: main `a26fe609439a18c128b2a2e557bc7eb2534fa128`;
  PR14 `d3884eecd03805a91fe5d12d7a90ceb1b5c082f2`;
  PR16 `e75a230db9ac89f6a638754cb6ad672735e4d644`. All remain open/unmerged.

Recovered work was retained. The interruption was not treated as a product
failure. No unknown work was reset, cleaned or force-pushed. Earlier cleanup
receipts concern proven disposable synthetic files before the continuation
boundary; original fixtures and historical evidence were retained.

## B. Independent review and repairs

Five independent fresh-context reviewers inspected actual8c4c848 source/diffs
using `openai-codex/gpt-6.1-sol`. Independence is separate contexts, not
cross-provider agreement. Full reports are in `recovery-20261024/`.

Reproduced/narrowly fixed:
- Whole-volume eviction's quadratic doomed-sibling unlinking and unused
  free-list capacity preventing memory convergence.
- Missing FTS structures falsely accepted as complete; schema repair now
  commits atomically with readiness invalidation, including interrupted repair.
- Failed rebuild volume enumeration accepted as exhaustion; coverage validation
  now precedes publishing ready state.
- Moved read sessions retaining another snapshot's readiness verdict.
- Failed reconciliation removal/metadata finalization losing retry ownership,
  successful timestamps, or a stronger deferred full-rescan obligation.
- Failed schema-version lookup guessed as fresh0; failed reconciliation identity
  enumeration falsely cleared its obligation. Actual ordinary error fixtures
  reproduce both, including unintended version-marker downgrade before the fix.
- Benchmark watcher data race and false success for incomplete/output-failed
  measurements. The allocator-free target is separate from historical probes.

Projection follow-up at08be96f approves its repaired logic, unchanged since.
A durability follow-up found the composition defects subsequently fixed. Final
data/performance follow-ups requested atdab2128 failed before verdicts with
**`Codex error: The usage limit has been reached`**. No final4c1c450 approval
is claimed. A disclosed availability check of the installed fixed-route
`opencode/hy3-free` adapter also failed closed: native Kimi version preflight did
not succeed; no ACP route or review ran. No fallback around that restriction,
doomed retries, installation/billing changes or safety-screening workaround.
Tasks5.10/5.14 remain open; exact receipts are in final-current/review-availability.json.

## C. Ordinary reliability and resource behavior

Current `ffengine_storage_pressure_tests --ordinary-only` passes. It uses
owned synthetic fixtures and simulated error returns from real SQLite xWrite
callbacks—**not physical disk exhaustion**. The active VM/file/offset are logged;
a spill can flush a page dirtied earlier, so page-level FTS attribution is not
invented.

Verified current coverage:
- Entry/candidate/FTS-maintenance-associated/removal/COMMIT/checkpoint/migration/
  rebuild write-error returns: rollback, no false success, raw-before-repair
  SQLite/FTS integrity and candidate identity/name consistency, then retry.
- An integrated second-chunk write error leaves exactly5000 removals committed,
 7000 projected children remaining, a newer query epoch and retained obligation;
  retry converges. Surrogate side-set membership is nonvacuous.
- Pinned/overlapping readers permit writer progress; failed RESTART is bounded,
  exposes BUSY/backfill counters and recovers after reader release. Observed
  small-fixture WAL1,668,632B is not a universal cap. Early-stop reader teardown
  releases its snapshot; a test-only TRUNCATE confirms physical reclamation.
- Actual SQLite version/read-enumeration errors fail closed and retain repair
  obligations; manager tests verify scheduled cursor-free rescan requests.
- Automatic ingestion-driven cold fallback during a parked search invalidates
  its cursor and returns exact mixed-volume identities; a small hot volume and
  its snapshot survive, oversized cold reload emits no partial snapshot, and
  alternating ingestion/search/export remains consistent.

Default projection budget500MiB / watermark400MiB is unchanged, **not an RSS
ceiling**. No finite WAL/reserve guarantee or automatic writer-pressure cap is
claimed. Deliberate termination/restart interruption cells were NOT_EXECUTED
under the continuation scope; earlier results stay historical. No live user
application was manually terminated, privileged storage operation performed,
actual user database modified, or trusted-root/signing state changed.

Fresh older-store20k smoke binds source/library/executable inputs, rejects v3,
then accepts the offline derived-state rollback with all20000 tuples unchanged
and integrity `ok`. It is not installed-app or newly bound historical5M proof.
Full limitations/test plans: `deferred-ordinary-acceptance.md`.

## D. Performance and provenance

At clean269d1c3, three fresh processes read the same verified2M synthetic
Store fixture. Every12-query-class sequence plus cancellation passes exact
capped cold/canonical comparison (**13/13 x3**). The SQLite-enforced read-only
URI path rejects writable connections, retains normal locking/change detection,
and cannot migrate the fixture. Source SHA256 is unchanged before/after every
run. Default owned-backup mode remains available.

| Query | Median ms, n=3 |
|---|---:|
| selective | 12.6182 |
| exact filename | 5.6932 |
| no match | 1.5028 |
| broad | 14111.7 |
| extension | 18357.7 |
| Unicode | 6360.08 |
| moderate | 5785.85 |
| prefix | 5881.48 |
| worst substring | 10047.3 |
| capped high-limit broad | 13977.2 |
| mixed case | 9907.45 |
| short prefix | 14554.5 |

Eligible selective searches remain millisecond-class. These are first iterations
within an ordered matrix, **not OS-cache-flushed cold**, and no p95 is inferred
from three samples. This Store fixture/timing boundary differs from the older
pipeline fixture and its warmup protocol; do not infer a regression/speedup by
pooling it with628de6d timings.

One current5M read-only oracle passes **13/13**:12 full-canonical comparisons
plus cancellation. Selective/exact/no-match:12.8635/7.7811/1.844ms;1/1/0 indexed
rows versus5000000 canonical rows for those normal cells. Exact identities,
order, cap/truncation and actual candidate admission are checked. This is capped
cold equivalence, not fully-hot/uncapped/targeted5M acceptance.

Allocator-free ingestion stages at earlier clean **dab2128**, n=1:

| Rows | generation ms | CPU ms including open/close | peak process private B | database B |
|---|---:|---:|---:|---:|
| 200000 | 3150.36 | 3406.25 | 144457728 | 40247296 |
| 1000000 | 30713.1 | 29687.5 | 916172800 | 199659520 |

Peak fields are cumulative process measurements;1M teardown private5,685,248B.
The measured executable hash is26bb2c7a1c72c200193b000873790047af152e7630ace85821f1a7fbe17b876a.
These are not4c1c450 timings. Compiler MSVC19.44.35228, release-test
RelWithDebInfo/optimized flags; exact commands, executable hashes and deterministic
fixture/batch/budget details are preserved. Link maps resolve normal new/delete
to the CRT, not the allocation probe. Historical132.368s5M remains explicitly
allocator-instrumented, not production-uninstrumented timing.

Current5M allocator-free dispatch was refused by preflight at966,193,152B free,
exit3 **before any ingestion row**. Full5M ingestion and current3.1M-cliff runtime
are NOT_EXECUTED; no extrapolation. Structural eviction guards pass, but cannot
replace that measurement. Multi-volume/candidate-cap2M performance cells and the
same pipeline-fixture repeat matrix remain open under5.7.

## E. Gallery and isolated compatibility

- PR16 e75a230: independent source review OK-with-notes; focused3/3; standalone
  catalog20/20 rerun. Settings persistence/restoration follows the real shell.
- PR15 current269d1c3: catalog17/20; `gallery-dark`, `gallery-narrow`,
  `gallery-wide` still fail. The separate fix was NOT copied into PR15.
- Isolated latest combined source **6d3de0e8c9242315bad004cefc685f8f2f75b2b9**,
  tree **6ecbca7586b8044bc11ec1b389f41e0fa4149d6e**; local validation branch only,
  not pushed/merged. One textual tests/ui/CMakeLists conflict was resolved by
  retaining both clipboard and theme test registrations. Production UI patch
  applied without conflicts; UI/protocol/search/ipc/setup source equals PR16.
- Combined focused Debug3/3 and release-test3/3 pass. Full combined application
  builds, Gallery and UIA are NOT_EXECUTED with roughly1GiB free. Therefore
  full `PR16_COMPATIBLE_WITH_PR15` remains UNPROVEN; source/focused compatibility
  alone does not close5.12. Owner integration/exclusion is not fabricated.

Observed capture DPI is96/100%. The catalog's printed125% host sentence is
hardcoded/stale and is not evidence for physical125% qualification.

## F. Current certification

All following executions are bound to clean269d1c3, executable inputs equal4c1c450:

| Gate | Actual result |
|---|---|
| Debug build + full CTest |130/130 PASS |
| release-test build + full CTest |130/130 PASS |
| Analyze | clean, strict warnings retained |
| existing ASan slice |4/4 PASS |
| canonical local CI parity |11PASS /0FAIL /0SKIP |
| strict OpenSpec, governance, markers, diagnostic policy, drift, hygiene |PASS through parity; final docs strict93/93 and focused docs/diagnostic tests3/3 |
| E2E |8PASS /0FAIL /0SKIP |
| UIA core / visual matrix |25/25 each |
| full UIA capability matrix |23PASS /5SKIP /0FAIL |
| PR15 visual catalog including Gallery |17PASS /3FAIL |

The five UIA skips are two privileged-scan cells, two foreground-ownership
cells, and the inherited Explorer drop-delivery environment limitation. No
foreign application was forced out of the way. Earlier interrupted runs,
stack-warning failure, transient linker write-open errors and stale-binary
preflight rejection are retained separately, not erased or called PASS.
The final canonical current-source parity run passes on its own.

LSP sweep is not globally clean: standalone include-context findings and two
timeouts remain; MSVC C++20/W4/WX and Analyze are the authoritative passing
checks. Policy diagnostics passing is not a claim that every IDE probe passed.

Evidence: `recovery-20261024/final-current/`. SHA records distinguish executable
bytes, original execution artifacts and canonical Git text blobs. The hardened
executable-tree gate must run from4c1c450 to the actual final evidence HEAD after
the final commit. That gate proves only its derived input scope, not identical
binaries or a new execution for every docs SHA. The final verdict stays outside
the checked tree and in PR15/session handoff.

## G. OpenSpec and readiness

Lifecycle remains **active**. Execution4.3/5.11 is checked from current evidence.
Open: **4.1,4.2,5.7,5.10,5.12,5.14**. The hot/cold contract was not narrowed to
make a checkbox green; historical5.9 means recorded execution, not resource closure.

| Classification | Verdict |
|---|---|
| PRODUCT_CORRECTNESS |PASS_WITH_LIMITATION: tested repairs; final independent review unavailable |
| DATABASE_DURABILITY |PASS_WITH_LIMITATION: ordinary error matrix; excluded crash/large-resource cells |
| PROJECTION_MEMORY_SAFETY |PASS_WITH_LIMITATION: estimator convergence, not process-RSS cap |
| LARGE_SCALE_PERFORMANCE |PASS_WITH_LIMITATION at2M; allocator-free5M ingestion UNPROVEN |
| INDEPENDENT_REVIEW |BLOCKED_EXTERNAL for final code |
| LOCAL_CERTIFICATION |PASS_WITH_LIMITATION; local canonical gates pass, acceptance still open |
| VISUAL_CERTIFICATION |VERIFIED_FAIL for PR15 catalog; separate PR16 passes |
| HOSTED_CERTIFICATION |NOT_OBTAINED at prior inspected run; final push inspected once externally |
| MERGE_READINESS |VERIFIED_FAIL: required acceptance/dependency gates remain open |
| PRODUCTION_RELEASE_READINESS |NOT_EXECUTED / outside ordinary campaign |

## H. Owner actions and handoff

1. Restore independent-review availability; review final4c1c450 source and latest
   harness, not only pre-fix reports. No billing/security changes by this agent.
2. Provision safe disk headroom for full allocator-free5M ingestion, missing
   resource/performance/hot-equivalence cells and full combined builds. An8GiB
   planning allowance is prudent here, not a universal WAL/reserve guarantee.
   Do not delete unknown work or fill a drive to obtain evidence.
3. Decide Gallery integration/dependency or an explicitly governance-valid
   exclusion. PR14 is still PR15's base dependency; all merges remain owner-only.
4. If hosted execution still fails before steps, restore external infrastructure;
   local parity is not hosted certification. Inspect final pushed SHA once.
5. Excluded reliability/privileged/physical-display work requires a separate
   permitted validation plan. No platform screening override or production
   deployment is authorized.

No known newly reproduced data-integrity defect is left unfixed; that is not
independent approval or completion of unexecuted gates. The known PR15 Gallery
failure and the listed acceptance blockers remain explicit.
