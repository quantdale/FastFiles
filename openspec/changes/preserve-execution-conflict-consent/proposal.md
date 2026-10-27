## Why

A deterministic controlled Shell-worker test proves that a destination created after filename preflight is silently overwritten without replacement consent. Preserving real destination data takes priority over cosmetic campaigns.

## What Changes

- Submit non-replacement Copy/Move/Restore with Shell collision-renaming policy, preserving data if a target appears during execution.
- Isolate explicitly approved replacements in a separate Shell operation within the same request and terminal outcome.
- Retain actual callback paths for Move undo and one request-scoped conflict/cancellation policy; reset progress speed samples between native phases.
- Add controlled late-file and late-directory tests, ordinary/mixed replacement regressions and current-source qualification.

## Capabilities

### New Capabilities
- `execution-conflict-consent`: write-boundary collision policy preserves destinations without replacement consent.

### Modified Capabilities

## Impact

Unprivileged FileOperations only. Existing native Shell engine and public IFileOperation APIs retained. No service, IPC, schema, trust, signing or user settings changed. Fixtures use unique owned temporary folders and generated Shell identities; no user Recycle Bin or external process experiment.
