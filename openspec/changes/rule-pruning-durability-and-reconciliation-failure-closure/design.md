## Context

See `proposal.md` Why. `Store::ApplyBatch` documents transactional rollback on failure with caller retry; callers hold the `IndexPipeline::mutex_` and must commit before applying to the projection (D4). Current secondary paths in `ApplyMftBatchChanges`, `ApplyUsnBatchChanges`, `RebuildAll`, and `FinishReconciliationPass` fail open: a failed secondary `ApplyBatch(ruleExcluded/removals)` skips `projection_.Remove` and still returns `true` or erases `reconciliationSeen_`, losing the repair obligation. Lifecycle and epoch advancement then make the failure invisible to search/snapshot consumers.

## Goals / Non-Goals

**Goals:**
- Make every secondary pruning transaction consistent: durable success → projection removals + epoch advance; durable failure → no success reported and no obligation discarded, with bounded retry/reconciliation.
- Add deterministic fault injection at named secondary boundaries without changing production defaults.
- Prove the contract via focused tests covering first failure, retry, repeated failure, cancellation/shutdown, and restart.

**Non-Goals:**
- Changing primary ingestion commit semantics, SQLite transaction behavior, or public protocol.
- Vendored SQLite edits; use existing `Store` seams.
- Performance optimization beyond keeping secondary work batched.

## Decisions

### D1 — Consistent return status for secondary pruning failures
When `ruleExcluded`/`removals` is non-empty and `store_.ApplyBatch` fails, the enclosing `ApplyMftBatchChanges`/`ApplyUsnBatchChanges`/`RebuildAll` iteration and `FinishReconciliationPass` MUST return `false` (or otherwise signal failure) rather than `true`. If the API must remain `bool`, the failure propagates as `false`; if the API is `void` (`RebuildAll`) the failure is recorded as a retained dirty/retry flag that forces the next pass. Rationale: callers already retry on `false` for the primary batch; extending the same status to the secondary batch prevents silent success with divergent state. Alternative of swallowing the error was rejected because it creates an untracked debt.

### D2 — Retain reconciliation obligation on durable removal failure
On failure of `store_.ApplyBatch(removals)` inside `FinishReconciliationPass`, `reconciliationSeen_.erase(it)` MUST be deferred or the entry re-inserted, and `searchableMutation` must stay `false`. The next pass must see the same `ListEntryIds` vs `seen` diff. Accepted trade-off: a failed reconciliation pass does not advance the searchable epoch; it remains retryable.

### D3 — Deterministic injectable secondary-write failure seam
Add an `IndexPipeline` test-only setter `SetSecondaryPruningFaultInjector` (or extend the existing `Store` fault seam) that can force the next secondary `ApplyBatch` at the named boundary to return `false` once or repeatedly, with automatic reset. This avoids flaky file-lock tricks and keeps production behavior unchanged when no injector is installed. Over a generic SQLite error injector, a named-boundary injector makes tests precise and not fragile to unrelated store calls.

### D4 — Minimal state for retry
For MFT/USN paths, the existing pattern of returning `false` is sufficient; callers retry the whole batch (which re-evaluates `IsEntryIncludedLocked`). For `RebuildAll`, a per-volume `rebuildPruningDirty_` flag (or simply returning failure for that volume's rebuild step) forces the next `RebuildAll` or next incremental batch to re-evaluate pruning. No new persisted state is needed because the exclusion rule is derived from `volumeRules_` and `ReconstructPath`.

## Risks / Trade-offs

- **Retry amplification:** callers that retry the whole MFT/USN batch will redo `IsEntryIncludedLocked` walks; bounded by batch size.
- **Epoch visibility:** returning failure delays `AdvanceQueryVisibleEpochLocked` until pruning succeeds; search continues to show the pre-pruning set, which is correct vs publishing a divergent set.
- **Shutdown:** a failure retained across shutdown is durable (SQLite unchanged) and will be retried after restart via normal rebuild/reconciliation; no new recovery mechanism.
- **Test coverage vs invasiveness:** the fault injector is test-only and not part of the product surface; it is guarded by `FASTFILES_TEST` or a `friend` test seam to avoid shipping it.

## Alternatives Considered

- **Whole-pipeline transaction (primary + secondary in one ApplyBatch):** rejected—would combine unrelated batches and change Store contract.
- **Ignore secondary failure as eventual consistency:** rejected—leaves a user-visible stale/excluded row with no bounded repair.
- **Filesystem-level fault injection (lock, disk full):** rejected—flaky and non-deterministic.
