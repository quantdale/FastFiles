# FastFiles — Certification Truthfulness Defect Ledger

- Date: 2026-09-04
- Author: Autonomous Certification Integrity Campaign
- Status: ACTIVE REPAIR

## Summary

Independent repository review identified 10 material certification-integrity and fail-open defects in the external certification framework and durable evidence subsystem introduced in prior campaigns. This ledger records forensic confirmation, root cause analysis, planned and verified remediation, and regression test verification for all defects CT-001 through CT-010.

---

## Defect Summary Table

| Defect ID | Title | Forensic Status | Root Cause Category | Target Component |
| :--- | :--- | :--- | :--- | :--- |
| **CT-001** | Zero-test CTest match produces false PASS | **CONFIRMED** | Fail-open command execution | `verify/certification/Invoke-LongRunCertification.ps1` |
| **CT-002** | Simulated UI activity recorded as successful execution | **CONFIRMED** | False evidence generation | `verify/certification/Invoke-ResourceSoak.ps1` |
| **CT-003** | Resource soak disposition ignores resource trends and growth | **CONFIRMED** | Missing stability verification | `verify/certification/Invoke-ResourceSoak.ps1` |
| **CT-004** | Startup baseline invalidates steady-state leak analysis | **CONFIRMED** | Flawed lifecycle phase modeling | `verify/certification/Invoke-ResourceSoak.ps1` |
| **CT-005** | Retained stress evidence not exact-tree-bound | **CONFIRMED** | Overstated provenance | `verify/certification/` / Retained Evidence |
| **CT-006** | Required application lifecycle x100 and rapid-input x50 evidence missing | **CONFIRMED** | OpenSpec task / evidence mismatch | OpenSpec Task 3.2 / Evidence Directory |
| **CT-007** | External frontier commands semantically mismatched to tasks | **CONFIRMED** | Semantic lane confusion | `verify/certification/external-frontier.json` |
| **CT-008** | OpenSpec lifecycle completion lacks authoritative hosted binding | **CONFIRMED** | Premature lifecycle closure | `openspec/lifecycle.json` |
| **CT-009** | Human soak summary disagrees with machine-readable JSON | **CONFIRMED** | Drift between prose and artifact | `evidence/resource-soak-summary.md` |
| **CT-010** | Artifact self-hash corrupts final file checksum | **CONFIRMED** | Inherent self-referential hash flaw | `Invoke-LongRunCertification.ps1`, `Invoke-ResourceSoak.ps1` |

---

## Detailed Defect Records

### CT-001: Zero-Test CTest Match Produces False PASS

- **Status**: **CONFIRMED**
- **Observed Behavior**: `Invoke-LongRunCertification.ps1 -TestTarget nonexistent_target_12345` falls back to `ctest --test-dir build/<preset> -R ^nonexistent_target_12345$`. When CTest finds zero matching tests, it outputs `No tests were found!!!` and exits with exit code `0`. The long-run runner interpreted `$exitCode -eq 0` as a PASS, recording 100% test success for a nonexistent test.
- **Expected Contract**: The runner must discover and enumerate matching tests prior to execution (e.g. via `ctest --show-only=json-v1`). Exactly one matching test target must exist; 0 matches must fail closed immediately as `INVALID_TARGET` with exit code non-zero, and >1 matches must fail as `AMBIGUOUS_TARGET`. Each iteration must verify that the test actually executed and reported a pass result.
- **Root Cause**: `Invoke-LongRunCertification.ps1` relied solely on CTest process exit code without performing test discovery or verifying execution proof in CTest output.
- **Fix**:
  1. Add pre-execution test discovery using `ctest --show-only=json-v1`. Require `tests.Count -eq 1`.
  2. Parse iteration output to confirm the target was started, completed, and passed.
  3. Direct executable preference where available.
- **Regression Test**: `verify/certification/tests/Test-LongRunCertification.ps1` asserting that a non-existent target fails with exit code non-zero and error message `INVALID_TARGET`.

---

### CT-002: Simulated UI Activity Recorded as Successful Execution

- **Status**: **CONFIRMED**
- **Observed Behavior**: `Invoke-ResourceSoak.ps1` lines 218-228 generated synthetic activity objects with `details = "Simulated periodic UI activity: $transName at t=$elapsed s"` and set `success = true` without sending any input or invoking any command on the running `FastFiles.exe` instance.
- **Expected Contract**: A certification harness must NEVER emit `success = true` for an action that was not executed. If UI activity is claimed, it must be driven via real semantic UIA transitions with verified postconditions. If running in headless/idle mode, it must explicitly declare `-IdleOnly` or `-Scope Idle` without claiming UI transitions.
- **Root Cause**: The soak runner used a placeholder simulation loop to populate `activityTransitions` rather than integrating with the repository's existing UIA automation harness.
- **Fix**:
  1. Delete simulated transition loop entirely.
  2. Implement real UIA action drivers (navigation, search open/close, settings dialog open/close, command palette open/close, dual-pane toggle, storage analysis) using `verify/capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1` or headless semantic interfaces.
  3. Record detailed postcondition checks (`invoked`, `opened`, `closed`, `focusReturned`, duration).
  4. Provide explicit `-IdleOnly` switch for environments without an interactive desktop.
- **Regression Test**: Fail-closed test ensuring that simulated activity cannot be generated or accepted as executed.

---

### CT-003: Resource Soak Disposition Ignores Resource Trends and Growth

- **Status**: **CONFIRMED**
- **Observed Behavior**: `Invoke-ResourceSoak.ps1` calculated disposition as:
  ```powershell
  $disposition = $(if ($crashed) { 'FAIL' }
                   elseif ($workingSetSamples.Count -eq 0) { 'FAIL' }
                   else { 'PASS' })
  ```
  Even when working set grew from 13.8 MB to 66.4 MB (175 KB/s drift), private bytes grew from 2.6 MB to 62.3 MB, handles grew from 147 to 724, threads from 6 to 47, and GDI from 0 to 108, the runner declared `PASS`.
- **Expected Contract**: Disposition must compose:
  1. Process survival (no crash, clean exit).
  2. Functional activity (all executed UIA transitions succeeded).
  3. Resource stability (steady-state slope within bounded envelopes, no runaway growth).
  4. Graceful shutdown (clean exit on request, not forced kill).
  5. Residue (zero orphan processes).
  A resource stability failure must fail closed.
- **Root Cause**: Disposition logic only checked process existence and sample count; metric statistics were purely informational.
- **Fix**: Implement a defensible steady-state resource stability model checking rate of growth, high-water mark, and first-half vs second-half medians after warm-up.
- **Regression Test**: Test fixture with artificial handle/memory growth asserting disposition `FAIL` on runaway resources.

---

### CT-004: Startup Baseline Invalidates Steady-State Leak Analysis

- **Status**: **CONFIRMED**
- **Observed Behavior**: `Invoke-ResourceSoak.ps1` sampled the process immediately after `proc.Start()` (sample 0 at t=0.5s: working set 13.8 MB, private bytes 2.6 MB, handles 147, threads 6, GDI 0, USER 4). These values represent uninitialized CRT/process startup before Direct2D, COM, DirectWrite, and thread pools initialize. Comparing sample 0 to sample 57 measured normal initialization growth, not a steady-state leak.
- **Expected Contract**: The runner must follow explicit lifecycle phases:
  `LAUNCH` -> `READY_WAIT` -> `WARMUP` -> `STEADY_STATE_BASELINE` -> `MEASURED_SOAK` -> `SHUTDOWN` -> `POST_SHUTDOWN_RESIDUE_CHECK`.
  Steady-state baseline must be captured only after semantic readiness and bounded warm-up.
- **Root Cause**: Lack of phase separation between process initialization and measured soak.
- **Fix**: Add semantic readiness polling (main window HWND, UIA root, or diagnostic ready event) followed by a bounded warm-up phase before taking the steady-state baseline.
- **Regression Test**: Test confirming readiness timeout fails closed, and baseline is taken after warm-up.

---

### CT-005: Retained Stress Evidence Not Exact-Tree-Bound

- **Status**: **CONFIRMED**
- **Observed Behavior**: The committed artifacts in `openspec/changes/external-certification-readiness-and-evidence-convergence/evidence/` recorded `"commit": "c3c14fac57b343e9deeb793e4e6daf1aaca743b2"`, `"exactTreeBound": false`. However, campaign reports and WORKSTATE described the state as having complete durable stress qualification.
- **Expected Contract**: Authoritative retained campaign closure evidence must be generated with `-RequireExactTree` against a committed clean tree, recording `exactTreeBound: true` matching HEAD commit and tree SHA.
- **Root Cause**: Prior evidence was captured during development on an uncommitted dirty working tree and retained without re-executing against the final clean commit.
- **Fix**: Complete all implementation, commit all changes, verify working tree is clean, and regenerate authoritative evidence on the clean commit with `-RequireExactTree`.
- **Regression Test**: Verification that authoritative evidence files have `exactTreeBound: true` and match git HEAD.

---

### CT-006: Required Application Lifecycle x100 and Rapid-Input x50 Evidence Missing

- **Status**: **CONFIRMED**
- **Observed Behavior**: OpenSpec change `external-certification-readiness-and-evidence-convergence` Task 3.2 required:
  `Execute representative application lifecycle and rapid stress suite (launch/ready/shutdown ×100, rapid-input ×50)`.
  Task 3.2 was marked `[x]` complete, but the retained evidence directory contained only `long-run-dialog-focus-50.json` and `long-run-concurrency-20.json`. No `launch/ready/shutdown x100` or `rapid-input x50` artifacts existed.
- **Expected Contract**: What a task specifies must actually be executed and retained, or the task must be formally amended with rationale.
- **Root Cause**: The previous campaign substituted `dialog-focus x50` and `concurrency x20` for the lifecycle/rapid-input requirement without running or retaining the required application lifecycle x100 and rapid-input x50 suites.
- **Fix**:
  1. Implement dedicated runners: application launch/ready/shutdown x100 and rapid-input x50.
  2. Execute both suites and retain machine-readable JSON artifacts.
- **Regression Test**: Artifact existence and schema validation for application lifecycle x100 and rapid-input x50.

---

### CT-007: External Frontier Commands Semantically Mismatched to Tasks

- **Status**: **CONFIRMED**
- **Observed Behavior**:
  1. All 7 `physical-environment` interactive tasks were mapped to `pwsh -File verify/certification/Invoke-LongRunCertification.ps1 -TestTarget ffuia_smoke_tests -Iterations 25`, but no CTest target named `ffuia_smoke_tests` exists.
  2. All 8 `external-provider` tasks (concerning Hyper-V, VMs, guest OS, and provisioning) were mapped to `pwsh -File verify/certification/Invoke-ProviderCertification.ps1 -Provider <provider-name>`, which is actually a vision-critic provider certification script.
- **Expected Contract**:
  1. Interactive tasks must map to real UIA execution adapters (e.g. `Invoke-UiaLongRunCertification.ps1` with valid scenario/group options).
  2. Environment provider tasks must map to `Invoke-EnvironmentProviderCertification.ps1`, distinctly separated from `Invoke-VisionProviderCertification.ps1`.
  3. Commands must not contain unresolved placeholder tokens like `<provider-name>` unless explicitly categorized as operator-templated.
- **Root Cause**: Collapsing disparate external requirements into generic command templates without verifying target existence or semantic lane alignment.
- **Fix**:
  1. Create `Invoke-UiaLongRunCertification.ps1` wrapping real UIA scenarios.
  2. Separate environment provider certification from vision provider certification.
  3. Update all 25 frontier entries in `verify/certification/external-frontier.json`.
  4. Add automated command executability contract test.
- **Regression Test**: Automated validator verifying that every frontier command points to an existing script with valid parameters and resolvable targets.

---

### CT-008: OpenSpec Lifecycle Completion Lacks Authoritative Hosted Binding

- **Status**: **CONFIRMED**
- **Observed Behavior**: In `openspec/lifecycle.json`, the change `external-certification-readiness-and-evidence-convergence` was marked `"state": "completed"`, but its `evidence` array contained only `repository-path` entries, with zero `git-commit` or `hosted-run` entries.
- **Expected Contract**: Per FastFiles lifecycle invariants, a completed change must bind to the exact git commit and hosted CI run ID certifying that commit.
- **Root Cause**: Change state was marked `completed` before the commit was pushed and certified on GitHub Actions.
- **Fix**: Add authoritative `git-commit` and `hosted-run` entries to `openspec/lifecycle.json` upon completion of hosted CI.
- **Regression Test**: Lifecycle schema and project status validation gates.

---

### CT-009: Human Soak Summary Disagrees with Machine-Readable JSON

- **Status**: **CONFIRMED**
- **Observed Behavior**: `resource-soak-summary.md` listed `ad9799fe50cd769555dd85c41838b8d31fcfddd3b7edf869c505fba93fedbe2d` as the artifact SHA-256, while `local-long-run-certification.md` listed `849d9a84fd7c2161db3ac69f14090efe95b2409855be435b85240892040f923e`, and the prose in `WORKSTATE.md` claimed "zero leaks" while metrics showed 175 KB/s working set growth.
- **Expected Contract**: Machine-readable JSON is the single source of truth. Markdown summaries must be generated directly from the JSON artifacts, never manually transcribed. Automated consistency tests must detect any divergence between prose/markdown and JSON.
- **Root Cause**: Manual transcription and double-write hashing desynchronization.
- **Fix**:
  1. Build a generator that produces markdown summaries directly from JSON.
  2. Implement an automated consistency check between summary tables and underlying JSON files.
- **Regression Test**: Automated test asserting markdown metrics match JSON values.

---

### CT-010: Artifact Self-Hash Corrupts Final File Checksum

- **Status**: **CONFIRMED**
- **Observed Behavior**: Both `Invoke-LongRunCertification.ps1` and `Invoke-ResourceSoak.ps1` performed:
  ```powershell
  $jsonText = $resultObject | ConvertTo-Json -Depth 10
  Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM
  $fileBytes = [IO.File]::ReadAllBytes($OutputPath)
  $artifactHash = Compute-Sha256($fileBytes)
  $resultObject.artifactHashes[([IO.Path]::GetFileName($OutputPath))] = $artifactHash
  $jsonText = $resultObject | ConvertTo-Json -Depth 10
  Set-Content -LiteralPath $OutputPath -Value $jsonText -Encoding utf8NoBOM
  ```
  The hash was computed on the file *before* adding `artifactHashes`. Writing the file a second time altered its byte content. Consequently, the hash stored inside the file does NOT match the file's actual hash on disk!
  - Recorded in `long-run-handshake-100.json`: `f6d8873d57c49f830319ed352fb951d7f161323c5bb58e0d258ca71a116d90e7`
  - Actual SHA-256 of `long-run-handshake-100.json`: `2e9294c25eb0a9907719966c65c63e59f84a452567d41d8ace8bc8d50f07ad50`
- **Expected Contract**: A file cannot truthfully store its own SHA-256 without canonical self-exclusion. The recorded hash must match the actual file bytes, or hashes must be maintained in an external manifest or sidecar.
- **Root Cause**: Naive two-pass write where the second write mutates the payload being hashed.
- **Fix**: External manifest hashing via `evidence-manifest.json` and/or sidecar `.sha256` files, plus canonical payload hashing if self-contained hash is required.
- **Regression Test**: Test independently hashing every retained artifact and requiring exact match with the manifest.
