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

Set `$env:FASTFILES_NINJA_EXE` (the VS-bundled Ninja) **before** configuring, then:

```powershell
cmake --preset debug            # configure
cmake --build --preset debug    # build all app + test targets
ctest --preset debug            # full suite
```

Fast single-test loop:

```powershell
cmake --build --preset debug --target ffprotocol_fuzz_tests
ctest --test-dir build/debug -R ffprotocol_fuzz_tests --output-on-failure
```

List tests: `ctest --test-dir build/debug -N`.

There is **no `analyze` test preset** — `ctest --preset analyze` fails. Flags are strict (`/W4 /WX`): test code must be warning-clean too.

## Environment notes

- Full host: 29 tests. `ffuia_driver_ps_tests`, `ffintake_gate_ps_tests`, and `ffdoc_drift_tests` are PowerShell tests registered only when `find_program` locates `pwsh` (`tests/uia-driver/CMakeLists.txt`, `tests/drift/CMakeLists.txt`), so the count varies by environment — 26–29 is not a regression. `tests/uia-driver/` and `tests/drift/` contain no C++ files; the former points at `verify/uia-driver/tests/*.ps1`, the latter at `tests/drift/check_doc_drift.ps1`.
- `ffindexstore_bench_projection_memory` is built but intentionally **not** registered with `add_test` — it reports a measurement, not pass/fail. Its RSS measurement reads `/proc/self/status`, so it reports nothing useful on Windows; do not rely on its output.

## Fuzz tests

`tests/protocol/test_fuzz.cpp` feeds malformed frames/records at the parsers with a fixed seed (`kSeed = 0xF457F17E`) for byte-for-byte reproducibility. It MUST be updated whenever protocol parsing changes — `src/protocol/Records.cpp`, `Frame.cpp`, `SnapshotFormat`.

## Coverage

New tests should cover success, malformed-input, recovery, and boundary cases where relevant.
