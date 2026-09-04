## 1. Harness Core, Capability Registry & Contracts

- [x] 1.1 Create the `verify/` tooling root and PowerShell orchestrator core exposing documented non-interactive verbs and exit codes.
- [x] 1.2 Define the capability interface and schema.
- [x] 1.3 Implement capability discovery, interface-version validation, and load diagnostics.
- [x] 1.4 Implement execution-context fingerprinting and persist `manifest.json`.
- [x] 1.5 Define isolated run artifacts and four-state result vocabulary.
- [x] 1.6 Implement capability/suite descriptors.
- [x] 1.7 Implement availability-probe-driven gating with explicit skip reasons.
- [x] 1.8 Implement the Capability Artifact Contract and run-tree indexer.
- [x] 1.9 Reject duplicate capability identities before execution.
- [x] 1.10 Validate declared dependencies and reject missing/cyclic graphs.
- [x] 1.11 Add repair posture and optional repair entry point to the capability interface.

## 2. Environment Providers

- [x] 2.1 Define the generic Environment Provider interface (`provision → activate → collect-logs → cleanup → snapshot-restore-if-supported`) and record the active provider in the manifest.
- [x] 2.2 Implement the `local` provider with mandatory idempotent teardown.
- [ ] 2.3 Historical vendor-specific VM-provider execution task — **OWNER-EXCLUDED 2026-09-04**. Vendor-specific guest provisioning/execution/testing is intentionally out of FastFiles local completion scope and must not be resumed unless the owner explicitly reverses that directive. Generic provider lifecycle/fake-adapter contract coverage remains permitted infrastructure.
- [ ] 2.4 Historical cross-provider execution task — **OWNER-EXCLUDED 2026-09-04** for vendor-specific VM-provider testing. No provider-specific guest image, credential, channel, or end-to-end VM run is required for FastFiles local completion.

> Scope authority: tasks 2.3–2.4 are retained as historical task IDs because lifecycle metadata references them, but they are not current engineering or validation requirements. Do not interpret their unchecked boxes as local work.

## 3. Diagnostics, Crash Analysis & Reporting

- [x] 3.1 Register Diagnostics as an independently runnable capability with native-tool availability reporting and explicit skip reasons.
- [x] 3.2 Implement Crash Analysis with dump capture, symbol/stack analysis where available, classification, and structured repair-loop verdicts.
- [x] 3.3 Implement Markdown, HTML, JSON, and JUnit reporters.
- [x] 3.4 Surface performance, failure, environment, tool-version, artifact, repair, and skip information without changing the result envelope contract.
- [x] 3.5 Add report-fidelity checks.
- [x] 3.6 Reuse Diagnostics discovery in `verify.ps1 doctor`.

## 4. Windows Build Automation (Tier 0)

- [x] 4.1 Implement Visual Studio/toolchain discovery and environment activation.
- [x] 4.2 Add Debug, Release, and Analyze presets and drive configure/build/test from discovered tooling.
- [x] 4.3 Implement clean and incremental build modes.
- [x] 4.4 Parse compiler/linker diagnostics and classify build failures.
- [x] 4.5 Implement build summaries.
- [x] 4.6 Add `fftest` and wire unit/fuzz suites as Tier-0 capabilities.

## 5. Installer & Service Validation (Tier 1)

- [x] 5.1 Implement install/upgrade/repair/uninstall drivers with logs.
- [x] 5.2 Implement post-install integrity verification.
- [x] 5.3 Implement service registration and SCM configuration validation.
- [x] 5.4 Implement service lifecycle validation with bounded polling.
- [x] 5.5 Implement service/Event Viewer lifecycle logging validation.

## 6. Privilege, Engine–Service & IPC Validation (Tier 1)

- [x] 6.1 Build `fftest.exe` probes for privilege, handshake, token, ACL, and shared-memory checks.
- [x] 6.2 Implement privilege/token/integrity validation, including real elevated raw-volume and USN-journal evidence where available.
- [x] 6.3 Implement ACL/object-security and Authenticode validation; unsigned development binaries remain genuine failures where policy requires signing.
- [x] 6.4 Implement engine–service validation including discovery, authentication, heartbeat/recovery, degraded mode, and startup sequencing.
- [x] 6.5 Implement IPC validation including first-instance/ACL/handshake/snapshot/session/timeout/reconnection behavior.
- [x] 6.6 Implement malformed/oversized-frame and large-valid-payload protocol robustness validation.

## 7. Filesystem, Reliability, Stress & Performance Validation

- [x] 7.1 Implement filesystem validation for volume enumeration, NTFS traversal, USN handling, incremental indexing, rescans, and staleness detection.
- [x] 7.2 Implement NTFS metadata/special-file coverage including links, reparse points, ADS, long paths, and in-use files.
- [x] 7.3 Implement index change/recovery validation including create/delete/rename/move, journal recovery, stale-index recovery, and scale-gated large-tree coverage.
- [x] 7.4 Implement single-session reliability validation and crash-analysis integration; unavailable dump configuration remains explicitly unavailable rather than fabricated.
- [x] 7.5 Implement resource-leak validation with handle/memory/thread tolerances and optional verifier tooling.
- [x] 7.6 Register Tier-3 stress capability with explicit context-absent skip semantics.
- [x] 7.7 Implement per-fingerprint performance baselines and policy-controlled regression gating.

## 8. UI Automation Capability (Tier 2)

- [x] 8.1 Implement UIA driver, provider/context availability probe, input abstraction, events, diagnostics, and headless contract tests.
- [x] 8.2 Implement launch, navigation, selection, keyboard, mouse, and scrolling verification through semantic UIA where exposed; record unsupported Direct2D-only surfaces honestly.
- [x] 8.3 Implement UI state/error-surface validation for connection/degraded state, dialogs, search, and observable rendering geometry/state.
- [x] 8.4 Use precise skip reasons for missing interactive context/provider; never substitute brittle pixel-only success.

## 9. Autonomous Repair Loop

- [x] 9.1 Implement capability-owned repair dispatch and validated repair posture.
- [x] 9.2 Implement bounded build/install/run/verify/diagnose/root-cause/fix/re-run loop.
- [x] 9.3 Implement fix classification separating harness/environment repair from product-source repair requiring review/evidence.
- [x] 9.4 Consume Crash Analysis verdicts and stop/escalate on repeated normalized failure signatures.
- [x] 9.5 Persist every iteration to `repair-log.jsonl`.

## 10. Four-State Archive-Gate Integration

- [x] 10.1 Implement gate resolution over PASS/FAIL/SKIPPED/REQUIRED-BUT-UNAVAILABLE and detect unrepresented product-source edits.
- [x] 10.2 Implement per-change gate policy.
- [x] 10.3 Integrate the reproducible run-tree gate into OpenSpec archival flow.

## 11. Documentation & Elevated-Context Setup

- [x] 11.1 Document approved elevated execution contexts and teardown/clean-host guarantees.
- [x] 11.2 Document destructive-operation allowlists, the prohibition on weakening product hardening, and generic environment-provider selection principles.
- [x] 11.3 Document capability extension, interface-version policy, run-tree interpretation, and reports.

## 12. Prove the Capability — Unblock the Foundation

- [x] 12.1 Run Tier 0 against the architecture foundation and retain real build/fuzz evidence.
- [x] 12.2 Run Tier 1 and retain available privilege, handshake, service, and degraded-mode evidence with explicit unavailable reasons for non-executed legs.
- [x] 12.3 Record higher-tier/UIA/stress capabilities as explicitly skipped when required context is absent.
- [x] 12.4 Produce complete verification reports and confirm the four-state archive gate resolves per policy; genuine unsigned-candidate failures remain visible.
- [x] 12.5 Define and exercise the single-agent verification acceptance flow with bounded repair-loop evidence.

## 13. Developer-Experience Inspection Verbs

- [x] 13.1 Implement `verify.ps1 list` with capability identity/version/tier/load/dependency output and no run-tree mutation.
- [x] 13.2 Implement `verify.ps1 doctor` as read-only presence/version detection for supported development and diagnostic tooling. Vendor-specific VM-provider detection/testing is not a FastFiles completion requirement under the 2026-09-04 owner scope directive.
- [x] 13.3 Ensure `list`/`doctor` are read-only and use only usage-error exit semantics.
