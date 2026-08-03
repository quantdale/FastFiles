## ADDED Requirements

### Requirement: Rule-Based Subtree Gating Of Ingestion
The `FastFilesEngine` ingestion pipeline SHALL honor a `IsPathIncluded(canonicalPath, VolumeSetting.rules)` predicate — implementing the longest-prefix-match, longest-match-wins include/exclude semantics defined by the `settings-ui` capability's "Directory Include/Exclude Rules" — across initial MFT ingestion, USN journal deltas, reconciliation, and rebuilds, so that any item whose canonical path is excluded by the applicable volume's rules is never persisted to the durable store or applied to the in-memory projection. A volume with no rules SHALL include everything, preserving current behavior.

#### Scenario: An excluded subtree is never persisted
- **WHEN** the ingestion pipeline receives a record whose canonical path matches an exclude rule for its volume
- **THEN** the pipeline SHALL drop that record before persisting it to the durable store or applying it to the projection

#### Scenario: A more specific rule wins over a broader overlapping rule
- **WHEN** a broader include rule and a more specific exclude rule both match the same canonical path
- **THEN** the pipeline SHALL apply the more specific (longer-prefix) rule to that path

#### Scenario: A volume with no rules includes everything
- **WHEN** a volume has no include/exclude rules configured
- **THEN** the pipeline SHALL ingest all of that volume's records unchanged, preserving the behavior prior to this change

### Requirement: Rule Application Across All Ingestion Paths
The ingestion pipeline SHALL apply `IsPathIncluded` uniformly across every ingestion path — `ApplyMftBatch` (initial MFT ingestion), `ApplyUsnBatch` (USN journal deltas), the reconciliation observed-set (`BeginReconciliationPass`/`FinishReconciliationPass`), and `RebuildAll` — so that a rule change takes effect without restarting the engine and a previously-included-but-now-excluded entry is reconciled away on the next reconciliation pass.

#### Scenario: A rule change takes effect without an engine restart
- **WHEN** the user adds or removes an include/exclude rule and the change reaches the engine via the control-plane notification
- **THEN** the engine SHALL apply the updated rules to subsequent ingestion and SHALL reconcile excluded subtrees away on the next reconciliation pass, without requiring a restart of `FastFilesEngine`

#### Scenario: Reconciliation removes a now-excluded entry
- **WHEN** a reconciliation pass observes an entry that is now excluded by the volume's rules
- **THEN** the pass SHALL NOT mark that entry as observed, and `FinishReconciliationPass` SHALL remove it as no-longer-resolvable

### Requirement: Deferred Decision For Unresolvable Parent Chains
Because MFT records are not guaranteed to arrive parent-first, the ingestion pipeline SHALL defer (buffer per volume) a record whose canonical path cannot yet be reconstructed because its parent chain is not yet resolvable, and SHALL decide its inclusion once its parent chain becomes resolvable, rather than silently including it; the deferred buffer SHALL be bounded with a decide-on-reconciliation fallback so ingestion never stalls indefinitely.

#### Scenario: A record whose parent has not yet arrived is deferred, not included
- **WHEN** a record arrives whose parent chain is not yet resolvable in the projection
- **THEN** the pipeline SHALL buffer the record per volume and SHALL decide its inclusion once the parent chain resolves, and SHALL NOT silently persist it as included

#### Scenario: A bounded buffer never stalls ingestion
- **WHEN** the deferred buffer reaches its bound
- **THEN** the pipeline SHALL fall back to deciding buffered records at the next reconciliation pass, so ingestion continues to make progress