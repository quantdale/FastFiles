# Repository Guidelines

## Project Structure & Module Organization

FastFiles is a Windows-only C++20 application built as several CMake targets. First-party code lives in `src/`: `protocol` defines shared messages, `ipc` handles pipe framing, `indexstore` owns SQLite-backed persistence, `indexsvc` performs privileged NTFS indexing, `engine` coordinates indexing, and `ui` contains the desktop interface. Installer and setup code is under `src/installer` and `src/setup`. Public headers belong in each component's `include/ff*` directory; implementation files belong in `src/`.

Tests mirror components under `tests/protocol`, `tests/indexstore`, `tests/indexsvc`, and `tests/engine`. `verify/` contains the PowerShell runtime-verification harness and JSON schemas. Design proposals and task tracking live in `openspec/changes/`. Treat `third_party/sqlite/` as vendored code and avoid incidental edits.

## Build, Test, and Development Commands

Run commands from a Visual Studio developer PowerShell on Windows. Set `FASTFILES_NINJA_EXE` to the Visual Studio-bundled Ninja executable before configuring.

- `cmake --preset debug` configures a Debug build in `build/debug`.
- `cmake --build --preset debug` builds all application and test targets.
- `ctest --preset debug` runs the full test suite and prints failures.
- `ctest --test-dir build/debug -R indexstore` runs a focused subset.
- `cmake --preset analyze && cmake --build --preset analyze` enables MSVC `/analyze` checks.
- `pwsh ./verify/verify.ps1` runs the runtime-verification entry point; keep generated `verify/runs/` and `verify/baselines/` untracked.

## Coding Style & Naming Conventions

Follow the existing C++ style: four-space indentation, opening braces on the same line, `PascalCase` for types and public methods, `camelCase` for locals and parameters, and trailing underscores for members (`store_`). Use `kPascalCase` for constants and `ff<component>` namespaces. Keep headers self-contained and prefer standard C++ types at component boundaries. The build enforces `/W4`, `/WX`, `/permissive-`, and C++20; warnings are errors.

## Testing Guidelines

Add tests beside the affected component and name files `test_<behavior>.cpp`; benchmarks use `bench_<behavior>.cpp`. Register new executables with CTest in the component's `CMakeLists.txt`. Cover success, malformed-input, recovery, and boundary cases where relevant. Always run the focused test first, then `ctest --preset debug` before submitting.

## Commit & Pull Request Guidelines

History favors imperative, outcome-oriented subjects such as `Wire engine ingestion pipeline` and scoped summaries such as `index-storage-and-scanning: ...`. Keep commits focused and explain non-obvious security or recovery behavior in the body. Pull requests should summarize behavior, identify affected components, link the relevant issue or OpenSpec task, and list exact validation commands. Include screenshots for visible UI changes and call out privilege, installer, schema, or migration impacts.
