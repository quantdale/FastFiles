## Why

The real `FastFilesIndexSvc` scan path proved that its virtual service account
holds and enables `SeBackupPrivilege`, yet opening `\\.\C:` still returns
`ERROR_ACCESS_DENIED` (`0x5`). The existing SeBackupPrivilege-only premise is
therefore insufficient for the raw-volume operation that scanning requires,
and the privilege/security boundary must be resolved before the indexing path
can be considered production-ready.

## What Changes

- Define and validate the minimum privilege or service-boundary change needed
  for the service account to open the raw volume used by `VolumeScanner`.
- Compare narrowly scoped alternatives, including an additional Windows right,
  a revised service identity/grant, and a narrowly constrained privileged
  broker, against least privilege, installation, recovery, and attack-surface
  requirements.
- Implement the selected option in service registration, startup/token setup,
  and the installer only after the design decision is recorded.
- Re-run the instrumented real-service scan and USN/journal validation under
  the final installed identity, retaining the exact token state and Win32
  error evidence.
- Update the foundation and index-storage task hand-offs only after the final
  configuration passes the real-service verification.

## Capabilities

### New Capabilities

- `raw-volume-privilege-model`: Establishes and verifies the least-privilege
  service boundary required for raw NTFS volume access and USN-based indexing.

### Modified Capabilities

None. Existing OpenSpec changes remain open until this follow-up produces a
verified configuration; this proposal does not close or rewrite their existing
requirements.

## Impact

- `src/setup/src/ServiceRegistration.cpp` and related setup/security helpers:
  service identity, LSA rights, startup ordering, and ACL changes.
- `src/indexsvc/src/PrivilegeVerification.cpp` and `VolumeScanner.cpp`:
  final token setup and real-call verification evidence.
- Installer payload, service recovery, and uninstall/rollback behavior.
- Potentially the engine/service boundary and IPC authorization if the selected
  option requires a constrained privileged broker.
- Runtime verification artifacts and the open tasks in
  `establish-architecture-foundation` and `index-storage-and-scanning`.
