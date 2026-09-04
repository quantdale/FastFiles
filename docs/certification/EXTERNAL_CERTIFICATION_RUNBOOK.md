# FastFiles External Certification Runbook

## Overview

This runbook provides deterministic instructions for FastFiles external certification on authorized Windows hosts.

Owner scope directive (2026-09-04): vendor-specific VM-provider execution/testing is intentionally excluded from FastFiles local completion and from this operator runbook unless the owner explicitly reopens that scope. Generic provider-contract/fake-adapter tests may remain in the repository as infrastructure, but no provider-specific guest execution is required here.

Each in-scope package is safety-bounded and self-verifying. Packages output machine-readable JSON artifacts conforming to `verify/certification/schemas/` that can be imported through `verify/certification/Import-CertificationEvidence.ps1`.

---

## 1. Prerequisites and Host Capability Matrix

| Lane | Target Environment | Required Privileges | Host Mutating? | Expected Duration | Primary Runner Script |
| :--- | :--- | :--- | :---: | :---: | :--- |
| **Top-Level Orchestrator** | Any supported Windows 10/11 host | Unprivileged or Elevated | No (audit mode) | < 30s | `Invoke-ExternalCertification.ps1` |
| **Clean-Host Certification** | Fresh / disposable Windows 10/11 host | Elevated (Admin) | Only if `-InstallCandidate` | 1–2 min | `Invoke-CleanHostCertification.ps1` |
| **Privileged NTFS Certification** | Authorized NTFS volume | Elevated (Admin) | No raw writes | < 15s | `Invoke-PrivilegedNtfsCertification.ps1` |
| **Production Signing** | Any Windows machine with signed artifact | None | No | < 10s | `Invoke-SigningCertification.ps1` |
| **Vision Review Provider** | Host with configured real vision provider | None | No | provider-dependent | `Invoke-ProviderCertification.ps1` |
| **Long-Run Stress** | Development / qualification host | None | No | scenario-dependent | `Invoke-LongRunCertification.ps1` / UIA runner |
| **Resource Soak** | Development / qualification host | None | No | 10 min default qualification target | `Invoke-ResourceSoak.ps1` |

---

## 2. Top-Level External Orchestrator

To inspect the host environment and execute applicable in-scope lanes in safe audit mode:

```powershell
pwsh -File verify/certification/Invoke-ExternalCertification.ps1
```

Optional inputs may include a candidate path, expected signer identity, clean-host install request, and exact-tree enforcement. Do not treat an unavailable or owner-excluded lane as a product PASS.

---

## 3. Clean-Host Certification Package

Use a disposable or isolated Windows host to verify absence of development residue, conflict-free deployment, and where an actual installable candidate exists, the install/run/shutdown/remove journey.

### Audit mode

```powershell
pwsh -File verify/certification/Invoke-CleanHostCertification.ps1
```

Audit-only success establishes host readiness only; it is not installation certification.

### Full candidate journey

```powershell
pwsh -File verify/certification/Invoke-CleanHostCertification.ps1 `
    -InstallCandidate `
    -CandidatePath "C:\path\to\FastFilesSetup.exe"
```

Safety rules:

- Existing FastFiles processes/services are treated as contamination unless the invocation explicitly supports the intended scenario.
- The runner must not terminate unrelated processes, remove foreign registry state, or delete non-campaign-owned paths.
- Missing installable candidate remains unavailable rather than being converted into a staged synthetic PASS.

---

## 4. Privileged NTFS Certification Package

Use an authorized elevated NTFS host to verify read-only privileged observation and fixture-to-filesystem ground truth.

```powershell
pwsh -File verify/certification/Invoke-PrivilegedNtfsCertification.ps1 -VolumeRoot "C:"
```

Safety and truth invariants:

- Raw-volume access is strictly read-only.
- Never write raw sectors or directly mutate filesystem metadata.
- Campaign fixtures are created only through normal filesystem APIs in a disposable directory.
- Raw-volume open by itself is insufficient for MFT/USN certification.
- Real default MFT/USN observation backends must execute and match campaign fixture truth before those capabilities may PASS.
- Without elevation, report the corresponding blocked state rather than manufacturing success.

---

## 5. Production Signing Certification Package

Use a real signed candidate and expected production signer identity.

```powershell
pwsh -File verify/certification/Invoke-SigningCertification.ps1 `
    -CandidatePath "C:\path\to\FastFiles.exe" `
    -SignerThumbprint "<approved-production-thumbprint>" `
    -ExpectedSubject "<approved-production-subject>"
```

Required checks include candidate identity, Authenticode validity, signer-pin match, chain/timestamp policy where required, and negative mismatch behavior. Missing production credentials/material remain external and unavailable; test certificates do not satisfy production certification.

---

## 6. Real Vision Provider Certification Package

Use this lane only for sanitized visual-review evidence when a real vision provider is configured.

```powershell
$env:FASTFILES_VISION_PROVIDER_KEY = "<provider-key>"
$env:FASTFILES_VISION_PROVIDER_ENDPOINT = "<approved-endpoint>"
pwsh -File verify/certification/Invoke-ProviderCertification.ps1 -Provider real
$env:FASTFILES_VISION_PROVIDER_KEY = $null
$env:FASTFILES_VISION_PROVIDER_ENDPOINT = $null
```

Security/privacy rules:

- Never commit or retain credentials.
- Sanitize visual inputs according to the repository privacy gate.
- Fake critic/provider fixtures are regression infrastructure only and cannot satisfy real external visual review.

---

## 7. Evidence Import and Staleness Verification

When in-scope external evidence is returned from an authorized host:

```powershell
pwsh -File verify/certification/Import-CertificationEvidence.ps1 `
    -EvidenceBundlePath "C:\path\to\evidence_directory" `
    -TargetChange "external-certification-readiness-and-evidence-convergence"
```

Importer expectations:

1. schema conformance;
2. secret scan;
3. exact commit/tree and candidate provenance where required;
4. artifact/sidecar integrity;
5. lane-specific staleness analysis;
6. no PASS inferred from skipped or unavailable execution.

---

## 8. Current Completion Scope

FastFiles local certification closure currently concerns:

- post-fix rapid-input stress;
- real default MFT/USN readers and privileged fixture comparison;
- visual-matrix engine-connected context or truthful external reclassification;
- clean-host/signing/vision tooling truthfulness where applicable;
- full local validation, exact-tree qualification, lifecycle convergence, and exact-final-SHA hosted CI.

Vendor-specific VM-provider testing is intentionally absent from this list and must stay absent unless the owner explicitly changes scope.
