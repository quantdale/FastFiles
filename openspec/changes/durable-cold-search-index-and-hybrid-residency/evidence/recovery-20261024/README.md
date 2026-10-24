# Acceptance recovery — first verified implementation slice

Intake PR15 HEAD: `8c4c8481f312aa4651a5c2bab3cb61e0b0e50af0`.
All five independent, read-only reviewers used fresh contexts on
`openai-codex/gpt-6.1-sol`; this is context independence, not cross-provider
agreement. Their complete source-inspection reports are preserved here.
No provider failure or fabricated approval occurred in this wave.

## Reproduced findings and narrow repairs

| Finding | Red evidence | Repair | Current focused result |
|---|---|---|---|
| A P1 whole-volume eviction performs quadratic sibling unlink | projection-red.log: structural comparison bound fails with20k siblings | erase doomed identities directly, rebuild surviving child links in existing compaction | projection suite PASS |
| A P2 unused free-list capacity defeats empty-projection budget | projection-red.log:20k tiny-victim fixture exceeds64KiB after enforcement | release unused free-list capacity at compaction | projection suite PASS |
| B P1 missing FTS table recreated empty yet ready | recovery-red.log: successful false-negative query, rebuild flag absent | check all required candidate/shadow tables BEFORE creation; force rebuild | new storage-pressure suite PASS |
| B P1 interrupted volume enumeration accepted as rebuild exhaustion | recovery-red.log: actual SQLite VM interrupt fires, Open incorrectly succeeds | require SQLITE_DONE; cross-check map/entry coverage before ready marker | new storage-pressure suite PASS |
| B P2 moved reader retains wrong-snapshot readiness | recovery-red.log: moved reader loses matching row | clear snapshot-specific readiness on Close/assignment | new storage-pressure suite PASS |
| inherited manager false completion after failed removal | manager-red.log: timestamp advances and retry owner absent | finish removals before successful terminal bookkeeping; retain bounded retry owner on removal/metadata failure | manager-green.log PASS |

Seven focused Debug CTests PASS in57.89s (`green-focused.log`), then manager
suite PASS separately (`manager-green.log`). This is NOT the final full
certification ladder or re-review of the repaired source.

## Storage pressure harness scope

New `ffengine_storage_pressure_tests` uses a forwarding default SQLite VFS
inside its own process. It returns SQLITE_FULL from an actual armed xWrite,
with statement/file/offset/byte metadata. Small test-only caches force spills.
No drive was filled, no durability setting weakened in production.

Cells: ordinary entry writes, candidate map, FTS-maintenance-associated
writes, removal writes, COMMIT (including FTS xSync), main-file checkpoint,
migration creation and rebuild. Failed mutations must return false, close
transactions, preserve authoritative tuples, pass raw-before-Store-repair
SQLite/FTS integrity and identity/name consistency, then retry successfully.
The xWrite statement identifies the active SQLite VM; a spill may flush a
page dirtied by another statement, so this does not assert btree-page-level
attribution for every write.

Pinned and overlapping read transactions preserve writer progress, report
bounded failed RESTART attempts, and recover after snapshot release. A
TRUNCATE probe verifies physical reclamation; production RESTART permits
file reuse and does not promise immediate physical shrinking. A cancelled
bounded Store reader releases its pin on teardown. No finite WAL bound is
claimed under indefinitely pinned readers.

Real crash class: parent TerminateProcess kills a separately launched child
at six acknowledged production reconciliation boundaries: before first
chunk, first uncommitted write, first committed chunk, later uncommitted
write, checkpoint main-file write, and final committed chunk before in-memory
bookkeeping. Expected raw counts are12001,12001,7001,7001,7001,1; candidate
consistency and integrity are checked before repair. Fresh pipeline objects
then reconstruct a NEW complete rescan and converge to the root. This is not
proof that an in-memory seen set survives restart, nor a power-loss test.
The normal manager scheduler can owe a new rescan after restart (up to its
six-hour cadence); immediate persistent repair is not claimed.

Initial harness targeting missed two VM/write boundaries and failed six
assertions; after correcting trace handling, the matrix passed. Those were
harness failures, not product data-loss findings. The later red regression
run deliberately reproduces the five independently found Store assertions.

## Ownership and resource constraints

Cleanup receipts record only generated, disposable databases. Original
`open-5m.db`, other original fixtures and all historical result artifacts are
preserved. A dead-PID interrupted fixture was identified by the exact
benchmark temp pattern, zero volume GUID, deterministic5M serial and generated
root/folder records before deleting its2.878GB WAL. Disk headroom remains a
preflight requirement, not an asserted reserve sufficient for every workload.

## Open acceptance

No merge is authorized. Tasks4.2/5.7/5.10/5.12/5.14 remain open. Task4.1 is
reopened because the governing hot/cold matrix is broader than the verified
capped cold-versus-canonical oracle. Task5.9 now records historical execution,
not completion of reopened acceptance. Benchmark race/false-success repairs,
allocator-free measurement, combined PR16 runtime qualification, final
re-review and the current full ladder remain subsequent work.
