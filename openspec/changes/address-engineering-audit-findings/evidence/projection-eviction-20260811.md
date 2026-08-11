# Projection eviction evidence — 2026-08-11

## Scope

Completed OpenSpec tasks 6.1–6.5 in the `projection-eviction` capability.
The implementation keeps the durable SQLite store as the source of truth and
does not alter IPC, authentication, signing, privilege policy, or trust
stores.

## Implementation

- `Projection` exposes a configurable maximum owned-memory limit with a
  500 MiB default, plus deterministic `EstimatedMemoryBytes` and
  `IsMemoryPressureHigh` signals. Accounting includes projection entries,
  dense/hash/children indexes, interned names, filename postings, and LRU
  metadata capacity.
- New entries receive monotonic access timestamps; lookups, child-directory
  access, path reconstruction, and updates refresh recency. When the limit is
  exceeded, the least-recently-used non-root entry is selected and an entire
  directory subtree is removed so parent/child/path invariants remain
  consistent. Dense storage, name interning, and filename postings are
  compacted after eviction so the limit measures reclaimed capacity rather
  than only logical row count.
- Evicted volumes are marked as durable-backed and are replayed from SQLite
  through `ReloadEvictedVolume`/`ReloadAllEvictedVolumes`. The engine invokes
  reload before volume snapshot export, folder aggregates, and indexed search.
- `IndexPipeline` emits one structured `FFDIAG_PROJECTION_MEMORY_PRESSURE`
  warning while usage is near the configured limit and re-arms after pressure
  clears.

## Focused validation

- `cmake --build --preset debug --target ffindexstore_projection_tests
  ffengine_index_pipeline_tests` — PASS.
- `ctest --test-dir build/debug -R
  'ffindexstore_projection_tests|ffengine_index_pipeline_tests'
  --output-on-failure` — PASS (2/2).
- The projection test verifies memory accounting, LRU recency ordering,
  eviction markers, SQLite-backed reload restoration, and the pressure signal.

## Full validation

- x64 Debug build — PASS (151 targets).
- x64 Debug CTest — PASS (45/45).
- x64 Analyze build — PASS.
- x64 Release build — PASS (162 targets).
- x64 Release CTest — PASS (45/45).
- `pwsh ./verify/verify.ps1 build` — PASS (`windows-build-validation`).
- `npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate --all --strict`
  — PASS (45/45).

The changed source and tracked evidence files were normalized to the repository's
CRLF convention before the final diff check. No signing material, trust store,
production pin, authentication policy, or IPC security control changed.
