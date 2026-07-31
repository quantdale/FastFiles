## ADDED Requirements

### Requirement: Autonomous Toolchain Discovery And Activation
The harness SHALL locate an installed Visual Studio toolset via `vswhere` (requiring the VC x64 tools component), activate its Developer environment (`VsDevCmd`/`vcvarsall`) into the build process, and use the CMake and Ninja bundled with that toolset — without requiring any globally installed compiler or CMake on `PATH`.

#### Scenario: Build works with nothing on PATH
- **WHEN** the harness is invoked to build and no compiler or CMake is present on the ambient `PATH`
- **THEN** it SHALL discover a VC toolset via `vswhere`, activate its Developer environment, and drive the build using the toolset-bundled CMake and Ninja

#### Scenario: Selected toolset is recorded
- **WHEN** multiple toolsets are installed and one is selected for the build
- **THEN** the selected toolset and SDK versions SHALL be recorded in the run fingerprint so results are attributable to a specific toolchain

### Requirement: Debug And Release Configuration Builds
The harness SHALL build the project in both Debug and Release configurations from a clean checkout, using CMake presets, and SHALL report per-configuration success or failure.

#### Scenario: Both configurations build from clean
- **WHEN** a build is requested for a clean checkout
- **THEN** the harness SHALL configure and build both Debug and Release and SHALL report the outcome of each independently

### Requirement: Clean And Incremental Builds
The harness SHALL support both a clean (from-scratch) build and an incremental build over an existing build tree, and SHALL report which mode was used.

#### Scenario: Incremental build reuses prior artifacts
- **WHEN** an incremental build is requested over an existing, valid build tree
- **THEN** the harness SHALL rebuild only what changed and SHALL report the build as incremental

#### Scenario: Clean build discards prior artifacts
- **WHEN** a clean build is requested
- **THEN** the harness SHALL remove prior build outputs and configure/build from scratch

### Requirement: Static Analysis Pass
The harness SHALL run MSVC static analysis (`/analyze`) as a distinct build variant and SHALL collect any analysis findings into the run's build results.

#### Scenario: Static analysis findings are collected
- **WHEN** the static-analysis variant is built
- **THEN** the harness SHALL capture the emitted analysis diagnostics and attach them to the build result

### Requirement: Compiler Diagnostics Collection And Failure Identification
The harness SHALL parse compiler and linker output into structured diagnostics (severity, file, line, code, message), SHALL classify a build as failed when the toolchain reports failure, and SHALL identify the first-failing target and diagnostic.

#### Scenario: A warnings-as-errors break is identified
- **WHEN** a build fails because a warning is treated as an error under `/WX`
- **THEN** the harness SHALL report the build as failed and SHALL surface the specific diagnostic (file, line, code, message) that caused it

#### Scenario: The first failing target is pinpointed
- **WHEN** a multi-target build fails
- **THEN** the harness SHALL identify which target failed first and the governing diagnostic, rather than only reporting an overall non-zero exit

### Requirement: Build Summary
The harness SHALL produce a build summary for each build capturing configuration, toolset, per-target result, warning/error counts, and duration.

#### Scenario: Summary reflects a successful multi-config build
- **WHEN** Debug and Release builds complete
- **THEN** the harness SHALL emit a summary listing each configuration's result, counts, and timing

### Requirement: User-Mode Test Suite Execution
The harness SHALL run the project's existing user-mode test suites via `ctest` — including the protocol unit tests and the seeded frame/parse fuzz suite — with no elevation, and SHALL record results in JUnit-compatible form.

#### Scenario: Unit and fuzz suites run at Tier 0
- **WHEN** the build succeeds and Tier 0 tests are requested
- **THEN** the harness SHALL execute the `ctest` unit and fuzz suites without elevation and SHALL capture their pass/fail results into the run artifacts

#### Scenario: A failing test fails the suite
- **WHEN** any `ctest` case fails
- **THEN** the harness SHALL report the corresponding suite as failed and reference the failing case in the result
