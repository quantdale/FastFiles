# FastFiles — Project Status

## Current OpenSpec frontier lifecycle truthfulness campaign

The current local implementation frontier is
`openspec/changes/openspec-frontier-lifecycle-truthfulness`. The project-status
pipeline now consumes the schema-validated `openspec/lifecycle.json` manifest,
preserves the raw unchecked-task inventory, and exposes only genuinely active
local work in `frontier.localOpen`. Completed/historical, superseded, mixed,
administrative, provider, signing, clean-host, elevated, and interactive tasks
retain explicit lifecycle/evidence provenance. Stale historical delivery tasks
from preceding campaigns were checked only where later Git/CI evidence proves
completion; genuine external gates remain open.

The current live projection has 25 raw unchecked tasks and 1 active-local task
(the new campaign's final handoff item). Focused project-status
fixtures pass, strict OpenSpec validation is 70/70, Debug and release-test CTest
are 83/83, Analyze is warning-clean, ASAN protocol/IPC is 3/3, and the official
verifier build gate passes 9/9. No production signing, privileged MFT/USN,
clean-host, guest/provider, or host-bound live UIA claim is inferred.

The full supported live UIA wrapper remains an honest host-boundary item: the
current resource-pressure condition prevented a bounded terminal run, so it is
not claimed as a product pass. No production signing, privileged MFT/USN,
clean-host, guest, physical-display, or foreground-owned interactive claim is
inferred.

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
evidence, the lifecycle manifest, and every OpenSpec task ledger. It is never
maintained as a test-count or checkbox summary in this file.

Run `pwsh ./verify/verify.ps1 status` to generate the structured JSON source and
sanitized Markdown projection under the ignored `verify/status/generated/`
directory. `tasks.md` remains the acceptance authority for each OpenSpec change.

The earlier UI/runtime campaigns remain historical evidence. Their corrected
provider semantics, direct-child UIA discovery, degraded-mode behavior, and
truthful foreground/Explorer skips are preserved by this verifier slice.
Elevated resource validation, privileged service validation, production
signing, and physical/guest UI matrices are not part of ordinary non-admin
development.

## Release-state interpretation

- `PASS` means the named evidence was actually observed.
- `FAIL` means an executable check ran and found a defect.
- `SKIPPED` means an optional check was not selected or has no available evidence.
- `BLOCKED_ENVIRONMENT` means a different interactive/elevated host configuration is required.
- `REQUIRED-BUT-UNAVAILABLE` means a provider, production signer, or clean-host resource is missing; it is never a certification.

## References

- `AGENTS.md` — repo guidelines: build/test commands, security invariants, workflows.
- `AUTONOMOUS.md` — autonomous engineering loop (`verify/intake.ps1`).
