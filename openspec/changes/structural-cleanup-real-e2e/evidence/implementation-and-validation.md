# Structural cleanup and real E2E validation

Date: 2026-09-16

This record covers the implementation and validation work for
`structural-cleanup-real-e2e`. The campaign started from `main` at
`0dc33562ad6380c8b505ab221769acb189cd83b1`. The final commit and hosted result
are intentionally derived from the Git handoff rather than copied into this
working-state evidence file.

## Repository and IDE organization

- Historical root reports now live under `docs/agent/history/`.
- The legacy `build_and_deploy.ps1` helper moved to
  `tools/development/build_and_deploy.ps1`; its path resolution is based on
  the script location, and the compatibility workflow was exercised with
  `-SkipBuild -SkipTests`.
- CMake enables folders globally and assigns application, engine, service,
  installer, library, tool, third-party, test, and verification targets to
  useful IDE folders. The UI target has responsibility-based `source_group`
  entries for shell, navigation, browser, search, storage, preview, file
  operations, accessibility, presentation, and public seams.
- `tests/e2e/` contains the IDE/test metadata and points at the real runner in
  `verify/e2e/`. The slow live runner remains outside default CTest while the
  harness contract is covered by a fast PowerShell-gated test.
- `docs/testing/e2e.md`, `tests/README.md`, `README.md`, and `dev.ps1` describe
  the source/test/tooling boundaries, fixture safety, artifact location, and
  canonical developer commands.

## Real E2E system

The live harness is `verify/e2e/Run-FastFilesE2E.ps1` with shared logic in
`verify/e2e/FastFilesE2E.psm1`. It launches the actual
`build\\debug-diagnostic\\src\\ui\\FastFiles.exe`, drives the production UI
through semantic UI Automation and real keyboard/mouse input, checks the
marker-bound filesystem independently, and controls process shutdown.

The `.\dev.ps1 test-e2e` command supports `-Filter` and bounded lifecycle
iterations. The final full run used:

```powershell
.\dev.ps1 test-e2e -LifecycleIterations 3
```

It passed 8/8 scenarios in
`build/e2e-artifacts/20260916-131821-4c5e9b7c`:

1. launch and ready state;
2. Home, direct navigation, nested browsing, Back/Forward/Up;
3. cold restored navigation;
4. exact, partial, no-result, superseded, selected, and opened search;
5. Storage Analysis terminal state, degraded treemap, geometry, and drilldown;
6. preview Ready/Unsupported transitions;
7. marker-safe create, rename, copy, move, and recycle-delete operations;
8. three launch/navigate/close lifecycle iterations.

Every failure path preserves a screenshot, diagnostic tail, process snapshot,
visible location, timeout stage and elapsed time, and a compact UIA snapshot
with visible columns, item identities, bounds, offscreen state, and selection.
The fixture is created outside the repository and all destructive operations
are marker-bound. The production conflict modal is documented as not invoked
by the disposable default scenario.

## Defects found and fixed through the real app

- Successful preview-provider payloads now publish `PreviewState::Ready`.
- Column and search UIA `FindItemByProperty` paths resolve foreign
  `startAfter` elements by bounded runtime identity instead of down-casting a
  caller-owned proxy.
- UIA element caches are protected by re-entrant mutexes across UI-thread
  invalidation and UIAutomationCore worker calls. This fixed the real
  treemap-tile-to-Settings Debug CRT crash.
- The synthetic This PC column reports `Current` after its synchronous drive
  population, avoiding a permanent refreshing state.

## Local certification

The following gates passed on the campaign tree:

- `.\dev.ps1 doctor` — toolchain/prerequisite diagnostics PASS.
- `.\dev.ps1 test -Preset debug` — 105/105 CTest tests passed.
- `.\dev.ps1 test -Preset release-test` — 105/105 CTest tests passed.
- `.\dev.ps1 build -Preset analyze` — PASS, warning-clean.
- ASAN protocol/IPC targets `ffprotocol_fuzz_tests`, `ffipc_framing_tests`,
  and `ffipc_listener_lifecycle_tests` — all 3 focused tests passed.
- `openspec validate --all --strict --no-interactive` — 83/83 items passed;
  the earlier change-only strict run also passed 51/51 changes.
- Documentation drift/line-ending checks — PASS, including 341 source/test
  checks; source-marker policy found no temporary debug markers in 233
  production files; diagnostic policy passed all 11 checks.
- `pwsh .\verify\verify.ps1 build` — 9/9 Debug/release-test/Analyze gates
  passed.
- Protocol robustness — 3 protocol suites passed.
- Filesystem validation — 7 scenario batteries passed.
- Active-path resilience — 600-second seeded degraded run passed 3/3
  executable scenarios; one GUI-only scenario was skipped by host capability.
- Performance baselines — capability PASS, but this filtered run compared 0
  metrics and is awaiting its first stored baseline; it is advisory, not a
  claim of a historical performance regression result.

The UIA capability was also run in bounded groups after the all-scenarios
envelope exceeded its 1200-second limit: core 16, search 7, treemap 3,
settings-dialog-structure 3, selection 2, error 3, drag 2, and isolated 6
passed. Host-limited Explorer drag cases remained explicit skips. The single
aggregate `all` run timed out and then exposed a verifier result-schema error
while composing the timeout; grouped live coverage is the reliable evidence
until that verifier aggregation defect is repaired.

The first release-readiness composition correctly identified the pre-commit
dirty tree and therefore rejected clean-worktree/candidate packaging. It also
reported the independent production signing and clean-host prerequisites as
external. A clean final composition is run after the implementation commit.

## Manual dogfood

At the available 125% DPI, a fresh diagnostic-app walkthrough covered Home,
direct fixture entry, nested browse, Back/Forward/Up, multiple tabs, exact and
partial search, no-result and rapid replacement search, Storage Analysis,
degraded treemap and tile drilldown, preview Ready/Unsupported transitions,
Settings open/close, maximize/restore, keyboard navigation, native close,
cold relaunch, and `.\dev.ps1 stop`. The cold relaunch showed the persisted
`05-content` location and its real entries without address-bar intervention.
The final stop left no `FastFiles.exe` or `FastFilesEngine.exe` process.
Screenshots were captured at the live checkpoints; the durable E2E evidence
and failure-artifact location is the generated path recorded above.

Only the host's available 125% scale was exercised. Additional 100%/150%/
200%/high-contrast modes and Explorer's synthetic drag cases remain host
coverage limitations rather than unverified passes.

## External conditions

Production release readiness remains gated by authorized Authenticode signing
pins, an elevated clean-host service/install validation, and real privileged
MFT/USN validation. Tier-3 disposable guest/provider coverage is unavailable
on this host. Hosted Actions status is queried during the final push; an
account billing block, if still present, is infrastructure evidence and not a
product failure.
