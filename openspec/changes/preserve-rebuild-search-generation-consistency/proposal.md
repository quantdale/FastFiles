# Preserve rebuild search-generation consistency

## Why

The readiness audit reproduced a Completed scoped search losing an existing matching file after `RebuildAll` repacked projection slots and then failed secondary rule pruning. Search retains captured slot indices; the failure path did not invalidate read dependencies. Success callbacks must remain suppressed, but suppressing reader invalidation is incorrect.

## What Changes

- Invalidate all potentially repacked projection dependencies under the mutation lock before a nonempty rebuild starts.
- Preserve per-volume success callbacks and retained failed-pruning rows/retry behavior.
- Add deterministic single/shared-volume overlap and empty-rebuild negative controls; correct two historical assertions that confused invalidation with success publication.

## Impact

Engine rebuild/search lifecycle only. No protocol, privilege, schema, service, signing or UI design changes. The broader production-readiness audit continues separately.
