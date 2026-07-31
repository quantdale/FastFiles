## 1. Harness Core, Capability Registry & Contracts

- [x] 1.1 Create the `verify/` tooling root and the PowerShell 7 orchestrator **core** (registry + scheduler + gate + reporting) exposing the verbs `build`, `install`, `run`, `diagnose`, `report`, `repair`, `gate`, each non-interactive with distinct documented exit codes
- [x] 1.2 Define the **capability interface** (id, `interfaceVersion` semver, tier, `availability()` probe, `run()`, `diagnostics()`, optional `baseline()`/`repair-hints()`) and a schema for it
- [x] 1.3 Implement **capability discovery**: scan the capability module directory, validate each against the interface schema, load only supported `interfaceVersion`s, and record an incompatible-version load as a diagnostic (never a silent drop)
- [x] 1.4 Implement the execution-context fingerprint (OS build, VS toolset + SDK versions, elevation state, session id/kind, provider/target identity) and persist it as `manifest.json`
- [x] 1.5 Define the per-run artifact tree `verify/runs/<change>/<timestamp>/` and the stable `result.json` schema with the four-state outcome vocabulary (`PASS`/`FAIL`/`SKIPPED`, plus gate-level `REQUIRED-BUT-UNAVAILABLE`); ensure concurrent runs are isolated
- [x] 1.6 Implement capability/suite descriptors (id → command → tier → required privileges → pass/fail predicate → on-failure diagnostics) so adding a validation needs no core edits
- [x] 1.7 Implement availability-probe-driven tier gating with SKIP-with-reason: compare each capability's requirement against the fingerprint and emit `SKIPPED(reason, requiredContext)` — never a silent pass — when unmet
- [x] 1.8 Implement the **Capability Artifact Contract**: the `result.json` envelope schema (id, interface version, tier, outcome, timing, produced-artifact paths+types), the namespaced `artifacts/<capability>/` layout, and a core indexer that builds `index.json` from envelopes only — archiving all payloads opaquely and never parsing capability-specific formats
- [x] 1.9 Implement registry hardening: reject duplicate capability ids and duplicate id+version pairs across all discovered manifests before any capability executes, recording each rejection as a load diagnostic
- [x] 1.10 Add an optional `dependsOn` (capability ids) field to the capability manifest schema; validate every dependency resolves to a loaded capability (missing ⇒ load diagnostic, dependent capability excluded) and that the dependency graph is acyclic (cycle ⇒ load diagnostic naming every capability in the cycle, none of them execute)
- [x] 1.11 Extend the capability interface (D11) with a declared repair posture (`repair-supported` | `repair-unsupported` | `repair-unavailable`) and, when supported, a `repair(context)` entry point; update the capability-interface schema accordingly

## 2. Environment Providers

- [ ] 2.1 Define the Environment Provider interface (`provision → activate → collect-logs → cleanup → snapshot-restore-if-supported`) and record the active provider in the manifest
- [ ] 2.2 Implement the `local` provider: Tier 0 in the current session; Tier 1 via on-demand elevation (elevated shell / one-time approved elevated scheduled task); **mandatory idempotent teardown** even on failure
- [ ] 2.3 Implement disposable/isolated provider adapters (Hyper-V, VMware, VirtualBox, Windows Sandbox, GitHub Actions Windows runner, self-hosted runner) behind the interface, with at least one snapshot-restorable reference implementation validated end-to-end
- [ ] 2.4 Verify the same capability runs unchanged across `local` and a disposable provider, with logs/artifacts collected back to the run tree

## 3. Diagnostics, Crash Analysis & Reporting

- [ ] 3.1 Register Diagnostics as its own independently-discoverable, independently-runnable capability (availability probe + `run` entry point invokable directly, e.g. `run diagnostics` — not only a cross-cutting on-failure mechanism) that probes for native tooling with no hardcoded paths and reports each as available (with version/path) or `SKIPPED(reason)` — never a capability failure merely because one tool is absent: Event Viewer (`Get-WinEvent`), ETW/WPR (`wpr`/`logman`, WPA-openable `.etl`), WPA, Process Monitor, ProcDump, Application Verifier + PageHeap, WinDbg/`cdb`, WER/mini dumps to a run-local admin-only path, installer/MSI logs, IPC traces
- [ ] 3.2 Implement the **Crash Analysis** capability: dump capture, executable and faulting-thread identification, symbol resolution, stack trace generation, crash classification bucket, reproduction-artifact preservation, report attachment, and a structured verdict for the repair loop; degrade to dump-only when symbols/debugger absent
- [ ] 3.3 Implement the reporters that project a run tree to Markdown, HTML (self-contained), JSON, and JUnit XML
- [ ] 3.4 Implement the performance summary (vs. baseline), failure summary (with linked crash-analysis artifacts), and — without changing the `result.json` envelope schema — surface each capability's duration/version (from the envelope), the run's environment fingerprint (from the manifest), any capability-recorded tool-version metadata, produced artifacts, repair attempts (from `repair-log.jsonl`), and skip/unavailable reasons
- [ ] 3.5 Add report-fidelity checks: regenerating from an unchanged run tree yields the same PASS/FAIL/SKIP/required-but-unavailable verdicts with no invented results
- [ ] 3.6 Ensure `verify.ps1 doctor` (section 13) reuses Diagnostics' tool-discovery logic rather than duplicating it

## 4. Windows Build Automation (Tier 0)

- [x] 4.1 Implement toolchain discovery via `vswhere` (require `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`) and Developer-environment activation (`VsDevCmd`/`vcvarsall`) into the build process; record the selected toolset/SDK in the fingerprint
- [x] 4.2 Add CMake presets for Debug, Release, and a `/analyze` static-analysis variant; drive configure/build with the toolset-bundled CMake + Ninja (no global PATH dependency)
- [x] 4.3 Implement clean and incremental build modes and report which was used
- [x] 4.4 Implement compiler/linker diagnostics parsing (severity/file/line/code/message), build-failure classification, and first-failing-target/diagnostic identification (incl. `/WX` breaks)
- [x] 4.5 Implement the build summary (configuration, toolset, per-target result, warning/error counts, duration)
- [x] 4.6 Add an `fftest` CMake target and wire the existing `ctest` unit + fuzz suites as Tier-0 capabilities emitting JUnit-compatible results

## 5. Installer & Service Validation (Tier 1)

- [ ] 5.1 Implement install / upgrade / repair / uninstall drivers (non-interactive) capturing installer logs into diagnostics
- [ ] 5.2 Implement post-install integrity verification: files present, install-dir ACLs, registry entries, scheduled task, and service presence/config
- [ ] 5.3 Implement service registration & SCM config validation (start type, service/virtual account, delayed start, SCM security descriptor denying client-group control)
- [ ] 5.4 Implement service lifecycle validation (start/stop/restart/delayed-start/recovery actions) with bounded-timeout state polling
- [ ] 5.5 Implement service logging / Event Viewer validation for lifecycle events, collected into diagnostics

## 6. Privilege, Engine–Service & IPC Validation (Tier 1)

- [ ] 6.1 Build `fftest.exe` probes wrapping existing self-checks (`VerifyBackupPrivilegeSufficiency`, `VerifyClientAtHandshake`) and new token/integrity/ACL/shared-memory probes; emit JSON + exit codes
- [ ] 6.2 Implement privilege/token/integrity validation (SeBackup/SeRestore-only posture; service/user/admin/standard/SYSTEM contexts) with explanatory failure diagnostics
- [ ] 6.3 Implement ACL/object-security validation for install dir, named pipes, and shared-memory mappings; Authenticode verification of binaries
- [ ] 6.4 Implement engine–service validation: discovery, mutual auth + Authenticode pinning, heartbeat-loss recovery, idle disconnect, version-mismatch → degraded mode, startup sequencing
- [ ] 6.5 Implement IPC validation: pipe first-instance/squatting hard-fail, pipe ACLs, handshake, snapshot publication/sync without round-trip, session isolation, timeout recovery, reconnection
- [ ] 6.6 Implement protocol-robustness validation: malformed/oversized frames rejected without crash/over-allocation; large valid payload handled within the protocol maximum

## 7. Filesystem, Reliability, Stress & Performance Validation

- [ ] 7.1 Implement filesystem validation: volume enumeration, NTFS traversal, USN journal handling, incremental indexing, rescans, staleness detection
- [ ] 7.2 Implement NTFS metadata & special-file validation: NTFS permissions, symbolic links, junctions, reparse points, alternate data streams (ADS), long (`\\?\`) paths, locked/in-use files
- [ ] 7.3 Implement index change & recovery validation: create/delete/rename/move reflected incrementally, USN journal recovery (wrap/reset/ID change), stale-index recovery, very large directory trees
- [ ] 7.4 Implement single-session reliability validation: unexpected service termination and engine/UI crash → recovery/degraded mode with crash-dump capture (via Crash Analysis)
- [ ] 7.5 Implement resource-leak validation (handle/memory/thread deltas across start/stop) with a configurable tolerance, leveraging Application Verifier/PageHeap where available
- [ ] 7.6 Register the **Stress** capability (startup/shutdown/reconnect/pipe/snapshot/large-fs/long-indexing/memory/CPU pressure), Tier-3-gated to `SKIPPED(context-absent)` until a Tier-3 execution context exists
- [ ] 7.7 Implement historical performance baselines: per-fingerprint baseline store, rolling comparison, configurable per-metric thresholds, graceful first-baseline seeding, advisory-unless-gated regressions

## 8. UI Automation Capability (Tier-2, registered)

- [ ] 8.1 Implement the UIA driver (element identity, patterns, properties, events; keyboard/mouse via UIA/`SendInput`) with an availability probe for an interactive UIA context and for the UI's custom-surface UIA provider
- [ ] 8.2 Implement launch, multi-column navigation, selection, keyboard navigation, mouse interaction, and scrolling verification through the UIA tree — no whole-screen pixel diffing
- [ ] 8.3 Implement UI state/error-surface verification: connection badge, dialogs, search, in-column error states, and rendering-where-practical via UIA geometry/state
- [ ] 8.4 Wire precise `SKIPPED(reason)` for missing interactive context or missing UIA provider, instead of any brittle pixel fallback

## 9. Autonomous Repair Loop

- [ ] 9.1 Implement the capability-owned repair interface: each capability declares its repair posture (`repair-supported` | `repair-unsupported` | `repair-unavailable`) in its manifest and, when `repair-supported`, exposes a `repair(context)` entry point containing its own fix logic; the orchestrator's repair coordinator invokes the declared entry point and records the outcome, containing no capability-specific repair logic itself
- [ ] 9.2 Implement the loop driver (build → install → run → verify → diagnose → root-cause → fix → rebuild → reinstall → re-run) with a hard iteration cap
- [ ] 9.3 Implement fix classification: Class A (harness/config/env) auto-apply + log; Class B (product source) apply on a work branch with recorded root-cause + diff, flagged for review, never silently accepted
- [ ] 9.4 Consume Crash Analysis structured verdicts at the root-cause step; implement the failure-signature (normalized error + capability + phase) no-progress detector that stops and escalates on recurrence
- [ ] 9.5 Implement `repair-log.jsonl` capturing every iteration (root cause, fix class, action, outcome)

## 10. Four-State Archive-Gate Integration

- [ ] 10.1 Implement the `gate` verb resolving each required capability to `PASS`/`FAIL`/`SKIPPED`/`REQUIRED-BUT-UNAVAILABLE`; pass only when every required capability is `PASS` AND no unrepresented product-source edit exists in the run
- [ ] 10.2 Implement the per-change gate policy (which capabilities/tiers are required; which SKIP reasons are acceptable; whether performance regressions gate)
- [ ] 10.3 Wire the gate into the OpenSpec archive flow — advisory first, then blocking — reading only run-tree artifacts + gate policy so the verdict is reproducible

## 11. Documentation & Elevated-Context Setup

- [ ] 11.1 Document the one-time elevated execution context (elevated shell for interactive; approved scheduled task for unattended) and the teardown/clean-host guarantee
- [ ] 11.2 Document the allowlisted destructive-verb set, the "never disable product hardening to pass" rule, and how to select an Environment Provider (incl. snapshot-restorable VM / disposable runner)
- [ ] 11.3 Write the harness/extensibility guide: adding a capability via the versioned interface, capability-interface version policy, reading a run tree and reports

## 12. Prove The Capability — Unblock The Foundation

- [x] 12.1 Run Tier 0 against `establish-architecture-foundation` and close task 1.4 (clean Debug+Release build from fresh checkout) and task 7.7 (fuzz suite runs) with real run-tree evidence
- [ ] 12.2 Run Tier 1 and close foundation task 7.1 (SeBackupPrivilege sufficiency), 7.2 (pipe-squatting hard-fail), 7.3 (impostor handshake rejection), 7.4 (client group cannot SCM-control the service), 7.5 (UI degraded-mode on service absent/stopped/killed) with captured evidence
- [ ] 12.3 Record 7.6 (multi-session load) and any UI-Automation/Stress capabilities as `SKIPPED(requires-Tier-2/3)` via the four-state contract, referencing the follow-up changes — not a silent pass
- [ ] 12.4 Produce the first complete verification report (md/html/json/junit + performance + failure summaries) and confirm the four-state archive gate passes for the foundation change on a green run
- [ ] 12.5 Define acceptance-criteria evidence: a single agent-invoked run that discovers capabilities, builds, installs, validates, diagnoses/repairs as needed, reports, and gates — with minimal human intervention (one-time elevation approval) — documented as the capability's success proof

## 13. Developer-Experience Inspection Verbs

- [ ] 13.1 Implement `verify.ps1 list`: enumerate every discovered capability (including ones rejected by registry hardening) with id, interface version, tier, load status, and declared dependencies, in human-readable and JSON form; creates no run artifact tree
- [ ] 13.2 Implement `verify.ps1 doctor`: probe and report environment readiness (PowerShell, Visual Studio/VC toolset, Windows SDK, CMake, Ninja, WPR, WPA, ProcMon, ProcDump, WinDbg, Application Verifier, Hyper-V presence, Windows Sandbox presence) as presence/version detection only — no environment provisioning, no hardcoded paths; note this is distinct from implementing the Hyper-V/Windows-Sandbox Environment Provider adapters themselves (section 2.3, still deferred)
- [ ] 13.3 Ensure `list`/`doctor` are read-only: no run directory is created under `verify/runs/`, and only usage-error exit codes apply (no PASS/FAIL/SKIPPED semantics)
