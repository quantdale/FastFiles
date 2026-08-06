# Command Results — Independent Validation

Commands recorded exactly as executed. "Result" reflects the actual exit code and supporting output captured this session. No "passed" is recorded without a real successful exit code.

| Command | Purpose | Start State | Result | Exit Code | Duration | Important Output | Artifact |
| ------- | ------- | ----------- | ------ | --------: | -------- | ---------------- | -------- |
| `git status --short` / `--porcelain=v2` | Working-tree state | clean-ish | Only `?? all-edit-with-results-20260804-1748.md` untracked | 0 | <1s | `? all-edit-with-results-20260804-1748.md` | — |
| `git rev-parse HEAD` | Commit | — | `216d931aae00657b2c57204a3cdea9721cad8d07` | 0 | <1s | main branch | — |
| `git log -1 --format=fuller` | HEAD detail | — | "refactor(ui): modernize appearance and harden renderer" Author quantdale 2026-08-04 | 0 | <1s | — | — |
| `git diff --stat HEAD~1 HEAD` | Change scope | clean tree | 76 files +5065/-666 | 0 | <1s | full file list | — |
| `git diff --check HEAD~1 HEAD` | Whitespace errors | clean | (no output) | 0 | <1s | no whitespace errors introduced | — |
| `git log --oneline -- <file>` (per file) | Locate claim-bearing commit | clean | All claimed files last touched in `216d931` | 0 | <1s | e.g. `run-intake-gate-tests.ps1` only in `216d931` | — |
| `Remove-Item -Recurse -Force build\debug` then `$cmake --preset debug` | Clean configure from wiped dir | VS DevShell loaded; ninja set | "Build files have been written to: D:/.../build/debug" | 0 | ~4.2s | MSVC 19.44.35208 detected | `build/debug/CMakeCache.txt` |
| `$cmake --build --preset debug` | Build all targets (clean) | freshly configured | 159/159 steps; all exes linked incl. `FastFiles.exe` | 0 | ~20.6s | last lines link `ffpreview_tests.exe` | `build/debug/build.log` |
| `Select-String build.log 'warning C\|error C'` | Hidden warnings | built | "NO warnings/errors found in build.log" | 0 | <1s | clean | — |
| `Select-String build.ninja '/W4 /WX...'` | Verify strict flags | built | `FLAGS = /DWIN32 /D_WINDOWS /EHsc /Zi /Ob0 /Od /RTC1 -std:c++20 -MDd /W4 /WX /permissive- /sdl /guard:cf /Zc:__cplusplus` | 0 | <1s | line 170 | — |
| `$cmake -E chdir build\debug ctest -N` | Discover tests (fresh config) | built | `Total Tests: 27`; `ffintake_gate_ps_tests` = Test #27 | 0 | <1s | list 1–27 | — |
| `$cmake -E chdir build\debug ctest --output-on-failure` | Full suite | built | `100% tests passed, 0 tests failed out of 27`; `Total Test time = 22.14 sec` | 0 | ~22.1s | all 27 Passed | `build/debug/ctest-full.log` |
| `$cmake -E chdir build\debug ctest -R ffintake_gate_ps_tests -V --output-on-failure` | New test direct | built | 10/10 "ok"; `RESULT: all intake gate regression tests passed`; `1/1 Test #27: ffintake_gate_ps_tests Passed 1.12 sec` | 0 | ~1.1s | command line shows quoted path with spaces | — |
| `$cmake -E chdir build\debug ctest -R ffuia_driver_ps_tests -V` | Pre-existing PS test | built | `RESULT: all uia-driver headless tests passed`; `1/1 Test #26 Passed 3.07 sec` | 0 | ~3.1s | pre-existed before HEAD | — |
| `search_codebase SystemAnimationsEnabled` | L1 + reduced-motion | — | Live `ffui::SystemAnimationsEnabled` in UiAnimation.h:21/.cpp:8; AGENTS.md:18; `test_ui_style.cpp`; WindowShell.cpp:703 calls `ffui::SystemAnimationsEnabled()`. Dead member gone. | — | — | — | — |
| `search_codebase TreemapView::Squarify/HitTest` | I2 stale refs | — | Only in audit merged-input file; none in source. | — | — | — | — |
| `pwsh verify/verify.ps1 list` | Discover verify verbs/capabilities | clean | Listed 15 capabilities; `windows-build-validation` (tier 0, no elevate); several tier-1 (need `-Elevate`); **no `test` verb** (capabilities, not AGENTS.md verb list) | 0 | <1s | `windows-build-validation … tier 0` | — |
| `pwsh verify/verify.ps1 doctor` | Inspect VS toolchain/prerequisites | clean | PASS — VS vc-toolset, cmake, ninja, windows-sdk, powershell all PASS; procmon/procdump/pageheap/windbg-cdb/windows-sandbox SKIPPED (tool-not-found, optional) | 0 | ~2s | `DOCTOR_EXIT=0` | — |
| `pwsh verify/verify.ps1 build` | windows-build-validation capability | clean | **Timed out (>30s)** — long-running (runs a full build). Superseded by the direct `cmake --build --preset debug` above (exit 0, 159/159). | — | >30s | (timeout) | — |
| `$cmake --preset analyze` | Configure Debug + /analyze | VS DevShell loaded | exit 0; `FASTFILES_STATIC_ANALYSIS=ON`; build files → `build/analyze` | 0 | ~1s | "Generating done" | `build/analyze/CMakeCache.txt` |
| `$cmake --build --preset analyze` | Build with /analyze static analysis | configured | **FAILED at LINK (environment)**: `LNK2019: unresolved external __std_regex_transform_primary_wchar_t` for `FastFiles.exe`, `ffengine_volume_session_manager_tests.exe`, `ffcommand_tests` (linking `ffprotocol.lib(Settings.cpp.obj)` / `CommandSystem.cpp.obj`). The link command used MSVC **14.50** from `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\...` (a newer VS "18" BuildTools installed on this host) mixed with the VS 2022 runtime → toolchain mismatch. **Compile stage succeeded** (all .obj built; `/analyze` produced no static-analysis errors). 8/31 link targets that don't use std::regex linked fine. **This is an environment/toolchain issue, not a code defect** — the documented `/W4 /WX` debug build is clean. | 1 | ~1.0s | `LNK2019 __std_regex_transform_primary_wchar_t`; `ninja: build stopped` | `build/analyze/analyze-build.log` |

| `search_codebase 'Move…\|Move\\.\\.\\.'` | L7 residual | — | No "Move…" in source. | — | — | — | — |
| `search_codebase SetEngineActive` | M4 wiring | — | `StorageAnalysis.cpp:760` setter; `WindowShell.cpp` WM_APP_ENGINE_STATUS call. | — | — | — | — |
| `search_codebase 'virtual account\|NT SERVICE\\\\FastFilesIndexSvc'` | I3/I4 stale | — | `CLAUDE.md:41-42` and `CODE_INDEX.md:148-149` STILL stale; design.md/AGENTS.md corrected. | — | — | — | — |
| `search_codebase 'stubbed\|NotYetImplemented'` | scanner status | — | No `NotYetImplemented` in protocol enum (Commands.h: real msg types 8–13). Stale "stubbed" only in audit file + design.md's historical (now-superseded) text. | — | — | — | — |
| `search_codebase 'kExpectedIndexSvcSignatureThumbprint\|PinnedSignatures'` | security blocker | — | `PinnedSignatures.h:16-17` both `{}` (all-zero); `AuthenticodeVerification.cpp:77` `return false; // fail closed`. | — | — | — | — |
| `git diff HEAD~1 HEAD -- AGENTS.md` | Repo-guide correction | — | service-account line + "Current state" + UI-layer para corrected (virtual→LocalSystem, stubbed→implemented). | 0 | <1s | extra correction not in supplied manifest | — |
| `git diff HEAD~1 HEAD -- PrivilegeVerification.h` | I3 exact change | — | old "virtual account… stubbed" → new "LocalSystem… constrained broker… raw-volume scanner". | 0 | <1s | — | — |
| `git diff HEAD~1 HEAD -- design.md` | I4 exact change | — | Non-Goals superseded marker + D5 correction + D4 rewrite. | 0 | <1s | — | — |
| `git diff HEAD~1 HEAD -- storage-analysis/tasks.md` | I2 exact change | — | `TreemapView::Squarify/HitTest` → `ffui::Squarify/HitTest`. | 0 | <1s | — | — |
| `git diff HEAD~1 HEAD -- ColumnView.cpp` (grep focused) | L10 exact change | — | opacity branch removed; `selectionSoftBrush_` + `isFocusedColumn` text logic. | 0 | <1s | — | — |
| `git diff HEAD~1 HEAD -- WindowShell.cpp` (grep focused) | M4/L1/L7 | — | `storageAnalysis_.SetEngineActive` added; dead `WindowShell::SystemAnimationsEnabled` removed; `ffui::SystemAnimationsEnabled` called. | 0 | <1s | — | — |

**Note on the opencode audit's "PARTIAL build / NOT EXECUTED tests":** That prior result was an *environment* limitation (no VS developer shell → linker could not find `kernel32.lib` etc.). With the VS 2022 DevShell loaded this session, the same sources build and link cleanly and all tests execute. The opencode "31 failed at link stage" is **superseded** by this session's clean link. It was never a code defect.
