# Structured diagnostics evidence — 2026-08-11

## Scope

Completed OpenSpec tasks 7.1–7.4 in the `structured-diagnostics` capability.
The implementation is additive to the existing metadata-only diagnostic log;
it does not change IPC, authentication, signing, privilege policy, or trust
stores.

## Implementation

- `src/protocol/include/ffprotocol/Diagnostics.h` defines append-only integer
  `DiagnosticCode` values, including `FFDIAG_CORRUPT_DB`, and exposes stable
  names through `DiagnosticCodeName`.
- Every current production `AppendDiagnostic` emission supplies an explicit
  `FFDIAG_*` code. The engine's integrity-check recovery emits
  `FFDIAG_CORRUPT_DB` before deleting and rebuilding an untrusted database.
- `SerializeDiagnosticEvent` and the `DiagnosticFormat::Json` overload of
  `AppendDiagnostic` provide one JSON object per line with both the integer
  code and stable code name, plus the human-readable message/state. Text
  output also includes both structured-code fields.
- JSON string escaping covers quotes, backslashes, controls, and `\\u` values.
  Diagnostic bundle export recognizes both text and JSON lines and continues
  to redact literal paths by default.

## Validation

- `cmake --build --preset debug --target ffprotocol_diagnostics_tests` — PASS.
- `ctest --test-dir build/debug -R ffprotocol_diagnostics_tests
  --output-on-failure` — PASS (1/1).
- The focused test verifies text numeric/name fields, JSON numeric/name fields,
  escaped human-readable messages, JSON stream append, and existing redaction
  behavior.

## Full gates

- `cmake --build --preset debug` — PASS.
- `ctest --test-dir build/debug --output-on-failure` — PASS (45/45).
- `cmake --build --preset analyze` — PASS with `/analyze`.
- `cmake --build --preset release` — PASS.
- `ctest --test-dir build/release --output-on-failure` — PASS (45/45).
- `pwsh ./verify/verify.ps1 build` — PASS (`windows-build-validation`).
- `npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate --all
  --strict` — PASS (45/45).
