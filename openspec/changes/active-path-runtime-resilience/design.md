## Context

The current `main` now includes the validated Phase 8–10 indexed read-plane
and search-availability train. The earlier `recovery-degraded-browsing`
change already bounds watcher roots, recovers notification-buffer overflow,
surfaces engine state, and protects snapshot freshness in isolated tests. The
remaining risk is composition: filesystem churn, publication failure,
disconnect/reconnect, and supersession can overlap across worker lifetimes.

The authoritative architecture remains the three-process split described in
`openspec/changes/establish-architecture-foundation/design.md`. The service is
not part of this change's implementation surface. Degraded browsing remains
the supported path and must work without service authentication or elevation.

## Goals / Non-Goals

**Goals:**

- Prove and correct only current, reproducible cross-lifecycle gaps.
- Preserve last-known-good data with an explicit freshness state when a new
  snapshot cannot be committed.
- Ensure watcher, engine, UI, and search ownership has deterministic shutdown
  and stale-result suppression.
- Make failure outcomes diagnosable without logging personal content.
- Add a marked-fixture verification capability and bounded resource checks.

**Non-Goals:**

- No replacement of `DirectoryWatcher`, `IndexPipeline`, `SnapshotPublisher`,
  `StoreReadSession`, or the existing UI freshness model.
- No generic Win32 mock framework, broad runtime polymorphism, or architecture
  rewrite.
- No service command, authentication, ACL, signing, protocol layout, or
  snapshot binary-format relaxation.
- No production trust-store mutation, elevated clean-host proof, physical
  display matrix, or guest-provider fabrication.

## Decisions

### D1 — Treat the existing lifecycle seams as the test boundary

**Why:** The repository already has narrow `FF_*_TESTING` hooks for snapshot
mapping and directory-watcher completions, plus pure freshness and search
state seams. Extending those hooks keeps failure injection deterministic and
keeps production ownership code on the same path used by tests.

**Choice:** Add only the smallest hook needed to reach a real native failure
edge. Prefer a completion/error provider, condition-variable barrier, or
move-only test dependency over a general interface or sleep-based fake.

**Alternative rejected:** A process-wide mock filesystem would test a second
implementation and could hide Windows handle/overlapped behavior.

### D2 — Preserve atomic snapshot publication and freshness semantics

**Why:** `SnapshotPublisher` already initializes an immutable generation before
the commit marker and `SnapshotFreshness` already distinguishes current,
stale, refreshing, and unavailable states. Duplicating generation state would
make reconnect races harder to reason about.

**Choice:** Keep generation IDs monotonic within a publisher context, retain
the previous mapping until a replacement is fully committed, and route
publication failures through the existing freshness/error path. Test context
identity separately from numeric generation so an engine restart cannot reuse
an old mapping.

**Alternative rejected:** Changing the snapshot binary layout or making one
mutable shared mapping would expand compatibility risk and weaken the current
commit-marker guarantee.

### D3 — Make cancellation and supersession identity-based

**Why:** The indexed read plane already uses bounded cancellation and coherent
durable snapshots, while UI search/preview paths use request identities. A
late callback is safe only when it proves that its request, scope, connection,
and accepted snapshot context still match.

**Choice:** Add deterministic sequence/generation assertions around existing
request identities and cancellation tokens. A result that fails validation is
discarded and diagnosed as stale; it is not converted into a successful empty
result.

**Alternative rejected:** Delaying callbacks or adding arbitrary retries does
not establish ordering and would mask the race.

### D4 — Use explicit recovery outcomes for watch retirement

**Why:** A watch can legitimately stop after path loss or repeated overflow,
but a silent stop leaves old rows ambiguous. The earlier recovery change's
final-refresh behavior is retained; this campaign fills only remaining paths
where callback ownership, rewatch, or terminal state is not observable.

**Choice:** On every non-shutdown terminal path, choose recovery, one final
refresh, or explicit unavailable state. Emit bounded structured diagnostics
with a normalized path identity and Win32 error, never file contents.

**Alternative rejected:** Automatic infinite rewatching could create a hot
loop against a permanently inaccessible path and would make the UI appear
healthy without current data.

### D5 — Make verification a layered capability

**Why:** Engine/headless state machines are deterministic and available in
ordinary development environments; GUI foreground, Hyper-V, signing, and
clean-host checks are not. A single all-or-nothing runtime test would either
hide useful failures or fabricate availability.

**Choice:** Run headless fixture churn and convergence unconditionally when
its prerequisites are present, then report desktop/restart/resource rows
individually as PASS, FAIL, SKIPPED, or REQUIRED-BUT-UNAVAILABLE. Use a fixed
seed and a configurable bounded duration, and clean only an independently
marker-validated root plus exact owned PIDs.

**Alternative rejected:** A global retry loop or an environment-dependent
pass threshold would violate the repository's flaky-test policy.

### D6 — Keep diagnostics additive and local to existing policy

**Why:** Diagnostic codes and categories already flow through the shared
protocol diagnostics policy. New states should be machine-classifiable without
creating a second free-form logging vocabulary.

**Choice:** Reuse existing watcher/snapshot/engine/search diagnostic codes and
extend the enum only if an actual distinct state cannot be represented. Any
additive code remains backward-tolerant and is covered by the diagnostic
policy/parser tests.

**Alternative rejected:** Logging raw paths, query text, or arbitrary payloads
would make runtime evidence privacy-sensitive without improving recovery.

## Risks / Trade-offs

- **[Risk]** Fault hooks can accidentally become production behavior. → Keep
  them behind existing test-only compile definitions, register them only on
  test targets, and run source-marker/diagnostic-policy guards.
- **[Risk]** A final refresh can be expensive or race a recreated path. → Keep
  it bounded, use the existing refresh/publish path, and validate path/context
  identity before applying its result.
- **[Risk]** Last-known-good rows can be mistaken for current rows. → Preserve
  explicit freshness/unavailable state and assert the state in headless tests.
- **[Risk]** Resource samples vary under host load. → Report deltas and
  monotonic growth/cleanup failures with existing tolerances; do not impose
  byte-for-byte flatness or blind retries.
- **[Risk]** The verification capability may be unavailable on a noninteractive
  or unelevated host. → Keep headless rows meaningful and mark only the
  unavailable optional rows honestly.

## Migration Plan

1. Land the focused tests and any minimal production corrections on `main`.
2. Run focused Debug tests, then Debug/release-test/analyze/ASAN and policy
   gates before accepting each implementation wave.
3. Run the resilience capability against a marked disposable fixture and keep
   raw interactive artifacts outside tracked evidence.
4. Update current status/workstate documents with exact live Git and test
   results while leaving historical audit records immutable.
5. Rollback is a normal Git revert of the focused implementation commit; no
   database, service, trust-store, or snapshot-format migration is required.

## Open Questions

None. Environment-dependent desktop/provider rows are runtime outcomes, not
design decisions, and will be reported by the capability contract.
