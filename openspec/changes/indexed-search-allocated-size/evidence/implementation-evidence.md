# Implementation evidence — indexed-search-allocated-size

Observed results recorded by the executing session. Host: Windows 10.0.26200,
MSVC 19.x (VS 2022 BuildTools), CMake/Ninja, 12 logical cores. All tests built
and run from `build/debug`, `build/release-test`, `build/analyze`.

## 1. Pre-change gap (authorized baseline)

- `ProjectionEntry` already carries `sizeBytes`, `allocatedSizeBytes`, `allocatedSizeKnown` (established by `allocated-size-storage-accuracy`).
- Indexed Search (`IndexPipeline::SearchIndexed`) materialized `ffsearch::CandidateView/Candidate` with logical `sizeBytes` only — `allocatedSizeBytes`/`allocatedSizeKnown` were dropped.
- The legacy `IndexedSearchResult` (message 18) candidate header carried logical size only; no allocated metadata.
- `SearchResultRow` and `UiaSearchResultInfo` showed logical size only.
- `SortField::Size` and the `size:` filter mean **logical size**; no allocated dimension existed.

## 2. End-to-end metadata path (changed symbols)

Allocated metadata now flows through the existing chain with no new source of truth:

`ProjectionEntry.allocatedSizeBytes/Known`
 → `ffsearch::Candidate{allocatedSizeBytes, allocatedSizeKnown}` and `ffsearch::CandidateView{...}` (`src/search/include/ffsearch/Query.h:38-39`, `:62-63`)
 → `ffsearch::SearchResult.candidate` (unchanged transit)
 → `ffprotocol::IndexedSearchCandidate.allocatedSizeBytes/Known` in the additive V2 response
 → `ffprotocol::SerializeIndexedSearchResponseV2` → `UiMessageType::IndexedSearchResultV2` (`src/engine/src/UiServer.cpp:203-205`)
 → `ffprotocol::ParseIndexedSearchResponseV2` (`src/ui/src/EngineClient.cpp:562-563`)
 → `SearchResultRow` allocated fields (`src/ui/src/SearchPanel.cpp`)
 → `ffui::UiaSearchResultInfo.allocatedSizeBytes/Known` (`src/ui/include/ffui/UiaSurface.h`)
 → `SearchUiaProvider` description via `ffui::SearchAllocatedSizeText` (`src/ui/src/SearchUiaProvider.cpp`, `src/ui/include/ffui/SearchResultMetadata.h`).

Engine propagation: `IndexPipeline::SearchIndexed` copies both fields straight from each
index entry while the existing projection traversal already holds the lock — no SQLite query,
filesystem probe, service call, or extra IPC per candidate (`src/engine/src/IndexPipeline.cpp`).
Degraded/snapshot fallback candidates are set `allocatedSizeKnown = false`
(`src/engine/src/UiServer.cpp:51-52`); logical size is never copied into allocated size.

## 3. Protocol design (additive V2)

- Legacy `IndexedSearchResult` (message 18) and its fixed candidate header are byte-for-byte unchanged and remain covered by regression tests.
- New additive message: `IndexedSearchResultV2 = 21` (`src/protocol/include/ffprotocol/UiProtocol.h:46`), following the existing `FolderAggregateResultV2` (message 20) additive convention.
- Legacy candidate header `IndexedSearchCandidateHeader` = **56 bytes** (unchanged).
- New `IndexedSearchCandidateHeaderV2` = **64 bytes**: identical to the legacy header, plus a trailing `uint64_t allocatedSizeBytes`; knownness is carried in flags bit 2 (`0x04`), not a new reserved field (`UiProtocol.h:248-278`).
- Serializer collapses any stray allocated bytes to zero when unknown (`UiProtocol.cpp:451-453`); the parser rejects an unknown candidate carrying a non-zero `allocatedSizeBytes` as a contradiction (`UiProtocol.cpp:378-383`).
- Parse validates header/status/count/reserved bits, length/count/frame arithmetic overflow, and rejects trailing data (`UiProtocol.cpp:343-416`).

### Protocol evidence

1. Legacy candidate header = 56 bytes; new V2 header = 64 bytes (+8 bytes/candidate).
2. Engine now emits V2 (`IndexedSearchResultV2`); UI parses V2 and still parses legacy 18.
3. Legacy decode → `allocatedSizeKnown = false` (never inferred from logical size).
4. V2 known nonzero round trips exactly (e.g. 8 MiB preserved; `tests/protocol/test_protocol.cpp::TestIndexedSearchV2RoundTrips`).
5. V2 known zero → `allocatedSizeKnown = true`, `allocatedSizeBytes = 0`.
6. V2 unknown → `allocatedSizeKnown = false`, zero bytes, no fabrication.
7. Malformed flags/reserved bits, invalid count/length, overflow, and trailing data are rejected fail-closed (`TestIndexedSearchV2Malformed`).
8. Frame-limit truncation remains deterministic; `truncated` is reconstructed truthfully from the header.
9. Old sort values/bytes unchanged; `SortField::AllocatedSize` (request sort value 6) is validated explicitly (`TestIndexedSearchLegacyRequestSortBytes`).

## 4. Search semantics evidence

### Filters (D2)

`size:` continues to evaluate logical `sizeBytes` (`ffsearch::PredicateKind::Size`); no `allocated:`/`ondisk:` token registered. `tests/search/test_search.cpp` re-pins existing scope/filter membership and confirms unchanged results after the model grew.

### Sorting (D6/D7)

Deterministic fixture in `tests/search/test_search.cpp` (AllocatedSize sort matrix):

| Row | Logical | Allocated |
|---|---:|---|
| a.txt | 100 | known 80 |
| b.txt | 50 | known 0 |
| c.txt | 75 | known 40 |
| d.txt | 25 | unknown |
| e.txt | 90 | known 80 |

Observed orderings:

- `Size on Disk` ascending: `b(0)`, `c(40)`, `a(80)`, `e(80)`, `d(unknown)` — known zero first, known-80 tie breaks alphabetically (a before e), unknown last.
- `Size on Disk` descending: `a/e(80)`, `c(40)`, `b(0)`, `d(unknown)` — unknown still last.
- `Size` (logical) ascending/descending: driven by `sizeBytes` only, confirmed unchanged.
- Unknown sorts after all known rows in **both** directions; known zero participates numerically; ties use deterministic alphabetical secondary ordering; logical and allocated orderings are independent.

## 5. UI evidence (`src/ui/src/SearchPanel.cpp`, `ffui::SearchAllocatedSizeText`)

- `Size on Disk` added to the sort selector (value 6) and routes to the real indexed Search sort; existing `Size` remains logical. Default sort stays `Size` (Task 8.3 / design).
- Visible row shows logical size and Size on Disk as distinct metadata via the existing virtualized row model.
- Known: e.g. `12 MB • 8 MB on disk • <date>`.
- Known zero: `0 B • 0 B on disk` (never "Unknown").
- Unknown: `Size on disk unknown` (never zero, never logical bytes repeated).
- `ffui::SearchAllocatedSizeText` is a pure helper (`src/ui/include/ffui/SearchResultMetadata.h`) — no `windows.h`/COM — used by both the row renderer and the UIA provider, so visible and accessible metadata share one source.

## 6. UIA evidence (headless, `tests/ui/test_search_uia.cpp`, `tests/ui/test_search_metadata.cpp`)

- `UiaSearchResultInfo` extended with `allocatedSizeBytes`/`allocatedSizeKnown`, populated from the exact `SearchResultRow` state.
- Provider description distinguishes logical size from Size on Disk and preserves known-zero vs unknown.
- No duplicate semantic child for the second metric; `Search.Results -> ListItem` hierarchy, runtime IDs, selection/invoke/scroll, and bounded cache unchanged.
- `TestSearchAllocatedSizeMetadata`: known/zero/unknown descriptions correct.
- `TestSearchAllocatedStaleAndReplacement`: a provider held across query replacement fails cleanly (stale contract) and never reveals the superseded query's allocated metadata.
- Property reads are in-memory only; no engine IPC or filesystem I/O from UIA callbacks.

### Live UIA note

This session ran in a **headless** build sandbox (no display, no running `FastFiles` GUI), so the interactive Search UI and the `windows-ui-automation-validation` capability were not launched here. Required known/zero/unknown/stale semantics are proven deterministically by the headless provider tests above, and degraded unknown semantics are proven by the degraded path plus those tests. Per design D11 and this change's own success criteria, a host without privileged indexed allocated values is **not a blocker**, and live known-value evidence is explicitly not required. When a live GUI host is available, re-run the established search scenarios and the full `windows-ui-automation-validation` capability for live confirmation (the source already supports it).

## 7. Performance evidence (`tests/engine/bench_large_index_scalability.cpp`)

Benchmark run at scale 100,000 (warm), comparing legacy wire (before) vs V2 wire (after).
Machine-readable runs: `build/bench-before.json`, `build/bench-after.json` (untracked; in `build/`).

| Query class (4096 results) | Before wire | After wire | Delta |
|---|---:|---:|---:|
| broad/common | 911,288 | 944,056 | +32,768 (= 8 × 4096) |
| moderate/report | 839,468 | 872,236 | +32,768 |
| extension/ext:txt | 895,208 | 927,976 | +32,768 |
| prefix/photo | 833,350 | 866,118 | +32,768 |
| worst_substring/component | 923,016 | 955,784 | +32,768 |
| selective (1 result) | 248 | 256 | +8 |

- Wire increase is a **fixed +8 bytes/candidate** (the trailing `uint64_t` in `IndexedSearchCandidateHeaderV2`), exactly as designed.
- `candidate_count`, `result_count`, and `lazy_path_materialization_count` are **identical** before/after (4096 for full pages).
- `total_ms`, `response_serialization_ms` (`~6 ms → ~5.5 ms`), `response_parse_ms` (`~6.5 ms → ~6.2 ms`), and `ui_result_materialization_ms` are within run-to-run noise (after slightly lower).
- Maximum observed response (944,056 bytes) remains well under `kMaxFrameSize` (1 MiB) — bounded page/frame behavior preserved.
- **No** database, filesystem, service, or extra IPC query was added per result; the two scalars travel with the existing lightweight candidate record.

## 8. Validation matrix

| Gate | Result |
|---|---|
| Focused protocol/search/engine/UI/UIA tests | PASS (see CTest below) |
| Debug configure/build/CTest | PASS (65/65) |
| release-test configure/build/CTest | PASS (65/65, 33 s, no load) |
| Analyze build (`/W4 /WX`) | PASS (warning-clean) |
| `pwsh ./verify/verify.ps1 build` | PASS (status=PASS, exit 0) |
| `pwsh ./verify/verify.ps1 doctor` | PASS (all tools/toolchains present; SKIPs are uninstalled diagnostics tools only) |
| source-marker check | PASS (no TEMP-DEBUG/DEBUG_STALL markers) |
| diagnostic-policy check | PASS (0 failures) |
| doc-drift check (`ffdoc_drift_tests`) | PASS (in CTest) |
| strict OpenSpec validation (`openspec validate --all --strict --no-interactive`) | PASS (55/55 items) |
| `git diff --check` | clean (only project CRLF-normalization warnings) |
| Hosted GitHub Actions (final SHA) | queried post-push (see handoff report) |

### Note on an earlier verify.ps1 timeout

An earlier `verify.ps1 build` run reported a single sub-result timeout:
`ffengine_volume_session_manager_tests` (`ctest-release-test`, exitCode 8 = timeout at 120 s).
That test was **not** modified by this change (Task 4.5 "no breakage"). Re-run in isolation
under no load it passes in **0.09 s**; a clean full `ctest --test-dir build/release-test` run
completes **65/65 in 33 s**. The timeout occurred during the dual-build + dual-ctest load of
the previous `verify.ps1` run on the same machine and is environmental, not a regression
introduced by this change. A subsequent `verify.ps1 build` run completed PASS.

## 9. Security/privacy

No file contents, credentials, tokens, private signing material, raw MFT/USN bytes, or
identifying user paths are included. All fixtures are synthetic/deterministic. No privileged
boundary was expanded (D13): allocated metadata comes only from the unprivileged engine
projection; the parked production-validation/signing campaign remains out of scope.
