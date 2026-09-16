# Testing conventions

Tests are **plain C++ executables registered with CTest — no gtest/catch**. Each test uses a hand-rolled `Check(condition, description)` helper that prints `FAIL:` to stderr, increments a failure counter, and returns non-zero exit on failure. The canonical implementation is `tests/protocol/test_protocol.cpp`: a file-local `g_failures` counter, `Check` increments it, and `main` reports the total and returns `EXIT_FAILURE` when any check failed. Some older tests (`tests/fileoperations/test_selection.cpp`, `tests/preview/test_preview.cpp`, `tests/indexsvc/test_mft_parser.cpp`) use exit-on-first-failure variants (`std::exit(1)` / early `return 1`) — new tests should use the counter variant from `test_protocol.cpp`.

## Naming

- Test files: `test_<behavior>.cpp`; benchmarks: `bench_<behavior>.cpp`.
- The CTest test name equals the CMake target name (e.g. `ffprotocol_fuzz_tests`, `ffindexstore_store_tests`), so `ctest -R <substring>` is reliable.

## Registration

Each `tests/<component>/CMakeLists.txt` does `add_executable` + `add_test`:

```cmake
add_executable(ffindexstore_store_tests test_store.cpp)
target_link_libraries(ffindexstore_store_tests PRIVATE ffindexstore)
add_test(NAME ffindexstore_store_tests COMMAND ffindexstore_store_tests)
```

Every component's `CMakeLists.txt` also sets `TIMEOUT 120` on its own tests via `set_tests_properties` — CTest test properties are directory-scoped since CMake 3.29, so a root-level call cannot reach tests registered by `add_subdirectory` children.

Two structural patterns:

- **Static libs** — most components link the shared static libraries directly: `ffprotocol`, `ffipc`, `ffindexstore`, `ffsearch`.
- **Direct sources** — engine/UI code has no standalone lib target, so compile the specific `.cpp` files from `src/` into the test executable and add the source dir to the include path:

```cmake
add_executable(ffengine_index_pipeline_tests
  test_index_pipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/IndexPipeline.cpp
)
target_include_directories(ffengine_index_pipeline_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
```

This keeps engine/UI code testable without building the exe targets. `tests/engine/test_volume_session_manager.cpp` additionally uses `#define private public` before including the header under test — an accepted (if fragile) seam.

## Security-descriptor and doc-drift guards

- `ffsetup_security_descriptor_tests` (`tests/setup/`) links `ffsetup` and asserts SDDL-level properties of the five security-descriptor builders in `src/setup/src/SecurityDescriptors.cpp`: no Everyone (`S-1-1-0`) or Authenticated Users (`S-1-5-11`) SID in any built descriptor; the `FastFilesUsers` client group gets read+write (never `WRITE_DAC`/`WRITE_OWNER`) on the Ctrl/Data pipe ACE and query-only rights on the service-object ACE. It skips the client-group assertions with a printed note when the group is absent (installer not run).
- `ffdoc_drift_tests` (`tests/drift/`, pwsh-gated like `tests/uia-driver/`) fails on superseded claims (`never LocalSystem`, `SeBackupPrivilege only`, `scan calls are stubbed`, `not yet implemented`) in README.md/AGENTS.md/CLAUDE.md/CODE_INDEX.md and on mixed CRLF/LF line endings in any `.cpp`/`.h` under `src/` or `tests/`.

## Commands

Run from a Visual Studio developer environment so CMake can discover Ninja on
`PATH`, then:

```powershell
cmake --preset debug            # configure
cmake --build --preset debug    # build all app + test targets
ctest --preset debug            # full suite
```

The optimized `release-test` preset is the unsigned CI/performance profile and
can be tested with `ctest --preset release-test`. The `release` preset remains
the production-signed configuration and requires authorized signing pins before
it can configure.

`debug-diagnostic` is an explicitly non-shipping Debug preset for disposable UIA/
degraded-mode process validation. It enables only the existing diagnostic unsigned
discovery seam; source policy rejects it for release and release-test builds.

Fast single-test loop:

```powershell
cmake --build --preset debug --target ffprotocol_fuzz_tests
ctest --test-dir build/debug -R ffprotocol_fuzz_tests --output-on-failure
```

List tests: `ctest --test-dir build/debug -N`.

There is **no `analyze` test preset** — `ctest --preset analyze` fails. Flags are strict (`/W4 /WX`): test code must be warning-clean too.

## Environment notes

- Test totals are environment-derived and must not be hand-copied into this
  document. Run `pwsh ./verify/verify.ps1 status` after configuring a build to
  record the discovered CTest count; PowerShell-gated tests are registered only
  when `find_program` locates `pwsh`. The current status generator also records
  the exact build configuration and last available CTest evidence.
- `ffindexstore_bench_projection_memory` is built but intentionally **not** registered with `add_test` — it reports a measurement, not pass/fail. Its RSS measurement reads `/proc/self/status`, so it reports nothing useful on Windows; do not rely on its output.

## Fuzz tests

`tests/protocol/test_fuzz.cpp` feeds malformed frames/records at the parsers with a fixed seed (`kSeed = 0xF457F17E`) for byte-for-byte reproducibility. It MUST be updated whenever protocol parsing changes — `src/protocol/Records.cpp`, `Frame.cpp`, `SnapshotFormat`.

## Coverage

New tests should cover success, malformed-input, recovery, and boundary cases where relevant.

## E2E classification

`tests/e2e/` is the discoverability pointer for the real black-box layer. The
implementation lives under `verify/e2e/` because it owns process control,
UIA-driver integration, evidence collection, and marker-safe cleanup rather
than compiling into a CTest executable. Run it with:

```powershell
.\dev.ps1 test-e2e
.\dev.ps1 test-e2e -Filter navigation
```

These tests are deliberately excluded from default CTest; see
[`docs/testing/e2e.md`](../docs/testing/e2e.md) for the scenario contract and
failure-artifact layout. Unit/integration tests may use seams, but E2E tests
must launch the real executable and verify visible UI plus real filesystem
outcomes.

## Dual-pane/UIA lifecycle coverage

`ffnavigation_tests` covers the production `NavigationWorkspace` transition
seams: enable/disable cycles, active-pane migration, typed Home/filesystem
state, tab restore, navigation isolation, and rapid toggles. `ffcolumnview_uia_tests`
covers the custom provider fragment-generation contract, pane-scoped RuntimeIds
and AutomationIds, geometry, pane-local SelectionItem routing, stale-provider
failure, fresh reacquisition, loading-column refresh/reconnect notifications,
no-action pane creation, and 50 deterministic replacement cycles.

`tests/uia-driver/` runs the recorded-tree PowerShell driver suite. Its
dual-pane fixture tests direct-child discovery, ambiguity reporting, provider
reacquisition, RuntimeId identity, and exact process-ownership filtering. These
recorded trees test driver mechanics only; live product evidence comes from
`windows-ui-automation-validation` and is never replaced by OCR or screenshots.
Its live scenarios also reacquire missing-path status rows, Search.Results
children, and Explorer top-level windows after provider/window reconstruction.
