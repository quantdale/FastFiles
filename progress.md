# FastFiles — Project Status

Windows-only C++20 native file manager built around a persistent NTFS index.

## Architecture

Three-process privileged/unprivileged split (rationale and decisions in
`openspec/changes/establish-architecture-foundation/design.md`):

- **FastFilesIndexSvc** — privileged Windows service (LocalSystem, constrained broker):
  stateless relay for raw MFT/USN bytes; no index, no query parsing.
- **FastFilesEngine** — unprivileged per-logon process that owns the index and the
  privileged-connection lifecycle.
- **FastFiles** — Direct2D/DirectComposition desktop shell (Column View).

## Current State

- The privileged MFT/USN scan is **implemented but inert**: mutual-auth signature pins
  in `src/setup/include/ffsetup/PinnedSignatures.h` are all-zero placeholders (fail-closed),
  so no peer can authenticate and the privileged path cannot activate.
- **Degraded mode is the active path**: unprivileged tree walks + directory watches.
  It is a first-class permanent state, not an error path — UI/engine work must keep
  working without the service.

## Open Work

- 12 of 15 OpenSpec changes closed. Current open change:
  `openspec/changes/close-independent-validation-gaps/` (storage "% of parent" correctness,
  responsive details-pane layout, service-account doc consistency, pinned-authentication
  hardening). Its `tasks.md` is the source of truth.

## Tests

- 27 CTest tests (plain C++ executables with the `Check()` helper).
  Run `cmake --preset debug && cmake --build --preset debug`, then `ctest --preset debug`.

## References

- `AGENTS.md` — repo guidelines: build/test commands, security invariants, workflows.
- `AUTONOMOUS.md` — autonomous engineering loop (`verify/intake.ps1`).
