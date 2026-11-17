## Context

Projection rebuild can remove a volume, compact ALL remaining slots, restream its rows, and repack iteration order. A subsequent durable stream/fit/pruning failure does not undo structural mutations. `RebuildAll` formerly advanced epochs only for successfully rebuilt/pruned volumes. A scoped query whose volume failed pruning could therefore consume old filename-candidate indices as new occupants and omit a real match.

## Decisions

### D1 — Structural invalidation is not success acknowledgement

Before the first projection mutation in a nonempty RebuildAll, call the existing `AdvanceResidencyEpochsForStructuralChangeLocked` helper under the pipeline mutex. Every resident volume may have its slots/name storage repacked, so a single-volume data epoch is insufficient. The existing bounded restart/stable durable-fallback search mechanism handles invalidated captures.

Retire captures before mutation so even a later exception cannot leave their dependency epoch looking current. This does not itself make allocation failure recoverable or certify strong exception safety; that remains a separate readiness audit frontier. An empty rebuild makes no structural mutation and must not invent invalidation.

### D2 — Preserve failure/publication contract

Only successful volume rebuild/pruning operations enter the callback list. Failed pruning retains Store/projection rows for retry and never receives a success callback. Two historical query-epoch equality assertions are corrected to require structural invalidation; their decisive callback/retention/retry assertions remain. Failed reconciliation with no mutation remains unchanged.

### D3 — Regression at the real overlap seam

Use actual IndexPipeline search with its WorkStarted phase hook, not pointer-level simulation. Non-ID insertion order makes rebuild slot repacking observable. A named secondary-prune fault forces the failed-volume path. Require exact result identity/name/size/folder, a generation restart, no failed-volume callback, a positive callback for an independently successful shared volume, and no epoch change for empty rebuild.

## Non-goals

No wider snapshot protocol changes, generic transactional rewrite, guessed allocation-exception recovery, signing/trust mutation, or assertion of whole-product production readiness from this slice alone.
