# Failed-rebuild read-capture qualification

## Reproduction

The actual IndexPipeline regression `TestFailedRebuildInvalidatesCapturedFilenameSlots` captures a scoped filename candidate list, then invokes RebuildAll at WorkStarted and injects a named secondary pruning failure. Deliberately permuted insertion order makes rebuilt slot order differ. Before the fix the response reports Completed but loses the existing needle.txt match; neither the result nor structural-restart assertion passes. `debug-rebuild-slots-red-test.log` retains the two failures.

Root cause: RebuildVolumeFromStore can compact shared storage and reorder slots even when later pruning fails, but RebuildAll advanced read dependencies only for successful volumes. A captured filename slot list bypasses the cursor's allocation-address staleness check.

## Correction and controls

Before any nonempty rebuild mutates storage, invalidate all potentially repacked projection dependencies under the existing lock. This uses the existing structural-residency invalidation and bounded recapture/fallback mechanisms. Success callbacks remain confined to volumes whose rebuild and pruning complete. Empty rebuild does not invent a mutation.

Both single- and shared-volume overlap arms now return the exact expected file ID, name, metadata and folder, with a generation restart. The neighboring successful volume still receives a callback; the failed-prune volume does not. All existing durable/resident retention, retry and reconciliation tests remain. Two historical failed-rebuild epoch assertions now distinguish invalidation from a success acknowledgement.

This does not claim strong allocation-exception recovery; retiring old captures before mutation prevents their epochs looking current but does not itself repair a partially mutated projection after every allocation failure. That remains an independent readiness-audit question.

## Local qualification

- Focused complete engine executable PASS (`debug-rebuild-slots-green-test.log`).
- Full Debug **147/147** (405.66s), full optimized **147/147** (384.68s).
- Affected engine ASan **1/1** (49.50s), full incremental MSVC `/analyze` warning-clean.
- Source/diagnostic policy and strict OpenSpec PASS; full suites include lifecycle/status/doc drift. Active C++ LSP inconclusive (timeout; unrelated historical typo hints), never counted as a clean pass.

Raw logs are in `D:/Documents/tryPython/_AgentArtifacts/FastFiles/sessions/20261117-pi-production-readiness/` under `*-rebuild-final-{build,test}.log` and `analyze-rebuild-final-build.log`. Builds remain isolated; protected build/debug and installed service/signing/trust state unchanged.

## Publication frontier

Implementation push, post-commit identity gates and terminal hosted exact-SHA qualification remain pending. Task 3.2 stays open. This bounded repair is one part of the continuing production-readiness audit, not an overall readiness declaration.
