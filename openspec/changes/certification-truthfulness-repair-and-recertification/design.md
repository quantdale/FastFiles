# Design: Certification Truthfulness Repair and Recertification

## Architectural Overview

This design addresses all 10 certification truthfulness defects by establishing an evidence standard grounded in provable execution, exact provenance, fail-closed contracts, and machine-readable authority.

```
       +-----------------------------------------------------------+
       |               Exact-Tree Implementation Commit             |
       +-----------------------------------------------------------+
                                     |
              +----------------------+----------------------+
              |                                             |
   +-----------------------+                     +-----------------------+
   |  Long-Run Hardening   |                     |     Soak Redesign     |
   | - CTest json-v1 disc. |                     | - 7-Phase Lifecycle   |
   | - Count == 1 enforce  |                     | - Ready + Warmup      |
   | - Execution proof     |                     | - Real UIA transitions|
   | - Direct exe pref     |                     | - Steady-state slope  |
   | - UIA long-run runner |                     | - Graceful shutdown   |
   +-----------------------+                     +-----------------------+
              |                                             |
              +----------------------+----------------------+
                                     |
       +-----------------------------------------------------------+
       |        Evidence Manifest & Independent Hash Verification  |
       | - Canonical hashing / External manifest                   |
       | - Logical repo-relative path sanitization                 |
       | - exactTreeBound == true verification                     |
       +-----------------------------------------------------------+
                                     |
       +-----------------------------------------------------------+
       |             OpenSpec & Hosted Exact-SHA Binding           |
       | - openspec/lifecycle.json: git-commit + hosted-run        |
       | - 6/6 GitHub Actions green run on exact tip commit        |
       +-----------------------------------------------------------+
```

## Detailed Subsystem Designs

### 1. Long-Run Certification Runner (`Invoke-LongRunCertification.ps1`)

- **Discovery Gate**: Prior to the execution loop, discover tests using `ctest --test-dir <preset> -R ^<target>$ --show-only=json-v1` (or direct executable match).
  - If matches == 0: Throw terminating error `INVALID_TARGET: Target '<target>' matched zero tests in preset '<preset>'`.
  - If matches > 1: Throw terminating error `AMBIGUOUS_TARGET: Target '<target>' matched multiple tests (<count>)`.
  - If matches == 1: Proceed with resolved executable or CTest invocation.
- **Execution Proof**: During each iteration, capture stdout/stderr. Verify that CTest actually ran the test by asserting that output contains `1/1 Test #` or `Test #...: <target>` and `Passed`, and does NOT contain `No tests were found!!!`.
- **Direct Executable Execution**: If `<target>.exe` exists in `build/<preset>`, prefer direct execution. Record executable SHA-256 hash, duration, and process exit code.

### 2. UIA Long-Run Adapter (`Invoke-UiaLongRunCertification.ps1`)

- Reusable adapter wrapping FastFiles UIA execution for interactive and stress certification.
- Provides support for:
  - `application-lifecycle`: Iterations of launch -> wait for semantic readiness (`FastFilesMainWindow` HWND, UIA root) -> graceful close via `WM_CLOSE`/`CloseMainWindow` -> verify process exit within 5s -> residue check (zero orphan processes).
  - `rapid-input`: Iterations of rapid focus/command palette/search/pane navigation transitions with semantic assertions at each step.
  - `core-smoke`: Iteration wrapper around `Invoke-UiaCoreSmoke`.
- Records timing metrics (min, median, p95, max), per-iteration assertions, and produces schema-compliant long-run JSON results.

### 3. Resource Soak Engine (`Invoke-ResourceSoak.ps1`)

- **7-Phase Lifecycle**:
  1. `LAUNCH`: Start target executable in diagnostic sandbox environment (`FASTFILES_DIAGNOSTIC=1`, `FASTFILES_SOAK_MODE=1`).
  2. `READY_WAIT`: Poll for main window HWND, UIA root element, and diagnostic ready event with a bounded timeout (15 seconds). Fail closed (`FAIL` or `BLOCKED_ENVIRONMENT`) if readiness is not reached.
  3. `WARMUP`: Bounded warm-up interval (minimum 10 seconds or 1 core transition) allowing CRT, COM, D2D, and thread pool initialization to settle.
  4. `STEADY_STATE_BASELINE`: Sample working set, private bytes, handles, threads, GDI, USER objects, and engine PID. This is the authoritative baseline against which leaks and drifts are evaluated.
  5. `MEASURED_SOAK`: Sample process metrics at regular intervals. In interactive mode, invoke real UIA transitions with explicit postconditions (opened, focused, closed, duration). In `-IdleOnly` mode, measure steady-state idle resources without emitting fake transition records.
  6. `SHUTDOWN`: Issue graceful close request (`CloseMainWindow()`, wait up to 5s). Distinguish `graceful` exit from `forced` termination. A forced kill invalidates graceful shutdown disposition.
  7. `POST_SHUTDOWN_RESIDUE_CHECK`: Inspect system process census for campaign-owned orphan `FastFiles.exe` or `FastFilesEngine.exe` processes. Zero orphans required for pass.
- **Steady-State Resource Stability Model**:
  - Growth Rate Slope: Linear slope over steady-state interval ($B/s$ for memory, $count/s$ for handles).
  - First-Half vs Second-Half Median: Detect sustained upward drift versus plateaued cache warming.
  - Maximum vs Baseline Ratio: Bound peak growth within sanity envelopes (e.g. handle growth $\le 150$ over baseline; working set slope $\le 50$ KB/s steady state).
  - Disposition Composition: `overall = survival && functionalActivity && resourceStability && shutdownGraceful && residueZero`.

### 4. Taxonomy and External Frontier Alignment

- Distinguish **Environment Providers** from **Vision Critic Providers**:
  - `Invoke-EnvironmentProviderCertification.ps1`: Exercises environment provider lifecycle (`preflight`, `provision`, `activate`, `collectLogs`, `cleanup`) for `local`, `hyperv`, `vmware`, etc.
  - `Invoke-VisionProviderCertification.ps1`: Renamed from `Invoke-ProviderCertification.ps1` to cleanly represent Visual Intelligence critic provider certification and FakeCritic fail-closed validation.
- Audit all 25 frontier tasks in `verify/certification/external-frontier.json`:
  - Map physical/interactive tasks to `Invoke-UiaLongRunCertification.ps1` or specific visual matrix scripts, not `ffuia_smoke_tests`.
  - Map environment provider tasks to `Invoke-EnvironmentProviderCertification.ps1`.
  - Ensure all commands are syntactically valid and free of unresolved placeholders unless classified as operator templates.

### 5. Artifact Hash Integrity and Manifest Architecture

- **Manifest-Based Integrity**: Maintain `evidence-manifest.json` recording the SHA-256 hashes of all generated JSON, markdown, and log artifacts computed on final closed file bytes.
- **Canonical Self-Exclusion**: If a result JSON file includes an `artifactHashes` field, compute the hash of the serialized JSON object with the `artifactHashes` field set to an empty object `{}` or omit the self-referential hash in favor of the manifest.
- **Path Sanitization**: Normalize all recorded paths to repository-relative forward-slash paths (`build/debug/src/ui/FastFiles.exe`) to eliminate machine-specific developer directory leaks.
- **Markdown Generator**: Generate `resource-soak-summary.md` and long-run markdown tables programmatically from the authoritative JSON artifacts.
