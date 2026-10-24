# Remaining acceptance and operational limits

Current implementation: `4c1c4501e5b1f0579a2d34f4f7afea5f83708979`.
Current executed validation/measurement source: `269d1c36036cb84b40c3ef01baee3c2b6c990579`.
Lifecycle remains active. No requirement is silently relaxed.

| Requirement | Status / reason | Available evidence | Remaining work / merge impact |
|---|---|---|---|
| Fully-hot versus cold identity/order at all mandated scales, including5M | UNPROVEN; hot5M not executed | Current cold-versus-full-canonical2M13/13 x3 and5M13/13; hot/cold unit fixtures | Complete governing matrix without changing the500MiB product default;4.1 remains merge-blocking |
| Same pipeline-fixture2M repeat matrix, multi-volume selective and candidate-cap performance boundaries | NOT_EXECUTED / disk capacity | Current read-only Store-fixture2M repetitions; volume/cap correctness tests; automatic mixed-volume regression | Provision safe scratch capacity and run missing performance cells;5.7 open |
| Allocator-free final-source5M ingestion and current runtime crossing3.1M | NOT_EXECUTED; preflight rejected before any row ingestion | Current target/link-map binding; earlier dab2128 allocator-free200k/1M stages; structural20k eviction-work guard | More disk headroom; one controlled production-path5M run, not extrapolation.5.7/4.2 open |
| Large-scale pinned-reader WAL/resource growth | PASS_WITH_LIMITATION only at bounded fixture scale | Current2000-row reader/write passes, multiple readers, BUSY/backfill counters, cancellation teardown and reclamation | Larger operational characterization with capacity budget; no finite universal WAL cap claimed.4.2 open |
| Deliberate large-removal process termination / restart interruption | NOT_EXECUTED in this continuation; outside owner scope | Historical pre-interruption results explicitly retained; current ordinary chunk-two error/retry and real manager retry scheduling | Separate permitted validation plan for transaction/checkpoint/bookkeeping boundaries and rescan reconstruction; no restricted operation retried under another name.4.2 open |
| Physical disk exhaustion / privileged storage experiments | NOT_EXECUTED; prohibited/out of scope | Simulated SQLITE_FULL is returned from actual SQLite xWrite callbacks on disposable fixtures | No system-drive fill, live-process termination, privileged operation, user database mutation or platform-screening workaround |
| Final independent Store/manager/performance review | BLOCKED_EXTERNAL: provider usage limit | Five initial source reviews, projection re-review, red-first regressions and current passing tests | Reviewer availability restored; inspect final4c1c450 source, not only previous summaries.5.10/5.14 open |
| Combined PR15+PR16 full application Debug/release builds and runtime Gallery/UIA | NOT_EXECUTED / disk capacity | Combined6d3de0e source, focused Debug/release3/3 each; standalone PR16 catalog20/20 | Complete combined runtime qualification; no full compatibility claim.5.12 open |
| Gallery integration or explicit owner exclusion | UNPROVEN disposition; no authorization | Current PR15 catalog17/20; standalone PR16 fix20/20 | Owner decision under project governance; independent compatibility alone does not close5.12 |
| Hosted certification | NOT_OBTAINED at previous run; inspect final push once | Local canonical parity11/11, not hosted execution | Restore hosted infrastructure if final run again has zero steps; no billing or policy changes by agent |

## Minimum supported operational assumptions

- The500MiB projection budget and400MiB watermark govern an estimator/residency
  policy, not process RSS/private bytes. SQLite caches, temporary compaction,
  allocator overhead and other components require separate capacity.
- A single serialized writer commits canonical entries and derived candidates
  together. Failed writes must return failure; retries occur after the external
  condition is resolved. Current ordinary tests check rollback and consistency
  before an Open can repair derived state.
- Readers must normally finish or dispose their snapshots. Indefinitely pinned
  readers can keep WAL history alive. Checkpoint diagnostics expose result codes,
  frames/backfill and busy/error counts through Store/IndexPipeline APIs; this is
  not a new user-facing WAL-pressure dashboard or writer-admission limit.
- Maintenance retries at the existing cadence; there is no uncontrolled busy
  loop. A failed checkpoint does not undo committed WAL transactions. RESTART
  permits reuse; it does not promise immediate file truncation.
- Capacity must cover the workload's largest atomic operation and retained reader
  history. The observed1,668,632-byte WAL in the small pinned-reader test, prior
 648MB migration sample and960MB lifecycle sample are not universal bounds.
  Current free space around1GiB is insufficient for a defensible5M write run.
- Reconciliation seen sets are in-memory. After restart, correctness convergence
  requires a new complete rescan. Six hours is a due threshold, not a completion
  guarantee: polling, pause state, service availability and scan duration apply.
- Operator rollback is offline, backup-first and drops derived candidate state
  only. A fresh20k older-store smoke preserves every entry tuple; it does not
  retroactively supply missing executable binding for historical5M output.

No hardware, production signer/pins, trusted-root provisioning or live privileged
MFT/USN validation was initiated. Those remain outside ordinary development.
