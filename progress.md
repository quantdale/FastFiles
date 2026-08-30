# FastFiles — Project Status

## Repository freeze — observed 2026-08-30

FastFiles is **LOCALLY COMPLETE — EXTERNAL PRODUCTION CERTIFICATION PENDING**.
At the start of this truth-convergence pass, Git observed `main` and
`origin/main` at `d1e9c0ba2875419cd632964aacb3dc9849f2d54f`, ahead/behind `0/0`,
with a clean worktree. That is a historical checkpoint for this pass; derive
the live branch, HEAD, origin parity, dirty state, and hosted state from Git
and GitHub at every resume.

The most recently certified product baseline is `0ad79a8caeab30f6c129c0f285279d1508b62c48`
(hosted run `33294518606` SUCCESS); the most recently certified ledger baseline
is `94a4830cb799a459844e7af421257fd525b82116` (hosted run `33295510209`
SUCCESS). The pre-freeze tip's hosted run `33296255140` was also 6/6 SUCCESS.
These are immutable evidence references, not a self-referential live-SHA claim.

The lifecycle model now represents 25 unchecked tasks across seven changes,
each with an explicit task-level disposition and evidence, including the
previously invisible nested IDs `4.2.2` and `4.5.1–4.5.3`. Active-local tasks
are `0`; the remaining open work is mixed local/external UI composition or
genuinely external signing, privileged, clean-host, provider, and interactive
certification. No implementation campaign is active. The default next action
is **none** unless a new reproducible P0/P1, regression, or authorized external
certification input appears.

Affected truth checks are green: strict OpenSpec `77/77`, the CTest truth slice
(`ffdoc_drift_tests`, `ffdoc_truth_fixture_tests`,
`ffsource_provenance_fixture_tests`, `ffproject_status_ps_tests`) `4/4`,
release-frontier/status generation, and `git diff --check`. Product code was
not changed in this freeze pass.

## Certification wave — clipboard/focus/visual-catalog + hosted-ANALYZE repair — 2026-08-30

The most recently certified ledger baseline was `94a4830cb799a459844e7af421257fd525b82116` (product `0ad79a8caeab30f6c129c0f285279d1508b62c48`, hosted runs `33294518606` and `33295510209`, both **SUCCESS** 6/6: debug, release-test, analyze, asan-protocol-ipc-lifecycle, unsigned-release-candidate-qualification, hygiene). Predecessor `a610bea` run `33286497275` was 5/6 with **analyze FAIL** `C6387` in `test_native_header_theme.cpp:34` (`SendMessageW` with possibly null `header`); the guard `if (header==nullptr||!IsWindow(header)) {DestroyWindow(list);return 1;}` closes it and the new SHAs are warning-clean (`cmake --preset analyze` 384/384, hosted analyze SUCCESS). The exact SHA and hosted state described here are historical certification evidence.

Product hardening: `QuickActions` clipboard cut now bound to `GetClipboardSequenceNumber` + exact path list (`CanUseLocalCutClipboardFallback`, new `ffquick_actions_tests`); `WindowShell` `WM_LBUTTONDOWN`/`WM_RBUTTONDOWN` `SetFocus` keeps keyboard ownership; UIA `drag-out-to-explorer` returns `SKIPPED` `interactive-drop-target-obstructed` when `TeraCopy` overlay covers the drop point instead of a false product `scenario-failed`; visual catalog `Invoke-VisualCatalog.ps1` now themes via `CB_SETCURSEL`, redacts every UIA-visible `*:\*` / `\\*\*` path (including `ListItem HelpText`) with deduplicated rects, establishes Home via real `navigation.home` (`Pane.0.Home`), and adds `home-wide`/`details-closed`/`dual-pane`/`search-filters-expanded` — 14/14 captures PASS with redacted host paths (`manifest.json` + 4 new PNGs). Debug CTest **94/94** and release-test **94/94** (including `ffquick_actions_tests`), ASAN **3/3**, OpenSpec **77/77**, `ffdoc_drift`/`ffdoc_truth_fixture`/`ffsource_provenance`/`ffproject_status` PASS, `check_source_markers`/`check_diagnostic_policy` PASS, `Invoke-ExactTreeQualification` **8/8**, `Test-ReleaseCandidate` **0 failures** (deterministic archive, SHA-bound provenance, redacted pins), `ffengine_bench_large_index_scalability` 100k selective 0.59ms / broad 197ms sanitary. Production signing/pins, privileged MFT/USN, clean-host, guest/provider, and full live foreground `6.15` remain `BLOCKED_EXTERNAL`/`BLOCKED_ENVIRONMENT` and are never fabricated as PASS.

## Historical native-chrome and Settings-page campaign — 2026-08-29

At the start of that historical campaign, Git observed `main` at `746b2b33d4028f069a47897ce7e9759606cc62b9`
(hosted run `33221305265` SUCCESS). Public GitHub API reports PR #6 closed
without merge; it is historical superseded evidence, not an open
administrative task.

The interactive visual catalog exposed remaining native chrome defects and a
real Settings Indexing-page regression: `SwitchPage` hid the rule list and
Exclude-rule checkbox and never restored them. This wave owner-paints
SysHeader32/TabCtrl, keeps Search/Storage layout from raising over the
command palette, and drives Settings page membership from a headless seam.
Debug CTest is 88/88 after adding `ffsettings_pages_tests`. Production
signing, privileged MFT/USN, and live Filters UIA remain external.

## Historical completion-integrity checkpoint — 2026-08-28

The current implementation candidate is `ceffa009ef566edfc9895c363a68fdd005ddc0fa`.
It closes the confirmed cold-reload integrity defect: failed durable projection
streams remove partial resident rows, retain the cold retry marker, propagate
failed rule enumeration, and fail closed at snapshot export. Focused recovery
tests pass, and the predecessor hosted baseline `2a3841ceef1b0b99f533ed3bf46d7a63096cc114`
/ run `33129228485` is green across all six jobs with non-empty, SHA-bound,
path-safe unsigned release evidence. The final candidate still requires its
own hosted exact-SHA result.

Local validation for this wave includes Debug and release-test CTest 86/86,
warning-clean Analyze, the authoritative ASAN 3/3 slice, release-tooling
qualification, clean detached release-candidate qualification, and the
100,000-entry cold-reload/eviction benchmark. Source, marker, skip/timeout,
security, and lifecycle truth-surface audits found no additional material
local defect.

At that checkpoint, the diagnostic UIA build was clean, and the verifier
supplied the shared search fixture, selected Current Folder on persistent
hosts, isolated Win32 helper type guards, and avoided repeated disclosure
toggles. A prior isolated allocated-size scenario passed, but the latest
focused run still stopped at the Filters disclosure; that historical
observation is superseded by the later root-cause and repeatability evidence.
The remaining UIA journey composition and final release gates are now
explicitly classified in the lifecycle as mixed/external where their
foreground, provider, signing, clean-host, guest/provider, or physical-display
prerequisites remain unavailable.

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
remain external. The follow-on
`completion-integrity-hardening-and-release-certification` was subsequently
executed; its release-artifact retention and action-runtime maintenance work
remain historical evidence for this completed dependency.

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
