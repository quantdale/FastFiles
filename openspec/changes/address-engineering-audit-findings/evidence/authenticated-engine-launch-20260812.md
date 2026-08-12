# Authenticated engine-launch hardening evidence

Date: 2026-08-12

## Implementation

- `CMakeLists.txt` accepts `FASTFILES_ENGINE_SIGNATURE_THUMBPRINT` and
  `FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT` from cache or the signing
  environment, validates each as 40 hexadecimal characters, generates
  `build/<config>/generated/ffsetup/PinnedSignaturesConfig.h`, and fails
  Release configuration if either value is absent or all-zero.
- `PinnedSignatures.h` consumes the generated values and retains an all-zero
  development fallback. `ArePinnedSignaturesConfigured()` is checked by the
  UI, Engine, and Index Service only in non-debug builds. No certificate or
  thumbprint value was invented or committed.
- Engine discovery now requires a regular non-reparse file whose
  `GetFinalPathNameByHandleW` result matches the long-name-normalized request,
  then verifies the Authenticode signer against the engine pin before process
  creation. The existing explicit `FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED` escape
  hatch remains confined to Debug configurations.
- Launch/rejection events carry the resolved path and observed signer
  thumbprint in structured diagnostics.

## Validation

- `cmake --preset debug`: PASS; generated development header contains only the
  fail-closed all-zero fallback.
- `cmake --preset release`: EXPECTED FAIL without signing inputs; CMake emits
  the explicit missing-pin error before generating a Release build. The same
  gate rejects a supplied all-zero 40-hex placeholder.
- Debug build targets `FastFiles`, `FastFilesEngine`, `FastFilesIndexSvc`,
  `ffengine_discovery_tests`, `ffengine_authenticode_verification_tests`, and
  `ffprotocol_diagnostics_tests`: PASS with MSVC `/W4 /WX`.
- Full `cmake --build --preset debug`: PASS; full Analyze configure/build:
  PASS with `/analyze`; serial Debug CTest: 45/45 PASS.
- `npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate --all
  --strict --no-interactive`: 45/45 PASS.
- Focused CTest: `ffprotocol_diagnostics_tests`,
  `ffengine_authenticode_verification_tests`, and `ffengine_discovery_tests`:
  3/3 PASS.
- Direct `ffengine_discovery_tests.exe`: PASS with the default 8.3 TEMP path,
  including long-name canonical comparison, unsigned rejection, and a
  symlinked-parent rejection fixture.
- Computer Use runtime: the normal unsigned Debug build visibly reached
  `Engine unavailable — click to retry`; an isolated Debug build with the
  explicit diagnostic-only override visibly launched the Engine and showed
  `Instant search: basic (degraded mode)` with browsable columns. The exact
  diagnostic Engine PID was verified by executable path and stopped after the
  UI check.

## Boundary

This slice proves the provisioning mechanism and fail-closed behavior only.
Signed release installation, production certificate provisioning, privileged
MFT/USN validation, and clean-host evidence remain external gates and are not
claimed here.
