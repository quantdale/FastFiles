# Audit Findings — Next-Campaign Selection

## Audit scope

Planner inspection covered the current repository/control-plane tree and the major first-party product modules: engine, index store, privileged index service, IPC, protocol, search, installer/setup, UI, tests, verifier, OpenSpec, agent handoff state, recent commits, and hosted-run state. Historical audit findings were treated as leads only and were not promoted unless revalidated against current code/state.

## Finding F1 — High-value local frontier: eager resident candidate ownership

**Priority:** High / performance-architecture

The hot resident indexed-search loop eagerly converts every examined projection name to `std::wstring` and builds an owned `Candidate` while holding `IndexPipeline::mutex_`, before the canonical matcher rejects most rows. The previous matcher campaign made predicate execution substantially cheaper, increasing the relative importance of this capture/materialization cost.

This is directly adjacent to existing intended architecture: `CandidateView`, view predicates, `ExecuteSearchViews`, per-row ASCII classification, filename candidate indices, and detailed materialization/lock counters already exist.

## Finding F2 — Safety blocker: current interned-name views are not stable across mutation

**Priority:** Critical invariant for F1

`NamePool` stores all UTF-16 code units in one `std::vector<char16_t>`. `Intern` appends and may reallocate. Projection memory compaction constructs a replacement pool and moves it into place. Therefore simply feeding `CandidateView` outside the projection mutex would create a dangling-view hazard. This must be solved with explicit stable storage/ownership or avoided with a measured bounded-copy alternative.

## Finding F3 — Existing specification forbids the obvious unsafe shortcut

**Priority:** Critical invariant for F1

The canonical `indexed-read-plane` specification requires long-running matching, path reconstruction, sorting, and response preparation outside the mutation/lifecycle critical section. Moving all view predicates under the mutex to avoid lifetime work would violate that contract and could regress ingestion/publication responsiveness. Any optimized capture must remain bounded.

## Finding F4 — Provenance campaign control-plane state was stale after successful implementation

**Priority:** Medium / repository truth

The committed-tree provenance OpenSpec tasks are fully checked, implementation commits exist, and hosted CI run `32956387761` succeeded for corrective SHA `35deafe13e601b82bc88fc2e893f0df0b788a5f4`; nevertheless the old executor prompt remained ACTIVE and lifecycle metadata omitted the change. This planner reconciles that state rather than asking the next executor to rediscover an already-completed campaign.

## Finding F5 — Lower-priority technical debt: `Store::cachedPageSize_` synchronization

**Priority:** Low / latent concurrency

The workstate records `Store::cachedPageSize_` as unsynchronized mutable state in `CheckpointIfWalExceeds`, safe under current single-threaded maintenance usage but technically racy if that call becomes concurrent. Do not expand the next campaign around this unless live call-graph reconciliation shows the concurrency assumption has changed. Add a focused issue/task only if independently reproduced.

## Areas explicitly not selected as the next campaign

- **MFT/USN parsers:** recently inspected; field parsing is already bounded/tight and privileged production validation remains external.
- **VolumeScanner:** dominant cost is per-record filesystem control I/O; no local micro-optimization was justified.
- **Snapshot serialization/publication:** current two-pass/zero-copy design was recently audited; a serializer capacity-contract regression was already found and fixed.
- **SQLite ingestion:** prepared statements and batched transactions are already present; no measured new bottleneck was found.
- **Degraded enumeration:** already uses buffered enumeration; no evidence for another rewrite.
- **Parallel hot-row scanning:** explicitly deferred because it raises mutation/epoch complexity and simple literals already benefit from the filename accelerator. Reconsider only after the single-threaded materialization frontier is resolved.
- **PCH/build-time optimization:** developer-productivity work with uncertain payoff; requires a separate measured trial.
- **UI monolith decomposition:** `ColumnView`/UIA files are large, but file size alone is not a production defect. No measured correctness/performance frontier justified making decomposition the next campaign.
- **Production signing, privileged runtime, provider/guest, clean-host, physical-display UIA:** genuine external evidence boundaries, not local implementation backlog to cosmetically close.

## Next-campaign verdict

Activate `indexed-search-zero-copy-hot-path-and-lock-pressure-convergence` as the sole new local campaign. It has a concrete hot path, existing instrumentation, existing architectural seams, deterministic correctness tests, and a clear stop condition if safe lifetime engineering does not produce a material measured benefit.
