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

The current local hardening frontier is `openspec/changes/active-path-runtime-resilience`.
It strengthens the already implemented degraded path with deterministic watcher
setup-failure recovery, atomic snapshot-publication fault coverage, engine
generation/reconnect ordering, SearchPanel supersession identity, filesystem-to-
snapshot convergence, and the marked-fixture
`windows-active-path-resilience-validation` capability. This is hardening of
the mainline implementation, not a replacement architecture. Production
signing pins, authenticated privileged runtime proof, clean-host MFT/USN
validation, and physical/guest environment matrices remain external evidence
gates.

The completed local hardening validation has 81/81 Debug CTests and 81/81
release-test CTests passing, a clean `/analyze` build, ASAN protocol/IPC 2/2,
strict OpenSpec 66/66, source-marker and diagnostic-policy passes, and a PASS
from the visual catalog. The active-path capability's 600-second default soak
also passed with bounded resource samples and exact owned-process cleanup.
The focused current Search UIA scenario passes after the icon-cache iterator
fix; the dual-pane selection scenario remains an explicit interactive-driver
failure because Pane.1 did not appear, and the elevated resource-leak
capability is SKIPPED on this non-admin development host.

## Release-state interpretation

- `PASS` means the named evidence was actually observed.
- `FAIL` means an executable check ran and found a defect.
- `SKIPPED` means an optional check was not selected or has no available evidence.
- `BLOCKED_ENVIRONMENT` means a different interactive/elevated host configuration is required.
- `REQUIRED-BUT-UNAVAILABLE` means a provider, production signer, or clean-host resource is missing; it is never a certification.

## References

- `AGENTS.md` — repo guidelines: build/test commands, security invariants, workflows.
- `AUTONOMOUS.md` — autonomous engineering loop (`verify/intake.ps1`).
