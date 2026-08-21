# FastFiles release procedure

`VERSION` is the canonical product version. The release-candidate command derives
the executable metadata checks, archive name, manifest, notes, and checksums from
that file and the clean checkout commit.

## Portable unsigned validation candidate

From a Visual Studio developer PowerShell on a clean checkout:

```powershell
pwsh ./verify/verify.ps1 release-candidate -ReleaseMode unsigned-validation
```

This runs the repository build gate, builds the explicit `release-test` profile,
validates the four executable payloads, and writes an ignored directory under
`out/release-candidate/` containing:

- `FastFiles-<version>-unsigned-validation-x64.zip`;
- `candidate-manifest.json`, `provenance.json`, and `SHA256SUMS.txt`;
- the exact package manifest and portable validation kit;
- `RELEASE_NOTES.md` generated from `RELEASE_NOTES_TEMPLATE.md`; and
- a lightweight `dependency-inventory.json`.

The result is useful for local validation only. It is explicitly not a production
release and cannot authenticate the privileged runtime.

## Authorized production candidate

Production signing remains an external, fail-closed prerequisite. The expected
sequence is:

1. configure/build the `release` preset with the two authorized non-placeholder
   pin values supplied through `FASTFILES_ENGINE_SIGNATURE_THUMBPRINT` and
   `FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT`;
2. sign the four PE artifacts with `verify/release/sign-release.ps1` using an
   authorized certificate and RFC3161 timestamp service;
3. generate the final package manifest and its detached signature through the
   existing signing workflow; and
4. validate/package the signed build:

```powershell
pwsh ./verify/verify.ps1 release-candidate `
  -ReleaseMode production `
  -BuildRoot ./build/release `
  -ManifestSignaturePath C:/secure-input/FastFiles.package.manifest.p7s `
  -ExpectedSignerThumbprint '<authorized-40-hex-thumbprint>'
```

The production mode refuses missing/placeholder pins, missing expected signer,
missing detached manifest signature, unsigned or untimestamped PE files, wrong
signer, version drift, and package hash mismatches. It does not create or store
certificates, private keys, passwords, or pins.

`-SkipVerification` is an explicit escape hatch for validating an already signed
external build; its output records `SKIPPED_BY_OPERATOR` and must not be reported
as a complete repository build gate.

## Artifact classification

Never rename an unsigned-validation archive to make it look production-ready.
The candidate manifest, archive name, notes, and provenance all carry the
classification. Production signing and elevated clean-host runtime validation
remain separate acceptance gates.
