## Context
PR32 source 12b40ee402582f27f1e21e9df44fffc8bebe51e3 queues ordinary collision-safe transfers and approved replacements in separate configured, advised Shell operations. Root callbacks retain actual ordinary destination paths. The owned Move fixture confirms that a vacated replacement target can become an ordinary result name before replacement executes.

## Goals / Non-Goals
Protect callback-observed completed ordinary results and their ancestor/descendant namespaces. Preserve existing cancellation, setup-before-mutation, Shell collision handling and root counts. This change does not qualify Explorer drag, Recycle Bin Restore, privileged paths, filesystem aliases or completely missing root callbacks.

## Decisions
Record source/target pairs only for successfully queued replacements, including Restore's per-item folder. After ordinary execution and before replacement execution, compare each approved target with actual completed result paths using ordinal Windows comparison, lexical normalization, extended drive/UNC prefix normalization and separator boundaries. Empty observed output names fail conservatively. This check protects the same request's results, not a new atomic filesystem identity promise against external mutation.

If any overlap exists, do not execute the queued replacement group. Retain one ERROR_ALREADY_EXISTS failure per queued replacement and all replacement sources. IFileOperation cannot selectively unqueue an item. Refusing the entire group preserves the established setup/Advise/queue-before-mutation contract; rebuilding the group after partial mutation would create new failure boundaries. Ordinary successes remain counted; refusals are neither skips nor cancellations. Unrelated queued replacements can therefore require a retry after the user resolves the overlap.

## Risks / Trade-offs
- Lexical aliases/reparse/short-name identities and absent root callbacks remain unqualified; claim only the observed result namespace protection. Independent review found no additional demonstrated P1/P2.
- The cross-product check runs only for requests containing replacements; extreme mixed batches have not been benchmarked.
- Holding-folder Restore exercises actual Shell identities without claiming Recycle Bin qualification.

## Migration Plan
No persistent-state or protocol migration. Isolate as a successor to PR32; retain the original failing fixture receipt. Review and qualify focused/full native tests before publication. No automatic merge or production release.
