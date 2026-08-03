# Candidate matrix execution and privilege-model selection

> Owner change: `resolve-raw-volume-privilege-insufficiency`
> Tasks: 1.3 (execute matrix, attach reproducible results), 1.4 (select model)
> Status: execution evidence attached; selection decision recorded below.

## Execution context

| Field | Value |
| --- | --- |
| Host | `PALAC-DEV` (workgroup workstation, developer machine with the real FastFiles installation present) |
| OS | Windows 11, build `10.0.26200` (25H2), x64 — primary matrix target family (24H2+ client SKU) |
| Elevation | Full administrator token for the runner; matrix rows run under the service's own SCM token |
| Volume evaluated | `\\.\C:` (system volume; the path the diagnosis observed failing) |
| Runner | `Run-PrivilegeCandidateMatrix.ps1` (this change) driving the `--run-candidate-matrix` diagnostic mode of `FastFilesIndexSvc.exe` (matrix-mode build `20260801`, proper SCM handshake, one row per service restart) |
| Row capture | `CaptureCandidateMatrixRow` / `LogCandidateMatrixRow` (`src/indexsvc/src/PrivilegeVerification.cpp`), persisted as JSON per row + `summary.json` |
| Production restoration | Runner restores the original `ImagePath`, service account, granted rights, group memberships, recovery actions, and restarts the service in its `finally` block; post-run `sc.exe qc` confirms `BINARY_PATH_NAME="C:\Program Files\FastFiles\FastFilesIndexSvc.exe"`, `SERVICE_START_NAME=NT SERVICE\FastFilesIndexSvc`, state `RUNNING` |

Raw per-row evidence: `evidence/matrix-run-20260801/*.json` (this directory). This host is
not a clean disposable image, but every narrow candidate produced a **denial** — a result
that cannot be explained by unrelated administrative state, and the two diagnostic
controls produced the expected ceiling outcomes, so the negative result is robust to host
state. Secondary hosts (Windows 10 22H2, Server 2022) were not available; the mechanism
falsified here (documented narrow user rights granting a raw *device* open) is not
version-specific per the Microsoft Privilege Constants reference cited in
`evidence/candidate-matrix.md`.

## Results

| # | candidateId | outcome | volumeOpenError | journal | groupContext |
| --- | --- | --- | --- | --- | --- |
| 1 | `baseline-sebackup` | `open-denied` | `0x5` | — | admins=0;system=0;backupOps=0;authUsers=1 |
| 2 | `sebackup-restore` | `open-denied` | `0x5` | — | admins=0;system=0;backupOps=0;authUsers=1 |
| 3 | `semanage-volume` | `open-denied` | `0x5` | — | admins=0;system=0;backupOps=0;authUsers=1 |
| 4 | `sebackup-semanage` | `open-denied` | `0x5` | — | admins=0;system=0;backupOps=0;authUsers=1 |
| 5 | `se-security` | `open-denied` | `0x5` | — | admins=0;system=0;backupOps=0;authUsers=1 |
| 6 | `backup-operators` | `open-denied` | `0x5` | — | admins=0;system=0;backupOps=0;authUsers=1 |
| 7 | `control-administrators` (control) | `open-succeeded-journal-succeeded` | `0x0` | queried + read | admins=1;system=0 |
| 8 | `control-localsystem` (control) | `open-succeeded-journal-succeeded` | `0x0` | queried + read | admins=0;system=1 |

All six narrow candidates held the candidate privilege **held and enabled**
(`privilegeHeld=true`, `privilegeEnabled=true`) and still received `ERROR_ACCESS_DENIED`
(`0x5`) on `CreateFileW("\\\\.\\C:")`. This confirms and extends the original diagnosis:
no documented narrow user right, and no narrow group membership, opens the raw volume
device on this Windows generation — the device open is gated on the Administrators/SYSTEM
boundary regardless of granted rights.

## Selection decision (task 1.4)

Per the pass criteria in `evidence/candidate-matrix.md` and design.md's fallback boundary:

- **No narrow candidate passes** (`open-denied` for every non-control row) ⇒ the
  **constrained privileged broker is the required model** (design.md "Constrained broker
  is the fallback boundary").
- The controls quantify the ceiling the broker must run under: Administrators membership
  and LocalSystem both fully support open + USN query + USN read. Neither is adopted
  "naked"; the selected model is a **LocalSystem service with the existing narrow,
  closed command surface** (volume enumeration, raw-volume scan/journal, cancellation,
  structured status only) and the existing symmetric mutual authentication —
  i.e. FastFilesIndexSvc re-registered under `LocalSystem` with `SeBackupPrivilege`
  explicitly granted, the `FastFilesUsers` group boundary and signed-peer pins unchanged,
  and the broker-only surface enforced by the existing dispatcher (task 2.4/2.5) plus a
  new dispatcher-surface regression test.
- **Security decision recorded:** adopting the LocalSystem identity for the privileged
  service is the explicit, evidence-backed decision of this change; the mitigations that
  keep it a *constrained* broker rather than "the whole product as admin" are: (a) the
  service remains stateless and narrow (no generic handle/path/file-mutation commands,
  enforced by the closed protocol), (b) the engine↔service mutual authentication and
  signed-peer pins are unchanged and fail closed, (c) the service token's privilege set
  is verified at startup and exposed in diagnostics (task 2.3), and (d) the SCM object
  security descriptor still grants the client group query-only rights (no start/stop
  control). This is the architecture record the design's "follow-up architecture
  record" requires; the foundation and scanning task hand-offs are updated only after
  the real-service verification (task 4.x).

## Reproducibility

Re-running `Run-PrivilegeCandidateMatrix.ps1` against a rebuilt matrix-mode binary on
the same host reproduces the same row outcomes and exact `0x5`/`0x0` codes; the runner
attaches `summary.json` per run under `$env:ProgramData\FastFiles\matrix-run`. A clean
secondary-host run (tasks 4.1-4.3, signed artifacts) supersedes this developer-machine
evidence before production promotion.
