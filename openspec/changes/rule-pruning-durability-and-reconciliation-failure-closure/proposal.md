## Why

Secondary rule-pruning durable writes are not propagated consistently with primary ingestion commits. Failed secondary `Store::ApplyBatch(ruleExcluded)` currently skips projection removals and continues through epoch advancement returning success, and reconciliation's failed removal still erases `reconciliationSeen_` discarding the repair obligation. This can leave SQLite and the resident projection divergent, with excluded rows remaining searchable until the next successful pruning pass or restart.

## What Changes

- Classify the intended contract for secondary rule-pruning writes across MFT, USN, rebuild/open, and reconciliation: consistent fail-closed vs explicit eventual-recovery with retained obligation, based on current `Store::ApplyBatch` rollback/ retry semantics and existing OpenSpec.
- Introduce deterministic writer fault injection at named secondary transaction boundaries (production defaults unchanged; prefer an injectable seam over flaky filesystem tricks).
- Make MFT `ApplyMftBatchChanges`, USN `ApplyUsnBatchChanges`, `RebuildAll` rule-pruning, and `FinishReconciliationPass` behave consistently with the chosen contract: failure is not reported as success while leaving an untracked repair obligation; reconciliation obligation is retained when durable removal fails.
- Add regression coverage for first failure, retry/recovery, repeated failure, cancellation/shutdown interaction, and restart, verifying durable state, projection state, search/snapshot/aggregate visibility, epoch publication, and next-reconciliation behavior.
- No new persisted schema, protocol wire shape, or production-signing change.

## Capabilities

### New Capabilities

- `index/rule-pruning-durability`: durable rule-exclusion pruning failure propagation and reconciliation-obligation retention.

### Modified Capabilities

- `index-storage-and-scanning`: `IndexPipeline` MFT/USN/rebuild/reconciliation batch-commit ordering and error propagation.
- `indexed-read-plane`: generation/epoch publication behavior for rule-pruned mutations and reconciliation failures (no new spec file, behavior clarified).

## Impact

- Correctness: pruned rows cannot remain visible after a reported success; failed secondary writes retain a bounded retry/reconciliation obligation.
- Performance: no per-record overhead; secondary `ApplyBatch` remains batched at the batch/reconciliation boundary.
- Security: no new privilege or IPC surface.

## Open Questions

- None; contract to be derived from `Store::ApplyBatch` transactional documentation and callers before implementation.
