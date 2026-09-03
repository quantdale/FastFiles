# FastFiles External Certification Runbook

## Overview

This runbook provides deterministic, one-shot instructions for operators executing FastFiles external certification packages on authorized, clean, elevated, or provider-enabled Windows hosts.

Each package is deterministic, safety-bounded, and self-verifying. Packages output machine-readable JSON artifacts conforming to `verify/certification/schemas/` that can be imported back into the repository via `verify/certification/Import-CertificationEvidence.ps1`.

---

## 1. Prerequisites and Host Capability Matrix

| Lane | Target Environment | Required Privileges | Host Mutating? | Expected Duration | Primary Runner Script |
| :--- | :--- | :--- | :---: | :---: | :--- |
| **Top-Level Orchestrator** | Any supported Windows 10/11 host | Unprivileged or Elevated | No (audit mode) | < 30s | `Invoke-ExternalCertification.ps1` |
| **Clean-Host Certification** | Fresh / disposable Windows 10/11 host | Elevated (Admin) | Only if `-InstallCandidate` | 1–2 min | `Invoke-CleanHostCertification.ps1` |
| **Privileged NTFS Certification** | Authorized NTFS volume | Elevated (Admin) | No (read-only raw volume) | < 15s | `Invoke-PrivilegedNtfsCertification.ps1` |
| **Production Signing** | Any Windows machine with signed artifact | None | No | < 10s | `Invoke-SigningCertification.ps1` |
| **External Provider** | Host with configured vision / VM provider | None | No (sandboxed workload) | < 30s | `Invoke-ProviderCertification.ps1` |
| **Long-Run Stress** | Development / qualification host | None | No | 1–5 min | `Invoke-LongRunCertification.ps1` |
| **Resource Soak** | Development / qualification host | None | No | ~5 min (300s) | `Invoke-ResourceSoak.ps1` |

---

## 2. Top-Level External Orchestrator

To inspect the host environment, discover capabilities, and execute all applicable certification lanes in safe audit mode:

```powershell
pwsh -File verify/certification/Invoke-ExternalCertification.ps1
```

### Optional Switches:
- `-CandidatePath <path>`: Path to production signed installer or executable.
- `-SignerThumbprint <hash>`: Expected SHA-1 thumbprint of the production signing certificate.
- `-IncludeCleanHostInstall`: Request isolated installation on a disposable clean host.
- `-RequireExactTree`: Reject execution if git working tree contains uncommitted changes.

### Output:
Emits consolidated capability matrix and lane results to:
`verify/runs/certification/orchestrator/external-certification-<timestamp>.json`

---

## 3. Clean-Host Certification Package

Used on a disposable or isolated Windows host to verify absence of development residue, confirm conflict-free deployment, and optionally exercise clean install/run/uninstall lifecycles.

### Step 1: Run Audit Mode (Default / Non-Destructive)
```powershell
pwsh -File verify/certification/Invoke-CleanHostCertification.ps1
```

### Step 2: Run Full Install & Zero-Residue Lifecycle (Disposable Host Only)
```powershell
pwsh -File verify/certification/Invoke-CleanHostCertification.ps1 `
    -InstallCandidate `
    -CandidatePath "C:\path\to\FastFilesSetup.exe"
```

### Safety Rules:
- The runner audits for running FastFiles processes or existing `FastFilesIndexSvc`.
- If pre-existing state is detected, it fails closed with `BLOCKED_ENVIRONMENT`. It will **never** blindly terminate unrelated processes or delete foreign registry keys.

---

## 4. Privileged NTFS Certification Package

Used on an authorized host to verify privileged read-only access to NTFS MFT records and USN change journals.

### Execution Command (Run in Elevated PowerShell):
```powershell
pwsh -File verify/certification/Invoke-PrivilegedNtfsCertification.ps1 -VolumeRoot "C:"
```

### Safety & Invariants:
- **Strictly Read-Only**: Opens raw volume (`\\.\C:`) with `FILE_SHARE_READ | FILE_SHARE_WRITE` and `GENERIC_READ`.
- Never writes raw sectors, alters filesystem metadata, or disables Windows security.
- Verification fixtures are written exclusively to an isolated campaign temporary directory.
- If executed without elevation, reports `BLOCKED_NOT_ELEVATED` (not FAIL).

---

## 5. Production Signing Certification Package

Used to verify an Authenticode-signed candidate binary against production pinning policies without manufacturing fake credentials.

### Execution Command:
```powershell
pwsh -File verify/certification/Invoke-SigningCertification.ps1 `
    -CandidatePath "C:\path\to\FastFiles.exe" `
    -SignerThumbprint "A1B2C3D4E5F6A1B2C3D4E5F6A1B2C3D4E5F6A1B2" `
    -ExpectedSubject "CN=Quantdale Production, O=Quantdale"
```

### Verification Checks:
1. **Unsigned Candidate Rejection**: Confirms that unsigned binaries are rejected as `NotSigned`.
2. **Authenticode Integrity**: Confirms signature status is cryptographically `Valid`.
3. **Pin Enforcement**: Confirms the signer certificate thumbprint matches the expected production pin.
4. **RFC3161 Timestamp**: Confirms valid counter-signature timestamp is embedded.
5. If signed inputs are omitted, reports `REQUIRED-BUT-UNAVAILABLE` (fail-closed).

---

## 6. Real Vision Provider Certification Package

Used to certify external vision critic capabilities against sanitized visual catalog images.

### Step 1: Configure Credentials in Environment
```powershell
$env:FASTFILES_VISION_PROVIDER_KEY = "your-provider-api-key"
$env:FASTFILES_VISION_PROVIDER_ENDPOINT = "https://your-provider-endpoint.com/v1"
```

### Step 2: Execute Provider Certification
```powershell
pwsh -File verify/certification/Invoke-ProviderCertification.ps1 -Provider real
```

### Step 3: Clear Credentials
```powershell
$env:FASTFILES_VISION_PROVIDER_KEY = $null
$env:FASTFILES_VISION_PROVIDER_ENDPOINT = $null
```

### Security & Privacy Rules:
- **No Credentials in Output**: The runner scans and redacts all secrets before writing artifacts.
- **FakeCritic Rejection**: Passing `-Provider fake` is rejected as `FAIL`. FakeCritic is permitted only for CI regression fixtures, not external provider certification.

---

## 7. Evidence Import and Staleness Verification

When external certification evidence is returned from an authorized host, import it into the repository:

### Validation & Import Command:
```powershell
pwsh -File verify/certification/Import-CertificationEvidence.ps1 `
    -EvidenceBundlePath "C:\path\to\evidence_directory" `
    -TargetChange "external-certification-readiness-and-evidence-convergence"
```

### Audits Performed:
1. **Schema Conformance**: Validates against `verify/certification/schemas/*.schema.json`.
2. **Secret Scan**: Scans all fields for private keys, passwords, and tokens.
3. **Commit & Tree Binding**: Rejects all-zero SHAs; verifies commit reachability.
4. **Staleness Analysis**: Compares the evidence commit against current `HEAD`:
   - If signed code changed since certification, flags `STALE`.
   - If parser code changed since privileged NTFS certification, flags `STALE`.
   - If packaging manifests changed since clean-host certification, flags `STALE`.
