## Why

`index-storage-and-scanning` task 4.6 recorded a raw-volume open (`\\.\D:`) that returned `ERROR_ACCESS_DENIED` under the real, installed `FastFilesIndexSvc` virtual service account with `SeBackupPrivilege` granted, while the same operation succeeded under `LocalSystem`. This directly contradicts `establish-architecture-foundation`'s D1/D2 privilege-minimization premise (`SeBackupPrivilege` alone is sufficient — flagged as an open Risk in `design.md:79`, to be "empirically verified... before assuming the minimization argument holds").

The existing in-process probe (`PrivilegeVerification.cpp`, task 7.1) checks privilege sufficiency in whatever process runs it — not necessarily the real service process at the real moment of the real `CreateFileW` call under the real LSA-granted account. The gap between "probe passed/unknown" and "real service call failed" has two very different explanations with very different consequences:

1. **Token/config bug**: the granted privilege wasn't actually present/enabled in the real service's token at call time (e.g., logon-session timing after `LsaAddAccountRights`, service needing a restart after grant, or a code path that enables the privilege on the wrong token) — fixable entirely inside the existing `SeBackupPrivilege`-only architecture.
2. **Architectural insufficiency**: `SeBackupPrivilege` genuinely cannot open a raw volume handle regardless of correct configuration, and D1/D2's privilege-minimization premise needs to be revisited (e.g., a narrower additional grant, or accepting a documented, higher-privilege boundary).

Committing to a redesign (a new "privileged broker," different service SID model, etc.) before distinguishing these two cases risks solving the wrong problem — and notably, the redesign floated in discussion (a small, stateless, narrow-command-surface privileged process) already describes the existing `FastFilesIndexSvc` architecture from D2/D4, not a new one. This change produces the diagnostic evidence needed to close `establish-architecture-foundation:7.1` and `index-storage-and-scanning:4.6` correctly, and implements the minimal fix if the root cause turns out to be case 1.

## What Changes

- Add token/privilege-state instrumentation to `FastFilesIndexSvc` that captures, at the exact call site of the raw-volume `CreateFileW`, the process token's actual privilege set (held vs. enabled) and the account/logon-session identity — logged distinctly from the existing ad hoc probe.
- Reproduce the task 4.6 failure under the real installed service (not a synthetic probe run as a different process/context) and capture the evidence artifact (token dump + `CreateFileW` result + timing relative to `LsaAddAccountRights`/service start).
- Produce a documented verdict: either (a) root cause is a token/config defect, with the fix applied and re-verified against the real service, or (b) `SeBackupPrivilege` alone is confirmed architecturally insufficient for raw volume opens, with the specific minimal privilege/grant delta required, handed off as a design decision for `establish-architecture-foundation` D1/D2 rather than assumed here.
- Do **not** introduce a new process, broker, or service boundary as part of this change — that redesign (if the verdict is (b)) is out of scope here and belongs to a follow-up change against `establish-architecture-foundation`, informed by this change's evidence.

## Capabilities

### New Capabilities
- `privilege-sufficiency-diagnostics`: instrumentation and evidence-gathering for verifying, inside the real installed `FastFilesIndexSvc` process and service account, whether `SeBackupPrivilege` alone suffices for raw NTFS volume handle opens and USN journal queries — distinguishing a token/configuration defect from a genuine privilege-model gap, and producing the verdict `establish-architecture-foundation:7.1` and `index-storage-and-scanning:4.6` need to close accurately.

### Modified Capabilities
(none — `establish-architecture-foundation` and `index-storage-and-scanning` are not archived, so their specs are not modified here; this change's verdict is handed off as input to those changes' own task closure, not as a spec delta against them.)

## Impact

- `src/indexsvc/src/PrivilegeVerification.cpp` / `.h` — extend or add alongside the existing probe with real-call-site instrumentation.
- `src/indexsvc/src/VolumeScanner.cpp` — the actual raw-volume-open call site the task 4.6 failure occurred in; instrumentation must observe this exact path, not a parallel synthetic one.
- `src/setup/src/ServiceRegistration.cpp` — where `LsaAddAccountRights` grants `SeBackupPrivilege`; timing/sequencing relative to service start is part of what's being verified.
- No IPC protocol, wire format, or process-boundary changes.
- Requires a real elevated Windows install with the actual `FastFilesIndexSvc` service running under its virtual account to gather real evidence (cannot be fully verified in a non-Windows or non-elevated sandbox) — ties into the separately-discussed Hyper-V VM provisioning for `autonomous-runtime-verification`.
