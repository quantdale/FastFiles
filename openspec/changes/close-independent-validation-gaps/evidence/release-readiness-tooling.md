# Release-readiness tooling checkpoint

Date: 2026-08-14

This evidence records preparation tooling only. It does not close tasks 4.2,
4.5–4.8, or 5.1 and does not claim production signing, authenticated elevated
MFT/USN execution, clean-host execution, or direct rendered visual evidence.

## Release artifact

The native installer is the release package boundary. A Release build produces:

```text
build/release/src/ui/FastFiles.exe
build/release/src/engine/FastFilesEngine.exe
build/release/src/indexsvc/FastFilesIndexSvc.exe
build/release/src/installer/FastFilesSetup.exe
```

`FastFilesSetup.exe` copies the three product executables into
`%ProgramFiles%\FastFiles` and registers the LocalSystem constrained broker,
scheduled task, client group, ACLs, and metadata according to the existing
installer contract. There is no MSI/WiX package in this repository.

`verify/release/New-ReleaseKit.ps1` creates a copyable directory package outside
Git containing those four files, `artifact-manifest.json`, the validation scripts,
and `validation/fftest.exe` when the probe was built. The manifest records the
package path, size, SHA-256, PE machine/architecture, version-resource values,
Authenticode status, signer subject/leaf thumbprint, and timestamp-signature
metadata. Missing version resources remain `null`.

## Signing and pin ordering

The repository-specific sequence is:

1. Configure a Release build with two externally supplied
   `FASTFILES_*_SIGNATURE_THUMBPRINT` values. CMake rejects missing/all-zero
   Release pins; no source fallback is changed.
2. Build the Release artifact set.
3. Use an authorized external signer through `verify/release/sign-release.ps1`
   (certificate store or externally mounted PFX). The wrapper rejects missing
   artifacts/wrong architecture, never logs secrets, and verifies each output
   after signing.
4. Use `verify/release/derive-pin.ps1` against the legitimately signed Engine
   and Index Service binaries to print the actual leaf thumbprints. It never
   writes source, cache, or pins.
5. Apply the reviewed leaf values as external Release configuration and rebuild.
   The pin values are certificate identity, so rebuilding does not change the
   certificate thumbprint; the rebuilt binaries must still be signed again.
6. Sign the final UI, Engine, Index Service, and Setup artifacts, then generate
   a final manifest and require the signature matrix to show the expected signer
   and the Engine/Service leaf-to-pin matches. There is no dual-pin acceptance
   window.

The production-validation audit found and repaired four release-tooling defects
before any production key was considered:

- `PV-BUG-01`: encrypted external PFX input was discovered without consuming the
  supplied password, so protected PFX files could not be used non-interactively.
  `sign-release.ps1` now passes the secure password to `Get-PfxCertificate` with
  prompting disabled; the release-tooling test exercises this with a temporary
  non-production certificate and removes it after the test.
- `PV-BUG-02`: the old combined PE-plus-manifest mode could sign PE files after
  their hashes had been embedded in the manifest. Normal PE signing now rejects
  manifest-signature arguments. The manifest is signed in a separate
  `-ManifestOnly` invocation after final PE bytes exist, with CMS verification
  before the signature is handed to kit generation.
- `PV-BUG-03`: `ReleaseReadiness.ps1 -ArtifactRoot` inspected one artifact root
  while reading pin state from the default `build/release` cache. Pin state now
  comes from the same artifact root, with a regression assertion for this case.
- `PV-BUG-04`: the readiness report previously hardcoded `BLOCKED_EXTERNAL` for
  every non-artifact-failure report. Its status calculation is now explicit and
  reaches `PASS` only when artifact integrity, signatures, pins, clean-host,
  architecture, and elevation all pass.

The final local signer-policy slice added and regression-tested:

- `PV-BUG-05`: `Test-ProductionCertificate` now requires a non-self-signed,
  currently valid leaf with the Code Signing EKU and a fully successful trusted
  Windows chain; signing providers additionally require the private key and an
  exact expected-thumbprint match. `Test-PackageManifestSignature` applies the
  same technical policy to the detached CMS signer. Release preflight reports
  revocation as `NOT_CHECKED_OFFLINE` rather than claiming an online result.
- `PV-BUG-06`: the production-validation workstate and ExecPlan now separate
  the completed prior push/CI slice from this signer-hardening slice and no
  longer point at an already-completed commit/push action.
- `PV-BUG-07`: all-zero expected thumbprints now fail closed through explicit
  boolean evaluation in `Test-UsableThumbprint`.
- `PV-BUG-08`: the certificate-policy helper initially leaked collection/OID
  mutation return values into the PowerShell pipeline, which made detached CMS
  verification receive an array instead of its policy object. All such outputs
  are now suppressed, with an exact-signer detached-CMS regression through the
  private non-production custom-root technical seam.
- `PV-BUG-09`: the first real expired-signer fixture signed the full
  Authenticode test executable, and the optimized release-test path could
  consume the short window before `signtool` completed. The fixture now signs
  the compact `ffprotocol_tests.exe` PE with a 120-second currently-valid
  window before the deliberate expiry wait.

The detailed finding matrix is in
`evidence/production-validation-finding-matrix.md`, and the exact local versus
external task-4.2 boundary is in `evidence/authentication-negative-tests.md`.

The final signed-package sequence is therefore: sign the final four PE files;
derive and provision the Engine/Service pins; rebuild and re-sign if pin
embedding changed bytes; generate the V2 manifest from those final bytes; call
`sign-release.ps1 -ManifestOnly` over that exact manifest; then run
`New-ReleaseKit.ps1 -ManifestSignaturePath` against the unchanged final artifact
root and validate the resulting package. The release-tooling regression suite
passes with zero failures, including encrypted-PFX loading, manifest-only
dry-run/no-mutation, unsafe combined-mode rejection, and artifact-root pin
provenance.

The current checkout has no authorized signer and the inspected Release outputs
are unsigned. Ignored CMake-cache values are classified as structural inventory,
not production provenance. No production pin values were changed.

## Canonical commands

```powershell
pwsh ./verify/verify.ps1 release-readiness -Mode report
pwsh ./verify/verify.ps1 release-readiness -Mode gate
pwsh ./verify/verify.ps1 release-readiness -Mode privileged -Elevate
pwsh ./verify/release/New-ReleaseKit.ps1 -BuildRoot ./build/release -OutputDirectory <kit>
```

The report/gate command returns `BLOCKED_EXTERNAL` (exit code 2) for the current
unsigned/placeholder state. The privileged driver stops before machine mutation
until signing, pins, clean-host preflight, and elevation are all ready. Once it
can run, it creates a disposable unique fixture, launches the real installed
Engine, observes service raw-volume and USN diagnostics, uses `fftest
index-aggregate` for initial and post-mutation aggregate/query checks, and
delegates exact uninstall to the installed setup executable. Process stops verify
the executable path and the transaction records resources created.

## Clean-host contract

A clean host is a supported Windows 10 1809-or-newer or Windows 11 x64 machine
with no prior FastFiles service/process, `\FastFiles\FastFilesEngine` task,
`FastFilesUsers` group, `%ProgramFiles%\FastFiles`, FastFiles ProgramData/user
state, or FastFiles uninstall registry entry. A VM is permitted. A source clone,
Visual Studio, CMake, and build tools are not required by the kit. The kit does
not install or modify trust-store entries. The preflight is read-only and reports
`CLEAN_HOST_PRECONDITION_FAILED`; it never cleans contamination and calls that a
clean-host PASS.

`verify/release/import-clean-host-result.ps1` accepts only a schema-tagged PASS
bundle whose artifact hashes and manifest commit agree. It copies reviewed
evidence to an explicit destination and never modifies OpenSpec task state.
