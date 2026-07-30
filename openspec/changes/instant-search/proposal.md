## Why

Search is the product's primary reason to exist and must feel instantaneous even across millions of indexed files — this is what makes FastFiles fundamentally different from Windows Explorer's search. This change builds the actual search experience on top of the index established in `index-storage-and-scanning`.

**Sequencing dependency:** assumes `index-storage-and-scanning` is complete for full-speed, whole-volume results. Until then, search can still operate against `index-engine`'s degraded-mode (directory-walked) data at reduced scope and speed — this is an acceptable interim state, not a blocker.

## What Changes

- Implement filename search with partial/substring matching and search-as-you-type, debounced so the UI stays responsive.
- Implement structured query filters (`ext:`, `size:`, `modified:`, `kind:`, `folder:`, `name:`) with a parser designed so additional filter types can be added later without redesigning the query engine.
- Implement wildcard pattern support (`*.pdf`, `*.py`, etc.).
- Implement search scopes — Current Folder, Current Folder and Subfolders, Current Drive, All Indexed Locations — with the active scope always visible to the user.
- Implement a result list showing filename, type, location/path, size, and modified time, sortable by relevance, filename, path, size, modified date, and created date.
- Implement search-to-navigation integration: selecting a result reconstructs and displays the full Column View hierarchy down to that file, not merely its containing folder.
- Implement local, configurable search history.
- Ensure correct handling of Unicode, spaces, and special filename characters in both queries and results.

## Capabilities

### New Capabilities
- `search-query-engine`: Query parsing, partial/wildcard matching, structured filters, ranking and sorting — independent of any UI.
- `search-ui`: The search box and debouncing, virtualized result rendering for large result sets, scope selection, search history, and the search-to-navigation integration.

### Modified Capabilities
(none — additive on top of existing capabilities; does not change `column-view-browsing`'s existing requirements, only adds a new entry point into it)

## Impact

- Depends on `filesystem-index-store` (from `index-storage-and-scanning`) for full-scope, high-speed results.
- Depends on `column-view-browsing` for the navigation surface search results resolve into.
- No changes to file-operation or storage-analysis behavior.
