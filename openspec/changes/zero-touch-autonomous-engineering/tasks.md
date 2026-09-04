## 1. Subtree Gating

- [x] 1.1 Implement longest-prefix include/exclude matching.
- [x] 1.2 Pass per-volume rules into the index pipeline on configuration changes.
- [x] 1.3 Apply subtree gating to MFT ingestion.
- [x] 1.4 Apply subtree gating to USN deltas.
- [x] 1.5 Apply subtree gating to reconciliation.
- [x] 1.6 Apply subtree gating to rebuilds.
- [x] 1.7 Implement bounded deferred-parent handling.
- [x] 1.8 Add rule/deferred/reconciliation tests.
- [x] 1.9 Build and run focused/full validation.

## 2. UIA Driver

- [x] 2.1 Implement the PowerShell UIA driver, semantic element identity, patterns, events, input, timeouts, and tree dumps.
- [x] 2.2 Implement availability probing and precise skip reasons without pixel fallback.
- [x] 2.3 Add headless UIA-driver contract tests.
- [x] 2.4 Implement deterministic diagnostic tree dumps and semantic-target failures.

## 3. UIA End-to-End Validation

- [x] 3.1 Verify FastFiles launch/main-window readiness.
- [x] 3.2 Verify multi-column navigation and semantic item-provider behavior where engine data is available.
- [x] 3.3 Verify selection/scroll provider surfaces and data-dependent behavior where available.
- [x] 3.4 Verify connection/degraded state semantically.
- [x] 3.5 Verify dialogs, search, error surfaces, and UI responsiveness.
- [x] 3.6 Verify observable rendering geometry/state; unsupported custom surfaces remain explicit skips.
- [x] 3.7 Verify drag-out behavior where semantic item data exists.
- [x] 3.8 Verify drag-in behavior where semantic item data exists.
- [x] 3.9 Verify intra-FastFiles drag behavior where semantic item data exists.
- [x] 3.10 Verify Storage Analysis UI/degraded behavior and preserve unavailable privileged/treemap paths honestly.
- [x] 3.11 Verify Search UI/service-state behavior and semantic result surfaces where engine data exists.
- [x] 3.12 Retain UIA trees/logs/screenshots/filesystem evidence per scenario.

## 4. Test-PKI Code Signing

- [x] 4.1 Implement isolated test-code-signing capability.
- [x] 4.2 Generate/use an isolated self-signed development signing certificate without committing secrets.
- [x] 4.3 Sign FastFiles development binaries with the test certificate.
- [x] 4.4 Verify Authenticode and pinned-thumbprint behavior.
- [x] 4.5 Implement the injectable production-certificate gate; missing production material remains explicitly unavailable.
- [x] 4.6 Ignore all generated cert/key material.

## 5. Signed Install and Service Validation

- [x] 5.1 Exercise available signed-install/service integrity checks without destroying pre-existing protected installations.
- [x] 5.2 Exercise raw-volume/journal/service scan surfaces where implemented; preserve stubbed/unavailable outcomes honestly.
- [x] 5.3 Verify service unexpected-termination recovery; preserve unavailable crash-dump paths honestly.
- [x] 5.4 Retain identity/privilege/raw-volume/service/rollback evidence.
- [x] 5.5 Preserve explicit elevation/unavailable evidence rather than fabricating execution.

## 6. Historical Disposable VM Provider Work

Owner scope directive (2026-09-04): vendor-specific VM-provider execution/testing is **out of scope** for FastFiles local completion. The task IDs below are retained only because historical lifecycle metadata references them. They must not be treated as current local work unless the owner explicitly reopens this scope.

- [x] 6.1 Historical environment/media discovery evidence retained; no further vendor-specific VM work required.
- [x] 6.2 Historical lawful-media acquisition investigation retained; no further vendor-specific VM work required.
- [ ] 6.3 Historical guest provisioning/channel task — **OWNER-EXCLUDED; NOT A COMPLETION GATE**.
- [ ] 6.4 Historical guest toolchain/reference-snapshot task — **OWNER-EXCLUDED; NOT A COMPLETION GATE**.
- [x] 6.5 Generic environment-provider lifecycle/plumbing exists; provider-specific implementation details remain repository infrastructure, not completion requirements.
- [ ] 6.6 Historical cross-provider live-guest equivalence task — **OWNER-EXCLUDED; NOT A COMPLETION GATE**.
- [x] 6.7 Historical fallback-environment evaluation retained; no vendor-specific VM execution is required by current owner scope.

## 7. Resumable Orchestrator, Future-Agent Docs, Flaky-Test Policy, Gate

- [x] 7.1 Implement the persistent resumable autonomous phase machine with bounded timeouts/retries and deterministic exits.
- [x] 7.2 Implement machine-readable status reporting with authoritative open-task counts.
- [x] 7.3 Implement flaky-test policy: preserve initial failure, stress/reproduce, capture diagnostics, root-cause, fix, and require repeated clean verification; retry-to-green alone is not terminal.
- [x] 7.4 Document the autonomous entry point, state file, dependency graph, future-agent intake, and machine-readable contract.
- [x] 7.5 Exercise the full autonomous loop and four-state archive gate end-to-end with explicit external/unavailable outcomes.
- [x] 7.6 Close tasks only from required verification/evidence. Historical vendor-specific VM task IDs 6.3/6.4/6.6 remain unchecked for lifecycle compatibility but are owner-excluded and must not reappear as local completion work.
