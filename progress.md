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
- **Memory-bounded indexed scale (Phase 8, `memory-bounded-indexed-scale`)**: indexed
  search is complete under forced projection eviction — cold volumes stream durably
  from SQLite instead of being reloaded into RAM; folder aggregates answer evicted
  volumes from the durable recursive CTE; snapshot publication is bounded by a
  recency-ordered indexed-directory working set (bulk exports are top-of-tree
  bounded); the deterministic seeded acceptance path lives in
  `ffengine_seeded_indexed_mode_tests`; scale evidence in `docs/benchmarks/`.

## Open Work

- Open OpenSpec changes (external/provider-gated): `close-independent-validation-gaps`,
  `autonomous-runtime-verification`, `zero-touch-autonomous-engineering`,
  `real-user-dogfooding-release-engineering` (final push/CI evidence), and the active
  `memory-bounded-indexed-scale`. Each `tasks.md` is the source of truth.

## Tests

- 74 CTest tests (plain C++ executables with the `Check()` helper), including the
  seeded indexed-mode acceptance suite and forced-eviction search-equivalence
  regressions. Run `cmake --preset debug && cmake --build --preset debug`, then
  `ctest --preset debug`.

## References

- `AGENTS.md` — repo guidelines: build/test commands, security invariants, workflows.
- `AUTONOMOUS.md` — autonomous engineering loop (`verify/intake.ps1`).
