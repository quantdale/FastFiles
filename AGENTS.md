# Repository Guidelines

## What this is

FastFiles is a Windows-only C++20 native file manager built around a persistent NTFS index. The whole product rests on one deliberate, first-made decision: a **three-process privileged/unprivileged split** so raw MFT/USN access (which needs elevation on *every* `DeviceIoControl`) never forces the app to run as admin for normal use. CMake `FATAL_ERROR`s on non-Windows; there is no cross-platform layer (Win32/COM/Direct2D/NTFS only). Before touching process boundaries, IPC, or the security model, read `openspec/changes/establish-architecture-foundation/design.md` — it is the authoritative record of *why* (decisions D1–D6 + an adversarial Risks section).

## Architecture

Three executables over shared static libs; each `src/<name>/` is one CMake target. Public headers live in each component's `include/ff*` directory; implementation files in `src/`. Known exception: `src/indexsvc/` hosts three targets (`ffmftparser`, `ffindexsvcprobes`, `FastFilesIndexSvc`) whose internal headers live in `src/indexsvc/src/`.

- **FastFilesIndexSvc** (`src/indexsvc/`) — the **privileged** Windows service. Runs under **LocalSystem** as a deliberately *constrained privileged broker* — the evidence-backed decision of `resolve-raw-volume-privilege-insufficiency` (see `openspec/changes/resolve-raw-volume-privilege-insufficiency/evidence/matrix-execution-and-selection.md`), because no narrow user right or group membership opens a raw volume device on modern Windows. Intentionally **stateless**: a thin, narrow relay for raw MFT/USN bytes — no index, no query parsing. The compensating mitigations that keep it a *constrained broker* rather than "the whole product as admin": stateless and closed command surface, symmetric signed-peer authentication, an SCM object granting the client group query-only rights, and startup privilege-set verification.
- **FastFilesEngine** (`src/engine/`) — the **unprivileged**, per-logon-session process that *owns* the index and the privileged-connection lifecycle. Entry: `src/engine/src/Main.cpp`.
- **FastFiles** (`src/ui/`) — the desktop shell (`WIN32` GUI). Direct2D/DirectComposition Column View (`d3d11 dxgi d2d1 dwrite dcomp`).
- **FastFilesSetup** (`src/installer/`) — installer/setup entry point driving `ffsetup`.

Shared libs: `ffprotocol` (wire protocol — depended on by everything), `ffipc` (named-pipe framing/listener), `ffindexstore` (SQLite store + in-memory projection), `ffsearch` (query parsing/history), `ffsetup` (privileged install-time ops: service/group/ACL/task registration, Authenticode verification), `fftest` (privilege-diagnostics probe binary).

**UI presentation layer** (`src/ui/src/`, part of the `FastFiles` target): a single Fluent-style design-token set in `UITheme.h` (`GetUiTheme(bool dark)` + `UiMetrics`; `ToColorRef`/`ToD2DColor` for GDI chrome; `gUiDarkTheme` global published by `WindowShell::ApplyTheme`; `UiSystemHighContrast()` gates token overlays), plus shared helpers `UiStyle.h` (rounded-rect fill/stroke, solid-brush ensure, `UiLerpColor`), `UiAnimation.h` (`SystemAnimationsEnabled()` + `FloatAnimation` ease-out lerp, gated and snap-instant when animations are off), and `IconCache.h` (bounded, DPI-aware, off-thread system image-list icons keyed by extension/folder, posted to the owner via `WM_APP_ICON_READY`). Every surface — Direct2D and owner-drawn Win32 chrome — sources colors/radii/spacing from this token set; new UI code must not hardcode `RGB`/`GetSysColor` literals (except High-Contrast fallbacks). Device loss (`D2DERR_RECREATE_TARGET`) is recovered by `Renderer` and fanned out to consumers via the same `ApplyTheme`-style dirty-marking.

**Two IPC seams, both named pipes only** — the only kernel-object type on purpose (one thing to ACL correctly; reference impls Everything and Docker Desktop shipped LPE/DoS CVEs from object-ACL mistakes here). 1) Engine↔Service = the elevation boundary (hardened hard). 2) Engine↔UI = same-privilege control plane only.

**Keystroke search has zero IPC round-trip**: the engine publishes an immutable, double-buffered index into a read-only memory-mapped section (`Local\FastFiles.IndexSnapshot.<SessionId>`) and only sends a lightweight "new generation ready" notification over the control pipe. The UI maps once, re-maps on notification, and reads search/filter/sort entirely in-process. See `SnapshotFormat.h` / `SnapshotPublisher`.

## Current state: degraded mode is the active path

**The privileged MFT/USN scan is implemented** (`VolumeScanner.cpp`, `UsnJournalReader.cpp`, `MftParser.cpp`, wired into `ServiceConnection.cpp`). Degraded mode remains the active/safe path today because the mutual-auth signature pins are placeholder and live-service operational validation is pending. Column View browses through the degraded path: unprivileged `FindFirstFileEx` tree walks + one `ReadDirectoryChangesW` watch per browsed/pinned root (`DegradedModeEnumerator`, `DirectoryWatcher`), shown as a small non-modal status badge. Degraded mode is a first-class, permanent state — not an error path — so any UI/engine work must keep working without the service. Do not assume the privileged scan path is production-validated end-to-end.

## Security invariants — do not regress

These are enumerated in the foundation `design.md` (D4 + Risks); the code exists to enforce them.

- **Closed command protocol.** Engine→service is a fixed tiny enum — the client→service commands (`Handshake / EnumerateVolumes / StartVolumeScan / OpenUsnJournal / StopVolumeScan / CloseUsnJournal / Heartbeat`) plus the additive service→client index-storage-and-scanning messages (`ScanRecordBatch / ScanComplete / UsnJournalOpened / JournalRecordBatch / JournalResumeInvalid`); `src/protocol/include/ffprotocol/Commands.h` is the source of truth. There is deliberately **no** generic "open this path/handle" primitive — do not add one.
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

- Test dirs mirror components: `tests/{protocol,ipc,indexstore,search,indexsvc,engine,navigation,commands,fileoperations,preview,ui,setup,uia-driver}` plus `tests/drift` (the doc-drift/line-ending guard). See `tests/README.md` for the test-directory overview.
- File naming: `test_<behavior>.cpp`; benchmarks `bench_<behavior>.cpp`. Register new executables with CTest in the component's `CMakeLists.txt`. The shared helper header is `tests/TestSupport.h` (`fftest::Check` / `fftest::FailureCount`) — use it in new tests instead of copy-pasting a local `Check`.
- CTest `-R` matches the **test name**, which equals the target name — e.g. `ffprotocol_tests`, `ffprotocol_fuzz_tests`, `ffprotocol_diagnostics_tests`. Use `-R <substring>` (e.g. `-R indexstore`) to run one component's tests.
- Single-target fast loop: `cmake --build --preset debug --target <test-target>`, then `ctest --test-dir build/debug -R <name> --output-on-failure`. Note there is no `analyze` test preset (CTest presets exist for `debug` and `release` only).
- `ffdoc_drift_tests` (pwsh-gated) checks the guidance docs for superseded claims and `src/`/`tests/` for mixed line endings — run it after editing root docs. The `asan` preset (configure/build only, no test preset) builds ASan-instrumented binaries for the fuzz/parser tests: `cmake --preset asan && cmake --build --preset asan --target ffprotocol_fuzz_tests`.
- Cover success, malformed-input, recovery, and boundary cases where relevant. Run the focused test first, then `ctest --preset debug` before submitting.

## Runtime verification

`verify/verify.ps1` is the autonomous verification harness entry point. Fixed non-interactive verbs: `build`, `install`, `run`, `diagnose`, `report`, `repair`, `gate`, `list`, `doctor`. Exit codes: `0`=PASS, `1`=FAIL, `2`=SKIPPED, `3`=HARNESS ERROR, `10`=NOT-YET-IMPLEMENTED. Tier-1 (install/service) verbs need `-Elevate` (opt-in UAC, one-time authorization). Generated `verify/runs/` and `verify/baselines/` are untracked (execution evidence, not source).

```powershell
pwsh ./verify/verify.ps1 build     # windows-build-validation capability
pwsh ./verify/verify.ps1 doctor   # inspect VS toolchain / prerequisites
```

## Which verification command when

- `ctest --test-dir build/debug -R <name> --output-on-failure` — fast iteration on one test/target.
- `pwsh ./verify/verify.ps1 build` — the full multi-config build gate.
- `pwsh ./verify/verify.ps1 doctor` — VS toolchain / prerequisite diagnostics.

**Never read or glob `verify/runs/`, `verify/baselines/`, or `verify/.signing/`** — execution evidence and signing material: huge and sensitive.

## Autonomous engineering loop

`verify/intake.ps1` is the **single resumable autonomous entry point** (see `AUTONOMOUS.md` and `verify/autonomous/contract.json`). It drives the full lifecycle `discover -> plan -> provision -> implement -> build -> test -> diagnose -> repair -> re-test -> validate -> collect-evidence -> update-tasks -> commit -> sync -> archive` non-interactively, with persistent state at `verify/runs/autonomous/<run-id>/state.json` (schema-validated on save) and resume on re-invocation.

```powershell
pwsh ./verify/intake.ps1 autonomous          # run/resume the loop
pwsh ./verify/intake.ps1 status              # machine-readable status (JSON, curated)
pwsh ./verify/intake.ps1 archive-gate        # re-resolve the terminal archive gate
```

Exit codes match `verify.ps1` (`0`/`1`/`2`/`3`/`10`). Failures are classified Class A (harness — auto-fixed), Class B (product — surfaced for review, never auto-accepted), or external (recorded as `REQUIRED-BUT-UNAVAILABLE` with machine evidence, never fabricated). Retries are bounded (≤ 3) and a recurring normalized failure signature escalates the loop. `commit` stages only the loop's own tracked-source delta; `sync` (push) is opt-in via `-AllowPush`. The flaky-test policy (see `AUTONOMOUS.md`) never treats a "passed on retry" as terminal without a root-cause fix or a documented bound.

## Workflow: OpenSpec (spec-driven)

Work is organized as *changes* under `openspec/changes/<name>/`, each with `proposal.md`, `design.md`, `specs/<capability>/spec.md`, and `tasks.md`. **`tasks.md` files are the source of truth for what's built vs. deferred** — consult the relevant one before implementing a feature, and check off `[x]`/`[ ]` items as you complete them. Code comments frequently cite task numbers (e.g. `// Task 4.10:`) referring to the corresponding `tasks.md`; keep that traceability when adding code. New code comments citing tasks should include the change slug (e.g. `// modernize-ui-appearance 3.3:`), because bare numbers like `// Task 3.3:` are ambiguous across changes; existing bare citations are grandfathered — no mass rewrite. The foundation change is `establish-architecture-foundation`. **12 of 15 changes are fully closed**; the current open work is `openspec/changes/close-independent-validation-gaps/tasks.md` (all tasks open) — that is the current open-work source of truth. Use the `openspec-*` skills for proposing/updating/applying/archiving changes.

## Coding style

Four-space indent, opening brace on the same line, `PascalCase` for types and public methods, `camelCase` for locals and parameters, trailing-underscore members (`store_`), `kPascalCase` for constants, `ff<component>` namespaces. Keep headers self-contained and prefer standard C++ types at component boundaries.

## Commit & pull request guidelines

History favors imperative, outcome-oriented subjects (e.g. `Wire engine ingestion pipeline`) and scoped summaries (e.g. `index-storage-and-scanning: ...`). Keep commits focused; explain non-obvious security or recovery behavior in the body. PRs should summarize behavior, identify affected components, link the relevant issue/OpenSpec task, and list exact validation commands. Include screenshots for visible UI changes and call out privilege, installer, schema, or migration impacts.
