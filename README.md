# FastFiles

FastFiles is a native **C++20 file manager for Windows 10 (1809+)/Windows 11**, inspired by WizFile, WizTree, and macOS Finder. It is built around a **persistent NTFS filesystem index**: a privileged service reads the raw MFT and USN journal, an unprivileged per-logon-session engine owns a SQLite-backed index and serves UI clients, and a Direct2D/DirectComposition desktop UI presents a Finder-style Column View with instant search, file operations, and file preview.

The whole product rests on one deliberately early architectural decision — a **three-process privileged/unprivileged split** — so that raw MFT/USN access (which requires elevation on *every* `DeviceIoControl` call) never forces the application to run as administrator for normal use.

**Running the app right now:** FastFiles ships **degraded mode as a first-class, permanent state**. The privileged MFT/USN scan is implemented but fails closed until authorized Authenticode pins are provisioned, so ordinary development builds browse real directories through unprivileged Win32 enumeration — no service, no admin rights, and no certificate required. See [Quick start](#quick-start).

## Contents

- [Quick start](#quick-start)
- [Features](#features)
- [Prerequisites](#prerequisites)
- [Building](#building)
- [Running FastFiles](#running-fastfiles)
- [Testing](#testing)
- [Diagnostics and troubleshooting](#diagnostics-and-troubleshooting)
- [Runtime verification](#runtime-verification)
- [Architecture](#architecture)
- [Security model](#security-model)
- [Repository layout](#repository-layout)
- [Project conventions & workflow](#project-conventions--workflow)
- [Related documents](#related-documents)

## Quick start

From a **Visual Studio Developer PowerShell** (see [Prerequisites](#prerequisites)):

```powershell
git clone https://github.com/quantdale/FastFiles.git
cd FastFiles

# Build a disposable local run of the desktop app (no admin, no service, no certificate).
cmake --preset debug-diagnostic
cmake --build --preset debug-diagnostic

# Launch the UI. It locates and starts its sibling engine automatically.
.\build\debug-diagnostic\src\ui\FastFiles.exe
```

For day-to-day test builds use the plain `debug` preset instead:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

`build_and_deploy.ps1` in the repository root wraps the configure/build/test sequence (and, only with `-Commit -CommitMessage`, an opt-in commit and push):

```powershell
./build_and_deploy.ps1              # configure + build + full test suite
./build_and_deploy.ps1 -SkipTests   # build only
```

## Features

- **Column View navigation** — Finder-style columns with keyboard-driven navigation (workspace, sidebar, chrome).
- **Home / Quick Access** — a first-class Home navigation state (sidebar entry, `Alt+Home`, default for new tabs) with Pinned bookmarks, recent folders, recent searches, common locations, and quick actions — fully local and degraded-mode capable.
- **Instant search** — query parsing over the indexed projection (`ffsearch`).
- **File operations** — copy/move/rename/delete driven by `IFileOperation` on a dedicated STA worker thread, with a conflict dialog.
- **File preview & properties** — in-app preview pane and property display.
- **Command palette & shell integration** — command system, quick actions, and shell menu integration.
- **Degraded mode is a first-class, permanent state** — when the service is absent, declined, or its connection drops, the engine falls back to unprivileged `FindFirstFileEx` tree walks plus `ReadDirectoryChangesW` watches, shown as a small non-modal status badge.
- **Active-path resilience** — degraded watcher setup/retirement, snapshot publication, engine reconnect, and asynchronous search supersession are covered by deterministic fault-injection and marked-fixture convergence tests; the privileged path remains fail-closed.
- **Interactive UI determinism** — dual-pane command activation and UIA provider reconstruction use semantic postconditions, direct live-child reacquisition, stale-fragment invalidation, exact scenario process ownership, and bounded cleanup. A foreign interactive foreground window is recorded as an external skip rather than a product pass or retry.

## Prerequisites

- **Windows 10 1809+ or Windows 11, x64.** FastFiles is Windows-only by design; CMake `FATAL_ERROR`s on any other platform.
- **Visual Studio 2022 or newer** (IDE or Build Tools) with the **Desktop development with C++** workload: the MSVC toolset, Windows SDK, CMake, and Ninja.
- **CMake >= 3.25** and **Ninja** discoverable from the active developer environment. The presets use Ninja as the generator.
- **PowerShell 7+ (`pwsh`)** — required by the verification harness and by the PowerShell-gated tests (those tests are registered with CTest only when `pwsh` is found).
- **Git.**
- Optional, only for repo-local agent/MCP tooling (see `ONBOARDING.md`): Node.js/npm and Python.

Open the environment first — the commands below assume `cl`, `cmake`, and `ninja` are on `PATH`:

- Start menu → **Developer PowerShell for VS** (or **x64 Native Tools Command Prompt**), or
- from any PowerShell, run `VsDevCmd.bat`/`Launch-VsDevShell.ps1` from your Visual Studio installation.

Sanity check:

```powershell
cmake --version
ninja --version
cl
```

## Building

### Configure and build

```powershell
cmake --preset debug            # configure Debug into build/debug
cmake --build --preset debug    # build every app + test target
```

### Presets

<!-- ff-truth:presets-subset -->
`debug`, `debug-diagnostic`, `release`, `release-test`, `analyze`, `asan`
<!-- /ff-truth:presets-subset -->

| Preset | Build type | Output | Purpose and notes |
| --- | --- | --- | --- |
| `debug` | Debug | `build/debug/` | Day-to-day development and the full CTest suite. Its UI starts normally, but its lazy engine launch fails closed while the Authenticode pins are placeholders — see [Running FastFiles](#running-fastfiles). |
| `debug-diagnostic` | Debug + `FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED=ON` | `build/debug-diagnostic/` | Disposable local run/UIA/degraded validation. Enables the existing unsigned discovery seam so the UI can launch its own engine. Debug-only; `release`/`release-test` configuration rejects the option. |
| `release-test` | RelWithDebInfo + `FASTFILES_RELEASE_TEST_BUILD=ON` | `build/release-test/` | Optimized **unsigned** regression/performance profile used by CI. Explicitly not a production release. |
| `release` | Release | `build/release/` | Production profile. Configuration **fails closed** unless both authorized Authenticode pins are supplied (below). |
| `analyze` | Debug + MSVC `/analyze` | `build/analyze/` | Static analysis build. Static-analysis warnings are errors. |
| `asan` | Debug + `/fsanitize=address` | `build/asan/` | AddressSanitizer build for the fuzz/parser tests. |

### Configuration options

| Option | Default | Effect |
| --- | --- | --- |
| `FASTFILES_BUILD_TESTS` | `ON` | Builds and registers the test executables. |
| `FASTFILES_STATIC_ANALYSIS` | `OFF` | Adds MSVC `/analyze` (set by the `analyze` preset). |
| `FASTFILES_RELEASE_TEST_BUILD` | `OFF` | Marks an optimized unsigned test artifact (set by the `release-test` preset). |
| `FASTFILES_ENABLE_PRIVILEGED_TRUST_TESTS` | `OFF` | Opt-in registration of the `CurrentUser\Root` WinTrust integration tests. Never required for ordinary development; see `verify/run_privileged_trust_tests.ps1`. |
| `FASTFILES_ENGINE_SIGNATURE_THUMBPRINT` | empty (`0` pins) | 40-hex SHA-1 pin for the signed `FastFilesEngine` release binary; also read from the environment variable of the same name. |
| `FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT` | empty (`0` pins) | 40-hex SHA-1 pin for the signed `FastFilesIndexSvc` release binary; also read from the environment variable of the same name. |

Production Release configuration refuses placeholder or missing pins:

```powershell
$env:FASTFILES_ENGINE_SIGNATURE_THUMBPRINT = '<40-hex-thumbprint>'
$env:FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT = '<40-hex-thumbprint>'
cmake --preset release
cmake --build --preset release
```

Never weaken this boundary to make development or CI convenient; ordinary development uses `debug`, `debug-diagnostic`, and `release-test`.

### Build targets and where artifacts land

Ninja writes each executable into its component's binary directory, `build/<preset>/src/<component>/`.

<!-- ff-truth:product-executables -->
| Executable | Built to | Role |
| --- | --- | --- |
| **FastFiles** | `build\<preset>\src\ui\FastFiles.exe` | Direct2D/DirectComposition desktop shell (`WIN32` GUI target). |
| **FastFilesEngine** | `build\<preset>\src\engine\FastFilesEngine.exe` | Unprivileged per-logon-session index owner; serves UI clients. |
| **FastFilesIndexSvc** | `build\<preset>\src\indexsvc\FastFilesIndexSvc.exe` | Privileged Windows service; registered by the installer, not run directly. |
| **FastFilesSetup** | `build\<preset>\src\installer\FastFilesSetup.exe` | Installer/uninstaller (`/install`, `/uninstall`); requires an approved signed package. |
<!-- /ff-truth:product-executables -->

`fftest.exe` (the privilege/diagnostics probe) is also built at `build\<preset>\src\fftest\fftest.exe`.

Build a single target, or clean-rebuild:

```powershell
cmake --build --preset debug --target FastFilesEngine

Remove-Item -Recurse -Force build\debug
cmake --preset debug
cmake --build --preset debug
```

### Versioning and flags

`VERSION` is the single product-version source. CMake embeds canonical Windows VERSIONINFO metadata in every shipped executable and the `fftest` probe; release-kit generation and release-readiness fail closed if the final artifact metadata is missing, inconsistent, or identifies the wrong file.

Compiler/linker flags are strict and non-negotiable (`/W4 /WX /permissive- /sdl /guard:cf`, `/Zc:__cplusplus`, `/utf-8`, and linker `/DYNAMICBASE /NXCOMPAT /guard:cf`); **warnings are errors**, including in new test code.

## Running FastFiles

### Recommended local run: `debug-diagnostic`

```powershell
cmake --preset debug-diagnostic
cmake --build --preset debug-diagnostic
.\build\debug-diagnostic\src\ui\FastFiles.exe
```

On launch the UI looks for the engine **only** at deterministic locations (its own directory, the sibling `..\engine\` development layout, then `%ProgramFiles%\FastFiles`) and starts `FastFilesEngine.exe` if the per-session control pipe is not already reachable. With `debug-diagnostic` that unsigned sibling engine is accepted through the diagnostic-only escape hatch, and the UI connects and browses immediately in degraded mode.

Why not a plain `debug` build? Without provisioned pins, the UI's engine-launch validation fails closed (`unsigned` / `pins-unconfigured`), so no engine is started and the UI reports the engine as unavailable. `debug-diagnostic` exists precisely for this disposable local run/UIA validation use and is never a shipped configuration. If you prefer a plain `debug` build for debugging the engine, start the engine yourself first — the UI then connects to the already-running engine:

```powershell
# Terminal 1
.\build\debug\src\engine\FastFilesEngine.exe
# Terminal 2
.\build\debug\src\ui\FastFiles.exe
```

### What you should see

- The **Home / Quick Access** location is the default for new tabs; browsing works without any service. A small non-modal badge reports the degraded/privileged state.
- Recent-folder history, bookmarks, settings, and recent searches persist under `%LOCALAPPDATA%\FastFiles\` (below).
- The engine keeps running after the UI exits; its idle manager drops the privileged connection after inactivity, but the process remains until stopped. Kill the whole tree when you are done:

```powershell
taskkill /IM FastFiles.exe /T /F
taskkill /IM FastFilesEngine.exe /F
```

(The `/T` form terminates the engine the UI launched as its child. Stop both if you started the engine separately; an orphaned engine holds the per-session `FastFiles.Ui.Ctrl.<session>` pipe and will break engine-client tests.)

### Debug gallery

Debug builds accept a `--gallery` flag that renders the component gallery instead of the shell:

```powershell
.\build\debug-diagnostic\src\ui\FastFiles.exe --gallery
```

The flag is debug-only: optimized (non-`_DEBUG`) builds refuse it at startup.

### The privileged service and the installer

`FastFilesIndexSvc.exe` is registered as a Windows service by `FastFilesSetup.exe`, not run directly. Both the installer and its package verification require an approved Authenticode signer and a signed package manifest; with the repository's placeholder pins both **fail closed**, by design. Ordinary development therefore uses the degraded engine path. Production registration, signing pins, and clean-host install validation are external, release-time prerequisites — see `docs/release/README.md`.

### Application state

| Path | Contents |
| --- | --- |
| `%LOCALAPPDATA%\FastFiles\index.db` | Engine SQLite index (WAL mode; rebuilt on corruption). |
| `%LOCALAPPDATA%\FastFiles\settings.json` | Persisted settings (with `settings.json.bak` fallback). |
| `%LOCALAPPDATA%\FastFiles\workspace-state.json` | Navigation workspace: tabs, panes, bookmarks, recent folders. |
| `%LOCALAPPDATA%\FastFiles\search-history.txt` | Recent search queries. |
| `%LOCALAPPDATA%\FastFiles\logs\diagnostics.log` | Structured append-only diagnostic events (engine launch, connection, indexing, watcher failures). |

All of this is disposable development state; deleting `%LOCALAPPDATA%\FastFiles` resets the app to first-run defaults.

## Testing

Tests are plain C++ executables registered with CTest — no gtest/catch. They use the shared `Check(condition, description)` helper in `tests/TestSupport.h` (a few older tests keep a local exit-on-first-failure variant) and return a non-zero exit code on failure. Test names equal their CMake target names, so `ctest -R <substring>` is reliable.

```powershell
ctest --preset debug                        # full Debug suite
ctest --test-dir build/debug -R indexstore  # focused subset (any substring)
ctest --test-dir build/debug -R ffprotocol_fuzz_tests --output-on-failure
ctest --test-dir build/debug -N             # list registered tests
```

Tight edit/test loop for one target:

```powershell
cmake --build --preset debug --target ffprotocol_fuzz_tests
ctest --test-dir build/debug -R ffprotocol_fuzz_tests --output-on-failure
```

CTest presets exist for `debug`, `release`, and `release-test` only:

<!-- ff-truth:test-presets-all -->
`debug`, `release`, `release-test`
<!-- /ff-truth:test-presets-all -->

There is **no test preset** for `analyze`, `asan`, or `debug-diagnostic`; point CTest at the directory instead, for example `ctest --test-dir build/debug-diagnostic`.

Notes:

- The default suite is fully portable: no Administrator, no certificate, and no trust-store mutation are required to build or test FastFiles normally.
- ASan fuzz/parser runs: `cmake --preset asan && cmake --build --preset asan --target ffprotocol_fuzz_tests`, then `ctest --test-dir build/asan -R ffprotocol`.
- The opt-in privileged trust-store suite runs only through `pwsh ./verify/run_privileged_trust_tests.ps1` (registered under the `privileged-trust` label when `FASTFILES_ENABLE_PRIVILEGED_TRUST_TESTS=ON`); it is never part of the default `ctest` run or CI.
- `ffdoc_drift_tests` guards the guidance docs (banned superseded claims, mixed line endings in `src/`/`tests/`, machine-checked truth contracts). Run it after editing root docs: `pwsh -NoProfile -File tests/drift/check_doc_drift.ps1`.
- `tests/protocol/test_fuzz.cpp` must be updated whenever protocol parsing changes (frames, records, snapshot format).
- Do **not** launch the app or a UIA probe while `verify.ps1 build` is running — a concurrent launch has made the gate report spurious assertion failures.

See `tests/README.md` for conventions and per-component coverage.

## Diagnostics and troubleshooting

### The `fftest` probe

`build\<preset>\src\fftest\fftest.exe` is a read-only diagnostics binary:

```
usage: fftest <privilege|token|acl <path>|mapping <name>|walk <root>|streams <file>|
              handshake-impostor <install-dir>|index-aggregate <db> <folder> <items> <bytes>|
              ntfs-observe <L> <semantics|find|usn> [...]>
```

- `fftest token` / `fftest privilege` — token elevation, integrity level, enabled privileges; raw-volume-open sufficiency.
- `fftest walk <root>` — reproduces the product's degraded enumeration (bounded, reparse-aware).
- `fftest mapping <name>` — verifies read access to a named snapshot section.
- `fftest ntfs-observe <L> <semantics|find|usn>` — read-only MFT/USN observation through the production ioctl sequences; non-admin runs report a clean `BLOCKED` exit.

### Common issues

| Symptom | Cause / fix |
| --- | --- |
| UI starts but the engine stays unavailable | Plain `debug` build with placeholder pins refuses the unsigned engine. Use `debug-diagnostic`, or start `FastFilesEngine.exe` yourself first; check `FFDIAG_ENGINE_SIGNATURE_REJECTED` in `diagnostics.log`. |
| `cmake --preset release` refuses to configure | Placeholder/missing Authenticode pins. Supply both 40-hex thumbprints; only for the authorized release workflow. |
| `ctest --preset analyze` fails | No test preset exists for `analyze`. Use `ctest --test-dir build/analyze`. |
| PowerShell-gated tests are missing from `ctest -N` | `pwsh` was not found at configure time. Install PowerShell 7+ and re-run CMake. |
| Build fails on a warning | Warnings are errors (`/W4 /WX`). Fix the warning; do not lower the warning level. |
| Weird persisted UI state | Delete `%LOCALAPPDATA%\FastFiles` (disposable development state) and relaunch. |
| `verify.ps1 build` reports assertion failures | A concurrent app/UIA probe launch perturbs the gate. Close FastFiles and re-run. |

## Runtime verification

<!-- ff-truth:verifier-verbs -->
`verify/verify.ps1` is the autonomous runtime-verification harness entry point. It exposes a fixed, non-interactive verb set (`build`, `install`, `run`, `diagnose`, `report`, `repair`, `gate`, `list`, `doctor`, `status`, `release-readiness`, `release-candidate`); the authoritative definition is the `$Verb` `ValidateSet` in that script.
<!-- /ff-truth:verifier-verbs -->
It documents exit codes, tiers, and capability composition in `verify/README.md`. Generated `verify/runs/` and `verify/baselines/` artifacts are untracked.

```powershell
pwsh ./verify/verify.ps1 doctor    # toolchain / prerequisite readiness
pwsh ./verify/verify.ps1 build     # full multi-config build + test gate
pwsh ./verify/verify.ps1 status    # machine-readable project/frontier state
pwsh ./verify/verify.ps1 run -Capability windows-active-path-resilience-validation
pwsh ./verify/verify.ps1 release-readiness -Mode report
```

Use `-CapabilityOption ActivePathSoakSeconds=<seconds>` to select a shorter or longer bounded soak for the active-path capability. Tier-1 (install/service) verbs need `-Elevate` (opt-in, one-time UAC authorization).

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
tests/          Plain C++ test executables mirroring components (see tests/README.md)
verify/         PowerShell runtime-verification harness (verify.ps1)
openspec/       OpenSpec design proposals, specs, and task tracking
third_party/    Vendored code (SQLite amalgamation) — avoid incidental edits
docs/           Release procedure, quality matrix, benchmarks, agent workstate
```

Public headers live in each component's `include/ff*` directory; implementation files live in `src/`. Tests mirror components under `tests/`; benchmarks use `bench_<behavior>.cpp`.

## Project conventions & workflow

This project uses **OpenSpec** (`openspec/`, schema: spec-driven). Work is organized as *changes* under `openspec/changes/<name>/`, each with `proposal.md`, `design.md`, `specs/<capability>/spec.md`, and `tasks.md`. `tasks.md` files track implementation with `[x]`/`[ ]` checkboxes and remain the raw acceptance source of truth. Which changes are active, completed, superseded, or external-evidence-only is owned by the schema-v2 lifecycle manifest `openspec/lifecycle.json`; consult it (plus `.agent/EXECUTION_PROMPT.md`) instead of any campaign list copied here — completed campaigns remain in-tree as historical evidence only. Code comments frequently cite task numbers (e.g. `// Task 4.10:`) that refer to the corresponding `tasks.md`.

C++ style: four-space indentation, opening braces on the same line, `PascalCase` types and public methods, `camelCase` locals and parameters, trailing-underscore members, `kPascalCase` constants, and `ff<component>` namespaces. Keep headers self-contained and prefer standard C++ types at component boundaries. Run the focused test first, then the full suite, before submitting.

Commit history favors imperative, outcome-oriented subjects; PRs should summarize behavior, identify affected components, link the relevant OpenSpec task, and list exact validation commands.

## Related documents

- `AGENTS.md` — authoritative repository guide for contributors and coding agents (architecture, security invariants, testing, workflow).
- `ONBOARDING.md` — fresh-machine bootstrap checklist.
- `CODE_INDEX.md` — file-and-type-level map of the source tree.
- `tests/README.md` — test conventions and registration patterns.
- `verify/README.md` — verification harness verbs, tiers, exit codes, and capability contract.
- `docs/release/README.md` — release-candidate and production-signing procedure.
- `docs/quality/SUPPORT_MATRIX.md` — supported vs. externally validated platform matrix.
- `docs/agent/WORKSTATE.md` — durable engineering workstate / resume checkpoint.
