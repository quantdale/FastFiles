# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

FastFiles is a native C++ Windows file manager (WizFile/WizTree/Finder-inspired) built around a
persistent NTFS filesystem index. The whole product rests on one hard architectural decision, made
first and deliberately: a **three-process privileged/unprivileged split** so that raw MFT/USN journal
access (which needs elevation on *every* `DeviceIoControl` call) never forces the app to run as admin
for normal use. Read `openspec/changes/establish-architecture-foundation/design.md` before making any
change that touches process boundaries, IPC, or the security model — it is the authoritative record of
why things are the way they are (decisions D1–D6, plus an adversarial-review Risks section).

Windows-only, by design. CMake `FATAL_ERROR`s on non-Windows; the code uses Win32/COM/Direct2D/NTFS
APIs throughout with no cross-platform layer.

## Build, test, run

Requires Windows + MSVC toolchain (Visual Studio / Build Tools), CMake ≥ 3.24, x64. Standard C++20.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure   # run all tests
ctest --test-dir build -C Debug -R ffprotocol_fuzz_tests --output-on-failure   # single test
```

Compiler/linker flags are **strict and non-negotiable** (set in the root `CMakeLists.txt`):
`/W4 /WX /permissive- /sdl /guard:cf` and `/DYNAMICBASE /NXCOMPAT /guard:cf`. Warnings are errors —
new code must be warning-clean. Tests are on by default (`FASTFILES_BUILD_TESTS=ON`).

Tests are plain C++ executables registered with CTest — no gtest/catch. They use a hand-rolled
`Check(condition, description)` helper and return a non-zero exit on failure (see
`tests/protocol/test_protocol.cpp`). `test_fuzz.cpp` feeds malformed frames/records at the parsers.

## Architecture: the three processes + shared libs

Each `src/<name>/` is one CMake target. Three executables plus an installer, over shared static libs.

- **`FastFilesIndexSvc`** (`src/indexsvc/`) — the **privileged** Windows Service. Runs under a dedicated
  virtual service account with **`SeBackupPrivilege` only** (never LocalSystem/admin). Intentionally
  **stateless**: it holds no index and parses no queries — a thin, narrow relay for raw MFT/USN bytes.
  Keeping this binary tiny and auditable is the entire point of the split.
- **`FastFilesEngine`** (`src/engine/`) — the **unprivileged**, per-logon-session process that *owns* the
  index. Manages the privileged-connection lifecycle (connect/handshake/heartbeat/reconnect/degrade),
  serves UI clients, and provides the degraded-mode directory browsing path. Entry: `src/engine/src/Main.cpp`.
- **`FastFiles`** (`src/ui/`) — the UI shell (a `WIN32` GUI target). Direct2D/DirectComposition compositor
  (`d3d11 dxgi d2d1 dwrite dcomp`) rendering the Finder-style Column View. Any number of windows.
- **`FastFilesSetup`** (`src/installer/`) — installer/setup entry point driving `ffsetup`.

Shared static libraries:
- **`ffprotocol`** (`src/protocol/`) — the wire protocol: frame format, closed command enum, record
  layouts, dispatch, version negotiation, the memory-mapped snapshot format. Depended on by everything.
- **`ffipc`** (`src/ipc/`) — named-pipe framing and listener (`PipeFraming`, `PipeListener`).
- **`ffsetup`** (`src/setup/`) — privileged install-time operations: service registration, group setup,
  install-dir ACLs, scheduled-task registration, Authenticode verification, security descriptors.

## Two IPC seams, both named pipes only

1. `FastFilesEngine ↔ FastFilesIndexSvc` — the actual **elevation boundary**. Hardened hard.
2. `FastFilesEngine ↔ FastFiles` — same-privilege, control-plane only.

Named pipes are the *only* IPC kernel-object type on purpose (one thing to ACL correctly instead of
several). Both reference implementations studied (Everything, Docker Desktop) shipped real LPE/DoS CVEs
from getting object-ACL details wrong on exactly this kind of boundary.

**Keystroke-latency search has zero IPC round-trip**: the engine publishes an immutable, double-buffered
index into a read-only memory-mapped section (`Local\FastFiles.IndexSnapshot.<SessionId>`) and only sends
a lightweight "new generation ready" notification over the control pipe. The UI maps once, re-maps on
notification, and reads search/filter/sort entirely in-process. See `SnapshotFormat.h` / `SnapshotPublisher`.

## Security invariants — do not regress these

These are enumerated in design.md D4 and its Risks section; the code exists to enforce them.

- **Closed command protocol.** The engine→service surface is a fixed tiny enum
  (`Handshake / EnumerateVolumes / StartVolumeScan / OpenUsnJournal / StopVolumeScan / CloseUsnJournal /
  Heartbeat`). There is deliberately **no** generic "open this path/handle" primitive. Do not add one.
- **Connection-scoped opaque handles.** `VolumeId`/`JournalId` are service-assigned, opaque, and scoped
  to the creating connection; a `Stop`/`Close` from another connection is rejected.
- **Frame/parser hardening.** One protocol-wide max frame size (`kMaxFrameSize`, 1 MiB in `Frame.h`),
  checked **before any allocation**, in `u64`/`size_t` arithmetic — never the `u32` field width (prevents
  integer-overflow-to-buffer-overflow). Batch record counts are cross-validated against bytes received
  before parsing any record. Every length-prefixed field is validated against its max and an out-of-range
  value **rejects the whole record** — never silently clamps. Treat raw on-disk NTFS attribute bytes as
  fully untrusted (a plugged-in USB/VHD reaches the parser with no admin action).
- **Symmetric mutual authentication.** Both directions verify the peer's image path (under the ACL-locked
  install dir) *and* a pinned Authenticode signature thumbprint — not just group membership — re-validated
  periodically on long-lived connections, not only at handshake.
- **No SCM control rights to clients.** The `FastFilesUsers` group gets `SERVICE_QUERY_STATUS/CONFIG` only,
  never start/stop. The service self-heals via self-directed staleness detection (hashes its own on-disk
  binary vs. what it loaded) + SCM's native failure-actions restart.
- **Never forward file content.** The MFT parser reads/forwards only `$STANDARD_INFORMATION` and
  `$FILE_NAME` — never `$DATA` (small files store content resident in the MFT record).
- **DLL hardening.** `SetDefaultDllDirectories` before any `LoadLibrary`; fully-qualified paths or static
  linking. Direct fix for the DLL-search-order LPE class (Everything's CVE-2020-24567).

## Degraded mode is a first-class, permanent state — not an error path

When the service is absent, declined, incompatible, or its connection drops, the engine falls back to
unprivileged `FindFirstFileEx` tree walks + one `ReadDirectoryChangesW` watch per browsed/pinned root
(`DegradedModeEnumerator`, `DirectoryWatcher`). This is shown as a small non-modal status badge, never a
blocking dialog. **In the current state of the repo the privileged MFT/USN scan calls are stubbed** —
Column View browses entirely through the degraded path today. Any UI/engine work must keep working in
degraded mode.

## Working in this repo: OpenSpec spec-driven workflow

This project uses **OpenSpec** (`openspec/`, `schema: spec-driven`). Work is organized as *changes* under
`openspec/changes/<name>/`, each with `proposal.md`, `design.md`, `specs/<capability>/spec.md`, and
`tasks.md`. `tasks.md` files track implementation with `[x]`/`[ ]` checkboxes and are the source of truth
for what's built vs. deferred — consult the relevant one before implementing a feature, and check off
tasks as you complete them. The foundation change is `establish-architecture-foundation`; later product
pillars (instant-search, storage-analysis, file-operations, navigation-and-workspace, etc.) are separate
changes, mostly still specs/design without code. Use the `opsx:*` / `openspec-*` skills for proposing,
updating, applying, and archiving changes.

Code comments frequently cite task numbers (e.g. `// Task 4.10:`) — those refer to the corresponding
`tasks.md`. Keep that traceability when adding code.
