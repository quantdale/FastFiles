# Real-service privilege sufficiency evidence

## Run

- Captured at: `2026-08-01T16:31:30.9196437Z`
- Host: `NAYEON_16`
- Service: `FastFilesIndexSvc`
- Service account: `NT SERVICE\FastFilesIndexSvc`
- Service image path: `C:\Program Files\FastFiles\FastFilesIndexSvc.exe`
- Scanned raw volume: `\\.\C:`
- Diagnostic run directory: `verify/runs/diagnose-backup-privilege-sufficiency/20260802-003127/`

The run used a temporary diagnostic build of the service and Engine with the
existing compile-time unsigned-peer diagnostic switch enabled. This switch
only allowed the local Engine/service handshake; it did not change privilege
granting, privilege enabling, raw-volume access, or scan behavior. The
original installed service and Engine binaries were restored automatically.

## Classified result

```text
outcome=enabled-but-denied
privilegeHeld=1
privilegeEnabled=1
error=0x5 (ERROR_ACCESS_DENIED)
account=NT SERVICE\FastFilesIndexSvc
sid=S-1-5-80-2770246295-1174983333-1227338532-1625611241-2685571913
```

The record was emitted by the instrumented `VolumeScanner` raw
`CreateFileW` call site, not by the standalone startup probe. The service
token held and had enabled `SeBackupPrivilege` immediately before that call.

## Restoration and verdict

- No token/configuration fix was applied.
- Original service SHA-256 before/after restoration:
  `8E42AB793DA476C572F56823B493930A2B9582523F21EE1E8BDCFC6020FC4053`
- Original Engine SHA-256 before/after restoration:
  `965C61B992B5BCDCC375299626737B1649867B6932C15547D98742B943204733`
- Service was restored to `Running` under `NT SERVICE\FastFilesIndexSvc`.

Because the actual service token held and enabled `SeBackupPrivilege`, yet
the real raw-volume open returned `ERROR_ACCESS_DENIED`, this run rules out a
missing-or-disabled-token explanation for the observed failure. It is input
for a follow-up privilege-model/security design decision against
`establish-architecture-foundation` D1/D2. This change does not choose or
implement that redesign.
