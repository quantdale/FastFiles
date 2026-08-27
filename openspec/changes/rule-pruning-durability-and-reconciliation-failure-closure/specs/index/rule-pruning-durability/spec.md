# Spec: Rule-Pruning Durability

## Purpose

Ensure rule-excluded rows are durably pruned with consistent failure propagation and that reconciliation retains its repair obligation until durable removal succeeds.

## ADDED Requirements

### Requirement: Secondary rule-pruning writes are not reported as success when durable removal fails

The system MUST NOT report a successful MFT, USN, rebuild, or reconciliation mutation while leaving an untracked durable-pruning obligation when the secondary `ApplyBatch` for `ruleExcluded` removals fails.

#### Scenario: MFT batch with failed rule-pruning durable write
- **WHEN** an `MFT` batch upserts rows and the follow-on secondary batch removing rule-excluded rows fails durably
- **THEN** the visible result MUST NOT claim success with prunable rows still present; either the call returns failure for retry or a retained obligation ensures the rows are removed before the next publication/epoch advance and no later step discards the obligation

#### Scenario: USN batch with failed rule-pruning durable write
- **WHEN** a `USN` batch upserts rows and the secondary rule-pruning removal batch fails
- **THEN** the same guarantee holds as for `MFT`: no success with untracked pruning debt

#### Scenario: Rebuild/open rule pruning failure
- **WHEN** `RebuildAll` rebuilds from the durable store and the secondary rule-pruning pass fails durably
- **THEN** the rebuild MUST NOT complete as if pruning succeeded; the excluded rows remain tracked for the next pruning attempt

#### Scenario: Reconciliation durable removal failure retains obligation
- **WHEN** `FinishReconciliationPass` computes removals for not-seen or rule-excluded rows and the `ApplyBatch(removals)` fails
- **THEN** `reconciliationSeen_` MUST NOT be erased for that volume/epoch in a way that discards the current repair obligation; the next reconciliation or retry must still observe the same deletions and epoch publication must reflect that no searchable mutation occurred for that failed pass unless durable state was actually changed

### Requirement: Deterministic secondary-write fault injection

The system MUST expose an injectable seam for secondary durable writes at named boundaries (MFT rule-pruning batch, USN rule-pruning batch, rebuild rule-pruning batch, reconciliation removal batch) with production defaults unchanged, so the above scenarios can be reproduced without relying on flaky file-system/SQLite-lock tricks.

#### Scenario: Injected secondary failure at each boundary
- **WHEN** a test installs a fault injector that forces the named secondary `ApplyBatch` to fail exactly once
- **THEN** the call under test MUST handle the failure per the contract above and the next operation/restart/retry MUST observable recover

### Requirement: Observable projection and durable divergence is exposed

After any secondary-write failure, durable SQLite state, resident projection state, search results, snapshot/browse results, and folder aggregates MUST remain consistent with the contract: either the excluded rows are absent from both or the failure is reported and the rows remain consistently present in both until the retry succeeds.

#### Scenario: Search and snapshot visibility after failed pruning
- **WHEN** rule-excluded rows exist and secondary pruning fails
- **THEN** search, `ExportDirectorySnapshot`, and `GetFolderAggregate` MUST reflect the same consistent set of rows as the durable store, not a divergent projection-only removal or SQLite-only removal

### Requirement: Cancellation, shutdown, and restart interactions preserve the contract

Cancellation or engine shutdown during a secondary pruning or reconciliation removal MUST NOT create a persistent divergent state, and restart MUST recover to a consistent state where the next reconciliation can repair any outstanding pruning debt.

#### Scenario: Restart after failed pruning
- **WHEN** a secondary pruning failure is followed by process restart and rebuild
- **THEN** the rebuilt projection MUST match the durable store and the next operation MUST produce a correct pruning outcome without manual intervention
