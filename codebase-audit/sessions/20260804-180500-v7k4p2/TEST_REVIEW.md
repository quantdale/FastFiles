# Test Review — Independent Validation

## Test discovery result
- Command: `$cmake -E chdir build\debug ctest -N` (from a freshly wiped + reconfigured `build/debug`)
- Result: **Total Tests: 27** (exit 0)
- All 27 tests are CTest-registered executables/PowerShell scripts; no fixtures, no disabled tests.

## Total test count
- **Discovered: 27. Executed: 27. Passed: 27. Failed: 0. Skipped/disabled: 0.**
- (Contradicts the supplied "26/26" claim — see Claim C-26tests.)

## New test details — `ffintake_gate_ps_tests`
- **Registered name:** `ffintake_gate_ps_tests`
- **Test number:** **#27** (NOT #26)
- **Execution command (from `ctest -V`):** `"C:\Program Files\PowerShell\7\pwsh.exe" "-NoProfile" "-ExecutionPolicy" "Bypass" "-File" "D:/Documents/tryPython/FastFiles/verify/uia-driver/tests/run-intake-gate-tests.ps1"`
- **Working directory:** `D:/Documents/tryPython/FastFiles/build/debug/tests/uia-driver`
- **Timeout:** 120s (computed)
- **Exit status:** 0 (Passed), 1.12s
- **Registration file:** `tests/uia-driver/CMakeLists.txt` lines 16–21, gated on `find_program(FF_PWSH_EXECUTABLE pwsh)`

## Test-registration analysis
- Registration is correct and uses the generator-correct Ninja/CMake pattern. The `${PROJECT_SOURCE_DIR}`-rooted path resolves from a clean build dir. Quoting handles spaces in the repo path (the command line shown by `ctest -V` has the full path quoted). Not accidentally disabled (the `if(FF_PWSH_EXECUTABLE)` block contains both PS tests; with `pwsh` present both register).
- A stale CMake cache does NOT affect the count: after `Remove-Item -Recurse -Force build\debug` + fresh `cmake --preset debug`, the count is still 27 and `ffintake_gate_ps_tests` is still #27.

## Script analysis — `run-intake-gate-tests.ps1`
- Dot-sources `verify/intake.ps1 -Verb 'status'` (skips the dispatch switch so no run starts) to import `Resolve-ValidateGateOutcome`.
- Constructs four synthetic gate verdicts and asserts on the resolved outcome:
  1. **CRITICAL regression:** gate `Passed=$false` caused ONLY by unrepresented product-source edits (no FAIL verdicts) → must fail validate (`-not $outcome1.ok`), classified `ClassB`, summary names "unrepresented product-source edit".
  2. **Blocking FAIL verdict:** gate `Passed=$false` with a `FAIL` verdict → must fail (`-not $outcome2.ok`), summary names `windows-build-validation`.
  3. **REQUIRED-BUT-UNAVAILABLE:** gate `Passed=$true` with an RBU verdict → stays ok (`$outcome3.ok`) and recorded as external (`$outcome3.external`).
  4. **Clean PASS:** gate `Passed=$true`, all PASS, no unrepresented edits → stays ok (`$outcome4.ok`), not external (`-not $outcome4.external`).

## Assertion quality
- Assertions are **meaningful and can fail**: each `Check` tests a concrete boolean (`-not $outcome.ok`, classification equality, summary regex match, `external` flag). The `Check` helper increments `$script:failures` and prints `FAIL:`; the script ends `if ($script:failures -gt 0) { exit 1 }`. `$ErrorActionPreference='Stop'` terminates on any thrown exception (e.g., if `Resolve-ValidateGateOutcome` is absent or throws). Exit codes propagate to CTest (non-zero ⇒ CTest FAIL).

## False-positive risks (deliberately assessed)
- Could it falsely pass? Only if (a) `Resolve-ValidateGateOutcome` returned the *exact* ok/class/summary/external values the assertions expect despite broken behavior (extremely unlikely — the four cases are designed to be mutually exclusive), or (b) `pwsh` silently swallowed a failure. Neither is plausible: the helper prints `ok:`/`FAIL:` per check and the failure counter gates `exit 1`. The test genuinely protects the intake gate (the CRITICAL case it guards is exactly the silent-PASS-on-unrepresented-edit bug that `verify/intake.ps1` was fixed to reject).
- It does NOT depend on developer-machine-only state, stale build artifacts, or a particular repo path (uses `$PSScriptRoot`-relative resolution and dot-sources intake.ps1). Deterministic. No temp files left behind.

## Individual test result
- `ctest -R ffintake_gate_ps_tests -V` → `1/1 Test #27: ffintake_gate_ps_tests … Passed 1.12 sec`; all 10 checks printed `ok:`; `RESULT: all intake gate regression tests passed`.
- `ffuia_driver_ps_tests` (Test #26, pre-existing) also passes: `RESULT: all uia-driver headless tests passed`, 3.07s — confirms the sibling PS-test harness is healthy and not masking the new test.

## Full-suite result
- `ctest --output-on-failure` → `100% tests passed, 0 tests failed out of 27`; `Total Test time (real) = 22.14 sec`; exit 0. Every test individually reported `Passed` with its duration. Slowest: `fffileoperations_e2e_tests` (8.11s) and `ffprotocol_fuzz_tests` (3.05s) — both within reason.

## Clean-configuration result
- Performed from a wiped `build/debug` (`Remove-Item -Recurse -Force` then `cmake --preset debug` then `cmake --build --preset debug` then `ctest`). 27/27 pass. This is not a stale-cache result.

## Skipped or disabled tests
- None. CTest reported no skips. (Note: some OpenSpec *task* evidence records `SKIPPED(...)` for live UI scenarios — those are manual/visual validations, not CTest tests.)

## Flaky behavior observed
- None. Single clean run; no retries needed. No tests near the timeout.

## Coverage gaps (consistent with prior audits, not introduced by these changes)
- IPC concurrency / partial-read / broken-pipe tests still absent (`tests/ipc/test_pipe_framing.cpp`).
- Selection-model tests remain minimal (6 assertions).
- No store-concurrency-on-same-SQLite-file test.
- D2D-rendered surfaces (column items, treemap) have no UIA item provider → live UI validation is manual/visual (the headless UIA-driver + animation/layout/style tests cover the testable seams).
- The new `ffintake_gate_ps_tests` and `test_connection_registry.cpp` close real gaps (intake-gate regression; connection-scoped handle ownership) — positive.
