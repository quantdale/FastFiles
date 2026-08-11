# File-operation E2E hardening — 2026-08-11

## Scope

This local repair closes two Windows Shell completion edge cases in the
already-implemented `file-operations` change without changing the worker STA,
IFileOperation, conflict-policy, or security boundaries.

## Implementation

- Successful NewItem completion now publishes the verified
  `destination\newName` path when the shell callback omits or canonicalizes
  its returned `IShellItem` path, preserving inline-rename handoff.
- Any move transfer plan containing a Replace item is structurally excluded
  from reversible history, even if Shell reports a callback path whose
  spelling differs from the preflight source path.

## Evidence

- `fffileoperations_e2e_tests`: PASS, 1/1.
- Full serial Debug CTest: PASS, 45/45.
- Full serial Release CTest: PASS, 45/45.
- No signing, authentication, privileged-service, or IPC behavior changed.
