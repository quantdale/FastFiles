# FastFiles

FastFiles is a native **C++20 file manager for Windows 10 (1809+)/Windows 11**, inspired by WizFile, WizTree, and macOS Finder. It is built around a **persistent NTFS filesystem index**: a privileged service reads the raw MFT and USN journal, an unprivileged per-logon-session engine owns a SQLite-backed index and serves UI clients, and a Direct2D/DirectComposition desktop UI presents a Finder-style Column View with instant search, file operations, and file preview.

The whole product rests on one deliberately early architectural decision — a **three-process privileged/unprivileged split** — so that raw MFT/USN access (which requires elevation on *every* `DeviceIoControl` call) never forces the application to run as administrator for normal use.

## Features

- **Column View navigation** — Finder-style columns with keyboard-driven navigation (workspace, sidebar, chrome).
- **Instant search** — query parsing over the indexed projection (`ffsearch`).
- **File operations** — copy/move/rename/delete driven by `IFileOperation` on a dedicated STA worker thread, with a conflict dialog.
- **File preview & properties** — in-app preview pane and property display.
- **Command palette & shell integration** — command system, quick actions, and shell menu integration.
- **Degraded mode is a first-class, permanent state** — when the service is absent, declined, or its connection drops, the engine falls back to unprivileged `FindFirstFileEx` tree walks plus `ReadDirectoryChangesW` watches, shown as a small non-modal status badge.

## Architecture

FastFiles splits into three executables plus an installer, over shared static libraries:

| Process | Target | Role |
| --- | --- | --- |
| **FastFilesIndexSvc** | `src/indexsvc/` | The **privileged** Windows service. Runs under **LocalSystem** as a deliberately *constrained privileged broker* (evidence: `openspec/changes/resolve-raw-volume-privilege-insufficiency/evidence/matrix-execution-and-selection.md`). Intentionally **stateless**: it holds no index and parses no queries — a thin, narrow relay for raw MFT/USN bytes, keeping the privileged binary small and auditable. |
| **FastFilesEngine** | `src/engine/` | The **unprivileged**, per-logon-session process that *owns* the index. Manages the privileged-connection lifecycle (connect/handshake/heartbeat/reconnect/degrade), serves UI clients, and provides the degraded-mode directory browsing path. Entry: `src/engine/src/Main.cpp`. |
| **FastFiles** | `src/ui/` | The UI shell (`WIN32` GUI target). Direct2D/DirectComposition compositor (`d3d11 dxgi d2d1 dwrite dcomp`) rendering the Column View; any number of windows. |
| **FastFilesSetup** | `src/installer/` | Installer/setup entry point driving `ffsetup`. |

Shared static libraries:

- **`ffprotocol`** (`src/protocol/`) — the wire protocol: frame format, closed command enum, record layouts, dispatch, version negotiation, and the memory-mapped snapshot format. Depended on by everything.
- **`ffipc`** (`src/ipc/`) — named-pipe framing and listener (`PipeFraming`, `PipeListener`).
- **`ffindexstore`** (`src/indexstore/`) — SQLite-backed durable store plus in-memory projection with WAL checkpointing, integrity verification, and crash-recovery.
- **`ffsearch`** (`src/search/`) — instant-search query parsing and history.
- **`ffsetup`** (`src/setup/`) — privileged install-time operations: service registration, group setup, install-dir ACLs, scheduled-task registration, Authenticode verification, security descriptors.
- **`fftest`** (`src/fftest/`) — a probe binary for privilege diagnostics against the running service.

## Security model

The engine-service seam is deliberately narrow and hardened (see `openspec/changes/establish-architecture-foundation/design.md`):

- **Closed command protocol** — a fixed tiny enum (`Handshake / EnumerateVolumes / StartVolumeScan / OpenUsnJournal / StopVolumeScan / CloseUsnJournal / Heartbeat`); there is deliberately no generic "open this path/handle" primitive.
- **Connection-scoped opaque handles** — `VolumeId`/`JournalId` are service-assigned and scoped to the creating connection.
- **Symmetric mutual authentication** — both directions verify the peer's image path (under the ACL-locked install dir) *and* a pinned Authenticode signature thumbprint, re-validated periodically on long-lived connections.
- **Parser hardening** — one protocol-wide max frame size checked before allocation, cross-validated batch record counts, and length-prefixed fields that reject the whole record on out-of-range values (never silently clamped). On-disk NTFS attribute bytes are treated as fully untrusted.
- **No SCM control rights to clients** — the `FastFilesUsers` group gets query/status only; the service self-heals via staleness detection plus SCM failure actions.
- **Never forward file content** — the MFT parser reads/forwards only `$STANDARD_INFORMATION` and `$FILE_NAME`, never `$DATA`.
- **DLL hardening** — `SetDefaultDllDirectories` before any `LoadLibrary`; fully-qualified paths or static linking.

## Repository layout

```
src/            First-party code, one CMake target per component
  protocol/     Wire protocol (shared messages, frames, dispatch, versioning)
  ipc/          Named-pipe framing and listener
  setup/        Privileged install-time operations
  indexstore/   SQLite-backed durable store + in-memory projection
  search/       Instant-search query parsing and history
  indexsvc/     Privileged NTFS indexing service
  engine/       Index ownership, privileged-connection lifecycle, UI serving
  ui/           Desktop interface (Direct2D/DirectComposition Column View)
  installer/    Installer/setup entry point
  fftest/       Privilege-diagnostics probe
tests/          Plain C++ test executables mirroring components (see below)
verify/         PowerShell runtime-verification harness (verify.ps1)
openspec/       OpenSpec design proposals, specs, and task tracking
third_party/    Vendored code (SQLite amalgamation) — avoid incidental edits
```

Public headers live in each component's `include/ff*` directory; implementation files live in `src/`. Tests mirror components under `tests/`; benchmarks use `bench_<behavior>.cpp`.

## Prerequisites

- Windows 10 1809+ or Windows 11, **x64** (the project is Windows-only by design; CMake `FATAL_ERROR`s on any other platform).
- Visual Studio 2022 (or Build Tools) with the **MSVC** toolchain and CMake >= 3.24.
- A Visual Studio-bundled **Ninja** executable, exposed as `FASTFILES_NINJA_EXE`.

## Building

Run commands from a Visual Studio developer PowerShell on Windows:

```powershell
# Set the Ninja path used by the CMake presets
$env:FASTFILES_NINJA_EXE = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

cmake --preset debug        # configure a Debug build in build/debug
cmake --build --preset debug
```

Presets:

- `debug` — Debug build (`build/debug`)
- `release` — Release build (`build/release`)
- `analyze` — Debug build with MSVC static analysis (`/analyze`)

Compiler/linker flags are strict and non-negotiable (`/W4 /WX /permissive- /sdl /guard:cf` and `/DYNAMICBASE /NXCOMPAT /guard:cf`); **warnings are errors**.

## Testing

Tests are plain C++ executables registered with CTest — no gtest/catch. They use a hand-rolled `Check(condition, description)` helper and return a non-zero exit code on failure.

```powershell
ctest --preset debug                        # full test suite
ctest --test-dir build/debug -R indexstore  # focused subset
ctest --test-dir build/debug -R ffprotocol_fuzz_tests --output-on-failure
```

Tests are on by default (`FASTFILES_BUILD_TESTS=ON`).

## Runtime verification

`verify/verify.ps1` is the autonomous runtime-verification harness entry point. It exposes a fixed, non-interactive verb set (`build`, `install`, `run`, `diagnose`, `report`, `repair`, `gate`, `list`, `doctor`) with documented exit codes. Generated `verify/runs/` and `verify/baselines/` artifacts are untracked.

```powershell
pwsh ./verify/verify.ps1
```

## Project conventions & workflow

This project uses **OpenSpec** (`openspec/`, schema: spec-driven). Work is organized as *changes* under `openspec/changes/<name>/`, each with `proposal.md`, `design.md`, `specs/<capability>/spec.md`, and `tasks.md`. `tasks.md` files track implementation with `[x]`/`[ ]` checkboxes and are the source of truth for what is built vs. deferred. Code comments frequently cite task numbers (e.g. `// Task 4.10:`) that refer to the corresponding `tasks.md`.

C++ style: four-space indentation, opening braces on the same line, `PascalCase` types and public methods, `camelCase` locals and parameters, trailing-underscore members, `kPascalCase` constants, and `ff<component>` namespaces. Keep headers self-contained and prefer standard C++ types at component boundaries. Run the focused test first, then the full suite, before submitting.