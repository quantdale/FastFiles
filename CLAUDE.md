# CLAUDE.md

This file is a short stub. The authoritative repository guide is **AGENTS.md** — read it before doing any work here. It covers architecture, security invariants, the OpenSpec workflow, and testing conventions.

## Critical quick facts

- **Build/test** (run from a Visual Studio developer PowerShell; `FASTFILES_NINJA_EXE` must be set **before** `cmake --preset debug`):

  ```powershell
  $env:FASTFILES_NINJA_EXE = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
  cmake --preset debug
  cmake --build --preset debug
  ctest --preset debug
  ctest --test-dir build/debug -R <test-name> --output-on-failure   # single test
  ```

- **Warnings are errors** (`/W4 /WX` and more) — new code must be warning-clean.

- **Windows-only**, by design — CMake `FATAL_ERROR`s on non-Windows (Win32/COM/Direct2D/NTFS only).

- **Service model:** `FastFilesIndexSvc` runs under **LocalSystem** as a deliberately *constrained privileged broker* (evidence: `openspec/changes/resolve-raw-volume-privilege-insufficiency/evidence/matrix-execution-and-selection.md`).

- **Current state:** the privileged MFT/USN scan is *implemented* but inert in practice — the Authenticode pins in `src/setup/include/ffsetup/PinnedSignatures.h` are all-zero placeholders, so mutual auth fails closed on every peer. Degraded mode is the active path.

- **Workflow:** OpenSpec — work lives in `openspec/changes/<name>/`; `tasks.md` is the source of truth. Current open work: `openspec/changes/close-independent-validation-gaps/tasks.md`. Read `openspec/changes/establish-architecture-foundation/design.md` before touching process boundaries, IPC, or the security model.
