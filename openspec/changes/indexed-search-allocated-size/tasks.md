# Tasks — indexed-search-allocated-size

Baseline when authored: `main` at `abe3a67204b2a804d80c8f9f65eaab0d297a0287`, with `deterministic-missing-path-state` complete. Live Git state always wins on resume.

Do not mark a task complete without executable evidence. Preserve historical evidence and legacy wire contracts; do not rewrite prior PASS/SKIP history to make this change appear older than it is.

## 1. Reconcile and baseline

- [x] 1.1 Fetch/prune `origin`, reconcile `main`, verify `HEAD == origin/main`, start clean, and read `AGENTS.md`, `docs/agent/WORKSTATE.md`, this change, `allocated-size-storage-accuracy`, current canonical `search-query-engine`, `search-ui`, `index-engine`, and `ui-automation` specs.
- [x] 1.2 Trace the current end-to-end Search data path from `ProjectionEntry` through `IndexPipeline::SearchIndexed`, `ffsearch::CandidateView/Candidate`, `IndexedSearchCandidate`, wire serialization/parser, `EngineClient`, `SearchResultRow`, renderer/sort controls, `UiaSearchResultInfo`, and `SearchUiaProvider`.
- [x] 1.3 Record the pre-change truth: projection has allocated bytes + knownness, indexed Search drops them, degraded Search has no allocated source, and `size:` / `SortField::Size` mean logical size.
- [x] 1.4 Run focused existing Search/protocol/UIA tests before modification and record the baseline; do not treat a stale build output as source truth.

## 2. Search candidate model

- [x] 2.1 Extend `ffsearch::CandidateView` and `ffsearch::Candidate` with `allocatedSizeBytes` and explicit `allocatedSizeKnown`; default construction must remain safe and must not turn unknown into known zero.
- [x] 2.2 Populate both fields directly from `ProjectionEntry` in the projection-backed indexed Search path with no new database query, filesystem probe, service request, or extra IPC per candidate.
- [x] 2.3 Ensure lazy materialization/copying preserves the fields through matched `SearchResult` objects.
- [x] 2.4 Mark degraded/snapshot fallback Search candidates allocated-unknown explicitly; never copy `sizeBytes` into `allocatedSizeBytes` as a fallback.
- [x] 2.5 Prove existing query membership is unchanged: `size:` continues to use logical `sizeBytes` and no new allocated-size query token is registered.

## 3. Size on Disk sorting

- [x] 3.1 Add a distinct `SortField::AllocatedSize` through the search library and engine request mapping while preserving the numeric meaning of all existing sort values.
- [x] 3.2 Implement known-value ordering: ascending small→large, descending large→small.
- [x] 3.3 In both directions, place UNKNOWN allocated-size rows after all known rows; do not implement unknown as zero or `UINT64_MAX` and then reverse it.
- [x] 3.4 Use deterministic alphabetical secondary ordering for equal known values and among unknown rows.
- [x] 3.5 Add focused sort tests covering known nonzero, known zero, unknown, equal allocated sizes, ascending, descending, and a case where logical and allocated order differ.
- [x] 3.6 Prove `SortField::Size` still sorts logical bytes exactly as before.

## 4. Additive indexed-search wire protocol

- [x] 4.1 Audit current `UiMessageType` numbering, `kCurrentStructVersion`, existing `FolderAggregateResultV2` precedent, and protocol byte-pinning tests before choosing the extended Search message/layout.
- [x] 4.2 Preserve the exact legacy `IndexedSearchResult` candidate header and serialization bytes. Do not enlarge/reinterpret its fixed structure in place.
- [x] 4.3 Add an extended/versioned indexed-search response using the next valid additive protocol representation. It must carry allocated bytes plus explicit knownness and retain all existing candidate fields.
- [x] 4.4 Extend parser/serializer validation: reserved fields/unknown flags rejected; lengths/counts/frame arithmetic overflow checked; malformed payloads fail closed.
- [x] 4.5 Legacy response decode must produce `allocatedSizeKnown=false`, never infer allocated size from logical size.
- [x] 4.6 Extended round trips must distinguish: known nonzero, known zero, unknown.
- [x] 4.7 If the request sort enum is extended for `AllocatedSize`, preserve old request bytes for old sort modes and validate the new value explicitly.
- [x] 4.8 Preserve cancellation, response truncation, max-result bounds, and malformed-frame behavior.

## 5. Engine propagation

- [x] 5.1 Extend `IndexPipeline::SearchIndexed` candidate creation/materialization so allocated metadata survives both accelerated and canonical projection traversal paths.
- [x] 5.2 Extend engine→protocol response materialization with the new fields; no per-result Store/SQLite lookup.
- [x] 5.3 Ensure directory candidates report the stored entry allocation semantics only; do not silently substitute recursive folder aggregate allocation.
- [x] 5.4 Verify projection reload/eviction paths preserve allocated metadata before Search consumes it.
- [x] 5.5 Keep cancellation checks and lazy path reconstruction placement unchanged or better; do not add an O(N) second pass merely for allocated size.

## 6. Search UI model and visible presentation

- [x] 6.1 Extend the UI-side indexed response handling and `SearchResultRow` with allocated bytes + knownness.
- [x] 6.2 Show logical size and Size on Disk as visibly distinct metadata in each virtualized result row using the existing row layout conventions.
- [x] 6.3 Present known zero as `0 B` on disk and unknown as an explicit `Size on disk unknown`/equivalent wording; never show unknown as zero.
- [x] 6.4 Add `Size on Disk` to the existing sort selector and route it to the real indexed Search sort, preserving existing `Size` as logical size.
- [x] 6.5 Ensure sort-direction UI state, search-as-you-type, cancellation, query supersession, virtualization, result activation/navigation, and no-results state remain correct.
- [x] 6.6 Ensure degraded Search rows remain useful and visibly report allocated unknown without triggering filesystem probes.

## 7. Semantic UI Automation

- [x] 7.1 Extend `UiaSearchResultInfo` with allocated bytes + knownness, populated from the exact `SearchResultRow` state used by rendering.
- [x] 7.2 Expose a concise accessible description/property that distinguishes logical size from Size on Disk and preserves known-zero vs unknown.
- [x] 7.3 Do not add duplicate semantic children solely for the second metric; keep the existing `Search.Results -> ListItem` hierarchy and patterns.
- [x] 7.4 Preserve stable runtime identity, bounded provider cache, stale-provider behavior after query replacement, selection, invoke, and scroll semantics.
- [x] 7.5 Provider property reads must remain side-effect free and must not perform engine IPC or filesystem I/O.
- [x] 7.6 Add headless provider tests for known, zero, unknown, stale result, and query replacement.

## 8. Protocol and search regression matrix

- [x] 8.1 Pin existing legacy indexed-search request bytes for every pre-existing sort value.
- [x] 8.2 Pin existing legacy indexed-search response bytes/header layout.
- [x] 8.3 Legacy response -> allocated unknown.
- [x] 8.4 Extended response known nonzero -> exact bytes/value.
- [x] 8.5 Extended response known zero -> known true + zero.
- [x] 8.6 Extended response unknown -> known false without fabricated bytes.
- [x] 8.7 Reject malformed knownness/flags/reserved bits, invalid count/length, overflow, and trailing data according to current protocol policy.
- [x] 8.8 Verify frame-size truncation remains deterministic with the larger candidate record and `truncated` is truthful.
- [x] 8.9 Verify existing `size:` filters and logical-size sorting produce unchanged results.
- [x] 8.10 Verify cancellation/superseding search behavior still prevents stale results from appearing.

## 9. Performance and memory

- [x] 9.1 Run the existing indexed-search scalability benchmark/diagnostic path (or the repository's current equivalent) before/after the implementation.
- [x] 9.2 Record candidate count, total search time, candidate materialization, lazy path materialization count, and result materialization for a representative large dataset.
- [x] 9.3 Demonstrate no new SQL/filesystem/service work per result and no algorithmic regression from the added scalar fields.
- [x] 9.4 Record the fixed protocol/result-width increase and verify bounded page/frame behavior remains within existing caps.
- [x] 9.5 Do not weaken current performance thresholds solely to make the change pass; if a threshold moves, justify it with evidence.

## 10. Live Search/UIA validation

- [x] 10.1 Rebuild the actual executable used by `windows-ui-automation-validation` before running live evidence. Do not reuse a stale `build/debug-diagnostic` binary; the previous change proved this can invalidate otherwise-correct source results. Evidence: `build/debug-diagnostic` reconfigured with `FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED=ON` and rebuilt (FastFiles + FastFilesEngine, diagnostic unsigned development config; no production signing / trust-store / service-install).
- [x] 10.2 Through normal Search UI, verify a degraded/unprivileged result displays logical size plus explicit Size-on-Disk unknown semantics and remains semantically discoverable through `Search.Results`.
- [x] 10.3 Verify UIA accessible metadata agrees with the visible result row for logical size and allocated unknown.
- [x] 10.4 Exercise the `Size on Disk` sort control in the live UI. If all live values are unknown in degraded mode, prove the control works deterministically without pretending those results have known allocation; known-value ordering remains covered by headless/integration tests.
- [x] 10.5 Re-run the established search scenarios including result discovery, structured filter, and deep-result navigation; zero unexpected FAIL.
- [x] 10.6 Run the full `windows-ui-automation-validation` capability and preserve all legitimate independent SKIPs; do not convert a new Search failure into a SKIP.

> **Live-execution note (Task 10).** The focused live scenario `search-allocated-size` (`verify/capabilities/windows-ui-automation-validation`) passed 10/10 isolated runs on this host with the freshly rebuilt `build/debug-diagnostic` binaries. The fixture contained 5 files with distinct names and deliberately different logical sizes so alphabetical `Size on Disk` (unknown) diverges from logical `Size`; in degraded mode `Size on disk unknown` for every result is the correct `Size on Disk` state — never fabricated as `0 B` or a logical echo. Through the normal Search UI `Search.Results` was discoverable, each result exposed logical `bytes`/`KB`/`MB`/`GB` + explicit `Size on disk unknown` via the raw `UIA_FullDescriptionPropertyId`, query-replacement was sound, UIA `FullDescription` agreed with the visible row, and the `Size on Disk` control (owner-drawn combo 7103 via `SelectionPattern`/`ExpandCollapse` + `BoundingRectangle` click) routed to `SortField::AllocatedSize` live: `Size on Disk` → alphabetical when all unknown, `Size` → logical ascending, and back, with displayed `SelectionPattern` selection in agreement. Known/zero/stale/replacement is additionally proved headless by `tests/ui/test_search_uia.cpp` and `tests/ui/test_search_metadata.cpp`; D11 does not replace the required live `Search`/`UIA` validation and was corrected in WORKSTATE/evidence accordingly. The established scenario group and full capability are run as the acceptance confirmation step; legitimate independent SKIPs are preserved and no new regression is converted to SKIP.

## 11. Evidence and documentation

- [x] 11.1 Add `evidence/implementation-evidence.md` with the pre-change gap, protocol design, known/zero/unknown matrix, sort matrix, performance measurements, live evidence, and full validation results.
- [x] 11.2 Preserve historical evidence from `allocated-size-storage-accuracy`; add a coordination note if useful instead of rewriting the old Search non-goal.
- [x] 11.3 Update canonical specs only as required by normal OpenSpec sync/archive workflow; do not weaken logical `size:` semantics.
- [x] 11.4 Update `docs/agent/WORKSTATE.md` with a concise session record and next action based on actual evidence.

## 12. Full validation and handoff

- [x] 12.1 Run focused protocol/search/engine/UI/UIA tests.
- [x] 12.2 `cmake --preset debug && cmake --build --preset debug` and `ctest --preset debug --output-on-failure` — all discovered tests PASS, none Not Run.
- [x] 12.3 `cmake --preset release-test && cmake --build --preset release-test` and release-test CTest — PASS.
- [x] 12.4 `cmake --preset analyze && cmake --build --preset analyze` — warning-clean `/W4 /WX`.
- [x] 12.5 `pwsh ./verify/verify.ps1 build` and `pwsh ./verify/verify.ps1 doctor` — PASS.
- [x] 12.6 Run source-marker, diagnostic-policy, doc-drift checks; `openspec validate --all --strict --no-interactive`; `git diff --check`.
- [x] 12.7 Review the complete diff for scope. Do not stage build outputs, Search/UIA dumps, live fixture files, local DBs, logs, or generated API JSON.
- [x] 12.8 Commit all validated task-owned changes and push to `origin/main`; verify `HEAD == origin/main` and `git status --short` empty.
- [x] 12.9 Query hosted GitHub Actions for the exact final pushed `head_sha` and report its exact status/conclusion. Do not make a recursive commit solely to store the final CI result.

## Completion gate

`COMPLETE` requires all of the following:

- indexed known/zero/unknown allocated-size semantics survive projection -> search -> protocol -> UI -> UIA;
- legacy indexed-search wire bytes remain pinned and legacy responses mean allocated unknown;
- `size:` and existing `Size` sorting retain logical-size semantics;
- `Size on Disk` sort is correct and unknown-last in both directions;
- degraded results never fabricate allocated size;
- visible and accessible Search metadata agree;
- cancellation, virtualization, navigation, stale-provider behavior, truncation, and performance remain sound;
- relevant live Search scenarios and full UIA capability have zero unexpected FAIL;
- all native/build/spec/policy gates pass;
- final validated state is committed/pushed to `main`, clean, and hosted CI is queried by final SHA.

If a live host lacks privileged indexed allocated values, that is NOT a blocker: known-value correctness is covered by deterministic projection/protocol tests and the live degraded path must prove unknown semantics. The parked production-validation campaign remains out of scope.
