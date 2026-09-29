# Existing measurement inventory — apply session 1

Task 4.1. Performed before any harness was added and before any optimization was chosen.
Every claim below was read from the checked-in sources at commit `0de46651f4536652e4e1d7fc4b636c3d9961b591`.

## 4.1.1 Informational benchmarks

| Bench | Source | Measures | Build type | Repetition model | Registered with CTest? |
| --- | --- | --- | --- | --- | --- |
| `ffengine_bench_large_index_scalability` | `tests/engine/bench_large_index_scalability.cpp` (1648 lines) | Synthetic SQLite database generation; pipeline reload/rebuild from the durable store; snapshot export, serialization, publish copy-and-commit, and UI mapping/parsing; per-query-case search latency (`selective`, `moderate`, `broad`, `no_match`, `prefix`, `extension`, `worst_substring`); forced-eviction cold-stream search; projection memory, handles, threads, and WAL bytes; sustained-write broad-search durable fallback; durable-fallback cancellation; incremental prefix search; lock-contention and read-plane probes | Any preset (numbers must be taken from a non-debug preset; Debug is `/Od` + `/RTC1`) | Explicit warm-up runs, then a measured block per query case (10 warm runs below the 500k threshold, 5 at or above it), then a median; snapshot sweeps use 100…500,000 rows plus 50/75/90/100 % of the largest probe plus one over-capacity probe | **No.** The header says "intentionally not registered with CTest: large synthetic indexes are repeatable evidence, not a wall-clock pass/fail gate." |
| `ffengine_bench_secondary_filename_index` | `tests/engine/bench_secondary_filename_index.cpp` (857 lines) | Candidate recall and system cost for a hypothetical secondary filename index: per-query-case median latency, estimated index memory, working set before/after build, index statistics, and the tokenized/trigram infix recall capability probes | Any preset | Explicit warm-up then a measured block with a median per case | **No.** |
| `ffindexstore_bench_projection_memory` | `tests/indexstore/bench_projection_memory.cpp` (110 lines) | The in-memory projection's RAM footprint at a representative entry count: entries, unique names, arena size, RSS before/after, approximate bytes/entry, and the naive full-path comparison | Any preset | Single pass over a generated population; it is a memory footprint, not a duration | **No.** The header records a historical 5M-entry measurement of ~198.6 bytes/entry. |
| `ffsearch_bench_literal_matcher` | `tests/search/bench_literal_matcher.cpp` (339 lines) | The literal matcher's per-case median, p90, and max latency, and an equivalence check against the parsed query for each case | Any preset | Per-case measured block with median/p90/max and a `PASS`/`FAIL` equivalence column | **No.** |

All four benches accept `--output` for a machine-readable record, and the two engine benches also accept `--scales`.
`ffengine_bench_large_index_scalability` defaults to `10,000 100,000 500,000 1,000,000 2,000,000`.

## 4.1.2 Advisory run-envelope capability

| Capability | Declaration | Measures | Gating |
| --- | --- | --- | --- |
| `windows-performance-baselines` | `verify/capabilities/windows-performance-baselines/capability.json`, tier 0, `timeoutSeconds` 900, `repairPosture` `repair-unsupported`, entry module `PerformanceBaselines.psm1`, entry points `Test-PerformanceBaselinesAvailability` / `Invoke-PerformanceBaselinesCapability` / `Get-PerformanceBaselinesDiagnostics` | Per-fingerprint duration baselines derived from run-tree envelopes. Its own description: "Maintains per-fingerprint duration baselines from run-tree envelopes and emits advisory regression verdicts; regressions only gate when the gate policy says so." | Advisory. It is a wall-clock store for whole verification runs, not a user-visible latency proof. Calling its envelope hosted certification or product-performance proof is out of scope for this change. |

## 4.1.3 What the inventory does **not** measure

The two engine benches cover the unprivileged engine and index-store internals, and the search bench covers
the matcher function. None of the four measures a path that the desktop shell renders or that a user waits on:

| Unmeasured user-visible path | Why it matters | Existing coverage |
| --- | --- | --- |
| Shell or engine process start to a first usable file list | The user's first wait after launching the product | None. No bench launches or binds a process, and E2E and UIA assert semantics, never latency. |
| Delivery of the first generation of a large index to the UI over the real UI protocol: `IndexPipeline` rebuild → bounded indexed-directory export → `UiServer` publication → client-side generation parse | The wait between "app opened" and "my files are on screen" for a large volume | Partial. `ffengine_bench_large_index_scalability` measures the snapshot export, serialization, publish, UI mapping, and search-response serialization/parse/materialization in-process, and `ffengine_snapshot_publisher_tests`/`ffengine_ui_server_lifecycle_tests` assert `SnapshotPublisher`/`UiServer` behaviour. No bench drives `UiServer` over its real pipe to a client, so the framing, dispatch, and client parse of the generation that carries the indexed directories are not measured. |
| Interactive keystroke-to-visible-result inside the shell, including pane update after the response arrives | The user's most frequent interactive wait | The search bench measures the matcher function and the engine bench measures `IndexPipeline::SearchIndexed` plus the response wire format. Neither includes the pane update the user actually watches. |
| Directory navigation / tree expansion in the shell | Every browsing action | `RunSnapshot` in the large-index bench measures snapshot construction for 100…500,000 rows; the UI-side per-navigation cost is not measured. |
| Storage/treemap render and drill-down | The storage analysis view | `fftreemap_layout_tests` and `ffstorage_percentage_tests` assert layout correctness, not duration. |
| Details pane and column population after navigation | Every selection | `ffdetails_layout_tests` and `ffchrome_layout_tests` assert layout, not duration. |
| UI-thread responsiveness while the index builds | Perceived product health | `ui-responsive` is a semantic UIA scenario. No bench. |
| File operations (copy/move/delete) | The core user promise | `fffileoperations_e2e_tests` assert outcomes. No bench. |

The advisory `windows-performance-baselines` envelope does not fill any of these gaps: it records whole-run
durations per fingerprint, not a per-path latency, and it is explicitly advisory.

## 4.1.4 Conclusion of the inventory

- The existing benches and the advisory envelope already cover the engine, index-store, search-internals, snapshot
  construction, and search-response wire layers.
- The first user-visible path that 4.1 shows is unmeasured, and that has a deterministic disposable fixture, is the
  **delivery of the first generation of a large index to the UI**: the bounded indexed-directory export fed to
  `UiServer`, carried over its real pipe, and parsed by the client. It is the wait between the application opening
  and the user seeing their files, and no existing harness measures it.
- No harness was added before this inventory, and no optimization was chosen before the baseline in 4.2.
