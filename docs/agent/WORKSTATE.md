# Fast Files — Agent Workstate

Last updated: 2026-08-10T19:05:00+08:00
Status: COMPLETE

## Repository

Path: `C:\Users\Michael Roy\Documents\FastFiles`
Branch: `recovery/fast-files-20260809`
HEAD at slice start: `8d68ac351d64e2c17eae0082562c2f24253590e5` (`Record indexed search optimization state`)
Working tree at slice start: clean
Benchmark implementation commit: `008779e` (`Add indexed search scalability benchmarks`)
Workstate bookkeeping: `7c0cd83` (`Avoid eager candidate copies in indexed search`); bookkeeping commit follows this update

## Current Engineering Slice

Name: Optimize lightweight indexed-search candidate/name materialization

Objective: Remove the remaining all-row owning candidate/name materialization from `IndexPipeline::SearchIndexed` while preserving the canonical query semantics, ordering, and 4,096-result cap.

Scope invariant: One search-performance frontier only. No SQLite replacement, speculative FTS, query-language changes, cancellation, snapshot optimization, MFT/USN activation, security/signing changes, or unrelated UI/product work belongs in this slice.

OpenSpec context: No existing OpenSpec change owns this optimization. The completed `instant-search` spec is the source context: indexed matching scans the in-memory projection with no per-search SQL query, and the UI uses a bounded result page. The OpenSpec CLI is unavailable in this environment.

## Current Slice Baseline Reproduction

- Starting point was clean `8d68ac3`; no source/test changes were present before measurement.
- Existing Release harness reproduced the current optimized path: 100K selective `36.43 ms`, 500K selective `300.28 ms`, 1M selective `473.34 ms`, 1M no-match `475.37 ms`, and 1M broad `448.50 ms` medians.
- Warm 1M selective attribution: lightweight candidate materialization `407.09 ms`, SQLite metadata `34.27 ms`, match/filter/sort `31.08 ms`, lazy path `0.012 ms` for one path, total `472.47 ms` for the sampled run.
- Fresh 1M private memory samples: database open `5.5 MB`, selective peak `543.6 MB`, broad peak `530.7 MB`, post-query `528.3 MB`.
- Root cause inside the remaining stage is confirmed: `ProjectionEntry` exposes an interned UTF-16 `NameId`, but `SearchIndexed` calls `ToWString` for every row, allocates/copies every name into an owning `Candidate`, copies scalar metadata, and retains all candidates in a reserved vector. The projection has already paid for name storage; non-matching rows do not need another name allocation.
- Design frontier: add a non-owning UTF-16 `CandidateView`/view-aware canonical matcher and stream projection rows while the projection lock protects NamePool lifetimes; materialize an owning `Candidate` only for path evaluation survivors/results. Keep existing owning `Candidate` APIs and fallback search behavior intact.

## Current Slice Implementation Checkpoint

- `ffsearch::CandidateView` carries a non-owning UTF-16 name view plus scalar metadata; `FilterDefinition`/`Query` now retain view-aware built-in predicates alongside the existing owning predicates. Extension/kind matching uses the existing `towlower` semantics without allocating a per-row normalized string; ordinal and glob matching operate directly on the UTF-16 view.
- `ExecuteSearchViews` streams a callback-provided view source, preserves canonical scope/filter/match/sort behavior and cap-before-sort semantics, and requests ownership only after cheap view predicates pass. `SearchIndexed` holds the projection lock for the view lifetime, so NamePool references cannot dangle; custom filters without a view factory use the old owning fallback.
- Structural attribution now records projection rows examined, owning candidate objects, name copies, result objects, projection traversal, and lazy-path counts. The 1M selective run examined 1,000,000 rows and created/copied/materialized exactly 1 candidate/name/result; broad stopped at the same 4,096-result cap boundary after 13,779 eligible rows and materialized 4,096 candidates.
- First Release after measurements: 100K selective `19.09 ms`, 500K `126.67 ms`, 1M selective `292.85 ms`, 1M no-match `298.32 ms`, 1M broad `59.44 ms`; incremental 1M `p/ph/pho/phot/photo` `56.39/71.01/70.95/71.12/70.59 ms`. 1M selective attribution: projection traversal/direct matching `257.72 ms`, owning candidate/name `0.003 ms`, lazy path `0.009 ms`, SQLite `34.52 ms`; private memory samples: open `5.0 MB`, selective warm peak max about `456.9 MB`, broad peak `465.4 MB`, post-query `467.5 MB`.
- Focused `ffsearch_tests`, `ffsearch_scan_tests`, and `ffengine_index_pipeline_tests` pass, including view/owning predicate agreement and existing parent-path, extension, duplicate-name, ordering, scope, and 4,096-cap coverage.

## Current Slice Validation Checkpoint

- Final default Release benchmark is recorded in `docs/benchmarks/large-index-scalability.json`: selective 100K/500K/1M `17.88/132.88/293.18 ms`, no-match 1M `300.96 ms`, broad 1M `58.41 ms`; full classes and snapshot-capacity probes completed. Incremental `p/ph/pho/phot/photo` is `55.44/70.53/70.85/71.04/70.41 ms`.
- Final instrumented 1M selective sample: SQLite `33.2357 ms`, projection traversal/direct matching `271.7994 ms`, owning candidate/name `0.0032 ms`, lazy path `0.0084 ms` with one successful path, total `305.0476 ms`; counts are 1,000,000 rows examined, 1 candidate/name/result materialized. Final 1M private samples: database open `6.0 MB`, selective peak max `466.3 MB`, broad peak `473.9 MB`, post-query `475.3 MB`.
- Debug build PASS, Analyze build PASS, and full serial CTest PASS `42/42` with the documented user TEMP/TMP override; the first CTest attempt failed only the mixed-line-ending guard and the normalized rerun passed all tests.
- Runtime smoke PASS for rebuilt Debug FastFiles launch, automatic Engine launch, UI/Engine responsiveness, normal UI close, and cleanup. No indexed privileged fixture was available; degraded navigation remains the active runtime path and no signing/authentication bypass was used.
- Benchmark Markdown preserves the original baseline → path-materialization optimization → candidate/name-materialization optimization progression. The new primary bottleneck is raw projection traversal plus direct UTF-16 filename matching; the next slice is not implemented.

Current product commit: `7c0cd83` (`Avoid eager candidate copies in indexed search`).

## Benchmark Trace Checkpoint

Actual indexed-search path: `SearchPanel::Dispatch` -> `EngineClient::RequestIndexedSearch` -> `UiServer::HandleRequestIndexedSearch` -> `IndexPipeline::SearchIndexed` -> SQLite `GetAllVolumes` metadata lookup only -> full projection candidate/path materialization -> `ffsearch::ParseQuery` -> `ffsearch::ExecuteSearch` linear scope/filter/match scan and `SortResults` -> response candidate materialization -> `SerializeIndexedSearchResponse`/pipe -> UI indexed-completion candidate materialization.

Actual browse snapshot path: `IndexPipeline::ExportDirectorySnapshot` -> `ffprotocol::SerializeSnapshot` -> `SnapshotPublisher::Publish` inactive-slot copy and interlocked slot/generation commit -> UI mapping/header selection -> `ffprotocol::ParseSnapshot`.

Measured hypothesis confirmed: indexed search latency scales with total projection size because `SearchIndexed` reconstructs/materializes every candidate for every request, while the returned result count is capped at `kMaxIndexedSearchResultCount` (4096). SQLite entry query time is effectively absent by design; the benchmark separately times the small metadata query.

Formal performance target: none found. Interpret numbers with clearly labeled engineering bands only (`<50 ms` immediate, `50-100 ms` excellent, `100-250 ms` responsive, `250-500 ms` noticeable, `>500 ms` poor, `>1 s` not instant).

## Completed Benchmark Slice

- Added optional, null-safe timing seams to `IndexPipeline::SearchIndexed` and `SnapshotPublisher::Publish`; default product callers preserve existing behavior.
- Added non-CTest `ffengine_bench_large_index_scalability` in `tests/engine/bench_large_index_scalability.cpp`. It uses the real SQLite/index projection/search/snapshot/publication/parser APIs, deterministic realistic synthetic rows, temporary per-scale databases, warm-run distributions, memory samples, incremental typing, and supersession inspection.
- Added machine-readable output at `docs/benchmarks/large-index-scalability.json` and concise interpretation at `docs/benchmarks/large-index-scalability.md`.
- Reproduction command: `cmake --preset release; cmake --build --preset release --target ffengine_bench_large_index_scalability; build\\release\\tests\\engine\\ffengine_bench_large_index_scalability.exe --output docs/benchmarks/large-index-scalability.json`.
- Reference environment: Release/MSVC 19.44.35228.0/CMake 3.31.6, Windows 10.0.26200, AMD Ryzen 5 7535HS (12 logical cores), 23.3 GiB RAM, Kingston OM8SEP4512Q-AA NVMe SSD, NTFS C:.
- Completed scales: 10,000; 100,000; 500,000; 1,000,000. Stopped before 2M/5M because 500K/1M already established the clear latency and memory boundary; larger scales would not change the next decision before optimization.
- Search medians (selective/broad/no-match): 10K 9.93/12.49/9.91 ms; 100K 145.07/145.41/145.19 ms; 500K 923.44/895.94/932.91 ms; 1M 2,037.15/1,983.53/2,098.81 ms. The similar times for zero, one, and 4,096-result queries prove total index size dominates returned count in the current path.
- Representative 1M selective attribution: candidate/path materialization 1,970.48 ms (96.7%); SQLite metadata 33.81 ms (1.7%); match/filter/sort 32.85 ms (1.6%); all other measured stages below 0.1% each.
- Actual snapshot export succeeded through 500K entries (57.37 MB serialized) and was rejected at 1M (115.16 MB) by the unchanged 64 MiB publication slot. Isolated capacity boundary: 556,454 rows / 67,108,794 bytes succeeds; 556,455 / 67,108,915 bytes fails with generation unchanged.
- Private bytes after rebuild/broad query/post-query: 10K 8.7/8.3/8.8 MB; 100K 51.2/53.7/53.8 MB; 500K 242.2/320.0/336.0 MB; 1M 478.4/678.6/722.5 MB. The run found no proven unbounded leak, but candidate copies create substantial scale-dependent memory pressure.
- Incremental `p -> ph -> pho -> phot -> photo` at 1M returned 4,096 rows at each step and measured 1,979.76–1,996.67 ms median per request. Engine cancellation is absent; UI generation checks discard stale completions while old engine work completes. No ordering correctness defect was found.
- Formal product latency target: none found. Engineering interpretation only: <50 ms immediate, 50–100 excellent, 100–250 responsive, 250–500 noticeable, >500 poor, >1 s not instant. Current behavior is acceptable through roughly 100K only; 500K is poor and 1M is not instant.
- Primary bottleneck: full projection candidate/path materialization in `IndexPipeline::SearchIndexed`. Optimization is required, but is explicitly deferred to one separate next slice.

## Optimization Trace Checkpoint

Root cause confirmed against source: `IndexPipeline::SearchIndexed` called `store_.GetAllVolumes()`, then `Projection::ForEachEntry`, copied every row into an `ffsearch::Candidate`, reconstructed every non-root parent path with `Projection::ReconstructPath` while holding `mutex_`, and only then parsed the query and called `ffsearch::ExecuteSearch`. `ffsearch::Candidate` owns `name` and `folder` strings, so every indexed row paid for full path traversal and allocation even when filename/metadata matching returned zero or one row.

Actual query requirements: default bare terms, glob/name, `ext`, `size`, `modified`, and `kind` predicates use filename/metadata only; `folder:` uses `Candidate::folder`; `CurrentFolder`, `CurrentFolderAndSubfolders`, and `CurrentDrive` scopes use the folder; `AllIndexedLocations` does not. Result sorting by Path/Relevance needs folders only for already-matched results. The existing matcher applies the result cap before sorting, so lazy materialization must happen before the cap is incremented but does not require a top-K redesign.

Concurrency constraint: the old implementation copied a consistent candidate/path view under the projection lock, then matched outside it. The optimization keeps lightweight candidate copying under the lock and acquires the same lock only when the lazy materializer first needs a path; this preserves safe projection access and bounds the new path work to path-evaluation survivors/results.

## Optimization Implementation Checkpoint

- `ffsearch::Query` now records `requiresFolder`, retains a parallel `predicatesWithoutFolder` set, and exposes `MatchesWithoutFolder`; `folder` key detection remains case-insensitive.
- `ffsearch::ExecuteSearchLazy` retains canonical scope/filter/match/sort/cap behavior while invoking a candidate materializer after cheap predicates and before path-dependent evaluation or result insertion.
- `IndexPipeline::SearchIndexed` parses before projection traversal, copies lightweight metadata/name candidates without folders, and reconstructs folders only for `folder:`/non-all scope survivors or final matched results. Unresolvable paths are rejected before the result cap is consumed.
- Focused deterministic engine coverage now includes parent-path-only matching, case-insensitive extension/name matching, multi-term AND semantics, nested duplicate names, path ordering, and the 4,096-result cap.

Focused Debug validation: `ffsearch_tests`, `ffsearch_scan_tests`, and `ffengine_index_pipeline_tests` PASS. The final full after run covers 10K/100K/500K/1M and all seven benchmark query classes; the instrumented 1M rerun reports one lazy path at 0.014 ms median and 411.42 ms lightweight candidate materialization.

## Optimization Completion Checkpoint

Status: COMPLETE; the focused optimization commit is recorded below.

Semantic evidence: deterministic engine tests pass for selective filename matching, no-match behavior, broad truncation at exactly 4,096, `FOLDER:` parent-path-only matching, `ext:TXT`, case-insensitive filename matching, multi-term AND semantics, nested duplicate names, path ordering, current-folder scope, and unresolved/disabled-volume boundary behavior. Existing search tests continue to pass for ranking, scope, Unicode/locale-independent matching, cancellation, and path reconstruction. The lazy matcher preserves the existing cap-before-sort behavior and rejects an unresolvable path before it consumes the cap.

Final benchmark evidence, using the committed historical baseline and the final 2026-08-10 Release rerun: selective 100K `145.07→46.00 ms` (3.15x), 500K `923.44→245.44 ms` (3.76x), 1M `2037.15→470.61 ms` (4.33x); 1M no-match `2098.81→472.99 ms` (4.44x); 1M broad `1983.53→441.56 ms` (4.49x). Instrumented 1M selective attribution: old combined candidate/path `1970.48 ms`; optimized lightweight candidate `411.42 ms`, lazy path `0.014 ms` for one result, SQLite `33.10 ms`, match/filter/sort `30.87 ms`, total `475.22 ms`. Incremental `p/ph/pho/phot/photo`: `1996.67/1990.67/1986.35/1990.34/1979.76 ms` before versus `445.45/448.34/446.03/444.62/446.84 ms` after. 1M private memory: database open `6.0→5.5 MB`, selective peak `601.3→540.4 MB`, broad peak `647.1→537.0 MB`, post-query `689.1→530.5 MB`; no material regression.

Validation: Debug build PASS; Analyze build PASS; full serial CTest PASS 42/42 with the established user TEMP/TMP override; `ffdoc_drift_tests` PASS. Runtime smoke PASS for normal Debug launch, automatic Engine launch, both processes responsive, and cleanup. Indexed UI search was not claimed because the active environment is degraded/no usable privileged fixture; deterministic engine coverage and the real Release benchmark are the authoritative indexed-search evidence.

Recorded commit: `e0f046b` (`Optimize indexed search path materialization`) before this WORKSTATE hash-record amendment.

Preliminary optimized benchmark (same machine, same harness, temporary after file): 100K selective/no-match/broad `37.71/36.27/53.83 ms`; 500K `219.32/224.10/214.16 ms`; 1M `497.81/495.24/457.28 ms`. Relative to the same-session pre-run (`131.02/131.99/132.63`, `854.48/895.78/843.30`, `2012.66/2077.76/1865.99 ms`), speedups are 3.47–4.20x. 1M incremental `p/ph/pho/phot/photo` is `448.50/451.93/468.85/460.35/458.16 ms` versus `1858.31/1852.13/1836.67/1838.40/1840.56 ms`. Representative 1M selective attribution changed from materialization 1944.98 ms + match 31.84 ms to materialization 427.21 ms + match 33.74 ms; the remaining materialization is lightweight name/metadata candidate construction plus one-result path work, not full projection path traversal. Same-session 1M private memory changed: open 5.7→5.8 MB, selective peak 610.6→542.5 MB, broad peak 647.9→536.8 MB, post-query 672.8→529.1 MB.

Validation: Debug build PASS; Analyze build PASS; Release benchmark target PASS. Full serial CTest PASS, 42/42, with `TEMP`/`TMP` set to `C:\Users\Michael Roy\AppData\Local\Temp`; the focused drift guard and all 42 tests passed.

## NEXT EXACT ACTION

Do not implement the next slice automatically. The single next recommended slice is `optimize projection iteration and direct filename matching` using measured dense live-entry traversal or a narrowly scoped exact filename prefilter. SQLite schema work and cancellation remain deferred.

## Previous Slice Record

Name: Stale snapshot / publish-failure UX

Objective: Make freshness, stale last-known-good data, and unavailable requested locations explicit without rewriting the snapshot architecture or discarding useful same-context data.

Safety / correctness invariant: A failed refresh or unreadable announced generation must never be rendered as current/empty success; a different requested context must never inherit rows from the previous context.

Non-goals: Watcher shutdown architecture, selection identity, Storage visibility, shortcuts, Treemap filtering, large-index/search performance, MFT/USN activation, signing/authentication, installer redesign, broad notification/toast framework, or visual redesign.

## Current Frontier

Trace completed for the real snapshot path. Producer: `UiServer::HandleRequestDirectory` / `OnDirectoryChanged` enumerate into `directories_`; `SnapshotPublisher::Publish` serializes into the inactive shared-memory slot and commits descriptor/slot/generation; `UiServer::RepublishAndBroadcastGeneration` sends `NewGeneration` only after a nonzero publish result. Consumer: `EngineClient::ConnectAndSubscribe` maps the section, `ReaderLoop` receives generation notifications, `WindowShell` posts `WM_APP_SNAPSHOT_UPDATED`, and `ColumnView`/`SearchPanel`/`StorageAnalysis`/`TreemapView` call `ReadSnapshot()`.

Silent-staleness edges proven: `EngineClient` discarded `NewGenerationPayload::generationId`; `ReadSnapshot()` exposed no generation and returned `nullopt` without a UI result; `ColumnView::OnSnapshotUpdated` logged read failure and returned, while `RefreshColumnFromSnapshot` returned silently on path miss; all source/request errors cleared same-context rows and displayed only a generic unavailable/error column. Request-time `PublishFailed` already exists as an additive enum and response path, but watcher-time publish failure was ignored by `OnDirectoryChanged`. New navigation creates new path-owned `Column` objects, so failed new contexts do not inherit old rows; same-context refreshes had no retained stale state.

Chosen direction: add one pure, headless `SnapshotFreshness` state seam (`Loading`, `Current`, `StaleWithError`, `Unavailable`) carried by each path-owned `Column`, with accepted/requested generation ordering and a failure reason. Retain rows only for the same existing `Column`; new context columns begin empty/loading and become unavailable on failure. Thread generation through `EngineClient`/`WindowShell` and make mapping/read failure explicit. Keep publish atomicity unchanged unless tests disprove it; add watcher publish-failure notification and focused deterministic tests.

Implementation milestone: `src/ui/include/ffui/SnapshotFreshness.h` now owns the pure state transitions and generation ordering. `EngineClient::ReaderLoop` validates/deduplicates `NewGenerationPayload`, `ReadSnapshotWithGeneration` returns the committed generation, and `WindowShell` coalesces the newest generation onto the UI thread. `ColumnView` retains same-context rows for source, publish, snapshot-load, path-missing, and disconnect failures with bounded inline status text; it clears only unavailable contexts and treats a successful zero-item snapshot as `Current`. `UiServer::OnDirectoryChanged` broadcasts watcher-time publish failure to subscribed clients. `tests/ui/test_snapshot_freshness.cpp` covers the deterministic state model; `test_snapshot_publisher.cpp` proves an oversized publication returns failure without advancing shared generation. Focused tests and full Debug build pass so far.

Runtime milestone: a rebuilt Debug UI used a disposable `build\\debug\\stale-runtime-fixture` containing `nested\\beta.txt` and `alpha.txt`. The real selection probe reported the fixture path and both rows. After moving that exact directory away and triggering F5, the probe still reported the same path and rows, while a captured real window showed `Showing previous results — refresh fa…` above the retained rows. Moving it back and refreshing restored the normal view with the same rows and no stale banner. A separate relaunch to a nonexistent disposable path reported that path with `VisibleItems=` and never exposed the fixture rows. The settings file and workspace state were restored to `This PC`/previous-session `C:\`; all FastFiles processes and created runtime artifacts were cleaned up.

File-operation finding: `ActiveSelectionPaths()` resolves live full paths from current rows; command handlers check existence for open/rename/conflict decisions, and `FileOperations::Execute` rebuilds transfer plans and creates `IShellItem` objects at operation time. No stale numeric metadata is used to execute an operation. No operation redesign is planned.

Completed from clean starting HEAD `273a871`; selection identity, Storage visibility, global shortcut routing, shortcut persistence, Treemap category-filter integration, watcher/UI publication lifecycle, and stale snapshot/publish-failure UX are complete. Git/source/tests remain authoritative over prior completion notes. Current product completion commit: `21481a5` (`Surface stale snapshot and refresh failures`).

## Completed Slice Record — Stale Snapshot / Publish-Failure UX

Root cause: the UI discarded announced generation IDs, exposed snapshot reads without generation, silently returned on mapping/path misses, and cleared same-context rows on request errors. Watcher-time `SnapshotPublisher::Publish` failure was also only logged. `SnapshotPublisher` itself already committed the inactive slot, descriptor, active slot, and generation in the correct order; failed oversized publication returns zero without advancing or announcing a generation.

Final model: `SnapshotFreshness` is authoritative per path-owned `Column`: `Loading`, `Current`, `StaleWithError`, or `Unavailable`, with a failure reason, requested generation, and last successful generation. Same-column data is retained only for the same logical path; a new path owns a new empty/loading column and cannot inherit another location's rows. A valid zero-item directory reaches `Current`.

Failure/recovery: source enumeration, watcher/request publication, announced-generation mapping/read, missing-path, and disconnect failures retain same-context rows as stale with bounded inline status text; contexts with no last-known-good data become unavailable and render an explicit reason. A later accepted generation clears the failure. Out-of-order generations are rejected. Existing file operations resolve current paths and filesystem state at execution time; stale rows remain actionable without relying on stale numeric metadata.

Protocol/publication scope: no protocol version change was needed; existing `DirectoryErrorReason::PublishFailed` now also reaches all subscribed clients for watcher refresh failure. `SnapshotPublisher` atomic publication was proven rather than rewritten.

Root cause established: `DirectoryWatcher::WatchLoop` (`src/engine/src/DirectoryWatcher.cpp:133-210`) invokes its `ChangeCallback` inline on the watcher thread. `UiServer::OnDirectoryChanged` (`src/engine/src/UiServer.cpp:128-135`) re-enumerates, updates `directories_`, and synchronously calls `RepublishAndBroadcastGeneration`. That publishes through `SnapshotPublisher::Publish` (`SnapshotPublisher.cpp:77-106`) and then holds `clientsMutex_` (`UiServer.cpp:150-157`) while `WriteUiFrame` -> `ffipc::WriteFrame` -> `WriteExact` -> synchronous `WriteFile` (`src/ipc/src/PipeFraming.cpp:20-31, 66-84`) writes the `NewGeneration` notification. `UiServer::Stop` (`UiServer.cpp:110-126`) calls `listener_.Stop()` then `watcher_.UnwatchAll()`/`StopEntry` joins the watcher before it reaches the subscribed-pipe `CancelIoEx` loop. A stalled UI reader therefore leaves the watcher in `WriteFile`, holding `clientsMutex_`, while shutdown waits forever in `StopEntry::thread.join()` and cannot reach cancellation. `directoriesMutex_` and `SnapshotPublisher::publishMutex_` are released before pipe output; `clientsMutex_` is the lock held across the blocking external call.

Lifecycle ownership facts: `DirectoryWatcher::StopEntry` sets `stopRequested`, signals `stopEvent`, cancels pending `ReadDirectoryChangesW`, and joins its `std::thread`; `PipeListener` accepts each UI connection on a detached `HandleConnection` thread; `WindowShell::Run` stops only its UI-side `EngineClient`, while the engine normally parks in `Main.cpp` and `UiServer` teardown is primarily destructor/process-lifecycle driven. Existing `CancelIoEx(pipe)` in `UiServer::Stop` was too late for the watcher chain; the final implementation moves cancellation before watcher join and adds active writer-thread cancellation.

Implementation milestone: `UiServer::ClientConnection` now owns each subscribed pipe handle, serializes only that connection's complete frames, and releases `clientsMutex_` before invoking the potentially blocking frame writer. `UiServer::Stop` snapshots and cancels subscribed connections before `DirectoryWatcher::UnwatchAll` joins watcher threads. Production cancellation uses `CancelSynchronousIo` for the active writer thread plus `CancelIoEx` on the pipe handle; connection close cancels before waiting for an in-flight writer and then performs the sole `CloseHandle`. The global cross-client write mutex and raw subscribed-handle vector were removed.

Lock-order invariant: `clientsMutex_` and `ClientConnection::stateMutex_` are never held across a pipe write. `ClientConnection::writeMutex_` intentionally serializes complete synchronous frames for one byte-stream connection and may span that write, but shutdown cancellation never acquires it: `ClientConnection::Close` marks closing, cancels the active writer, then waits for `writeMutex_`; a later writer observes closing before issuing I/O. Watcher `mutex_` remains released before `StopEntry` joins. `directoriesMutex_` remains released before `SnapshotPublisher::Publish` returns to notification output.

Deterministic tests: `tests/engine/test_ui_server_lifecycle.cpp` injects a blocking frame sink into the real `UiServer`/`DirectoryWatcher` path. It verifies normal watcher publication, blocked-sink shutdown, downstream disconnect unwind, and idle shutdown. Focused result: `ffengine_ui_server_lifecycle_tests` PASS; blocked shutdown observed 10 ms against a 500 ms deadline. Existing `ffengine_directory_watcher_tests` and `ffengine_snapshot_publisher_tests` also PASS.

Runtime evidence: rebuilt Debug `FastFilesEngine.exe` served a real .NET named-pipe client. The client observed SubscribeAck/status, initial directory publication, and a new generation after an externally-created disposable file. After the client disconnected, the Engine remained alive and responsive. The actual rebuilt Debug `FastFiles.exe` launched, exited via `WM_CLOSE`, relaunched, and exited again; no FastFiles/Engine processes or disposable runtime directories remained after cleanup. The synthetic stalled-pipe evidence remains the exact bounded-shutdown proof; no fragile large-pipe-fill claim is made.

Validation: current-slice Debug rebuild PASS; Analyze build PASS. Full serial CTest PASS, 42/42, with `TEMP`/`TMP` set to `C:\Users\Michael Roy\AppData\Local\Temp`; the new `ffsnapshot_freshness_tests` is registered and passing. Focused `ffsnapshot_freshness_tests`, `ffengine_snapshot_publisher_tests`, and `ffengine_ui_server_lifecycle_tests` PASS. Runtime stale/recovery/new-context smoke PASS as above.

NEXT EXACT ACTION: Do not implement the next slice. The single next recommended slice is `large-index search/snapshot scalability benchmarking`.

Required data-flow map:
`category-filter UI/state` -> `Storage Analysis model` -> `filtered dataset/request` -> `Treemap layout input` -> `rendered Treemap`.

Root cause established: `StorageAnalysis::categoryFilter_` is the only live filter state, and `RefreshData()` applies it only to the list's `items_` through `ApplyCategoryFilter()` (`src/ui/src/StorageAnalysis.cpp:171-188, 495-497`). The category-combo `CBN_SELCHANGE` path calls `RefreshData()` but never informs the Treemap (`:690-693`). `TreemapView::BuildTree()` (`src/ui/src/TreemapView.cpp:129-265`) reads `EngineClient::ReadSnapshot()` directly, has no `CategoryEngine` or selected-category input, and builds/layouts the unfiltered hierarchy. `ApplyModeVisibility()` starts that raw build on Treemap entry, and `OnSnapshotUpdated()` rebuilds it again, so a selected list filter is dropped on entry/re-entry and after snapshot refresh; raw folder aggregate callbacks can likewise describe unfiltered data.

Category semantics established from the existing implementation: `CategoryEngine::Match()` is the canonical case-insensitive final-extension classifier. Default selectable categories are Video, Image, Document, Archive, Executable, Development, VM Image, and Game; unmatched/no-extension files are consumed as `Other` by the existing By Category/list fallback, but `PopulateCategoryFilter()` currently exposes only All plus configured definitions. `All categories` is the unfiltered state. Existing list filtering retains directories for drillability and filters files by the classifier; the Treemap fix will apply the same classifier to files, retain only directory containers with matching descendants, and recompute their totals from retained children.

Planned data-flow fix: carry a category ID (empty = All) from the existing combo state through `StorageAnalysis` into `TreemapView`; use `CategoryEngine` for both list and Treemap matching; filter the constructed hierarchy before `RebuildLayout()`; skip unfiltered aggregate responses while a category is active; rebuild on filter changes, mode entry, and snapshot updates. The renderer remains layout/paint-only.

Persisted formats:
- Canonical `shortcuts.json`: version/bindings array; each customization is keyed by stable `commandId` and stores numeric `key` + `modifiers` (`CSA`), or `unbound`; scope is restored from current registry metadata, not persisted.
- Legacy `settings.json`: nested `shortcuts.bindings` array of `command` + human-readable `binding`; it cannot represent scope/unbound state and is not suitable as the runtime model.
- Settings JSON is `%LOCALAPPDATA%\\FastFiles\\settings.json`; the dedicated file is `%LOCALAPPDATA%\\FastFiles\\shortcuts.json`; both currently use the same AppDirectory convention but different loaders/writers.

Canonical source: dedicated `%LOCALAPPDATA%\\FastFiles\\shortcuts.json`, because it is the existing runtime model and preserves stable IDs, unbound state, conflict behavior, and registry-derived Global/ActiveView scope.
Runtime in-memory source: the one WindowShell-owned `ShortcutMap`, shared with CommandPalette and SettingsDialog.
Legacy source: `settings.json`'s old `shortcuts.bindings` section, migration-only.
Migration policy: if canonical `shortcuts.json` exists, it wins (including an intentionally empty bindings array); if it does not exist, parse valid legacy entries, resolve command IDs/display names against the registry, apply valid bindings deterministically, save canonical once, and ignore/log unknown or malformed entries. Missing legacy data uses registry defaults. No later startup reads legacy shortcuts once canonical exists.
Live apply policy: SettingsDialog edits a draft of the shared ShortcutMap; accepted changes replace the WindowShell map, save canonical `shortcuts.json`, and refresh the existing CommandPalette pointer immediately. Settings/general persistence remains separate and no longer carries shortcut values.

Implementation milestone: `CategoryEngine::Matches` is the shared category predicate; `StorageAnalysis::activeCategoryId_` is the authoritative UI/model state (empty = All); `TreemapView::SetCategoryFilter` carries that state into `BuildTree`; `FilterTreemapTree` prunes before `RebuildLayout` and recomputes retained folder totals. Filter changes, category-definition changes, mode entry, and snapshot updates rebuild the Treemap; filtered builds do not request or accept unfiltered folder aggregates. Product commit: `ff0533e`.

OpenSpec CLI is unavailable in this environment; checked-in OpenSpec artifacts remain the planning context. Source mapping, implementation, actual Settings UI edit/apply/live/restart acceptance, native-edit/global regression, Debug/Analyze builds, full CTest, and the focused product commit are complete.

## Established Facts

- `Column` owns independent `selectedIndices`, `selectionAnchor`, and `focusIndex`; `columns_` and `columns2_` provide separate pane state.
- `RefreshColumnFromSnapshot` rebuilt/sorted `column.items`, restored selection by case-insensitive name, and could leave `focusIndex` at an unrelated row when the focused item disappeared.
- `ActiveSelectionPaths`, `CurrentSelectionSummary`, command dispatch, drag start, open, rename, delete, copy, cut, move/paste, and context actions resolve paths from the active column's current selected indices.
- Degraded enumeration entries carry no stable file ID; indexed snapshot entries carry `volumeRowId`, `fileIdLow`, and `fileIdHigh` from the existing index projection.
- Indexed identity is valid only when both a nonzero volume row and nonzero FRN data are present. Degraded identity is normalized full path, compared case-insensitively with Windows ordinal comparison.
- If both sides have stable indexed identity, matching requires volume row plus FRN. If either side has stable indexed identity and the other does not, no path downgrade is allowed; path matching is used only when neither side has stable identity.
- Error/missing directory refreshes now clear items, selection, anchor, and focus.
- Search-result pending selection still uses the existing path/name navigation mechanism; the refreshed column state now uses the same identity-safe remapping once entries are materialized.
- Debug-only `WM_COPYDATA` observation reads the active path, visible row names, selected/focused identity and row, and the existing `ActiveSelectionPaths()` operation target; it has no selection-control path.

## Decisions

- decision: Add a small header-only `ffui` identity/capture/restore seam instead of a global selection subsystem.
  rationale: Reuse existing index identity fields and current path data while keeping the behavior headless and deterministic.
  consequence: `ColumnView` retains index-based rendering/storage, but indices are regenerated only after identity matching.
- decision: Preserve indexed rename continuity only when volume+FRN proves continuity.
  rationale: A same-path replacement must not inherit an old indexed selection.
  consequence: Degraded mode follows full path; indexed-to-degraded downgrade clears safely.
- decision: Keep infrastructure and product commits separate.
  rationale: The workstate protocol must survive independently from the unfinished product slice.
  consequence: Commit only `AGENTS.md` and `docs/agent/WORKSTATE.md` first.

## Implementation State

### Completed

- Root cause trace across ColumnView, snapshot projection, degraded enumeration, search selection, and operation-target resolution.
- `src/ui/include/ffui/SelectionIdentity.h` identity and remapping seam.
- `ColumnView::RefreshColumnFromSnapshot` identity capture/restore for selection, anchor, and focus.
- Safe clearing on AccessDenied, NotFound, I/O error, and directory error callbacks.
- Eight focused deterministic regression scenarios in `tests/ui/test_selection_identity.cpp`.
- CTest registration in `tests/ui/CMakeLists.txt`.
- `src/ui/include/ffui/SelectionDiagnostics.h`, `ColumnView::CurrentSelectionDiagnostics`, and `tests/ui/selection_identity_runtime_probe.cpp` provide the minimal read-only runtime observation seam.
- Storage Analysis visibility policy, HWND application point, and focused headless regression tests are complete.

### In Progress

- None. Slice complete.

### Not Started

- None for this slice.

## Files Changed In Prior Storage Slice

- `src/ui/include/ffui/StorageVisibility.h`
  - Pure Storage mode/control visibility policy.
- `src/ui/src/StorageAnalysis.h` and `src/ui/src/StorageAnalysis.cpp`
  - Single `ApplyModeVisibility` owner; refresh is data-only and reposition is geometry-only.
- `tests/ui/test_storage_visibility.cpp` and `tests/ui/CMakeLists.txt`
  - Headless mode matrix, transitions, resize-policy stability, and idempotence coverage.
- `docs/agent/WORKSTATE.md`
  - Durable frontier and validation checkpoint.

## Files Changed In Prior Shortcut Slice

- `src/protocol/include/ffprotocol/Settings.h` and `src/protocol/src/Settings.cpp`
  - Removed shortcut vectors from general Settings persistence, exposed the shared canonical shortcut path, and added one-shot legacy extraction.
- `src/ui/src/CommandSystem.h` and `src/ui/src/CommandSystem.cpp`
  - Made the runtime ShortcutMap the validated canonical model, with atomic save/load, stable IDs, scope-derived bindings, and legacy import.
- `src/ui/src/SettingsDialog.h` and `src/ui/src/SettingsDialog.cpp`
  - Settings Shortcuts page now edits a draft of the live ShortcutMap and applies it on OK.
- `src/ui/src/WindowShell.h` and `src/ui/src/WindowShell.cpp`
  - Startup canonical load/legacy migration and save/live-apply wiring.
- `tests/commands/test_command_system.cpp` and `tests/protocol/test_protocol.cpp`
  - Persistence, malformed/unknown, scope, path, and legacy migration coverage.
- `src/ui/src/CommandSystem.h` and `src/ui/src/CommandSystem.cpp`
  - Central native-edit eligibility policy; `file.undo` is ActiveView so Ctrl+Z remains native while editing.
- `src/ui/src/WindowShell.h` and `src/ui/src/WindowShell.cpp`
  - GetFocus/message-target native-edit routing; Global dispatch runs before child-dialog routing and consumes once.
- `src/ui/src/NavigationChrome.h` and `src/ui/src/NavigationChrome.cpp`
  - Focus-loss cancellation no longer steals focus from a Global command's destination edit.
- `tests/ui/test_shortcut_routing.cpp` and `tests/ui/CMakeLists.txt`
  - Headless actual-binding matrix, edit suppression, non-edit regression, unknown chord, and no-double-route coverage.
- `docs/agent/WORKSTATE.md`
  - Durable routing root cause, validation, and completion checkpoint.

## Files Changed In Current Treemap Slice

- `src/ui/src/CategoryEngine.{h,cpp}`
  - Canonical category-ID match predicate shared by list and Treemap filtering.
- `src/ui/src/StorageAnalysis.{h,cpp}`
  - Owns active category ID, preserves it across mode changes, and forwards it to Treemap; list filtering uses the same predicate.
- `src/ui/src/TreemapFilter.h`
  - Pure pre-layout hierarchy filter that prunes non-matching files and recomputes retained folder totals.
- `src/ui/src/TreemapView.{h,cpp}`
  - Consumes the active category, filters before layout, recursively materializes snapshot hierarchy, and ignores unfiltered aggregate requests while filtered.
- `tests/ui/test_treemap_filter.cpp` and `tests/ui/CMakeLists.txt`
  - Deterministic All/category/empty/nested/aggregate/transition/classifier coverage and CTest registration.

## Tests Added / Modified

- `ffstorage_visibility_tests`
  purpose: Covers all six modes, active-mode/exclusive-control visibility, requested transitions, resize stability, and idempotence.
  status: PASS (focused and full CTest).
- `ffshortcut_routing_tests`
  purpose: Covers Global bindings from native edit focus, native Ctrl+C/V/X/A/Z ownership, view-local suppression, non-edit routing, unknown chords, and one-dispatch behavior.
  status: PASS (focused).
- `fftreemap_filter_tests`
  purpose: Covers full data, Image/Video filtering, nested container retention, filtered aggregates, empty results, classifier reuse, and repeated transitions.
  status: PASS (focused).

## Validation

Debug: PASS; `cmake --build --preset debug` rebuilt the current worktree and linked `build/debug/src/ui/FastFiles.exe`.
Analyze: PASS; `cmake --build --preset analyze` completed cleanly with `/analyze`.
CTest: PASS, 39/39 serial with process-local `TEMP=C:\Users\Michael Roy\AppData\Local\Temp` and `TMP=C:\Users\Michael Roy\AppData\Local\Temp`; the first run's only failure was corrected documented mixed line endings, and the final run passed all 39.
Focused persistence/routing: PASS — `ffprotocol_tests`, `ffcommand_tests`, and `ffshortcut_routing_tests`; tests cover missing defaults, save/reload, old binding removal, Global/ActiveView scope retention, malformed canonical fallback, unknown commands, legacy import, invalid chords, and settings path/source separation.
Focused Treemap/filter: PASS — `fftreemap_filter_tests`, `fftreemap_layout_tests`, and `ffstorage_visibility_tests`; the pure filter target builds and all three tests pass. The fixture proves All (775 bytes), Image (125 bytes with nested 25-byte folder), Video (400 bytes), empty Game (0 bytes), classifier reuse, and deterministic All → Image → Video → All rebuilds.
Current-slice CTest: PASS, 40/40 serial with the established long-form user TEMP/TMP override. The first post-change run's only failure was mixed line endings in touched files; normalization fixed it and the final run passed all 40.
Runtime (Treemap integration/smoke): PASS for legitimate degraded-mode shell/model coverage. Rebuilt `build/debug/src/ui/FastFiles.exe` launched with `FastFilesEngine`; Storage Analysis opened with the expected partial-coverage status and 29 browsed items. The existing category combo was driven through the owner command path, Treemap mode activated, and Treemap mode kept list/filter/status controls hidden. Treemap ↔ Largest Files plus a 1500x900 resize preserved the visibility policy; `IsHungAppWindow` was false and the Engine remained alive. Data totals/nodes were accepted from `fftreemap_filter_tests` because the current UI has no read-only Treemap-node diagnostic and degraded runtime has no trustworthy visual tile observation seam.
Runtime (Settings persistence acceptance): PASS — actual FastFiles Settings HWND `FastFilesSettingsDialog` opened via physical `Ctrl+,`; Shortcuts tab selected by native mouse plus five Right-arrow inputs; stock `SysListView32` row 24 (`Search`) entered its temporary native `EDIT` through physical double-click. Literal `F6` was observed in that editor via cross-process `GetWindowText`, committed with Enter, and applied through the real OK button control path. Canonical file contained `search.focus` key 117 with no Ctrl+F binding; legacy `settings.json` shortcut section was removed. Actual F6 focused Search query EDIT live. After Ctrl+L focused address EDIT `20776946`, old Ctrl+F left address focus unchanged. A normal close/relaunch loaded the same F6 binding; fresh Search query EDIT `3999782` focused on F6, and fresh address EDIT `133296` retained focus after old Ctrl+F.
Runtime (native-edit/global regression): PASS — from the native address EDIT, F6 focused Search; Ctrl+L focused the address EDIT; Ctrl+Shift+P opened command-palette EDIT `723122` plus ListBox `329884`, Escape closed it; F5 while address EDIT owned focus left it focused. Disposable `probe` text exercised native Ctrl+A/C/X/V/Z: selection result `327680` (0..5), copied clipboard `probe`, cut length 0, paste length 5, undo length 5. Original Unicode clipboard text was restored.
Runtime (shortcut slice): PASS on the rebuilt current Debug UI via direct Win32 `keybd_event` and live HWND state. `Ctrl+L` focused address `EDIT` HWND 264124. Native acceptance used actual typed `probe` text and `WM_GETTEXTLENGTH`/`EM_GETSEL`: Ctrl+A selected all (0..5), Ctrl+C yielded clipboard `probe`, Ctrl+X reduced edit length to 0, Ctrl+V with `PasteProbe` produced length 10, and Ctrl+Z restored length 5; focus remained the address edit throughout. Global acceptance from the edit passed: Ctrl+Shift+P focused the command-palette `EDIT` HWND 133068 with its ListBox visible, Ctrl+F focused the search `EDIT` HWND 133076, and Ctrl+L returned focus to address `EDIT` HWND 264124. F5 while address edit focus remained on HWND 264124, with no view-routing UI change observed. Escape cleanup completed separately. Storage runtime evidence below is from the completed prior slice.
Runtime (shortcut regression): PASS on a fresh current Debug GUI instance. Real address-edit paste/Enter navigation reached `C:\Windows\Temp` and `C:\Windows`; after each commit the edit hid and focus returned to the main HWND. Ctrl+Shift+P from normal view focus opened the palette `EDIT`/ListBox and Escape closed it; `IsHungAppWindow` reported responsive. No destructive file operation was used.
Runtime (prior Storage slice): PASS. Live Debug FastFiles accepted Overview, Drill Down, Largest Folders, Largest Files, By Category, and Treemap. For each mode, `IsWindowVisible`/`GetWindowRect` was queried before and after a 1500x900 resize; hidden mode-owned HWNDs remained hidden, radio state tracked the active mode, and closing Storage hid every Storage child. The Direct2D Treemap surface had no layout in degraded coverage, so its screenshot was not treated as visual tile evidence; native control visibility and checked Treemap mode were verified.
Other: OpenSpec CLI is unavailable; checked-in `storage-analysis` artifacts were read manually. Build tools are available under the installed VS 2022 Build Tools instance.
Prior product commit: `412e25b` (`Fix Storage Analysis mode visibility`). Prior shortcut-routing product commit: `9db73c1fed5c8ed3a96c95e9ef89b463254ce197` (`Preserve global shortcuts while editing text`). Prior shortcut-persistence product commit: `3ec28db` (`Unify shortcut settings persistence`). Current Treemap category-filter product commit: `ff0533e` (`Apply category filters to Treemap data`).

## Failures / Blockers

- The first full CTest run failed only on the known clean-profile TEMP path issue and the edited source's mixed line endings. The focused rerun with the documented TEMP/TMP override and the final full run both passed.
- The first probe request deadlocked because `SMTO_BLOCK` prevented the synchronous WM_COPYDATA reply; removing that flag fixed the probe without affecting product state.
- Hard-coded mouse coordinates hit a breadcrumb at the fresh process's 1.0 DPI scale; deriving click geometry from the live chrome height fixed navigation. This was harness-only and no product behavior changed.
- `cmake` was initially absent from the plain shell PATH; using `Launch-VsDevShell.ps1` from the installed Build Tools instance resolved the harness setup without changing the repository.
- Runtime probe: a read-only PowerShell/PInvoke query used the live main HWND and Storage child control IDs; normal command-palette and radio-button commands drove mode transitions. FastFiles remained responsive and the Engine/UI process pair stayed alive.
- Treemap screenshot capture was limited by the existing degraded empty-layout path (`TreemapView::Render` returns before clearing when `layout_` is empty); no Treemap rendering claim is made from that capture.
- Full visual Treemap tile acceptance remains unavailable: the current UI has no read-only diagnostic exposing `TreemapView::root_`/`layout_`, and degraded coverage does not provide trustworthy visual tile evidence. Pure model/data acceptance is complete; no screenshot or visual tile claim is fabricated.
- A synchronous synthetic owner-data list double-click blocked its caller during runtime probing, so that method was abandoned; the UI remained responsive and the asynchronous PostMessage path was used for smoke navigation. No product behavior changed.
- The first UI driver pass targeted row 22 (`Toggle Details Pane`) because the registry has 15 pre-Search rows, not 14; it added only an unbound customization for a command with no default. This test-profile state was removed by restoring the backed-up user files; no product source changed.
- Win32 runtime driver required a foreground handoff because each shell invocation left the terminal foreground; `GetForegroundWindow` + returned thread IDs from `GetWindowThreadProcessId` + `AttachThreadInput`/`SetForegroundWindow` made the product interactive. Cross-process pointer messages (`TCM_GETITEMRECT`) were abandoned after a timeout; no unsafe target-buffer observation was used.
- FastFiles UI closed through the native title-bar close region on each acceptance run; the exact Engine child remained orphaned after its UI parent exited and was stopped by PID after parent exit. This is runtime-process cleanup evidence, not a shortcut persistence failure.
- Plain-shell `cmake` is unavailable; focused validation succeeded after launching the installed VS 2022 Build Tools developer shell, as documented above.
- OpenSpec CLI is unavailable in this environment; checked-in OpenSpec artifacts were used as planning context and no unrelated change artifacts were edited.
- The first runtime probe used a stale staged executable and invalid cross-process `GetWindowText`/`EM_GETSEL` observations; those results are explicitly excluded. The rebuilt run used the source-matching executable, actual key-generated text, `WM_GETTEXTLENGTH`, `EM_GETSEL` return values, and clipboard contents.
- Full CTest environment evidence: default 8.3 user TEMP passed `ffindexstore_store_tests` but failed unrelated `fffileoperations_e2e_tests`; `C:\Windows\Temp` passed the file-operations test but aborted the index-store test in a Debug CRT dialog; long-form user TEMP/TMP passed both isolated tests and is the selected full-suite environment.
- Runtime Treemap probe: a synchronous synthetic owner-data list double-click blocked the caller, so that method was abandoned; the UI remained responsive and the asynchronous PostMessage path was used for smoke navigation. No product behavior was changed by the probe.
- Full visual Treemap tile acceptance remains unavailable: the app is degraded/partial and no existing read-only diagnostic exposes `TreemapView::root_`/`layout_`; no screenshot or visual tile claim is fabricated. Pure model/data acceptance is complete.

## Deferred / BACKLOG

- Any broader selection-model test depth beyond this slice.
- Any UIA/provider work for Direct2D-painted ColumnView rows.
- Storage-specific limitation retained: degraded mode may have no Treemap layout, so visual Treemap tiles were not claimed by this slice; allocated-size metadata remains the existing projection gap.

## Runtime Acceptance Checkpoint

- Settings control facts: top-level `FastFilesSettingsDialog`; tab index 5 is Shortcuts; shortcut rows are a native `SysListView32`; `NM_DBLCLK` on subitem 1 creates a temporary native `EDIT`; Enter/blur commits and Escape cancels; IDOK runs `FinishShortcutEdit`, `SaveCurrentPage`, and the real `onChanged_` callback.
- Test binding: `Search` old `Ctrl+F`, test `F6`. Search is registry row 24 (file operations 9 + navigation/selection 15).
- Acceptance evidence: canonical `shortcuts.json` has `{"commandId":"search.focus","key":117,"modifiers":""}` and no `Ctrl+F`; physical F6 focused Search query EDIT `5441536`; physical Ctrl+L focused address EDIT `20776946`; physical old Ctrl+F left address EDIT focus unchanged.
- Completed: normal close, relaunch without rewriting config, F6/old Ctrl+F restart checks, native-edit/global regression, exact user-profile restoration, generated canonical cleanup, temporary-directory removal, final Debug/Analyze builds, full serial CTest, and the focused product commit.

## Historical Prior-Slice Note

The prior slice recommended `large-index search/snapshot scalability benchmarking`; that recommendation is now completed by the current slice record above.

## Resume Instructions

1. Read this file.
2. Inspect `git status --short`, branch, and HEAD.
3. Reconcile stale entries against Git/source/tests.
4. Execute `NEXT EXACT ACTION`.
5. Do not restart completed investigation.
