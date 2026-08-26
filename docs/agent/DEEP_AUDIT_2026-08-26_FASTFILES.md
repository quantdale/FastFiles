# FastFiles Deep Audit — Next-Campaign Evidence

Date: 2026-08-26 (Asia/Manila)
Audited-Head: `9cec4f06ca3dae652b05d13615cb8ef2f7e13ea2`
Target-Branch: `main`
Purpose: determine the next locally actionable FastFiles engineering frontier and prepare a 12-hour autonomous execution handoff.

## Executive verdict

The current active OpenSpec, `indexed-search-zero-copy-hot-path-and-lock-pressure-convergence`, remains the strongest confirmed locally actionable frontier on this audited tree. The resident indexed-search hot path still constructs an owning search candidate and copies the interned filename while holding the engine state mutex before canonical matching rejects most examined rows. The repository already has view-based matching machinery, but a naive borrowed-view optimization is unsafe because `NamePool` stores UTF-16 data in a movable `std::vector<char16_t>` arena and `Projection::CompactStorage()` can replace that pool wholesale. Search generation/epoch validation protects semantic freshness; by itself it does not extend pointer lifetime.

The active design correctly requires matching to remain outside the mutation critical section. Therefore the next implementation should not simply return `CandidateView` objects that point into mutable projection storage and should not move long-running matching under `IndexPipeline`'s state mutex.

A second, potentially correctness-relevant seam was identified in ingestion/reconciliation failure handling: rule-pruning is a second durable mutation after initial MFT/USN persistence. Current `IndexPipeline` logic appears able to report the ingestion operation as successful when the subsequent durable removal of rule-excluded rows fails. Related cleanup/rebuild paths have similar second-mutation behavior. This requires contract review and deterministic fault injection before classification; it is queued as the first post-search campaign investigation, not asserted here as a confirmed production defect.

No stale historical audit finding is automatically re-opened by this report. Historical reports are pinned to older SHAs and must be reproduced against the current tree before implementation.

## Audit method and coverage

This planner pass used the live GitHub tree and current repository-native documentation/specification rather than treating old reports as current truth.

### Tracked-tree inventory

The repository's recursive tracked tree was enumerated and classified across:

- root build/policy/configuration files;
- `.agent` planner/executor state;
- OpenSpec lifecycle, specs, active/completed changes;
- documentation and durable workstate;
- `src/protocol`;
- `src/ipc`;
- `src/setup` / trust and installation primitives;
- `src/indexsvc` privileged service;
- `src/engine` read/write/index/search coordination;
- `src/indexstore` SQLite + projection/name/index structures;
- `src/search` query and matching machinery;
- `src/ui` shell/state/interaction surfaces;
- `src/installer`;
- tests and benchmarks, including engine/index/search/security fixtures;
- verification/provenance automation;
- vendored dependencies.

The maintained `CODE_INDEX.md` was cross-checked against the current source topology. Critical runtime seams and the active frontier were then inspected at source level, including `IndexPipeline`, `Projection`, `NamePool`, `FlatHashMap`, `Store`, the search/query ownership boundary, current engine tests, OpenSpec contracts, and autonomous verification policy.

This is a repository-wide audit and tracked-file inventory, but it is not a claim that every line of every vendored/generated/historical document was manually re-read. Vendored SQLite was treated as an external dependency and historical audit documents as evidence leads. The downstream 12-hour executor is explicitly required to re-enumerate the live tracked tree after pulling and to investigate any path changed since this audit.

## Current architecture truth

FastFiles remains a Windows-native C++20 file manager with a deliberate three-process split:

1. `FastFilesIndexSvc` — privileged LocalSystem broker/service for raw-volume indexing work.
2. `FastFilesEngine` — unprivileged index/search coordinator and durable/read-plane owner.
3. `FastFiles` — UI process.

The repository preserves two named-pipe seams and a zero-IPC keystroke-search architecture through generation-named immutable shared-memory snapshots. The index engine supports both resident projection search and cold SQLite-backed read sessions when data is evicted/unavailable in the resident projection. Search work is chunked/cancellable and generation-aware.

The verification stack is substantial: focused C++ CTest suites, large-index/search benchmarks, signing/authentication fixtures, Debug/release-test/Analyze builds, ASAN slices, OpenSpec validation, repository verifier scripts, committed-tree/provenance checks, and hosted CI evidence.

## Confirmed finding A — resident indexed-search ownership remains expensive

### Evidence

`IndexPipeline`'s resident search path captures rows under its state mutex. The current capture/materialization path constructs an owning `ff::search::Candidate` and copies the filename from `Projection::Names().Get(entry.nameId)` into an owning wide string before canonical matching executes outside the lock.

This means rows that will be rejected by a cheap filename/metadata predicate still pay for candidate construction and name ownership while the critical section is held.

The current workstate already records a major prior matcher optimization: ASCII-fold-aware matching materially improved mixed-case search and fixed a latent anchor bounds bug. That makes ownership/materialization the next plausible local hot-path cost rather than re-optimizing the matcher itself.

### Why the obvious fix is unsafe

`NamePool` stores characters in a contiguous `std::vector<char16_t>` and exposes `std::u16string_view` into that arena. Growth can reallocate the arena. `Projection::CompactStorage()` builds a new pool and swaps/replaces storage. A view surviving either event is invalid regardless of whether a semantic generation integer later tells the caller that the search should restart.

Projection cursors/indices likewise have explicit mutation-lifetime assumptions. Any zero-copy design must establish actual ownership/lease/generation-lifetime guarantees, not merely compare epochs after dereferencing stale memory.

### Required campaign decision

Proceed with the existing OpenSpec only after current baselines are captured. Compare the design alternatives already identified there (stable/slab-like ownership, generation-owned immutable storage, bounded lightweight capture). Keep an invasive implementation only if measurements prove a material useful win with parity and bounded memory/lock behavior.

Severity: performance/concurrency frontier, not a known correctness defect.
Confidence: high.
Priority: first active implementation campaign.

## Candidate finding B — rule-pruning second-write failures can be hidden

### Observed pattern

MFT and USN ingestion conceptually performs:

1. persist the incoming batch to `Store`;
2. update the projection;
3. evaluate volume rules;
4. collect newly rule-excluded records;
5. perform a second `Store::ApplyBatch({}, ruleExcluded)` removal;
6. remove those rows from the projection only when that second durable mutation succeeds.

On the audited source, failure of the second removal batch is not clearly propagated through the main ingestion method's success result. Related reconciliation/rebuild cleanup also performs secondary removals whose failure handling is weaker than the first durable mutation.

### Why this matters

Depending on intended contract, a failed second removal can create a state where:

- the caller believes ingestion succeeded;
- rule-excluded records remain durable;
- the projection may retain rows that should have been pruned;
- search-time `Allows(...)` checks can mask the problem for indexed search while other consumers (snapshot/browse/aggregate/rebuild) may observe different state;
- reconciliation may eventually repair the row, but only if that eventual repair is guaranteed and the failed obligation is not forgotten.

### Why it is not yet labeled a defect

The repository may intentionally define rule pruning as eventually reconciled rather than transactionally coupled to initial ingestion. Current tests cover reconciliation, restart equivalence, cold mutations, eviction, search rules, and durable state broadly, but the inspected pipeline suite does not establish a deterministic fault-injection contract for this second mutation.

### Required next step

After Campaign 1, unless a higher-severity live issue appears:

- locate the canonical OpenSpec rule/volume/index-store contract;
- identify all MFT/USN/rebuild/reconciliation second-mutation callers;
- add an injectable deterministic failure at the second `ApplyBatch` boundary;
- assert durable state, projection state, browse/snapshot/search visibility, return status, recovery obligation, restart behavior, and next-reconciliation behavior;
- only then classify/fix.

If fail-closed/atomic pruning is required, create a bounded OpenSpec such as `rule-pruning-durability-and-reconciliation-failure-closure`. If eventual reconciliation is intentional, specify it explicitly and prove bounded recovery instead of imposing an unrequired transactional rewrite.

Severity: potential durability/correctness issue pending contract proof.
Confidence: medium-high that the failure-propagation seam exists; unclassified impact until fault injection.
Priority: first post-Campaign-1 investigation.

## Candidate finding C — storage/lifetime accounting deserves re-check if Campaign 1 changes it

`NamePool` stores offsets/lengths in 32-bit fields while source sizes originate as `size_t`. Under normal configured projection memory ceilings this is unlikely to be reachable, but any new stable-storage/generation-retention design must keep the bound explicit rather than relying on practical memory size alone.

`NamePool::MemoryUsageBytes()` is intentionally approximate. Hash buckets contain per-hash vectors and allocator overhead that are difficult to account exactly. If Campaign 1 retains generations/slabs or changes dedup layout, re-measure the effective projection ceiling under adversarial large-name workloads so memory-bound behavior remains real.

Severity: low unless storage design changes expose a reproducible bound/accounting failure.
Priority: conditional.

## Tests and assurance surfaces reviewed

The current test topology includes, among other coverage:

- MFT/USN ingestion and metadata persistence;
- restart/rebuild equivalence;
- reconciliation missed-delete correction;
- USN delete and partial-update semantics;
- volume lifecycle/forget behavior;
- indexed search scopes, filters, sorting, caps, case handling;
- cancellation/supersession;
- allocated-size propagation/filtering;
- forced projection eviction with resident/cold equivalence;
- repeated cold searches without reload/eviction thrash;
- post-eviction insert/rename/move/delete + restart equivalence;
- cold folder aggregates;
- cold-stream cancellation;
- large-index and secondary-filename-index benchmarks;
- authentication/signature fixtures and other engine hardening suites.

The active search OpenSpec already calls for additional adversarial lifetime, compaction, churn, Unicode, cancellation, and memory stress coverage. Do not create a parallel disconnected test harness when an existing benchmark/suite can be extended.

## Historical findings and external boundaries

The old engineering audit report is pinned to an earlier SHA and includes findings that subsequent campaigns have changed or closed. Treat it as a forensic map only. Reproduction on current code is mandatory before reopening an old issue.

The following remain external/environment-sensitive boundaries and must not be faked or weakened merely to make the 12-hour campaign look complete:

- production signer/pin evidence;
- privileged live MFT/USN evidence requiring the proper host/environment;
- provider/guest matrices;
- clean-host qualification;
- physical-display / foreground-owned UIA evidence;
- other lifecycle entries explicitly marked external-evidence-only.

Local work can improve harnesses and evidence collection for these boundaries, but cannot manufacture a PASS.

## Priority order for the next 12 hours

1. Execute or evidence-reject `indexed-search-zero-copy-hot-path-and-lock-pressure-convergence`.
2. Perform full exact-tree validation and hosted-CI reconciliation for Campaign 1.
3. Investigate rule-pruning/secondary-durable-mutation failure closure with deterministic fault injection.
4. If confirmed, create and execute a dedicated bounded OpenSpec repair.
5. Otherwise, re-audit current mutation/read-plane failure propagation, then security/IPC regressions, UI/engine state-machine coverage, and verification blind spots.
6. Continue bounded OpenSpec cycles until the 12-hour campaign deadline; never force bad code just to fill time.

## Planner handoff

The executable orchestration contract is `.agent/NEXT_CAMPAIGN_12H.md`. The existing `.agent/EXECUTION_PROMPT.md` and active OpenSpec remain the detailed Campaign 1 implementation instructions. At Campaign 1 completion/no-change closure, control returns to the 12-hour frontier loop rather than terminating the autonomous session.
