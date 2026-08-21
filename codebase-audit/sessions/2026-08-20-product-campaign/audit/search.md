# Audit area: SEARCH EXPERIENCE — audit src/ui/src/SearchPanel.{h,cpp}, src/ui/include/ffui/SearchLayout.h, SearchResultMetadata.h, the whole src/search/ component (query parsing, filter registry incl. size:/ondisk:, history), and the search request/response paths in src/ui/src/EngineClient.{h,cpp} plus engine-side serving. Cover: query-generation safety (can results from a superseded query ever render?), cancellation of in-flight searches, per-keystroke latency risks, invalid-query handling surfaced to users, empty/no-results/loading states, sort dropdown behavior, result activation, keyboard navigation within results, recent-searches lifecycle, large result sets, scoped vs global search, extension/type filters.

(agent agent-2, outcome completed)

# Search Experience Audit — FastFiles

## Defects

### [P2] Invalid filter tokens are silently swallowed — no user feedback — src/search/src/Query.cpp:580
- Symptom (user-visible): typing `size:abc report`, `ext:`, `modified:13/45/2024`, or any unknown `foo:bar` token silently degrades the query — the bad token is dropped entirely and the remaining terms are searched, with zero indication that part of the query was ignored. A user filtering `size:>10GB` who typos the unit (`size:>10gbt`) gets *unfiltered* results instead of an error.
- Evidence: `Query.cpp:580` `else query.invalidFilters.push_back(std::wstring(key));` and `Query.cpp:583` `query.unrecognizedKeys.push_back(std::wstring(key));` — these vectors exist (`src/search/include/ffsearch/Query.h:131-132`) but a repo-wide grep shows **no consumer anywhere in `src/`**; `SearchPanel::Dispatch` (`SearchPanel.cpp:388`) parses and discards the `Query` diagnostics.
- Failure scenario: user types `name: budget ext:xlsxx` → sees every file containing "budget", concludes xlsx filter matched, trusts wrong results.
- Root cause hypothesis: diagnostics were plumbed into the parse result but the UI layer was never wired to surface them.
- Suggested fix: in `SearchPanel::Dispatch`, after `ParseQuery`, if `!query.invalidFilters.empty() || !query.unrecognizedKeys.empty()`, prepend a status-line warning (e.g. `Ignored invalid filter: size:`) via `UpdateStatus(..., StatusKind::Info)`; keep executing the valid remainder.
- Regression test idea: headless test asserting `ParseQuery(L"size:abc doc")` populates `invalidFilters`; plus a seam-level test that the panel's status text mentions the invalid key.

### [P2] Date filters mix UTC and local-time day boundaries — wrong results near midnight / non-UTC timezones — src/search/src/Query.cpp:306
- Symptom: `modified:today` and `modified:>7d` use UTC-day arithmetic while explicit dates use local time, so in UTC+X the "today" window opens/closes hours off from what the user sees on disk (Explorer timestamps), missing files modified this morning or including yesterday evening.
- Evidence: `Query.cpp:308` `const auto day = now - now % kDay;` operates on raw FILETIME (UTC-based, epoch 1601-01-01 which was a Monday), whereas `ParseDay` (`Query.cpp:296-297`) uses `SystemTimeToFileTime`, which interprets the SYSTEMTIME as **local** time. Two different timezone semantics in the same filter grammar.
- Failure scenario: machine in UTC+2, 01:00 local. A file modified at 00:30 local (= 22:30 UTC yesterday) does not match `modified:today` although its Explorer timestamp says today.
- Root cause hypothesis: `CurrentFileTime() % kDay` assumed FILETIME day boundaries align with local days.
- Suggested fix: convert `now` through `FileTimeToSystemTime` (local) and rebuild the day start via `SystemTimeToFileTime`, mirroring `ParseDay`.
- Regression test idea: test that `modified:today`'s computed range contains `SystemTimeToFileTime(local now)` truncated to local midnight.

### [P2] Every debounced keystroke records a history entry and synchronously flushes the history file on the UI thread — src/ui/src/SearchPanel.cpp:401
- Symptom: typing a multi-word query with natural pauses >125 ms pollutes Recent searches with prefixes (`docu`, `documen`, …) and performs a temp-file + `MoveFileExW(MOVEFILE_WRITE_THROUGH)` disk flush per keystroke pause on the UI thread.
- Evidence: `HandleTimer` → `Dispatch()` with default `recordHistory=true` (`SearchPanel.cpp:367-371, 107`); `RecordHistory` (`SearchPanel.cpp:1139`) calls `historyStore_.Record(query, HistoryPath())`, and `SearchHistory::Record` ends in `Save(path)` (`History.cpp:174`) which does create-directories + `GetTempFileNameW` + write + `MoveFileExW ... WRITE_THROUGH` (`History.cpp:96-147`) — all synchronously inside the message handler.
- Failure scenario: type "quarterly report 2026" with normal typing rhythm → history dropdown fills with 5+ useless prefixes; each one also hits the disk mid-typing.
- Root cause hypothesis: history recording was attached to the dispatch pipeline instead of to query *commitment* (Enter/activation/navigation).
- Suggested fix: record history only on result activation, explicit Enter, or panel hide — e.g. pass `recordHistory=false` from `HandleTimer` and call `RecordHistory` in `ActivateResultItem`/`Hide`.
- Regression test idea: simulate two debounced dispatches of `doc`/`docs` without activation and assert `historyStore_.Queries()` stays empty.

### [P2] Snapshot updates re-run a full indexed search and discard the candidate cache on every generation bump while the panel is open — src/ui/src/SearchPanel.cpp:960
- Symptom: during active indexing (snapshot generations publishing frequently), an open search panel with text re-dispatches a full engine-side indexed search and forces a complete snapshot→candidate projection rebuild per generation; typing during this window competes with that churn, producing perceptible lag exactly when the index is busiest.
- Evidence: `OnSnapshotUpdated` (`SearchPanel.cpp:964-966`) bumps generation, resets `candidateCache_`, then `if (visible_ && ... GetWindowTextLengthW(query_) > 0) Dispatch(false);` (`SearchPanel.cpp:990-992`). `PopulateCandidates` rebuilds the entire mapping projection (`SearchPanel.cpp:1016-1092`) whenever the cache is empty.
- Failure scenario: index service scanning a large volume publishes generations every few seconds; user opens search, types `report`, and every subsequent generation restarts both the engine query and the local candidate materialization.
- Root cause hypothesis: refresh-on-snapshot was implemented unconditionally rather than throttled/coalesced.
- Suggested fix: coalesce — only re-dispatch if the last dispatch for this text completed more than N seconds ago, or mark dirty and refresh on next keystroke/visibility change.
- Regression test idea: worker-thread benchmark asserting consecutive `OnSnapshotUpdated` calls within T don't enqueue more than one search.

### [P3] `SetQueryAndDispatch` lacks the `updatingHistory_` guard — programmatic text set triggers a second debounced dispatch — src/ui/src/SearchPanel.cpp:1165
- Symptom: activating a recent search from Home runs the query twice (once immediately, once 125 ms later via the stray debounce timer), doubling engine load and re-recording history.
- Evidence: `RecordHistory` wraps its own `SetWindowTextW(query_, ...)` in `updatingHistory_ = true/false` precisely because a programmatic set raises `CBN_EDITCHANGE` (`SearchPanel.cpp:1141-1145`), but `SetQueryAndDispatch` (`SearchPanel.cpp:1166-1167`) calls `SetWindowTextW` bare before `Dispatch(true)`. The ensuing `CBN_EDITCHANGE` (`SearchPanel.cpp:337-344`) bumps the generation and arms `kDebounceTimer`, which fires `Dispatch()` again.
- Failure scenario: click any entry under Home → Recent searches; two identical indexed searches hit the engine back-to-back.
- Suggested fix: wrap the `SetWindowTextW` in `SetQueryAndDispatch` with the same `updatingHistory_` flag.
- Regression test idea: seam test counting dispatches after `SetQueryAndDispatch` (needs the edit-subclass notification path exercised).

### [P3] Esc does not close the search panel while focus is in the query box or result list — src/ui/src/WindowShell.cpp:2171
- Symptom: the documented-feeling "Esc closes search" works only when the main frame itself has focus; immediately after `ShowAndFocus` puts focus in the query edit (`SearchPanel.cpp:281`), Esc goes to the child control and nothing happens.
- Evidence: the only search-hide-on-Esc path is `WM_KEYDOWN` in the frame wndproc (`WindowShell.cpp:2171-2176`); the message loop (`WindowShell.cpp:1060-1075`) has no `IsDialogMessage` and no Esc shortcut binding (grep of `CommandSystem.cpp` shows no `search.close` binding; `IsShortcutDispatchAllowed` would allow Global-scope chords even with edit focus).
- Failure scenario: Ctrl+F → type → press Esc expecting to return to browsing; focus stays trapped in the panel until mouse-clicking elsewhere.
- Suggested fix: register a Global-scope `search.close` shortcut bound to Esc (Global chords dispatch even when `nativeEditFocused`), invoking `searchPanel_.Hide(); SetFocus(hwnd_)`.
- Regression test idea: uia-driver/keyboard test sending Esc with focus on the query edit and asserting `Visible()==false`.

### [P3] Query placeholder promises indexed locations even in degraded mode — src/ui/src/SearchPanel.cpp:916
- Symptom: with no service/index (the current default state per project docs), the ghost text still reads "Search indexed locations…", contradicting the scope combo right next to it ("All Browsed Locations").
- Evidence: hardcoded `text = L"Search indexed locations…";` in `PaintSearchEdit`; no `persistentCoverage_`/`engineActive_` conditioning.
- Suggested fix: choose placeholder from `persistentCoverage_ ? L"Search indexed locations…" : L"Search browsed folders…"`.
- Regression test idea: none practical (paint path); manual check in degraded mode.

### [P3] "Clear history" button is unstyled native chrome in dark theme — src/ui/src/SearchPanel.cpp:222
- Symptom: every other search control is owner-drawn from `UITheme` tokens; `clearHistory_` is a plain `BUTTON` (`WS_CHILD` only), rendering as a light system button inside the dark panel — violating the project rule that new UI surfaces source colors from the token set.
- Evidence: `CreateWindowExW(0, L"BUTTON", L"Clear history", WS_CHILD, ...)` at `SearchPanel.cpp:222`; `HandleDrawItem` handles `sortDirection_` but not `clearHistory_` (`SearchPanel.cpp:617-634`).
- Suggested fix: give it `BS_OWNERDRAW` and route through `HandleDrawItem` like `DrawDirectionButton`.
- Regression test idea: drift/manual visual check.

### [P3] Search-history dedup is case-sensitive exact match — src/search/src/History.cpp:160
- Symptom: `Report`, `report`, and `REPORT` occupy three history slots; the persisted cap (100) fills with case variants and Recent searches show near-duplicates. (Folder history elsewhere dedups case-insensitively per project convention.)
- Evidence: `std::find(queries_.begin(), queries_.end(), query)` — plain `wstring` equality (`History.cpp:160`); same in `Load` (`History.cpp:88`).
- Suggested fix: compare with `_wcsicmp` (or `CompareStringOrdinal(..., TRUE)`), matching the case-insensitive matching semantics of the search itself.
- Regression test idea: record `"abc"` then `"ABC"`, assert one entry.

## Verified sound
- **Superseded-query staleness**: both completion paths gate on `completion->generation != currentGeneration_` (`SearchPanel.cpp:477, 498`); generation is bumped on every keystroke, scope/sort change, snapshot update, and destruction — a stale result cannot render.
- **Indexed-search cancellation**: superseding dispatches cancel the prior request id (`SearchPanel.cpp:415-418`); engine side supersedes/cancels pending+active tasks under one mutex and suppresses sends of cancelled responses (`UiServer.cpp:110-208`); disconnect fails all outstanding callbacks with `Unavailable` (`EngineClient.cpp:675-692`), which triggers a single snapshot fallback (`SearchPanel.cpp:499-504`) — no stranded "Searching…" state, no fallback loop.
- **Worker-thread lifecycle**: destructor flips `alive`, cancels, joins the worker before members die; completions are heap-posted and ownership-transferred only on `PostMessage` success (`SearchPanel.cpp:180-200, 470-471`).
- **Frame/parser hardening on the search wire**: V1/V2 response parse failures drop the frame; batch counts cross-checked elsewhere per protocol invariants.
- **Sort dropdown ↔ enum alignment**: combo indices 0–6 map exactly to `SortField {Relevance..AllocatedSize}` (`SearchPanel.cpp:240` vs `Search.h:20`); direction toggle label/state consistent; unknown allocated size sorts after known values in both directions (`Search.cpp:84-93`).
- **Scope labels/persistence logic**: `persistentOnly` correctly excludes browsed-only rows for broad scopes while keeping Current Folder as available-data view (`SearchPanel.cpp:398-399`); engine always enforces `persistentOnly=true` for durable rows (`IndexPipeline.cpp:753`).
- **Result activation safety**: dbl-click/Enter/UIA activation all resolve the candidate against `candidates_` and degrade to an empty reconstruction rather than crashing; off-thread UIA activation is marshaled to the UI thread with a PostMessage-failure fallback (`SearchPanel.cpp:1272-1301`).
- **Highlight spans** expand to avoid splitting surrogate pairs (`SearchPanel.cpp:70-88`); `size:`/`ondisk:` overflow-guarded parsing, UNKNOWN never matches numerics (`Query.cpp:229-284`).
- **History file integrity**: unique temp name, UTF-8 explicit conversion, delete-on-failure, `MOVEFILE_WRITE_THROUGH` replace (`History.cpp:96-147`).
- **Truncation honesty**: capped at 10 000 results with an explicit "showing the first N" status (`SearchPanel.cpp:481-483`); note the cap applies pre-sort, so it is a traversal-order subset, but the status text says exactly that.
