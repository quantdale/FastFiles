## Review — Lane B: 5M search identity/ordering differential oracle design

### 1. API audit

**Oracle-driving API** (`src/indexstore/include/ffindexstore/Store.h`, impl `src/indexstore/src/Store.cpp`):

- `Store::OpenReadSession(const StoreReadSessionOptions&)` → `StoreReadSession` (Store.h:152; Store.cpp:966–972). Options carry the two seams the oracle needs: `faultInjector` (`ReadSessionFaultPoint::CandidateStreamError`, Store.h:22–33) and `candidatePlannerProbe` (`void(bool candidateRouteUsed, size_t countedCandidates)`, Store.h:36) — the probe is what proves *which arm* produced a stream.
- Canonical cold stream: `StoreReadSession::ForEachEntryUntil(VolumeRowId, EntryVisitorUntil)` (Store.cpp:1735) — full `entries` scan.
- Cold-with-index rung: `StoreReadSession::ForEachEntryWithAsciiNameUntil(VolumeRowId, asciiFoldedNeedle, EntryVisitorUntil)` (Store.cpp:1818). Three internal outcomes: staged FTS5 candidate rung (Store.cpp:2010–2014), atomic fall-through, and the LIKE/GLOB conservative prefilter stream (Store.cpp:2110–2115).
- Result row = `EntryRecord` (EntryRecord.h:17–28): `id`/`parentId` (`FileId{low,high}`, Identity.h), `name` (u16), `sizeBytes`, `allocatedSizeBytes`+`allocatedSizeKnown`, `creation/lastModified/lastAccessTime`, `attributes`. Identity key = `EntryKey{volumeRowId, frn}` (Identity.h:57).

**Ordering semantics — the critical audit finding.** Neither store stream has any `ORDER BY` (grep confirms the only `ORDER BY` in Store.cpp is `volumes ... ORDER BY id` at :1144). Canonical scan order is SQLite plan order; candidate rung order is FTS-postings ∪ exceptions join order (Store.cpp:2010–2014) — **the two raw streams can and do enumerate tied rows in different orders** (Store.h:222 even documents "unspecified but stable" order). Ordering is imposed **only at the search layer**: `ffsearch::ResultLess` (Search.cpp:79–124) is a total order — primary sort key → `CompareOrdinal(folder)` → `volumeId` → `id` — applied by `SortResults` (Search.cpp:126–129) in `SearchAccumulator::Finish()` and mirrored by the bounded top-N heap (Search.cpp:242–257). Because the chain terminates in unique identity, tied rows cannot sort differently across paths *for NTFS (frn.high == 0)*. Caveat: `MakeDurableCandidate` drops `id.high` (`candidate.id = record.id.low`, IndexPipeline.cpp:223), so two ReFS rows differing only in `frn.high` become a tie class with nondeterministic `std::sort` order — a real P2 ordering seam (not reachable on the 5M fixture).

### 2. Fixture

`build/campaign/open-5m.db` exists (with `-wal/-shm` sidecars); it is the `bench_store_open.cpp --build-fixture 5000000 --db …` open-matrix fixture (bench_store_open.cpp:13, 201–217), built through production `Store::ApplyBatch` with candidate maintenance ON, using the 10-pattern synthetic names (bench_store_open.cpp:188–198).

**Read-only requirements:** `Store::Open` is a *writer* open — integrity check plus, if `state≠1 OR mapped≠entries OR fold_signature mismatch`, a full `RebuildCandidateIndex` that mutates the file (Store.cpp:901–945). A healthy same-host fixture skips rebuild, but `Store::Open` still touches WAL/header; the read-only-attribute case makes it fail closed (bench_store_open.cpp:617–622). `StoreReadSession` itself is strictly read-only (`SQLITE_OPEN_READONLY` + `PRAGMA query_only=ON` + `BEGIN`, Store.cpp:1750–1775), but `OpenReadSession` requires an already-`Open`ed `Store` (Store.cpp:968). **Conclusion:** the oracle must run against a scratch copy (SHA-256 the golden before/after as belt-and-braces), open once via `Store::Open` (assert `OpenPhaseTimings::candidateIndexRebuilt == false`), then drive both rungs through sessions. The candidate route additionally requires `entry_name_index_meta` state=1 and host fold-signature match (Store.cpp:1791–1816) — satisfied on the building host.

**Side-set caveat:** the fixture's only non-ASCII names are Cyrillic `список_N.txt` (bench_store_open.cpp:197) — non-exceptional on this host and no surrogates, so `entry_name_exceptions` is near-empty (consistent with the acceptance note). The "exceptional side-set / surrogate" targeted classes need the injected-host seam (`FoldClassification.h` testing seam) or a small supplemental fixture; they cannot be measured on open-5m.db as-is.

**Scaling `test_store_read_session.cpp`:** its differential tests (e.g. `TestAsciiNamePrefilterIsConservativeAndPrecise`, :236–320) build tiny fresh DBs and are CTest-gated; scaling them to 5M inside CTest is wrong (88-min fixture build, timeouts). Keep them as-is; add a bench executable per repo convention (`bench_<behavior>.cpp`, registered like `ffindexstore_bench_store_open`, tests/indexstore/CMakeLists.txt:60–62 — **not** `add_test`).

### 3. Oracle design

**Files to create:**
1. `tests/indexstore/bench_identity_ordering_oracle.cpp` → target `ffindexstore_bench_identity_ordering_oracle` (links `ffindexstore ffsetup sqlite3 psapi bcrypt`, mirrors :60–62). Store-level: exact identity surface, both rungs, fault/cap/tamper arms.
2. `tests/engine/bench_search_identity_ordering.cpp` → links `ffengine`, drives `IndexPipeline::SearchIndexed(const ffprotocol::IndexedSearchRequest&, SearchTimings*, cancelled)` (IndexPipeline.h:372) for search-level ordering/caps across `SortField` × descending × `maxResults`, following `bench_large_index_scalability.cpp` conventions (`--output`, `--source-sha`, `--source-state` identity embedding).

**Command lines:**
```
cmake --build --preset release-test --target ffindexstore_bench_identity_ordering_oracle ffsearch
.\build\release-test\tests\indexstore\ffindexstore_bench_identity_ordering_oracle.exe --db build\campaign\open-5m.db --scratch %TEMP%\ff-oracle --output build\campaign\oracle-5m.json --source-sha <sha> --preset release-test
.\build\release-test\tests\engine\bench_search_identity_ordering.exe --db build\campaign\open-5m.db --output build\campaign\oracle-5m-search.json
```

**Procedure per query class** (12 classes = needle table at bench_large_index_scalability.cpp:2126–2140: `unique_component_7391`, `report`, `common`, `zzzz_never_present`, `photo`, `ext:txt`, `component`, `unique_component_7391.txt`, `co`, `CoMmOn_CoMpOnEnT`, `список`, `common`@50k; plus targeted classes):

- **Reference arm R:** `ForEachEntryUntil` + in-harness canonical matcher (same `CompareStringOrdinal(…, TRUE)` semantics as `Query::Matches`) + identical `SearchRequest`.
- **Index arm I:** `ForEachEntryWithAsciiNameUntil` with the class needle; **the cell is valid only if `candidatePlannerProbe` reports `candidateRouteUsed==true`**; otherwise it exercises fallback (record separately). A third recorded arm uses a tamper scratch copy (`meta.state=0`, `fold_signature+1`) to force the fallback branch of the same API.
- **Sequence representation:** for capped runs (`maxResults ∈ {1,2,17,4096,50000}`) keep the full ordered tuple vector; for full sequences stream tuples `(volumeRowId, frn.low, frn.high, parentId.low/high, name16, size, alloc+known, ctime, mtime, atime, attributes)` to a scratch file in delivered order and compare BLAKE3/SHA-256 digests + counts (bounded memory), dumping the first divergent index on mismatch.
- **Verdicts:** `IDENTITIES_EQUAL` — multiset of identity keys equal (compare hash of sorted key list; catches duplicate delivery); `ORDERING_EQUAL` — element-wise equal sequence **after** `ResultLess` sort, for every `SortField` × direction (tie-heavy `Size`/`Modified` sorts exercise the tie-break chain directly); `CAPS_EQUAL` — equal `results.size()` **and** equal `truncated` flag **and** retained set == top-N of the uncapped reference under `ResultLess`; `NO_SILENT_TRUNCATION` — both arms return `true`, `Failed()==false`, post-matcher row count == reference count, equal `cancelled` status (cancelled ⇒ cleared results, per Search.cpp `Finish()`).
- **Cancellation:** rerun each class with the `cancelled` callback firing at deterministic `chunkSize` boundaries; assert equal verdict and that visitor early-stop returns `true` (bounded stop, Store.h:231–234).
- **Cap-invariance sweep:** repeat with `SetTestingCandidateCapOverride` ∈ {0, 1, 3, 16383, 16384, 16385} (Store.h:81) — identity/order digests must be byte-identical across caps; only `countedCandidates`/route may change.
- **Fault arm:** `CandidateStreamError` mid-stream with staged delivery ON (default, Store.cpp:412): assert delivered sequence == R with zero leaked/partial rows; with `SetTestingCandidateStagedDelivery(false)` assert the oracle *detects* the documented duplicate hazard (red-first discrimination).
- **Hot caveat:** at 5M the projection is cold by design (500 MiB budget; acceptance Stage A) — the authoritative reference **is** the canonical cold scan; I-vs-R is the acceptance gap. Run the hot leg (`ExecuteSearchViews`/`AdmitView`) only at ≤2M or via `Projection::EvictVolume` forced residency states, where hot-vs-cold identity/order is meaningful.

### 4. Risk seams (each mapped to an assertion above)

| Seam | Location | Oracle assertion |
|---|---|---|
| Query-time readiness gate (one-shot cache) | Store.cpp:1791–1816 | tamper cells: identical results, probe=false, no truncation |
| Staged buffer overflow → atomic fall-through | Store.cpp:2047 (`staged.size() >= cap`) | fault arm: zero partial deliveries, result == R |
| Cost cap 16,384 + conservative sum double-count | Store.cpp:424, 1901–1930 | cap-invariance sweep (cost-only knob) |
| Volume-scoped admission stage 2 | Store.cpp:1945–1995 | multi-volume fixture (`--volumes 3`): per-volume probe + identity |
| Hot `AdmitView` truncated flag | Search.cpp:158–223 | `truncated == (matches > maxResults)` both arms |
| `id.high` dropped in tie-break | IndexPipeline.cpp:223, Search.cpp:122 | P2 note; ordering determinism only for 128-bit IDs |
| `AdmitView`/heap top-N | Search.cpp:242–257 | `CAPS_EQUAL` retained-set check |

**Fixed:** n/a (read-only). **Findings:** P2 — ReFS `frn.high` collisions break `ResultLess` totality (report-only; NTFS-only fixture unaffected); P2 — open-5m.db cannot exercise the exceptional side-set on this host (needs injected-host seam or supplemental fixture).

**Merge verdict: OK with notes** — design is implementable with existing seams (`candidatePlannerProbe`, fault injector, cap override, staged-delivery toggle) and no production-code changes; run on a scratch copy and gate every cell on probe-verified routing.