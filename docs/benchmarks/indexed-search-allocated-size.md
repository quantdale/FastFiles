# indexed-search-allocated-size — wire/result width impact

Companion to `openspec/changes/indexed-search-allocated-size`. Measures the fixed protocol
and result-width increase introduced by carrying on-disk allocated size through indexed Search.

## Method

Reuse `tests/engine/bench_large_index_scalability.cpp` (the repository's large-index
scalability benchmark, **not** registered with CTest). It builds a real temporary SQLite index,
rebuilds the real projection, runs the real indexed-search path, and serializes/parses the
engine→UI response. The benchmark's wire measurement was switched to
`SerializeIndexedSearchResponseV2` / `ParseIndexedSearchResponseV2` for this change.

```powershell
cmake --preset release-test
cmake --build --preset release-test --target ffengine_bench_large_index_scalability
build\release-test\tests\engine\ffengine_bench_large_index_scalability.exe --output build\bench-after.json
```

Capture the before-change baseline from `build/bench-before.json` (legacy wire), committed as
untracked execution evidence under `build/`.

## Result

At scale 100,000 (warm), every 4096-result query page grew by exactly **8 bytes/candidate**
(32,768 bytes/page), matching the trailing `uint64_t allocatedSizeBytes` appended to
`IndexedSearchCandidateHeaderV2` (legacy header 56 B → V2 header 64 B). A single-result query
grew 248 → 256 bytes (+8).

`candidate_count`, `result_count`, and `lazy_path_materialization_count` were identical
before/after; total/serialize/parse/materialize timings stayed within run-to-run noise, and the
largest response (944,056 B) remained well under the 1 MiB frame cap. No per-result
DB/filesystem/service/IPC work was added.

Full numbers: `openspec/changes/indexed-search-allocated-size/evidence/implementation-evidence.md` §7.
