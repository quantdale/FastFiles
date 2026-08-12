# Production debug-marker evidence — 2026-08-11

## Scope

Completed OpenSpec task 8.1. The guard is CI-visible and scans first-party
`src/` C/C++ source only, so test fixtures and audit/history documents do not
create false positives for production binaries.

## Implementation

- Added `verify/check_source_markers.ps1`, which fails with file/line evidence
  when `TEMP-DEBUG` or `DEBUG_STALL` occurs in production source.
- Added the guard as the first source-validation step in `.github/workflows/ci.yml`
  before configure/build/test for every preset.
- No production code, IPC behavior, authentication, signing, privilege, or
  trust-store policy changed.

## Focused validation

- `pwsh -NoProfile -File verify/check_source_markers.ps1` — PASS; no markers in
  171 first-party production source files.
- x64 Debug CTest — PASS (45/45).
- x64 Release CTest — PASS (45/45).
- `pwsh ./verify/verify.ps1 build` — PASS (`windows-build-validation`).
- `npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate --all --strict`
  — PASS (45/45).

The preceding IPC slice's Debug/Analyze/Release builds were also green; this
change only adds a CI PowerShell guard, workflow step, task checkbox, and
evidence record, so no C++ rebuild was required for its own diff.
