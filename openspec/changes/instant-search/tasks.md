## 1. Query Grammar and Filter Registry

- [x] 1.1 Implement the tokenizer: split on unquoted whitespace, honor double-quoted values, classify each token as structured filter / wildcard pattern / free-text term
- [x] 1.2 Define the filter-registry data structure (`key → {value-parser, predicate-factory}`) and the lookup/dispatch used by the tokenizer for `key:value` tokens
- [x] 1.3 Implement `ext:` filter (extension match, case-insensitive, leading dot optional)
- [x] 1.4 Implement `name:` and `folder:` filters (literal/quoted value match against filename and containing path respectively)
- [x] 1.5 Implement `size:` filter: comparison operators (`>`, `<`, `>=`, `<=`, `=`), unit suffixes (`B`/`KB`/`MB`/`GB`), and `..` range form
- [x] 1.6 Implement `modified:` filter: absolute dates, `..` date ranges, relative offsets (e.g., `>7d`, `today`)
- [x] 1.7 Implement `kind:` filter as a registry entry with a minimal built-in default category set (document/image/video/audio/archive/executable/folder), structured so a later change can supply the authoritative category table without altering the parser
- [x] 1.8 Implement wildcard compilation (`*`, `?`) into a glob matcher for bare wildcard tokens
- [x] 1.9 Implement unrecognized-key fallback: treat as literal free-text match, report the key as unrecognized to the caller
- [x] 1.10 Unit tests: each filter key's value parser (valid and invalid inputs), quoting/embedded-space handling, wildcard compilation, unrecognized-key fallback behavior
- [x] 1.11 Unit test: registering a new filter key in isolation does not change parsing/matching behavior of any existing key

> Consolidation disposition: the query grammar/filter registry is intentionally self-contained and complete. Sections 2-4 remain open; this change does not widen the shared snapshot format merely to claim matching or path-reconstruction work before those consumers are implemented.

## 2. Matching Engine

- [x] 2.1 Implement the in-process reader over `FastFilesEngine`'s mapped snapshot (map-once, re-map on generation-change notification, per the existing engine↔UI control-pipe contract)
- [x] 2.2 Implement the chunked, interruptible linear scan across the projection's interned filename array, evaluating the compiled AND-combination of tokens per candidate
- [x] 2.3 Implement ordinal (non-locale-sensitive), surrogate-pair-safe case-insensitive substring comparison and wildcard matching
- [x] 2.4 Implement scoped candidate restriction (Current Folder / Current Folder+Subfolders / Current Drive / All Indexed Locations) as an explicit parameter to scan execution
- [x] 2.5 Implement the input-generation token: assign on each dispatched search, check periodically (per chunk) during the scan, abort early on supersession, discard stale completions
- [x] 2.6 Run the scan on a dedicated worker thread, off the UI thread, with results delivered back asynchronously
- [x] 2.7 Unit tests: substring match at start/middle/end of filename; wildcard patterns with `*` and `?`; scope restriction correctness for each of the four scopes; cancellation discards stale results

## 3. Ranking and Sorting

- [x] 3.1 Implement match-tier classification (exact / prefix / substring / path-only) per candidate
- [x] 3.2 Implement the default relevance comparator: tier, then filename length, then path length, then ordinal alphabetical tiebreak
- [x] 3.3 Implement explicit sort orders: filename, path, size, modified date, created date, each ascending/descending with a deterministic alphabetical secondary tiebreak
- [x] 3.4 Unit tests: tier ordering (exact > prefix > substring > path-only); tiebreak ordering within a tier; each explicit sort order's ascending/descending correctness and tiebreak determinism

## 4. Path Hierarchy Reconstruction

- [x] 4.1 Implement the parent-directory-ID chain walk from a matched entry up to the volume-root sentinel, using the in-memory projection's interned strings
- [x] 4.2 Implement reversal into root-to-entry ordered path segments
- [x] 4.3 Implement graceful partial-resolution handling: return resolved segments plus an explicit stop-point indicator when a segment in the chain can no longer be resolved
- [x] 4.4 Unit tests: reconstruction for a deeply nested entry; reconstruction when the entry is itself a top-level (drive-root-child) item; partial-resolution behavior when a mid-chain directory is deleted between match and walk

## 5. Search UI — Input and Debouncing

- [x] 5.1 Implement the search box control (Direct2D/DirectComposition, consistent with the existing UI shell) with text input and focus handling
- [x] 5.2 Implement debounced dispatch: fixed delay after the last keystroke before triggering search execution (initial value from design.md's Open Questions, tunable)
- [x] 5.3 Wire the input-generation token (Section 2.5) through from keystroke to dispatch to result handling
- [x] 5.4 Implement in-progress vs. completed vs. no-results visual states for the search box/result area

## 6. Search UI — Result List and Virtualized Rendering

- [x] 6.1 Implement the result row layout: filename, type, location/path, size, modified time
- [x] 6.2 Implement fixed-row-height viewport windowing: compute visible row range from scroll offset, realize draw state only for visible rows plus overscan buffer
- [x] 6.3 Implement scrollbar/scroll-position handling for result sets from zero up to several thousand entries
- [x] 6.4 Implement sort-field selection UI (column headers or equivalent) wired to Section 3's explicit sort orders, with active sort field/direction indication
- [x] 6.5 Implement the "no results" empty state, visually distinct from "search in progress"
- [x] 6.6 Manual/perf validation: confirm no per-row UI element is allocated for off-screen rows at several-thousand-result scale, and that scroll-to-end remains responsive

## 7. Search UI — Scope Selector

- [x] 7.1 Implement the scope selector control with the four scope options and an always-visible active-scope indicator
- [x] 7.2 Wire scope selection to re-execute the current query immediately on change
- [x] 7.3 Implement scope-availability reflection of engine connection state: disable Current Drive/All Indexed Locations with an explanatory tooltip while in degraded mode
- [x] 7.4 Implement live re-enabling of broader scopes when the privileged connection transitions to active mid-session
- [x] 7.5 Implement forced scope fallback with a one-time inline notice when an explicitly selected scope becomes unavailable mid-session
- [ ] 7.6 Manual test: toggle `FastFilesIndexSvc` availability while the search UI is open and confirm scope options and any active fallback notice update correctly

## 8. Search UI — Search-to-Navigation Integration

- [x] 8.1 Wire result selection to invoke the query engine's path reconstruction (Section 4)
- [x] 8.2 Drive Column View's existing column-population/replacement primitive with the reconstructed root-to-entry segment list
- [x] 8.3 Implement selection of the matched entry within its terminal column (file) or population through and including it (folder)
- [x] 8.4 Wire partial-resolution results (Section 4.3) to Column View's existing in-column "no longer available" error state
- [ ] 8.5 Manual test: select a search result several levels deep and confirm every intermediate column populates correctly; rename/delete a mid-path directory between search and selection and confirm graceful degradation to the existing error state

## 9. Search History

- [x] 9.1 Implement local storage for executed search queries (query text, timestamp), scoped to the local machine only
- [x] 9.2 Implement recall UI: surface prior queries when the search box is focused/opened
- [x] 9.3 Implement "clear history" action
- [x] 9.4 Implement a setting to disable history recording without affecting previously recorded entries
- [x] 9.5 Unit/manual tests: history persists across app restarts; clearing removes all entries; disabling stops new entries while preserving existing ones until cleared

## 10. Unicode, Special-Character, and Cross-Cutting Validation

- [x] 10.1 Test corpus: filenames with surrogate-pair characters, combining/accented characters, embedded spaces, parentheses, apostrophes, and mixed scripts
- [x] 10.2 Verify ordinal case-insensitive matching produces identical results under at least two different Windows display-language/locale settings
- [x] 10.3 Verify no match-highlighting or wildcard boundary computation ever splits a UTF-16 surrogate pair
- [x] 10.4 Verify quoted filter values and free-text terms containing reserved-looking characters (parens, apostrophes) match literally without requiring escaping
- [x] 10.5 End-to-end test: search-as-you-type against a large synthetic index (hundreds of thousands to low millions of entries) to validate debounce/cancellation keeps the UI responsive
- [ ] 10.6 End-to-end test: full flow from typing a structured-filter query, through ranked results, to search-to-navigation landing on the correct Column View hierarchy, in both active and degraded engine modes
