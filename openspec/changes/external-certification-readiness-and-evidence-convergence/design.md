# Design: External Certification Readiness and Evidence Convergence

## Context

FastFiles completed product-level development, Visual Intelligence Autopilot, overnight hardening, and multi-preset validation. The historical external frontier included production signing, privileged NTFS, clean-host qualification, external provider requirements, and physical display/interactive matrices.

Owner scope directive (2026-09-04): vendor-specific VM-provider execution/testing is intentionally excluded from FastFiles local completion. Generic provider contracts and fake-adapter infrastructure may remain, but no provider-specific guest image, credential, channel, provisioning, or end-to-end VM execution is a completion requirement unless the owner explicitly reopens that scope.

## Decisions

### D1 — Authoritative Machine-Readable External Frontier

External tasks are inventoried in `verify/certification/external-frontier.json` with task identity, frontier group, disposition, required capabilities/credentials/privileges/host properties, evidence requirements, execution command, and result schema. Current readiness must distinguish locally incomplete in-scope machinery from genuinely external prerequisites; owner-excluded vendor-specific VM work must not appear as local completion work.

### D2 — Durable Long-Run and Soak Evidence Schemas

Structured evidence records counts, timings, exact Git commit/tree provenance, executable hashes, resource metrics, and first-failure information. Evidence claiming committed provenance must fail closed on dirty trees or contradictory counts.

### D3 — Reusable Stress and Soak Runners

`Invoke-LongRunCertification.ps1` repeats a designated real test with bounded timeouts, stops on genuine failure, proves the target executed, computes timing statistics, and emits sanitized provenance-bound JSON.

`Invoke-ResourceSoak.ps1` separates launch/readiness/warm-up from measured steady state, samples resource metrics, optionally executes real semantic UI activity, verifies graceful shutdown/residue, and never substitutes simulated activity for product execution.

### D4 — Clean-Host Certification Package

`Invoke-CleanHostCertification.ps1` separates audit-only readiness from actual candidate execution. Installation/runtime/uninstallation certification requires real execution proof and campaign-owned cleanup. Conflicting host state fails closed rather than triggering destructive cleanup of foreign state.

### D5 — Privileged NTFS Read-Only Certification Package

`Invoke-PrivilegedNtfsCertification.ps1` enforces read-only raw-volume access, creates campaign fixtures through normal filesystem APIs, and compares real privileged MFT/USN observations against filesystem truth. Raw-open alone and parser-only tests cannot satisfy ground-truth certification.

### D6 — Production Signing Certification Package

`Invoke-SigningCertification.ps1` validates candidate identity, Authenticode integrity, chain/timestamp policy, and expected signer pins. Missing production inputs remain unavailable; test PKI cannot satisfy production certification.

### D7 — Real Vision Provider Certification Package

`Invoke-ProviderCertification.ps1` is reserved for real external visual-review evidence over sanitized inputs. Fake critic/provider fixtures validate mechanics only and cannot satisfy real-provider requirements.

### D8 — Generic Environment-Provider Contract

The repository may retain a generic environment-provider lifecycle/state machine and deterministic fake-adapter tests covering phase ordering, provenance, cleanup, and failure classification. Under the owner scope directive, this generic infrastructure does not create a requirement to implement or execute any specific VM provider.

### D9 — Top-Level Certification Orchestrator

`Invoke-ExternalCertification.ps1` inspects in-scope host capabilities and synthesizes PASS/PARTIAL/BLOCKED_EXTERNAL/FAIL without “success by skip.” Owner-excluded vendor-specific VM lanes are not local completion gates.

### D10 — Evidence Import and Staleness

`Import-CertificationEvidence.ps1` validates schema/provenance/integrity, rejects contradictory or stale evidence, scans for secrets, and applies lane-specific staleness rules for signing, privileged NTFS, clean-host, and visual-provider evidence.

## Safety and Security Invariants

1. **No Secret Leakage** — evidence must not contain passwords, API keys, private keys, or credential material.
2. **No Host Corruption** — do not delete foreign files, kill unrelated processes, mutate unrelated system state, or write raw disk sectors.
3. **No Fake Passes** — mocks and test PKI validate mechanics only; unavailable in-scope external work remains blocked.
4. **Exact-Tree Binding** — authoritative retained evidence must match the intended clean committed source/tree and executable hashes.
5. **Owner Scope Wins** — vendor-specific VM-provider testing remains excluded unless explicitly re-enabled by the owner.
