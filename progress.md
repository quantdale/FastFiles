# FastFiles — Project Status

## Completed rule-pruning durability closure

The `rule-pruning-durability-and-reconciliation-failure-closure` change is
complete. The four secondary-write boundaries now have a test-only named fault
seam; failed MFT/USN pruning returns failure, failed RebuildAll pruning does
not publish completion, and failed reconciliation retains its active repair
obligation. Deterministic tests cover durable Store state, resident
projection, search/snapshot/aggregate views, epochs, retries, repeated
failures, and Close/Open recovery.

Implementation SHA `f65b3bfc770587b4f28f3fa49f3da68faa4142a3` passed the exact
committed-tree qualification and hosted GitHub Actions run `33122565103`.
Debug and release-test CTest passed 86/86, Analyze was warning-clean, and the
authoritative ASAN protocol/IPC/lifecycle slice passed 3/3. Production
signing, privileged clean-host, guest/provider, and physical-display evidence
remain external. The active follow-on is
`completion-integrity-hardening-and-release-certification`; its release
artifact-retention and action-runtime maintenance tasks remain separate from
this completed dependency.

## Completed repository truth-surface and doc-drift convergence campaign

The `repository-truth-surface-and-doc-drift-convergence` campaign made
current-facing guidance a mechanically checked execution input.
`tests/drift/DocTruth.psm1` derives expectations from executable seams —
the verifier `$Verb` ValidateSet and `$Exit*` constants, `CMakePresets.json`,
first-party CMake target declarations, schema-v2 `openspec/lifecycle.json`
states, and the snapshot-publisher source seam — and validates fenced claims
plus whole-file anti-staleness rules in README/AGENTS/CLAUDE/CODE_INDEX/
AUTONOMOUS/verify docs/progress. Orphan fences, unreadable authority input,
and stale current-campaign claims fail closed with reason codes; adversarial
fixtures (`ffdoc_truth_fixture_tests`) prove both false-negative resistance
and historical pass-through. All previously live guidance drift (stale verb
enumerations, completed campaigns presented as current, removed
snapshot slot-flip mechanics) is reconciled to stable pointers.

Validation: strict OpenSpec 72/72, fixtures 19/19, both drift tests green in
CTest, official build gate 9/9 across debug/release-test/analyze, ASAN
protocol/IPC 3/3, doctor PASS, lifecycle model PASS with 24 raw unchecked /
0 active-local tasks preserved. Hosted CI for the exact implementation SHA is
recorded in the campaign's lifecycle entry; the final handoff commit's own
Git/CI parity is derived live on resume.

## Completed lifecycle evidence integrity campaign

The `openspec-lifecycle-evidence-integrity-and-semantic-convergence`
campaign migrated `openspec/lifecycle.json` to schema-v2 typed,
offline-resolvable evidence and added separate structural, evidence, and
semantic integrity diagnostics. Invalid metadata preserves raw unchecked tasks
as ambiguity and cannot produce a production-ready release projection.

At final handoff, 24 raw unchecked tasks remain with 0 active-local tasks; the
lifecycle model passes and that campaign is completed. Focused status fixtures
pass, strict OpenSpec validation is 71/71, the official build gate is 9/9
across Debug/release-test/Analyze, protocol robustness is 3/3, and
policy/doc-drift checks pass. Corrected hosted Actions run `32865014793`
completed successfully for exact implementation SHA
`09bac46a5371ecaad58b7c38ab044deb2c5d8163`. Signing, privileged, clean-host,
provider, and interactive prerequisites remain external and are not inferred
as complete; final handoff parity and its later CI result are derived live.

## Completed OpenSpec frontier lifecycle truthfulness campaign

The completed `openspec-frontier-lifecycle-truthfulness` campaign left the
project-status pipeline consuming the schema-validated
`openspec/lifecycle.json` manifest, preserving the raw unchecked-task inventory,
and exposing only genuinely active local work in `frontier.localOpen`. Completed/historical, superseded, mixed, administrative, provider, signing,
clean-host, elevated, and interactive tasks retain explicit lifecycle/evidence
provenance. Stale historical delivery tasks from preceding campaigns were
checked only where later Git/CI evidence proves completion; genuine external
gates remain open.

The final live projection has 24 raw unchecked tasks and 0 active-local tasks.
Focused project-status
fixtures pass, strict OpenSpec validation is 70/70, Debug and release-test CTest
are 83/83, Analyze is warning-clean, ASAN protocol/IPC is 3/3, and the official
verifier build gate passes 9/9. No production signing, privileged MFT/USN,
clean-host, guest/provider, or host-bound live UIA claim is inferred.

Implementation commit `2b600eb9a8d1f8d4e20168653da50afcddc97717` and hosted
Actions run `32848021114` are green. The final documentation handoff is kept
non-self-referential: derive its exact SHA, branch parity, and later CI state
from live Git/Actions on resume.

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
