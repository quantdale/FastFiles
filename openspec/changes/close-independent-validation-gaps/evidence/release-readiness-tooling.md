# Release-readiness tooling checkpoint

Date: 2026-08-13

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
