# Implementation evidence — allocated-size-search-filter

## 1. Registry and parser

- `FilterRegistry::WithDefaults()` registers exactly one new default key `ondisk` (`src/search/src/Query.cpp:373`, `src/search/include/ffsearch/Query.h:78` `PredicateKind::AllocatedSize`).
- `ondisk:` reuses `ParseSizeFilter` — the same `SizeFilter` grammar as `size:`: operators `= > >= < <=`, inclusive `..` ranges, overflow checks via `ParseSize`, units `B/KB/MB/GB` (powers of 1024). No `allocated` alias.
- Malformed values (`ondisk:banana`, empty, `10XB`, overflow `18446744073709551616GB`) enter the existing `invalidFilters` path; `ondisk` is not reported as `unrecognizedKeys` (`test_ondisk_filter.cpp` §4.8). Misspelled keys remain `unrecognizedKeys`.
- Diagnostics attribution: `PredicateKind::AllocatedSize` is distinct from `PredicateKind::Size` (`Query.cpp:443-447`, `571`). `EvaluateViewPredicates` counts both under `metadataPredicates`/`metadataMatchingMs` (no silent conflation).

## 2. Predicate semantics

Both predicate factories gate on `allocatedSizeKnown` and read only `allocatedSizeBytes`:

- Owning: `MakeAllocatedSizePredicate` (`Query.cpp:266-283`) — `if (!c.allocatedSizeKnown) return false`.
- View: inline lambda in `WithDefaults()` (`Query.cpp:373-386`) — identical logic, using `CandidateView::allocatedSizeBytes/Known` (two scalars already present, no path materialization, `requiresFolder==false`, `viewCompatible==true`).

Truth table (deterministic, `tests/search/test_ondisk_filter.cpp`):

| Candidate | Logical | Allocated | Known | Expected | Evidence |
|---|---:|---:|---|---:|---|
| known-large | 1 MiB | 8 MiB | true | `ondisk:8MB` ✓, `ondisk:>7MB` ✓, `>8MB` ✗, ranges ✓ | §4.1-4.3 |
| known-zero | 512 KiB | 0 | true | `ondisk:0B` ✓, `<=0B` ✓, `>0B` ✗, `0B..1KB` ✓, `1B..1KB` ✗ | §4.4 |
| unknown-zero-storage | 2 MiB | 0 | false | every numeric `ondisk:` ✗ (incl. `0B`, `>=0B`, ranges) | §4.5 |
| unknown-stray | 9 MiB logical | 8 MiB stored | false | every numeric `ondisk:` ✗ (never falls back to logical or stray bytes) | §4.5 |

Additional:
- `size:` remains logical (`sizeBytes` only); a fixture where the two dimensions disagree proves the filters select different rows (`test_ondisk_filter.cpp` §4.6).
- Implicit AND: `ext:iso ondisk:>1GB` and free-text + `ondisk:` range require both predicates (`§4.7`).
- Scope: `CurrentDrive` vs `AllIndexedLocations` still respected under `ondisk:` filtering (`§4.7`).
- Owning vs view equivalence: sweep over 10 queries x 6 candidates asserts `Matches == MatchesView` and `MatchesWithoutFolder == MatchesViewWithoutFolder` (`§4.9`).

## 3. Engine / degraded integration

`tests/engine/test_index_pipeline.cpp:TestIndexedSearchOndiskFilter`:

- Fixture: 4 projection candidates — `known_large` (logical 1 MiB, allocated 8 MiB), `known_zero` (512 KiB, 0 known), `known_small` (10 MiB, 1 MiB known), `unknown_stray` (9 MiB logical, unknown). Source: V2 records for known (allocated authoritative), V1 for unknown (stays unknown).
- `ondisk:8MB` exact, comparisons, `<=0B`, zero-range, `unknown` never matches, `size:` vs `ondisk:` divergence, `extendedEntries` implicit AND (`ext:iso ondisk:>1MB`), scope, cancellation — all exercised via `IndexPipeline::SearchIndexed` (projection-backed).
- Accelerated view path remains eligible (`viewCompatible==true`); `ondisk:` is metadata-only and does not add eager path reconstruction.
- `SearchTimings` diagnostic: `ondisk:>1MB` and `size:>1MB` traverse the same `candidateCount`; no second pass.
- Cancellation still chunk-bounded; superseded queries discard partial results.
- Degraded/snapshot source already sets `allocatedSizeKnown=false`; numeric `ondisk:` on degraded candidates produces zero matches (the correct "cannot prove" state, not known-zero). No DB/FS/service/IPC/protocol work added.

## 4. Wire and protocol

No wire-format change. The indexed-search request already carries the raw query string; `ondisk:` is parsed engine-side by the shared `ffsearch` library. No new message ID, struct version, response layout, or compatibility branch. Byte-pinning tests (`tests/protocol/test_protocol.cpp`) unchanged and green.

## 5. Live Search UIA (actual, `build/debug-diagnostic`, `FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED=ON`)

No production signing, trust-store, or service-install campaign.

Changes: `verify/capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1:Invoke-UiaScenarioSearchAllocatedSize` now extends the existing `search-allocated-size` isolated scenario (still within the same lifecycle as the accepted Size-on-Disk sort proof) to additionally demonstrate, on the normal degraded development path where every candidate is `UNKNOWN`:

1. `alloc` (fixture term) discovers 5 deterministic fixture files (`allocA-E.txt`, distinct logical sizes).
2. `ondisk:>=0B` is accepted as a recognized structured filter (not unrecognized) and returns zero fabricated matches from UNKNOWN rows — empty `ListItem` set handled via `UiaTargetNotFound` catch.
3. Restoring `alloc` re-establishes the 5 fixture results under current generation/cancellation rules.
4. The existing `Size on Disk` ↔ `Size` sort-control toggle (combo 7103 via `ExpandCollapse`/`SelectionItem` on `BoundingRectangle` + `SelectionPattern` verification) proves alphabetical when all unknown vs logical ascending.

Validation (this session, local interactive host):

- Isolated `search-allocated-size`: 10/10 PASS (`verify/runs/ad-hoc/20260820-013115` … `013738`).
- Full `windows-ui-automation-validation`: 23 PASS / 4 legitimate SKIP / 0 FAIL (`verify/runs/ad-hoc/20260820-012613` and subsequent full run). Legitimate SKIPs: `storage-mid-scan-calculating`/`search-toggle-service-while-open` (privileged scan unavailable), `in-column-error-state`/`drag-in-from-explorer` (engine-connected / Explorer drag source unavailable) — preserved, not converted from FAIL.

Known/zero/stale/replacement wire contracts remain proved headless by `tests/ui/test_search_uia.cpp` and `tests/ui/test_search_metadata.cpp` (complementary, per prior change note).

## 6. Performance / regression

No algorithmic regression: `ondisk:` uses the same two scalar fields and the same single projection traversal as `size:`. `SearchDiagnostics` / `IndexPipeline::SearchTimings` show identical `candidateCount` for comparable metadata-only queries; no extra `pathLookups`, no second pass, no per-candidate SQL/FS/service/IPC. Result/page caps, wire response width, request layout, and protocol IDs/versions unchanged. The accepted `indexed-search-allocated-size` wire delta (+8 bytes/candidate V2) is unaffected.

## 7. Validation matrix

| Gate | Result |
|---|---|
| `ffsearch_ondisk_filter_tests` (new) | PASS |
| `ffsearch_tests` (query grammar, incl. size overflow, unrecognized keys) | PASS |
| `ffsearch_scan_tests` (scope, cancellation, sort, history, large-index) | PASS (incl. AllocatedSize sort matrix unchanged) |
| `ffengine_index_pipeline_tests` (incl. `TestIndexedSearchOndiskFilter`) | PASS |
| Full Debug CTest | PASS 66/66 |
| Full release-test CTest | PASS 66/66 (1 transient `ffengine_client_handshake_tests` `error 5` under UIA process leak — environmental, not owned by this change; isolated retry PASS, subsequent debug run 66/66) |
| Analyze `/W4 /WX` | PASS warning-clean (stack-size warning suppressed via `#pragma warning(disable:6262)` on the dedicated test) |
| `pwsh ./verify/verify.ps1 build` | PASS (status=PASS) |
| `pwsh ./verify/verify.ps1 doctor` | PASS (all required tools present; SKIPs are uninstalled diagnostics only) |
| source-marker | PASS |
| diagnostic-policy | PASS (0 failures) |
| `openspec validate --all --strict` | PASS 56/56 (after MODIFIED spec now preserves all base scenarios) |
| `git diff --check` | PASS (LF/CRLF warning on licensed file only) |
| UIA `search-allocated-size` isolated | 10/10 PASS |
| Full UIA capability | 23 PASS / 4 SKIP / 0 FAIL |
