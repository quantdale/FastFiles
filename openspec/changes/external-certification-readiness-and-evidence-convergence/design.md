# Design: External Certification Readiness and Evidence Convergence

## Context

FastFiles has completed product-level development, Visual Intelligence Autopilot, overnight hardening, and multi-preset CTest validation. The 25 remaining unchecked tasks in OpenSpec belong exclusively to the external frontier:
1. Production code signing and pin tables (5 tasks);
2. Privileged NTFS raw MFT/USN scanning (2 tasks);
3. Clean-host installation, execution, and zero-residue cleanup (3 tasks);
4. External environment providers (VM/Hyper-V, cloud, vision critic) (8 tasks);
5. Physical multi-display, DPI, and interactive desktop matrices (7 tasks).

These tasks cannot be legitimately satisfied by development mocks, fake roots, or unprivileged non-interactive runners. However, having them exist only as blocked placeholders creates ambiguity. This design formalizes the external frontier into deterministic, one-shot certification tooling with durable evidence schemas and strict safety bounds.

## Decisions

### D1 — Authoritative Machine-Readable External Frontier

All 25 unchecked tasks are inventoried in `verify/certification/external-frontier.json`, validated against `verify/certification/schemas/external-frontier.schema.json`. Each entry defines:
- `change`, `taskId`, `description`, `frontierGroup`, `disposition`, `categories`, `status`, and `reason`;
- `requiredCapabilities`, `requiredCredentials`, `requiredPrivileges`, and `requiredHostProperties`;
- `evidenceRequirements`, `executionCommand`, and `resultSchema`.

No task disposition is artificially altered to force `activeLocalOpenTasks = 0`. Instead, the inventory provides deterministic reproducibility for every task.

### D2 — Durable Long-Run and Soak Evidence Schemas

Stress and soak evidence is moved from informal prose in `WORKSTATE.md` to structured, exact-tree-bound JSON documents:
- `long-run-result.schema.json`: Enforces non-negative counts, `passed <= completed`, no success with nonzero failures, exact Git commit and tree bindings, non-zero SHAs, timing percentiles (min, median, p95, max), and first-failure diagnostics.
- `resource-soak-result.schema.json`: Records FastFiles and engine PIDs, working set, private bytes, handles, threads, GDI/USER objects, CPU metrics across periodic samples, linear trend rates, and activity transitions.
- `certification-result.schema.json`: Unified envelope for lane runners reporting explicit status (`PASS`, `FAIL`, `BLOCKED_ENVIRONMENT`, `BLOCKED_NOT_ELEVATED`, `BLOCKED_UNSUPPORTED_FILESYSTEM`, `BLOCKED_RAW_VOLUME_ACCESS`, `REQUIRED-BUT-UNAVAILABLE`).

### D3 — Reusable Stress and Soak Runners

- `Invoke-LongRunCertification.ps1`: Executes a designated test target repeatedly with per-iteration timeouts, stops on first genuine failure, computes timing statistics, captures Git provenance, rejects dirty exact-tree claims, and emits sanitized JSON.
- `Invoke-ResourceSoak.ps1`: Launches FastFiles in diagnostic mode, samples performance counters via Win32 process queries, executes periodic UI activity transitions, computes summary statistics and drift rates, and ensures process tree cleanup.

### D4 — Clean-Host Certification Package

`Invoke-CleanHostCertification.ps1` operates by default in **Audit Mode**:
1. Verifies OS architecture, version, and privilege level;
2. Audits for conflicts (existing processes, services, tasks, registry residue);
3. Verifies unsigned or signed candidate hash and provenance;
4. If `-InstallCandidate` is explicitly passed on a disposable host: installs the package, starts services/UI, exercises smoke checks, initiates clean shutdown, uninstalls, and audits that zero residue remains;
5. Fails closed with `BLOCKED_ENVIRONMENT` if host conflicts exist, never performing blind destructive uninstallation.

### D5 — Privileged NTFS Read-Only Certification Package

`Invoke-PrivilegedNtfsCertification.ps1` strictly enforces read-only access to raw volume structures:
1. Confirms process elevation (`SeBackupPrivilege`, `SeRestorePrivilege`);
2. Confirms volume filesystem is NTFS;
3. Validates read-only raw MFT opening (`\\.\<Drive>:`) and USN journal query;
4. Creates isolated test fixtures in a dedicated campaign temporary directory to verify ground-truth path resolution and record parsing;
5. Emits explicit blocked codes: `BLOCKED_NOT_ELEVATED`, `BLOCKED_UNSUPPORTED_FILESYSTEM`, `BLOCKED_RAW_VOLUME_ACCESS`;
6. Never writes raw sectors or alters live volume metadata.

### D6 — Production Signing Certification Package

`Invoke-SigningCertification.ps1` evaluates release candidates against production trust policy without generating fake production credentials:
1. Validates Authenticode signature integrity and timestamp;
2. Validates certificate subject, chain, and signer thumbprint against expected pins;
3. Verifies peer-image identity and hash matches;
4. Rejects unsigned candidates and candidates with untrusted roots;
5. Returns `REQUIRED-BUT-UNAVAILABLE` if production inputs or signing materials are absent; test PKI is explicitly classified as test PKI.

### D7 — Real Vision Provider Certification Package

`Invoke-ProviderCertification.ps1` provides an opt-in path for external vision critic evaluation:
1. Inspects environment variables for provider configuration without echoing secrets;
2. Submits sanitized, redacted evaluation images;
3. Validates returned schema against visual finding specifications;
4. Enforces that FakeCritic never satisfies a real-provider requirement (returns `REQUIRED-BUT-UNAVAILABLE` when unconfigured).

### D8 — Top-Level Certification Orchestrator

`Invoke-ExternalCertification.ps1` inspects host capabilities (elevation, OS, filesystem, desktop interactivity, configured providers, signing materials), executes applicable lane runners, and synthesizes an aggregate capability matrix:
- Overall status: `PASS`, `PARTIAL`, `BLOCKED_EXTERNAL`, or `FAIL`.
- Never reports `PASS` merely because unavailable lanes were skipped.

### D9 — Evidence Import and Staleness Engine

`Import-CertificationEvidence.ps1` safely consumes external certification bundles:
1. Validates JSON payload against the corresponding schema;
2. Rejects zero SHAs, dirty trees, and contradictory iteration counts;
3. Binds candidate Git commit, tree, and executable SHA-256;
4. Enforces lane staleness rules:
   - Signing evidence is invalidated if binaries, pin tables, or trust code change;
   - Privileged NTFS evidence is invalidated if parser or volume access code changes;
   - Clean-host evidence is invalidated if installer or packaging manifests change;
   - Provider evidence is invalidated if critic adapters or finding schemas change.

## Safety and Security Invariants

1. **No Secret Leakage**: Runner outputs and committed JSON artifacts must never contain passwords, API keys, private keys, or PFX material.
2. **No Host Corruption**: Scripts must never delete foreign files, kill foreign processes, or clear machine-wide event logs.
3. **No Fake Passes**: Mocks and test PKI verify script execution mechanics only; external task states remain `BLOCKED_ENVIRONMENT` or `REQUIRED-BUT-UNAVAILABLE` until real credentials/environments execute them.
4. **Exact-Tree Binding**: Evidence claiming committed status must match `git rev-parse HEAD` and `git write-tree` on a clean worktree.
