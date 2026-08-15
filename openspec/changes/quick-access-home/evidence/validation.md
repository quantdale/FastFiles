# quick-access-home — validation evidence

Date: 2026-08-15. Session: ordinary development (no elevation, no certificates).

## Portable test suites (default CTest, no admin, no signing)

| Suite | Result |
| --- | --- |
| `ctest --preset debug --output-on-failure` | 55/55 passed (incl. `ffnavigation_tests` 130 checks, `ffhome_layout_tests` 60+ checks, `ffhome_interaction_tests` 21 checks, `ffdoc_drift_tests`) |
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
