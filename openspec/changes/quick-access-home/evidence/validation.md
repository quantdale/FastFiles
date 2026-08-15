# quick-access-home — validation evidence

Date: 2026-08-15. Session: ordinary development (no elevation, no certificates).

## Post-integration hardening validation (HOME-POST-01..04)

Date: 2026-08-15. Session: ordinary development (no elevation, no certificates).
Four independently verified post-implementation defects were closed and
validated on the normal portable matrix; tasks 8.1–8.7 record the fixes.

| Suite | Result |
| --- | --- |
| `ctest --preset debug --output-on-failure` | 55/55 passed |
| `ctest --preset release-test --output-on-failure` | 55/55 passed |
| `cmake --preset analyze` + build (`/analyze`, `/W4 /WX`) | green |
| `cmake --preset asan` + `ffprotocol_fuzz_tests` build + run | pass |
| `verify/verify.ps1 doctor` | toolchain PASS |
| `verify/verify.ps1 build` | PASS |
| `verify/check_diagnostic_policy.ps1` | PASS |
| `verify/check_source_markers.ps1` | PASS |
| `openspec validate --all --strict --no-interactive` | PASS |
| `git diff --check` | clean |
| `ffdoc_drift_tests` | PASS (in both CTest runs) |

Headless coverage added: typed recently-closed-tab reopen kinds (Home /
filesystem / This PC) + mixed-stack order + serialize/deserialize round-trip +
legacy string migration + malformed-entry skip (`test_navigation_workspace.cpp`);
dual-pane enabled AFTER navigating to Home with exact `{Home}` fresh history and
filesystem/This-PC clones (`test_home_location.cpp`); focus-list construction,
Clear-stop reachability/activation, unavailable-known-folder exclusion from
focus/hover/activation, and empty-Folder activation rejection
(`test_home_interaction.cpp`). The focus-list tests also exposed and fixed a
latent `HomeLayout` seam bug: `HomeSectionLayout::itemCount` was never
populated (its first consumer is `BuildHomeFocusList`).

Behavior decisions recorded per the mission:
- Unavailable known folders are rendered DISABLED (secondary-text treatment,
  matching the sidebar), skipped by keyboard focus and hover, and rejected at
  the activation boundary — option A of HOME-POST-02.
- Keyboard traversal places a section's Clear stop before its section's items
  (header-above-items spatial order); initial focus / Home / End land on items
  only, so a stray Enter cannot clear history.
- `lastLocation` intentionally never stores Home (a Home session ends with an
  empty `lastLocation`, so the next start opens at the startup/This PC
  location); Home is a session surface, not a persisted startup location.

## Portable test suites (default CTest, no admin, no signing)

| Suite | Result |
| --- | --- |
| `ctest --preset debug --output-on-failure` | 55/55 passed (incl. `ffnavigation_tests` 130+ checks, `ffhome_layout_tests` 60+ checks, `ffhome_interaction_tests` 59 checks, `ffdoc_drift_tests`) |
| `ctest --preset release-test --output-on-failure` | 55/55 passed |
| `cmake --preset analyze` + build (`/analyze`, `/W4 /WX`) | green |
| `cmake --preset asan` + `ffprotocol_fuzz_tests` build + run | pass (7.02 s) |
| `verify/verify.ps1 doctor` | toolchain PASS |
| `verify/verify.ps1 build` | see run result in this session |
| `verify/check_diagnostic_policy.ps1` | PASS (release caches do not enable the escape hatch) |
| `verify/check_source_markers.ps1` | PASS (no TEMP-DEBUG/DEBUG_STALL markers in 186 production files) |
| `openspec validate --all --strict --no-interactive` | 47/47 passed |
| `git diff --check` | clean |

## Launch smoke (non-admin desktop session)

`build/debug/src/ui/FastFiles.exe` launched normally; the process stayed alive
and presented its window ("FastFiles") for the observation window, then was
closed. The engine child process launch was not separately observed.

## UIA / visual limitation (recorded per mission)

The FastFiles shell renders through Direct2D with owner-drawn Win32 chrome; the
Column View and the new Home surface are not exposed through a Windows
UI-Automation tree beyond the standard controls. This session ran headless
state/layout/interaction tests as the deterministic acceptance for Home
behavior; live visual inspection of Home (sections, focus stroke, High
Contrast, 100%/150% DPI, narrow/wide widths) remains a manual interactive
check. The headless layout contract (no overlap, clipping bounds, column
counts at 100%/150% DIP viewports) is covered by `ffhome_layout_tests`.

## Degraded-mode independence

Home model, layout, and interaction tests exercise no engine/service state:
`NavigationWorkspace` (model + recents), `HomeLayout` (layout seam), and the
ColumnView hit-test/focus seams run without an engine process. Home data
(bookmarks, recents, search history, known folders) is entirely local.
