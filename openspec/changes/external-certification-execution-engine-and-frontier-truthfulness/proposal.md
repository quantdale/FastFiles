# Proposal: External Certification Execution Engine and Frontier Truthfulness

## Why

The prior campaign (`external-certification-readiness-and-evidence-convergence`) built
certification prerequisite/preflight scaffolding and is hosted-certified. Independent
review found the remaining gap is execution truthfulness: several "certification"
runners validate prerequisites while their OpenSpec tasks require actual execution:

1. `Invoke-CleanHostCertification.ps1 -InstallCandidate` can record
   `Candidate installation verified in campaign sandbox` without invoking any
   installation, runtime, or uninstallation journey.
2. `Invoke-EnvironmentProviderCertification.ps1` (Hyper-V) verifies module
   availability, elevation, and guest-image shape — provider preflight, not provider
   execution. No payload is transferred, executed, or collected.
3. `Invoke-PrivilegedNtfsCertification.ps1` opens `\\.\C:` and runs isolated parser
   unit tests — raw-volume open plus parser substitutes, not MFT/USN ground-truth
   validation against real retrieved records.
4. `Test-ExternalFrontierInventory.ps1` validates broad lane mappings (physical tasks
   use UIA runner, signing tasks use signing runner) but cannot detect semantic
   mismatch, e.g. a Light+Dark / 100%+150% DPI / High Contrast / multi-width /
   details-pane / single+dual-pane task mapped to one generic
   `-Scenario core-smoke -Iterations 25` invocation.
5. `Invoke-UiaLongRunCertification.ps1` rapid-input claims dual-pane and
   Search/Filters postconditions stronger than the code explicitly enforces.
6. `Invoke-ResourceSoak.ps1` has no independent private-bytes stability gate and
   allows close-without-verification activity transitions.
7. Project status reports `25 external / 0 active-local`, which is only truthful if
   every external task's runner is locally complete. Incomplete runners must remain
   visible as local work (`NOT_IMPLEMENTED`), not collapse into external blockage.

## What Changes

* **Clean-host execution contract**: audit-only mode (READY/BLOCKED_*, never product
  PASS) vs installation-certification mode (real stage→launch→smoke→shutdown→remove
  journey with per-leg checks, fail-closed `BLOCKED_MISSING_INSTALLABLE_CANDIDATE`
  when no installer exists, destructive-operation guardrails).
* **Provider execution state machine**: explicit 14-phase lifecycle
  (DISCOVER→PREFLIGHT→PROVISION→ACTIVATE→WAIT_READY→CHANNEL→TRANSFER→EXECUTE→
  COLLECT_LOGS→COLLECT_RESULTS→PROVENANCE→ROLLBACK→CLEANUP→RESIDUE_CHECK) with
  per-phase timestamps/status/reason/identity/artifacts; preflight-only success never
  yields PASS; deterministic fake-provider fixture covering the full failure
  permutation matrix; guest-result provenance binding (commit/tree/payload/candidate
  digests, VM identity, command, counts, exit code, cleanup disposition).
* **Privileged NTFS ground-truth leg**: campaign-owned fixtures created through normal
  filesystem APIs, normal-filesystem truth captured, privileged/raw observation path
  compared field-by-field; raw-open alone cannot PASS; injectable seams for
  unelevated contract tests; strict read-only raw rule preserved.
* **Signing audit completion**: candidate identity, chain, thumbprint-pin comparison,
  Authenticode, timestamp, negative mismatch tests, fail-closed `BLOCKED_EXTERNAL`
  without credentials; `production-signing` capability only on verified execution.
* **Capability-aware frontier contract**: every external task declares machine-readable
  required capabilities; every runner advertises provided capabilities; inventory
  validation fails on mismatch (DPI task vs generic core-smoke, High Contrast vs
  runner lacking contrast, etc.). UIA runner gains explicit requested-vs-observed
  display-matrix dimensions.
* **UIA/soak semantic tightening**: Pane.1 existence/absence assertions, explicit
  Search/Filters disclosure postconditions, enriched stress artifacts, separate
  private-bytes gate with documented threshold, explicit Idle/Interactive modes,
  verified-close activity transitions, first-class cleanup reporting everywhere.
* **Status/lifecycle truthfulness**: three-state model (NOT_IMPLEMENTED /
  READY_EXTERNAL / EXECUTED); project status surfaces locally-incomplete external
  runners as active local work.

## Impact

* **Files**: `verify/certification/Invoke-*.ps1` runners, `verify/certification/tests/*`,
  `verify/certification/external-frontier.json` + schema, `verify/status/*`,
  this change's tasks/evidence. Zero product-code changes expected.
* **Safety**: no destructive host mutation; campaign-owned disposable dirs, synthetic
  fixtures, read-only raw access preserved.

## Non-Goals

* Procuring production signer credentials, lawful Windows guest images, elevated
  raw-volume hosts, or physical display labs — those stay genuinely external.
* Product-code refactoring without certification-run evidence of a product defect.
* Rewriting historical evidence; corrective evidence supersedes, never edits history.
