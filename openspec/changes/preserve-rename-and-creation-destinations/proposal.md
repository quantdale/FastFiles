# Preserve rename and creation destinations

## Why

Owned native probes on PR33's source show that NewItem reports an existing file/folder as successfully created and offers its path for inline rename. Separately, Shell RenameItem with the worker's flags overwrites a destination created between queuing and execution. The worker's earlier existence check is not an atomic protection.

## What Changes

- Use a non-replacing Win32 rename boundary, retaining exact requested names and rename history.
- Create new empty files and directories exclusively; report collisions as failures and never offer an existing item for inline rename.
- Cover actual worker collisions/positive controls and a deterministic production-primitive late-collision test, keeping the legacy Shell observation separate.

## Capabilities

### New Capabilities

- `non-replacing-item-creation`: preserve existing rename/create destinations and report truthful creation outcomes.

### Modified Capabilities

None.

## Impact

Unprivileged UI FileOperations only. No protocol, service, schema, signing, elevation or visible redesign. Purpose-specific successor on PR33; no main or existing-PR merge.
