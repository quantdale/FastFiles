# Repository Guidelines

## What this is

FastFiles is a Windows-only C++20 native file manager built around a persistent NTFS index. The whole product rests on one deliberate, first-made decision: a **three-process privileged/unprivileged split** so raw MFT/USN access (which needs elevation on *every* `DeviceIoControl`) never forces the app to run as admin for normal use. CMake `FATAL_ERROR`s on non-Windows; there is no cross-platform layer (Win32/COM/Direct2D/NTFS only). Before touching process boundaries, IPC, or the security model, read `openspec/changes/establish-architecture-foundation/design.md` — it is the authoritative record of *why* (decisions D1–D6 + an adversarial Risks section).

## Architecture

Three executables over shared static libs; each `src/<name>/` is one CMake target. Public headers live in each component's `include/ff*` directory; implementation files in `src/`.

- **FastFilesIndexSvc** (`src/indexsvc/`) — the **privileged** Windows service. Runs under a virtual service account with **`SeBackupPrivilege` only** (never LocalSystem/admin). Intentionally **stateless**: a thin, narrow relay for raw MFT/USN bytes — no index, no query parsing.
- **FastFilesEngine** (`src/engine/`) — the **unprivileged**, per-logon-session process that *owns* the index and the privileged-connection lifecycle. Entry: `src/engine/src/Main.cpp`.
- **FastFiles** (`src/ui/`) — the desktop shell (`WIN32` GUI). Direct2D/DirectComposition Column View (`d3d11 dxgi d2d1 dwrite dcomp`).
- **FastFilesSetup** (`src/installer/`) — installer/setup entry point driving `ffsetup`.

Shared libs: `ffprotocol` (wire protocol — depended on by everything), `ffipc` (named-pipe framing/listener), `ffindexstore` (SQLite store + in-memory projection), `ffsearch` (query parsing/history), `ffsetup` (privileged install-time ops: service/group/ACL/task registration, Authenticode verification), `fftest` (privilege-diagnostics probe binary).

**Two IPC seams, both named pipes only** — the only kernel-object type on purpose (one thing to ACL correctly; reference impls Everything and Docker Desktop shipped LPE/DoS CVEs from object-ACL mistakes here). 1) Engine↔Service = the elevation boundary (hardened hard). 2) Engine↔UI = same-privilege control plane only.

**Keystroke search has zero IPC round-trip**: the engine publishes an immutable, double-buffered index into a read-only memory-mapped section (`Local\FastFiles.IndexSnapshot.<SessionId>`) and only sends a lightweight "new generation ready" notification over the control pipe. The UI maps once, re-maps on notification, and reads search/filter/sort entirely in-process. See `SnapshotFormat.h` / `SnapshotPublisher`.

## Current state: degraded mode is the active path

**The privileged MFT/USN scan calls are currently stubbed.** Column View browses entirely through the degraded path today: unprivileged `FindFirstFileEx` tree walks + one `ReadDirectoryChangesW` watch per browsed/pinned root (`DegradedModeEnumerator`, `DirectoryWatcher`), shown as a small non-modal status badge. Degraded mode is a first-class, permanent state — not an error path — so any UI/engine work must keep working without the service. Do not assume MFT/USN scanning works end-to-end.

## Security invariants — do not regress

These are enumerated in the foundation `design.md` (D4 + Risks); the code exists to enforce them.

- **Closed command protocol.** Engine→service is a fixed tiny enum (`Handshake / EnumerateVolumes / StartVolumeScan / OpenUsnJournal / StopVolumeScan / CloseUsnJournal / Heartbeat`). There is deliberately **no** generic "open this path/handle" primitive — do not add one.
- **Connection-scoped opaque handles.** `VolumeId`/`JournalId` are service-assigned, opaque, and scoped to the creating connection; `Stop`/`Close` from another connection is rejected.
- **Frame/parser hardening.** One protocol-wide max frame size (`kMaxFrameSize`, 1 MiB in `Frame.h`), checked **before any allocation** in `u64`/`size_t` arithmetic — never the `u32` field width (prevents integer-overflow-to-buffer-overflow). Batch record counts are cross-validated against bytes received before parsing any record. Out-of-range length-prefixed fields **reject the whole record**, never silently clamped. Treat raw on-disk NTFS attribute bytes as fully untrusted (a plugged-in USB/VHD reaches the parser with no admin action).
- **Symmetric mutual authentication.** Both directions verify the peer's image path (under the ACL-locked install dir) *and* a pinned Authenticode signature thumbprint — not just group membership — re-validated periodically on long-lived connections, not only at handshake.
- **No SCM control rights to clients.** The `FastFilesUsers` group gets `SERVICE_QUERY_STATUS/CONFIG` only, never start/stop. The service self-heals via self-directed staleness detection (hashes its own on-disk binary vs. what it loaded) + SCM native failure-actions restart.
- **Never forward file content.** The MFT parser reads/forwards only `$STANDARD_INFORMATION` and `$FILE_NAME` — never `$DATA` (small files store content resident in the MFT record).
- **DLL hardening.** `SetDefaultDllDirectories` before any `LoadLibrary`; fully-qualified paths or static linking. Direct fix for the DLL-search-order LPE class (Everything's CVE-2020-24567).

## Kilo configuration

`kilo.json` already has `"permission": { "bash": "allow" }` — bash commands are enabled for this project. No additional setup is needed.

Run from a Visual Studio developer PowerShell on Windows. Set `FASTFILES_NINJA_EXE` (the VS-bundled Ninja) **before configuring** — the presets read it for `CMAKE_MAKE_PROGRAM`.

```powershell
$env:FASTFILES_NINJA_EXE = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

cmake --preset debug            # configure Debug into build/debug
cmake --build --preset debug    # build all app + test targets
ctest --preset debug            # full test suite
ctest --test-dir build/debug -R ffprotocol_fuzz_tests --output-on-failure   # single test
cmake --preset analyze && cmake --build --preset analyze   # + MSVC /analyze
```

Presets: `debug`, `release`, `analyze` (Debug + `/analyze` via `FASTFILES_STATIC_ANALYSIS=ON`). CTest presets exist for `debug` and `release` only (no `analyze` test preset). Tests are on by default (`FASTFILES_BUILD_TESTS=ON`).

Compiler/linker flags are strict and non-negotiable (`/W4 /WX /permissive- /sdl /guard:cf /Zc:__cplusplus`; link `/DYNAMICBASE /NXCOMPAT /guard:cf`); **warnings are errors** — new code must be warning-clean. The vendored SQLite C amalgamation (`third_party/sqlite/`) keeps its own warning config (compiled as C for the SQLite target only) and must not trip FastFiles analysis; avoid incidental edits there.

## Testing

Tests are **plain C++ executables registered with CTest — no gtest/catch**. They use a hand-rolled `Check(condition, description)` helper that increments a failure counter and returns non-zero exit on failure (see `tests/protocol/test_protocol.cpp`). `test_fuzz.cpp` feeds malformed frames/records at the parsers — keep it updated when protocol parsing changes.

- Test dirs mirror components: `tests/{protocol,ipc,indexstore,search,indexsvc,engine,navigation,commands,fileoperations,preview}`.
- File naming: `test_<behavior>.cpp`; benchmarks `bench_<behavior>.cpp`. Register new executables with CTest in the component's `CMakeLists.txt`.
- CTest `-R` matches the **test name**, which equals the target name — e.g. `ffprotocol_tests`, `ffprotocol_fuzz_tests`, `ffprotocol_diagnostics_tests`. Use `-R <substring>` (e.g. `-R indexstore`) to run one component's tests.
- Cover success, malformed-input, recovery, and boundary cases where relevant. Run the focused test first, then `ctest --preset debug` before submitting.

## Runtime verification

`verify/verify.ps1` is the autonomous verification harness entry point. Fixed non-interactive verbs: `build`, `install`, `run`, `diagnose`, `report`, `repair`, `gate`, `list`, `doctor`. Exit codes: `0`=PASS, `1`=FAIL, `2`=SKIPPED, `3`=HARNESS ERROR, `10`=NOT-YET-IMPLEMENTED. Tier-1 (install/service) verbs need `-Elevate` (opt-in UAC, one-time authorization). Generated `verify/runs/` and `verify/baselines/` are untracked (execution evidence, not source).

```powershell
pwsh ./verify/verify.ps1 build     # windows-build-validation capability
pwsh ./verify/verify.ps1 doctor   # inspect VS toolchain / prerequisites
```

## Workflow: OpenSpec (spec-driven)

Work is organized as *changes* under `openspec/changes/<name>/`, each with `proposal.md`, `design.md`, `specs/<capability>/spec.md`, and `tasks.md`. **`tasks.md` files are the source of truth for what's built vs. deferred** — consult the relevant one before implementing a feature, and check off `[x]`/`[ ]` items as you complete them. Code comments frequently cite task numbers (e.g. `// Task 4.10:`) referring to the corresponding `tasks.md`; keep that traceability when adding code. The foundation change is `establish-architecture-foundation`; later product pillars (instant-search, storage-analysis, file-operations, navigation-and-workspace, etc.) are separate changes, mostly still specs/design without code. Use the `openspec-*` skills for proposing/updating/applying/archiving changes.

## Coding style

Four-space indent, opening brace on the same line, `PascalCase` for types and public methods, `camelCase` for locals and parameters, trailing-underscore members (`store_`), `kPascalCase` for constants, `ff<component>` namespaces. Keep headers self-contained and prefer standard C++ types at component boundaries.

## Commit & pull request guidelines

History favors imperative, outcome-oriented subjects (e.g. `Wire engine ingestion pipeline`) and scoped summaries (e.g. `index-storage-and-scanning: ...`). Keep commits focused; explain non-obvious security or recovery behavior in the body. PRs should summarize behavior, identify affected components, link the relevant issue/OpenSpec task, and list exact validation commands. Include screenshots for visible UI changes and call out privilege, installer, schema, or migration impacts.
