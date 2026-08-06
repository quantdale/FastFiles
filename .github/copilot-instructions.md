# Copilot instructions for FastFiles

- `AGENTS.md` at the repository root is the authoritative repo guide — read it before proposing changes; it documents the architecture, security invariants, build/test commands, and coding style.
- FastFiles is Windows-only C++20 (Win32/COM/Direct2D/NTFS, no cross-platform layer). Build via CMake presets (`debug`/`release`/`analyze`/`asan`); `FASTFILES_NINJA_EXE` must point at the VS-bundled `ninja.exe` before any `cmake --preset` configure.
- Compiler flags are strict: `/W4 /WX` (warnings are errors), `/permissive-`, `/sdl`, `/guard:cf` — new code must be warning-clean.
- Tests are plain C++ executables registered with CTest where test name == target name, so `ctest --test-dir build/<preset> -R <name>` runs a single test; there is no `analyze` test preset.
- Three-process privileged/unprivileged architecture (service / engine / UI): read `openspec/changes/establish-architecture-foundation/design.md` before touching IPC, process boundaries, or the security model.
