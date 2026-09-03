# Proposal: External Certification Readiness and Evidence Convergence

## Why

FastFiles has completed comprehensive product hardening, visual intelligence autopilot, overnight reliability sweeps, and hosted CI certification across all six jobs. However, the remaining 25 unchecked OpenSpec tasks across 7 changes represent an external/environment certification frontier (production signing pins, privileged NTFS raw-volume access, clean-host installation, external guest VM / vision providers, and physical multi-display interactive topologies).

Currently, these tasks are recognized as `external-evidence-only` in `lifecycle.json` and documented in `docs/agent/WORKSTATE.md`, but:
1. Long-run stress and resource soak results are recorded primarily in prose rather than committed, machine-readable, exact-tree-bound JSON artifacts;
2. An external operator in an authorized, elevated, clean, or production-signing environment lacks one-shot, deterministic certification scripts to execute and return verified evidence;
3. There is no automated, fail-closed mechanism to validate imported external evidence and enforce staleness rules when product binaries or interfaces change;
4. The external frontier is tracked as raw counts rather than an authoritative, structured inventory binding every task to its required host capabilities, credentials, privileges, execution commands, and result schemas.

## What Changes

* **Authoritative External Frontier Inventory & Schema**: Produce `verify/certification/external-frontier.json` and schema governing all 25 unchecked tasks, categorizing them into production signing, privileged NTFS, clean host, external provider, and physical environment.
* **Durable Long-Run & Soak Schemas**: Establish `verify/certification/schemas/long-run-result.schema.json`, `resource-soak-result.schema.json`, and `certification-result.schema.json` with strict validation rules (no negative counts, no pass-exceeding-completed, no success-with-failures, no all-zero hashes, no exact-tree claims on dirty worktrees).
* **Durable Certification Runners**: Implement `verify/certification/Invoke-LongRunCertification.ps1` and `Invoke-ResourceSoak.ps1` to produce sanitized, machine-readable evidence bound to exact Git commit and tree provenance.
* **One-Shot External Certification Packages**:
  - `Invoke-SigningCertification.ps1`: Authenticode, thumbprint, pin verification, and unsigned candidate rejection.
  - `Invoke-PrivilegedNtfsCertification.ps1`: Safe read-only MFT, USN journal, and raw volume access validation with campaign test fixture isolation.
  - `Invoke-CleanHostCertification.ps1`: Default audit/readiness mode, conflict detection, optional isolated installation, runtime validation, and zero-residue cleanup.
  - `Invoke-ProviderCertification.ps1`: Provider credential and capability detection, redacted test workload, schema validation, and fail-closed FakeCritic rejection.
* **External Certification Orchestrator**: Implement `Invoke-ExternalCertification.ps1` to discover local host capabilities, run applicable lanes, and produce an aggregate capability matrix without "success by skip".
* **Evidence Import & Staleness Engine**: Implement `Import-CertificationEvidence.ps1` to validate external bundles, bind commit/tree provenance, and enforce lane-specific staleness rules.
* **Durable Stress Re-Execution**: Re-run representative IPC, lifecycle, and soak suites to commit durable machine-readable evidence under `openspec/changes/external-certification-readiness-and-evidence-convergence/evidence/`.
* **External Certification Runbook**: Document clear, copy-pasteable operator workflows in `docs/certification/EXTERNAL_CERTIFICATION_RUNBOOK.md`.

## Impact

* **New Artifacts**: `verify/certification/*` (schemas, runners, orchestrator, importer, tests) and `docs/certification/EXTERNAL_CERTIFICATION_RUNBOOK.md`.
* **Product Code Freeze**: Zero product code modifications unless certification preparation exposes an undeniable behavioral defect.
* **Repository Truth**: OpenSpec frontier gains deterministic paths for every remaining task; local stress evidence becomes permanently machine-readable.

## Non-Goals

* Weaken fail-closed production signing pins or generate fake self-signed "production" certificates.
* Write to raw disk sectors or mutate live volume MFT/USN structures.
* Destructively uninstall software or alter registry on arbitrary hosts.
* Fabricate passes for unavailable external environments or claim FakeCritic represents real provider validation.
* Re-run generic product hardening sweeps without new evidence.
