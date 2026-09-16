## Why

FastFiles already has useful unit, integration, and UI Automation pieces, but a
new developer still has to cross a flat UI target and several overlapping
verification entry points to understand the product. More importantly, the
existing live checks do not form one durable user-flow gate for cold restore,
real browsing, search, Storage Analysis, preview, safe mutations, and repeated
process lifecycles. This change completes that omitted confidence layer while
making the source and IDE structure reflect the three-process architecture.

## What Changes

- Add a repository-owned black-box E2E runner that launches the actual
  diagnostic FastFiles executable, drives semantic UI Automation, verifies real
  filesystem outcomes, bounds every wait, and preserves failure artifacts.
- Cover launch readiness, Home, fixture browsing/navigation, cold restore,
  search supersession, Storage Analysis terminal state and geometry, preview,
  marker-protected file operations, and repeated launch/stop cleanup cycles.
- Add simple `dev.ps1` commands for fast tests, the E2E suite, and the complete
  local certification composition, while retaining existing verifier commands.
- Organize CMake targets and IDE source groups by product role and module
  responsibility; make test and verification grouping mirror the architecture
  without changing process boundaries or security behavior.
- Update the README and developer/test documentation with the canonical live
  workflow, fixture safety rules, artifact locations, and honest external
  blockers.

## Capabilities

### New Capabilities

- `real-e2e-validation`: bounded black-box validation of real FastFiles user
  flows, process ownership, filesystem effects, UIA state, and failure evidence.

### Modified Capabilities

- None. Existing product behavior is exercised and protected; no protocol,
  privilege, IPC, or security requirement changes.

## Impact

The primary changes are under `verify/e2e`, `tests/e2e`, `dev.ps1`, CMake
organization metadata, and developer documentation. The runner reuses the
existing semantic UIA driver and marked disposable fixture conventions. It
does not require elevation, certificates, service installation, or production
signing pins, and it never performs destructive operations outside its own
marker-protected fixture root.
