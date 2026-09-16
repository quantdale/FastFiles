## 1. Live E2E foundation

- [x] 1.1 Add the marker-bound reusable UIA session lifecycle needed for a
  clean close/relaunch restore check, with exact owned process and pipe cleanup.
- [x] 1.2 Add `verify/e2e/Run-FastFilesE2E.ps1` and its helper module with
  bounded stage waits, semantic UIA actions, independent filesystem checks,
  process snapshots, diagnostics tails, UIA dumps, and generated screenshots.
- [x] 1.3 Add live scenarios for launch readiness, Home/direct navigation,
  nested browse, Back/Forward/Up, cold restore, and repeated lifecycle cycles.
- [x] 1.4 Add live search, Storage Analysis/treemap, and preview terminal-state
  scenarios; record explicit degraded/unsupported outcomes instead of waiting
  forever.
- [x] 1.5 Add real fixture-scoped create/rename/copy/move/delete operations and
  conflict coverage where the production UI exposes a safe deterministic path.
  The current production conflict path is modal and is recorded as not invoked
  by the disposable default scenario; all five non-conflict mutations are
  verified on disk.

## 2. Developer/test integration

- [x] 2.1 Add `dev.ps1 test-e2e`, filter support, artifact reporting, and a
  concise `dev.ps1 dogfood` entry point without changing existing verbs' meaning.
- [x] 2.2 Add `tests/e2e` IDE/test metadata and label the live runner separately
  from the default CTest suite; retain the existing recorded UIA driver tests.
- [x] 2.3 Add failure-artifact and fixture safety regression checks for the
  runner's pure helper functions and generated result envelope.

## 3. Repository and IDE organization

- [x] 3.1 Enable CMake target folders for App, Engine, Service, Installer,
  Libraries, Tests, and Verification targets.
- [x] 3.2 Add responsibility-based source groups for the flat UI target and
  public seam headers, plus test/verification grouping metadata.
- [x] 3.3 Move the legacy root build helper to `tools/development/`, update all
  references, and record any intentionally retained root entry points.
- [x] 3.4 Update README and developer/test docs with the canonical commands,
  executable path, fixture safety, E2E filters/artifacts, and external gates.

## 4. Validation and handoff

- [x] 4.1 Run focused CTest and the real E2E filters; fix product or harness
  defects rather than weakening assertions.
- [ ] 4.2 Run Debug, release-test, analyze, ASAN protocol/IPC, OpenSpec strict,
  doc drift/source policy, verifier, and release-readiness gates available on
  the host; record external blockers honestly.
- [x] 4.3 Perform the final manual dogfood walkthrough at the available 125%
  DPI, capture meaningful screenshots, and verify stop/reopen/orphan cleanup.
- [ ] 4.4 Update WORKSTATE, commit coherent slices, fetch/reconcile origin/main,
  push without force, and verify the final exact SHA and hosted result.
