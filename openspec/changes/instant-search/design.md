## Context

`establish-architecture-foundation` locked in the three-process split and, critically, the mechanism that makes keystroke-level search possible at all: `FastFilesEngine` publishes its filesystem view as an immutable, double-buffered snapshot in a read-only shared memory mapping, and `FastFiles.exe` maps it once and re-maps only on a generation-change notification over the control pipe. `index-storage-and-scanning` (assumed complete, sibling change) fills that snapshot with a real, whole-volume, incrementally-updated in-memory projection — parent-directory-ID references plus interned strings, not repeated full path strings — backed durably by SQLite.

This change, `instant-search`, is the first capability that actually reads that projection for a purpose other than populating one Column View column at a time. It has no IPC design work of its own to do — the snapshot-mapping mechanism already exists — its job is entirely: (1) what a query means (grammar/parsing), (2) how a query becomes a ranked, scoped result set against the projection's in-memory structures, and (3) how a selected result becomes a fully-populated Column View hierarchy.

Degraded mode (no privileged service, or it declined/crashed — permanent, first-class per D5 of the foundation design) means the projection may only cover browsed/pinned directories rather than the whole volume. This change treats that as a real, explicitly-surfaced scope limitation, not a special case to special-case around everywhere — the scope model (Decision D6 below) is designed so degraded mode is just "fewer scope options available," not a different code path.

## Goals / Non-Goals

**Goals:**
- Deliver search-as-you-type (debounced) filename search that reads the same in-memory projection `filesystem-index-store`/`index-engine` already maintain, with no SQL query and no IPC round trip per keystroke.
- Support the query grammar from the proposal — free-text terms (implicit AND), structured `key:value` filters (`ext:`, `size:`, `modified:`, `kind:`, `name:`, `folder:`), and bare wildcard patterns (`*.pdf`) — via a filter-registry design so new filter keys are additive, not parser rewrites.
- Provide a simple, explainable default relevance ranking, plus explicit sort by name/path/size/modified/created.
- Reconstruct the full Column View hierarchy (drive root → matched entry) on result selection, by walking the projection's parent-directory-ID chain, reusing `column-view-browsing`'s existing population/replacement/error-state mechanisms rather than duplicating them.
- Scale to result sets in the thousands via virtualized rendering — no per-row UI element for off-screen rows.
- Make search scope explicit, always visible, and correctly narrowed (never silently under-reported) under degraded mode.
- Handle Unicode filenames (including surrogate pairs) and special characters (spaces, parens, apostrophes, accents, embedded punctuation) correctly in both the parser and the matcher.

**Non-Goals:**
- Full-text/content search — filename and metadata only, never file contents.
- Boolean query operators beyond implicit AND (`OR`/`NOT`/parentheses/grouping) — deferred; flagged as an open question.
- Fuzzy/typo-tolerant matching (edit-distance, phonetic, etc.) — the proposal specifies substring and wildcard matching only.
- The final `kind:` category taxonomy — that belongs to the future `storage-analysis` change's `file-type-categorization` capability. This change ships a minimal built-in default set (document/image/video/audio/archive/executable/folder) and the extension point, not the final scheme.
- Search history sync across machines — local-only per this design.
- Any modification to `column-view-browsing`'s existing requirements — search only adds a new entry point into it, per the proposal's Impact section.
- Committed latency SLAs/benchmarked numbers — "feels instant" is the bar; real hardware benchmarking is an implementation-time/task-list concern, not fixed by this document.

## Decisions

### D1: Query grammar — tokenizer over an extensible filter registry, implicit AND only

The query string is split on unquoted whitespace into tokens. Each token is classified, in order:
1. **Structured filter** — matches `<key>:<value>` where `<key>` is a registered filter key (`ext`, `size`, `modified`, `kind`, `name`, `folder`, and any later addition). `<value>` may be double-quoted to embed spaces/parens/etc. (`folder:"Program Files"`, `name:"my report (final).docx"`).
2. **Bare wildcard pattern** — a token containing `*` or `?` outside any recognized key (`*.pdf`, `report_??.docx`) is compiled as a glob against the filename.
3. **Free-text term** — anything else, matched as a case-insensitive substring against the filename (and, when `scope`/`folder:` implies it, the path).

All tokens are ANDed — a matching entry must satisfy every token. There is no OR/NOT/grouping in this MVP grammar (see Non-Goals/Open Questions).

**Filter registry, the extensibility point the proposal asks for:** each filter key is an entry in a table mapping `key → {value-parser, predicate-factory}`. The value-parser turns the raw (possibly quoted) string into a typed value (e.g., `size:` parses `>10MB` into `{op: GT, bytes: 10_485_760}`; `modified:` parses a date, relative offset, or range into a timestamp predicate). The predicate-factory produces a closure evaluated per candidate entry against the in-memory projection's fields. Adding a new key (e.g., a future `owner:` or `attr:`) means registering one new table entry, not touching the tokenizer, the AND-combination logic, or any existing filter. `kind:` is deliberately implemented as one such registry entry today, with a small built-in category set, precisely so `storage-analysis`'s later `file-type-categorization` capability can supply the authoritative category table without the query engine itself changing shape.

**Comparison/range operators:** `size:` accepts `>`, `<`, `>=`, `<=`, `=` (default, if no operator, is exact-or-"starts with this magnitude" — implementation detail left to tasks) with unit suffixes (`B`/`KB`/`MB`/`GB`), plus an explicit range form (`size:10MB..100MB`). `modified:` accepts absolute dates, `..`-delimited ranges, and relative offsets (`modified:>7d`, `modified:today`). Both are registry entries following the same value-parser/predicate-factory shape as any other filter — not special-cased in the parser core.

**Unrecognized filter key handling:** since `:` is not a legal character in Windows filenames, any `word:value` token is unambiguously an attempted structured filter, not accidental free text. An unrecognized key is not silently dropped and not treated as a hard parse error either — it falls back to being matched as a literal free-text substring (safe, non-crashing default) while the query bar surfaces a lightweight inline indicator (e.g., an underline/tooltip) that the key wasn't recognized. This avoids the worse failure mode of a query silently returning nothing with no explanation.

**Alternatives considered:**
- *Full boolean grammar (AND/OR/NOT/parentheses, à la Everything's own advanced syntax)* — rejected for MVP: significantly more parser and UI-affordance complexity for a first version; implicit-AND-only covers the proposal's stated scope. Left as an explicit open question for a later iteration.
- *Regex-only search box* — rejected: too unfriendly for the primary "just start typing a name" use case; power users can still get most regex-like behavior from wildcards.
- *Hardcoded filter set with a switch statement* — rejected: this is exactly the "parser rewrite for every new filter" shape the proposal calls out to avoid, and it would specifically block `kind:`'s planned hand-off to `storage-analysis`.

### D2: Matching — chunked linear scan over the interned filename array, not an inverted index, executed in-process against the mapped snapshot

Search execution happens **inside `FastFiles.exe`**, directly against the memory-mapped snapshot it already holds (per the foundation design's zero-IPC-round-trip guarantee) — there is no new RPC to `FastFilesEngine` for a query. A dedicated worker thread (not the UI thread) performs an ordinal, case-insensitive substring/wildcard scan across the projection's interned filename table, evaluating each candidate against the compiled token list.

The scan is a straightforward linear pass, not an inverted (n-gram/trigram) index, and not a prefix trie. This mirrors Everything's/WizFile's own documented approach: a contiguous, cache-friendly array scan with a fast substring routine is fast enough at millions-of-entries scale on modern hardware, and — unlike an inverted index — requires no incremental maintenance cost every time the live filesystem changes (every create/rename/delete would otherwise need inverted-index upkeep on top of the projection's own incremental updates). Given the projection already changes continuously in the background, avoiding a second incrementally-maintained structure is a real simplicity win, not just laziness.

**Debouncing and cancellation:** input is debounced by a short fixed delay (tuned at implementation time, see Open Questions) after the most recent keystroke before a scan is dispatched. Each dispatch carries a monotonically increasing input-generation token (the same generation-counter idiom the foundation design already uses for snapshot generations). The scan loop checks this token periodically (once per chunk, not once per candidate, to keep the check cheap) and aborts early if a newer generation has superseded it; stale results that do complete are discarded on arrival rather than replacing the currently-displayed list. This keeps typing responsive without needing true OS-level thread cancellation.

**Alternatives considered:**
- *Trigram/n-gram inverted index for substring search* — rejected: its incremental-maintenance cost against a continuously-changing live index outweighs the benefit at this project's scale, and it's unproven to be necessary — linear scan is the approach of both named reference products.
- *Prefix-only (radix tree) index* — rejected: the proposal explicitly requires substring matching (`*.pdf` and mid-name matches like typing "report" and matching "Q3_report_final.docx"), which a prefix structure alone doesn't give.
- *Route the query through `FastFilesEngine` as an RPC* — rejected: would reintroduce exactly the per-keystroke IPC round trip the snapshot-mapping design exists to avoid; the whole point of D3 in the foundation design was that reads against the mapped snapshot happen with zero IPC.

### D3: Ranking — simple tiered match-quality heuristic, not an IR ranking model

Default "relevance" sort orders candidates by:
1. **Match tier** (best wins): exact filename match (case-insensitive) → prefix match → substring match elsewhere in the filename → (lowest) match only within the folder path, for tokens/scopes that search path text.
2. Within a tier, **shorter filename** ranks higher (a tighter, more specific match).
3. Still tied: **shorter full path** ranks higher.
4. Still tied: ordinal alphabetical order, for deterministic, non-flickering output.

**Why this and not a real IR ranking model (BM25/tf-idf):** filenames aren't documents — there's no meaningful term-frequency/corpus-statistics signal to exploit (a filename either contains the term or doesn't, usually once). A tiered match-quality heuristic is simple, fully explainable to a user glancing at why one result outranked another, cheap to compute per candidate (no corpus-wide statistics pass), and is explicitly an MVP choice: this is a file-search product feature, not a search-relevance research project.

**Alternatives considered:**
- *Full IR ranking (BM25/tf-idf)* — rejected: no real term-frequency semantics apply to short, mostly-single-occurrence filenames; adds a corpus-statistics maintenance burden for no proven benefit here.
- *No ranking, alphabetical only (Explorer's default)* — rejected: defeats the "feels smart" product differentiation goal from the proposal.
- *Fuzzy/edit-distance-weighted ranking* — rejected for this MVP: fuzzy matching itself is a non-goal (see Non-Goals); ranking shouldn't imply a matching capability that doesn't exist.

### D4: Search-to-navigation — walk the parent-directory-ID chain to reconstruct path segments, then drive Column View's existing population mechanism

Every entry in the in-memory projection carries a parent-directory-ID reference rather than a stored full path (per `index-storage-and-scanning`'s explicit memory-efficiency design). On result selection: walk `parent_id` upward from the matched entry until reaching the volume-root sentinel, collecting each directory's interned name; reverse the collected list to get root-to-entry order. This ordered segment list is then fed into `column-view-browsing`'s existing column-population primitive — the same one manual navigation uses — populating column 0 with the drive root's contents, auto-selecting the next segment to populate column 1, and so on, terminating with the matched file/folder selected in its containing column (files never get their own column, per that capability's existing "File and Folder Visual Distinction" requirement).

This is a read against the same in-process mapped snapshot as matching (D2) — no new IPC, no engine round trip.

**Race handling:** the chain can be invalidated between when a result was matched and when the user clicks it (a rename/move/delete lands via the engine's live incremental updates). If a segment can no longer be resolved partway through the walk, navigation proceeds as far as it can and the deepest reachable column shows `column-view-browsing`'s already-specified "no longer available" in-column state for the missing segment — this reuses existing, already-hardened error UX rather than inventing a parallel one.

**Alternatives considered:**
- *Store a full path string per entry and just split it* — rejected: directly undoes the memory-efficiency design decision in `index-storage-and-scanning` (interned strings + parent-ID references specifically to avoid repeated full-path storage across millions of entries).
- *Navigate only to the containing folder and highlight the file* — rejected: the proposal explicitly asks for full Column View hierarchy reconstruction (the Finder-style "drill all the way in" experience), not a flat jump.

### D5: Virtualized result rendering — fixed-row-height windowing over a single in-memory results array

`search-query-engine` produces one scored/sorted results array, owned in memory (not per-row UI objects). `search-ui`'s list view computes the visible row range purely from scroll offset and a fixed row height (`O(1)` index arithmetic, no per-row measurement pass) and only creates/updates Direct2D draw state for rows in that range (plus a small overscan buffer for smooth scroll). Off-screen rows have no realized UI element at all — memory and per-frame CPU cost scale with viewport size, not with total match count.

**Alternatives considered:**
- *Realize a retained UI element per result row* — rejected: doesn't scale to "thousands of matches," the explicit scale target from the proposal.
- *Paginate results (page 1 of N, click for more)* — rejected: an extra interaction step that works against the "feels instantaneous" product goal; continuous virtualized scroll is the closer match to both Column View's own feel and to Everything's/WizFile's result-list UX.

### D6: Search scope model — four scopes, narrowed (not hidden-without-explanation) under degraded mode

Scopes, from narrowest to broadest: **Current Folder**, **Current Folder + Subfolders**, **Current Drive**, **All Indexed Locations**. The active scope is always visibly indicated (never buried in a menu), consistent with the foundation design's own "non-modal, always-visible status" pattern for engine connection state.

- When the privileged path is `Active` (full whole-volume/all-volumes projection available), all four scopes are selectable; default is **All Indexed Locations** — the flagship "search everything instantly" experience.
- When the engine is in degraded mode (per the foundation design's D5, a permanent, first-class state — not an error), the projection only covers browsed/pinned directories. **Current Drive** and **All Indexed Locations** are disabled (visibly, with an explanatory tooltip), not merely absent, because either would otherwise silently under-report matches while looking like a complete result set. Default narrows to **Current Folder + Subfolders**.
- If the privileged connection transitions `Active` mid-session (matching `index-engine`'s existing reconnection state machine), previously-disabled scopes re-enable live, without requiring an app restart. If the user had explicitly selected a scope that becomes unavailable (privileged connection drops mid-session), the UI falls back to the next-narrower available scope with a one-time inline notice, rather than silently returning results scoped differently from what's displayed as selected.

Scope is enforced as an explicit parameter to the query execution (D2) — the matching scan restricts its candidate set (or, for a folder-rooted scope, its subtree of parent-IDs) before/while scanning, independent of how `search-ui` presents the picker. This keeps "what scope means" a query-engine concern and "how scope is presented/greyed out" a UI concern.

**Alternatives considered:**
- *No scope selector — always search everything available* — rejected: the proposal explicitly asks for a scope selector with clear indication, and "search just this folder" (mirroring Explorer's own in-folder search) is a distinct, common use case that global search would conflate away.
- *Silently fall back to whatever scope is available without telling the user* — rejected: directly risks the "looks exhaustive but isn't" failure mode this design is trying to avoid; every foundation-design precedent (status badges, explicit `IncompatibleVersion` replies) favors explicit, visible state over silent guessing.

### D7: Unicode and special-character correctness — ordinal comparison, UTF-16 throughout, no linguistic normalization

Filenames are already stored/interned as UTF-16 (`WCHAR`) in the in-memory projection; the search path never transcodes. Case-insensitive comparison and substring search use **ordinal** case folding (`CompareStringOrdinal`-equivalent semantics) rather than locale-sensitive linguistic casing (`CompareStringEx`/culture-aware collation) — matching Everything's own approach — so results are deterministic and don't vary by the user's Windows locale, and combining marks/accented characters are matched literally rather than being linguistically decomposed or recomposed.

Deliberately **no** Unicode normalization (NFC/NFD) is performed: NTFS itself does not normalize filenames on disk, so a filename containing a precomposed accented character and one containing a decomposed combining-mark sequence are, correctly, different byte sequences that should not be silently unified — doing so risks matching a file the user didn't mean.

Because matching operates on UTF-16 code-unit arrays, ordinary equality/substring comparison is correct without special-casing surrogate pairs (code-unit array equality is well-defined regardless of how code points map to code units). The one place surrogate pairs need explicit care is **computing display-safe ranges** — e.g., highlighting the matched span in a result row — which must never fall in the middle of a surrogate pair; this is a rendering-time boundary check, not a matching-correctness concern.

Reserved characters in the query grammar itself are deliberately minimal: `:` only inside a recognized key, `*`/`?` only as bare-token wildcards, `"` only for quoting. Since none of these (except `:`, which Windows filenames cannot contain) commonly collide with real filenames, free-text tokens containing spaces, parens, apostrophes, or accented characters need no escaping to match literally.

**Alternatives considered:**
- *ICU-based locale-aware collation* — rejected: unnecessary dependency and complexity for filename substring matching, where predictable literal matching is the more expected behavior, not linguistic equivalence.
- *Always-on Unicode normalization before comparison* — rejected: could introduce false-positive matches between genuinely different on-disk filenames; flagged as an explicit open question if real-world complaints surface.

## Risks / Trade-offs

- **[Risk]** A linear scan across the whole in-memory projection could slow down as indexed file counts grow into the tens of millions across multiple volumes, especially under the "All Indexed Locations" scope. → **Mitigation:** the scan is chunked and interruptible via the generation-token check (D2), so debounced typing stays responsive even if a single full pass takes longer than the debounce window; flagged as a scaling assumption to revisit with real benchmarking (see Open Questions) rather than pre-optimized away.
- **[Risk]** The simple tiered relevance heuristic (D3) will misrank some real queries — no fuzzy/typo tolerance, no path-token-aware weighting. → **Mitigation:** explicitly scoped as MVP; users always have explicit name/path/size/modified/created sort as an escape hatch; revisit if usage feedback shows it's inadequate.
- **[Risk]** Ordinal, non-normalizing matching (D7) will not match visually-identical filenames that differ only in Unicode normalization form (precomposed vs. combining-mark accents). → **Mitigation:** accepted trade-off, matching how Everything/WizFile themselves behave and how NTFS itself stores names unnormalized; tracked as an open question rather than silently "fixed" with always-on normalization that could introduce false positives.
- **[Risk]** Degraded-mode scope narrowing (D6) could surprise users if the privileged connection state changes mid-session and previously-available scopes disappear or the effective scope silently changes underneath an unattended search. → **Mitigation:** reuses the foundation design's existing connection-state-machine/status-badge pattern; scope changes are always visibly indicated, and an explicit one-time inline notice accompanies any forced scope fallback rather than a silent one.
- **[Risk]** Search-to-navigation's parent-ID walk (D4) assumes the chain is intact at click time; concurrent renames/moves/deletes between match and click can break it. → **Mitigation:** reuses `column-view-browsing`'s already-specified graceful in-column error states for exactly this class of race, rather than inventing new error UX; navigation proceeds as far as still resolvable.
- **[Risk]** Fixed-row-height virtualization (D5) is a simplifying assumption that would need revisiting if a later change introduces variable-height result rows (e.g., thumbnails, multi-line metadata). → **Mitigation:** documented as a current constraint; the core "never realize off-screen rows" principle survives a future move to variable heights, only the offset math would need an index/offset cache.
- **[Risk]** The filter-registry pattern (D1) adds one layer of indirection (value-parser + predicate-factory per key) versus a flat switch statement. → **Mitigation:** accepted deliberately — this is the specific mechanism that lets `kind:` hand off to `storage-analysis`'s future `file-type-categorization` capability, and lets later filter keys ship without touching the parser core; the indirection cost is small and one-time per key, not per query.

## Migration Plan

Greenfield, additive capability — no existing user data or schema to migrate. `instant-search` introduces exactly one new persistent artifact: a small local search-history store (per Decision in `search-ui`'s spec), which is local-only (not synced/roaming), user-clearable, and independently disable-able; there is no server-side or cross-machine component to migrate. No rollback concerns beyond "delete the local history file," since no other capability depends on its contents.

## Open Questions

- Should the query grammar eventually grow boolean operators (`OR`/`NOT`/parentheses) beyond implicit AND? Deferred; not in this MVP's scope per the proposal.
- Should relevance ranking eventually incorporate access-frequency/recency (an MRU-style boost, similar in spirit to Everything's run-count weighting)? Deferred; no such requirement in the proposal.
- The exact debounce interval (a fixed ~100–150ms vs. an adaptive value based on measured index size) is left to implementation-time tuning and benchmarking, not fixed by this design.
- The final `kind:` category taxonomy belongs to the future `storage-analysis` change's `file-type-categorization` capability; this change ships only a minimal built-in default set plus the extension point.
- How search scope/state interacts with `tabs`/`dual-pane` from the not-yet-built `navigation-and-workspace` change (e.g., independently-scoped search per tab) is deferred to that change; this design assumes a single active navigation surface.
- Whether Unicode-normalization-aware matching should be added as an opt-in later, if real-world users hit the precomposed-vs-decomposed accent mismatch described in Risks — deferred pending actual reports.
