# FastFiles — Project Status

Windows-only C++20 native file manager built around a persistent NTFS index.

## Architecture

Three-process privileged/unprivileged split (rationale and decisions in
`openspec/changes/establish-architecture-foundation/design.md`):

- **FastFilesIndexSvc** — privileged Windows service (LocalSystem, constrained broker):
  stateless relay for raw MFT/USN bytes; no index, no query parsing.
- **FastFilesEngine** — unprivileged per-logon process that owns the index and the
  privileged-connection lifecycle.
- **FastFiles** — Direct2D/DirectComposition desktop shell (Column View).

## Current State

The privileged MFT/USN scan is implemented but remains fail-closed while the
authorized mutual-auth pins and live production validation are unavailable.
Degraded mode remains the active, first-class path: unprivileged tree walks and
directory watches continue to work without the service.

Dynamic release state is generated from Git, CTest, capability manifests, release
evidence, and every OpenSpec task ledger. It is never maintained as a test-count or
checkbox summary in this file.

Run `pwsh ./verify/verify.ps1 status` to generate the structured JSON source and
sanitized Markdown projection under the ignored `verify/status/generated/`
directory. `tasks.md` remains the acceptance authority for each OpenSpec change.

The current local hardening frontier is
`openspec/changes/interactive-ui-runtime-determinism`. It closes the locally
actionable dual-pane/UIA verification ambiguity around the real Toggle Dual Pane
command while preserving the production split and degraded path. The baseline
product transition was correct (workspace 1→2 and ColumnView dual-pane true);
the failure was harness semantic discovery of a dynamically replaced UIA
fragment. The repaired harness uses direct-child live-root reacquisition,
semantic palette dispatch/postconditions, exact scenario process ownership, and
bounded pipe cleanup. The provider now retires stale fragment descendants on
structure replacement, with headless coverage for fresh identities, selection
isolation, geometry, and 50 replacement cycles.

The live interactive capability now passes its full wrapper with zero
unexpected failures and no longer fails at Pane.1 discovery. This desktop
still records a truthful
`interactive-foreground-unavailable` skip when a foreign interactive window
(`FocusTube` in the latest run) owns the foreground during Pane.1 activation;
no downstream input assertion or retry is treated as
proof, and the requested 20/20 live run is not claimed. Production signing
pins, authenticated privileged runtime proof, clean-host MFT/USN validation,
and physical/guest environment matrices remain external evidence gates.

The prior active-path hardening validation had 81/81 Debug and release-test
CTest passes, a clean `/analyze` build, ASAN protocol/IPC 2/2, strict OpenSpec
66/66, source-marker and diagnostic-policy passes, and a passing 600-second
active-path soak. The current campaign's focused headless tests and full UIA
capability are green; final Debug/release-test, Analyze, ASAN, verifier,
OpenSpec, policy, drift, and hygiene gates are green. Elevated resource
validation, privileged service validation, and physical/guest UI matrices are
not part of ordinary non-admin development.

## Release-state interpretation

- `PASS` means the named evidence was actually observed.
- `FAIL` means an executable check ran and found a defect.
- `SKIPPED` means an optional check was not selected or has no available evidence.
- `BLOCKED_ENVIRONMENT` means a different interactive/elevated host configuration is required.
- `REQUIRED-BUT-UNAVAILABLE` means a provider, production signer, or clean-host resource is missing; it is never a certification.

## References

- `AGENTS.md` — repo guidelines: build/test commands, security invariants, workflows.
- `AUTONOMOUS.md` — autonomous engineering loop (`verify/intake.ps1`).
