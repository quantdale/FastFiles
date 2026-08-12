## Context

The engineering audit of commit 52d33da0 identified 2 CRITICAL, 4 HIGH, 5 MEDIUM, and 2 LOW findings. The product currently operates exclusively in degraded mode because the privileged MFT/USN path is blocked by placeholder Authenticode pins. Additionally, engine discovery launches executables without signature verification, FlatHashMap can infinite-loop at 100 occupancy, SnapshotPublisher has an ARM64 publication-safety race, and the SQLite store has no size management.

This change addresses the highest-severity findings to unblock production readiness while preserving the existing three-process constrained-broker architecture.

## Goals / Non-Goals

**Goals:**
- Make the privileged path functional by provisioning real Authenticode pins or removing the broken authn code with clear degraded-mode documentation.
- Close the engine-discovery executable-planting vulnerability with signature verification.
- Fix the deterministic FlatHashMap infinite-loop bug.
- Fix the SnapshotPublisher ARM64 memory-ordering race.
- Replace the ServiceConnection busy-wait with event-driven I/O.
- Add SQLite VACUUM/ANALYZE scheduling and projection eviction to bound resource usage.
- Add snapshot format versioning and structured diagnostics.

**Non-Goals:**
- Changing the three-process architecture or named-pipe IPC seams.
- Adding network or cross-platform support.
- Rewriting the UI rendering layer.
- Implementing full ETW tracing or screen-reader automation (deferred to backlog).

## Decisions

### D1: Authenticode Pin Provisioning Strategy

**Decision:** Replace placeholder pins with build-time-injected, environment-specific pin files. Debug builds use a pins-dev.json checked into the repo; release builds use pins injected by the CI signing step.

**Rationale:** Hardcoding production pins in source is a secret-management risk. Build-time injection keeps pins out of the public repo while allowing debug/testing with known dev pins. The previous all-zero placeholder approach gave false confidence that authn was implemented.

**Alternatives considered:**
- Hardcode real pins in source - rejected because it exposes thumbprints in git history and makes rotation painful.
- Remove authn entirely and document degraded mode - rejected because it removes the security boundary entirely rather than fixing it.

### D2: Engine Discovery Launch Hardening

**Decision:** Require Authenticode signature verification before launching any engine executable. The UI and engine discovery paths both verify the candidate thumbprint against the same pinned set used for mutual authn. If verification fails, the launcher falls back to the registered scheduled-task activation path.

**Rationale:** The current directory-scan approach is a classic executable-planting vector (CWE-426). Signature verification is the minimal fix that preserves the existing discovery mechanism while closing the gap.

**Alternatives considered:**
- Hardcode the engine path and remove discovery - rejected because it breaks per-user installs and dev scenarios.
- Use only the scheduled task - rejected because the task may not be registered yet (first run).

### D3: FlatHashMap Grow Trigger

**Decision:** Grow the hash map when load factor exceeds 75% (matching std::unordered_map behavior). Add a maximum probe distance of 64 as a safety break.

**Rationale:** 75% is the industry standard for open-addressing hash maps. The max probe distance prevents pathological infinite loops even if the grow logic has a bug.

**Alternatives considered:**
- Grow at 100% - rejected because it is the current buggy behavior.
- Use std::unordered_map - rejected because the custom map exists for performance and memory-layout reasons.

### D4: SnapshotPublisher Memory Ordering

**Decision:** Make generation_ an std::atomic<uint64_t> with memory_order_release on write and memory_order_acquire on read. Add a static assert that sizeof(std::atomic<uint64_t>) == sizeof(uint64_t) to guarantee no hidden locking overhead.

**Rationale:** std::atomic with explicit ordering is the portable, correct way to synchronize shared-memory publication. It works on x86, ARM64, and future architectures without conditional compilation.

**Alternatives considered:**
- Keep implicit x86 ordering and add an #ifdef _M_ARM64 barrier - rejected because it is fragile and easy to forget on new architectures.
- Use a mutex - rejected because it adds contention on the hot path.

### D5: ServiceConnection Async I/O

**Decision:** Replace the PeekNamedPipe + Sleep(2) busy-wait with ReadFile overlapped I/O and GetOverlappedResult with a timeout. If overlapped I/O is not available (e.g., tests), fall back to WaitForSingleObject on the pipe handle with a 100ms timeout.

**Rationale:** Overlapped I/O is the standard Win32 pattern for scalable pipe reads. It eliminates the busy-wait and reduces CPU usage to near-zero when idle.

**Alternatives considered:**
- IOCP - rejected as overkill for the single-pipe engine use case; adds complexity without measurable benefit.
- Keep polling with exponential backoff - rejected because it still wastes CPU and adds latency jitter.

### D6: SQLite Store Maintenance

**Decision:** Run ANALYZE after every bulk insert batch (every 10,000 records). Run VACUUM during engine idle time, gated by a configurable interval (default: 24 hours) and a size-threshold check (only if the DB grew by >10% since last VACUUM).

**Rationale:** ANALYZE is cheap and keeps the query planner accurate. VACUUM is expensive but necessary to reclaim space; gating it by size and interval avoids unnecessary I/O.

**Alternatives considered:**
- Auto-VACUUM pragma - rejected because it incurs overhead on every transaction and is not suitable for high-throughput bulk inserts.
- Never VACUUM - rejected because the store grows without bound on long-lived systems.

### D7: Projection Eviction

**Decision:** Add a configurable maximum projection size (default: 500 MB). When the limit is exceeded, evict the least-recently-accessed directory entries until the projection is under the limit. Evicted entries are reloaded from SQLite on next access.

**Rationale:** Bounding memory usage is necessary for 8 GB-class systems indexing large volumes. LRU is simple and effective for the typical access pattern (user browses recent directories).

**Alternatives considered:**
- No eviction (current behavior) - rejected because it leads to OOM on large volumes.
- TTL-based eviction - rejected because directory data does not have a natural expiration.
- Full projection offload - rejected because it would require a major rewrite of the snapshot publication path.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Real Authenticode pins are not available before release | Ship with a clearly documented development mode that bypasses pin verification, with a prominent warning. Do not ship placeholder pins. |
| Signature verification slows engine launch | Cache the verified thumbprint in memory for the session; only re-verify on engine restart. |
| VACUUM during idle causes I/O spikes | Gate VACUUM behind user-idle detection (no UI input for 5 minutes) and a size threshold. |
| Projection eviction causes UI jank on reload | Prefetch evicted directories asynchronously when the user hovers or scrolls near them. |
| Atomic generation_ adds memory overhead | std::atomic<uint64_t> is 8 bytes; the shared-memory header is already 64-byte aligned, so this is negligible. |
| Overlapped I/O in ServiceConnection requires refactor | The pipe handle is already opened with FILE_FLAG_OVERLAPPED in the service; only the engine side needs changes. |

## Migration Plan

1. **Branch:** `audit-findings` from `main` at `52d33da0`.
2. **Phase 1 (P0):** Authenticode pins, engine discovery verification, FlatHashMap fix, IPC integration tests. Merge to `main` after sign-off.
3. **Phase 2 (P1):** SnapshotPublisher barriers, ServiceConnection async I/O, SQLite maintenance. Merge to `main`.
4. **Phase 3 (P2):** Projection eviction, snapshot versioning, structured diagnostics. Merge to `main`.
5. **Rollback:** Each phase is independently releasable. If a phase causes regressions, revert only that phase commit.

## Open Questions

1. **Q:** Where should the real Authenticode pins be stored for release builds?
   **A:** TBD. Options: (a) CI-injected resource, (b) secure bootstrapper, (c) signed config file. Needs input from the build/release team.

2. **Q:** Should the engine discovery fallback to scheduled-task activation be immediate or retry directory scan first?
   **A:** Immediate fallback is safer (no window for executable planting), but scheduled task activation is slower. Need to measure startup latency impact.

3. **Q:** What is the acceptable maximum projection size for low-end hardware (4 GB RAM)?
   **A:** TBD. Should be a user-configurable setting with a safe default.
