# Phase H — projection memory: remeasure and duplication classification

Candidate: release-test binaries embedded at `0936b1e` (built on the clean
tree; no production source changed in this campaign). Measurement host was
user-saturated (load 86-91) at run time — byte-level working-set/private
evidence is not wall-clock sensitive, so these numbers are valid; latency
columns of the same run are NOT used as evidence anywhere.

Run: `ffengine_bench_large_index_scalability --scales 200000 --diagnostics`
(synthetic 200,000-row SQLite fixture through the real rebuild/search/snapshot
paths). Raw output: `build/_apply26/mem_200k.json` (untracked scratch); the
durable numbers are below.

## Task 8.1 — current-candidate remeasure (200,000 rows)

Measured process private-bytes (working-set evidence, `GetProcessMemoryInfo`):

| stage | private bytes | delta | note |
|---|---:|---:|---|
| after fixture DB generation | 143,450,112 | — | SQLite ingestion process state |
| after database open (empty projection) | 6,082,560 | baseline | SQLite handle resident |
| after rebuild (projection resident) | 115,867,648 | **+109,785,088** | the resident projection at 200k rows |
| after broad query | 122,982,400 | +7,114,752 | query results/transient query state |
| post-query (stable) | 125,288,448 | +2,306,048 | retained result set |

Derived: **resident projection ~= 549 bytes/row measured** (109.8 MB private
delta over 200,000 rows), vs the historical 329.2 bytes/entry figure which was
an `EstimatedMemoryBytes` estimate, not a measured resident set. The two are
different quantities (estimator sum vs process private delta) and are not
averaged or reconciled into one number. The historical figure stays as
reference only, per the spec ("Current memory is not inherited").

Snapshot/UI layers at the largest bounded export probe (32,277,860 serialized
bytes, ~1.98M snapshot rows across the bounded export cap):

| stage | private bytes | delta |
|---|---:|---:|
| before model | 6,365,184 | — |
| after model (export map built) | 58,056,704 | +51,691,520 |
| after publish (mapped section committed) | 58,093,568 | +36,864 |
| after UI parse (owning map) | 58,359,808 | +266,240 |

Observations: the engine-side export map is the large transient consumer
(~51.7 MB at this probe size); the published mapping is shared (not private);
the probe's UI parse delta is small because the probe process parses into a
map that shares the mapped view's page cache at this scale (the production UI
parse cost is separately measured by the first-generation bench's
`client_parse` stage).

Remaining (queued behind the campaign's idle-gate wait for wall-clock work; a
1M-row remeasure and an allocation-instrumented breakdown pass will be added to
this record before 8.3's decision):

- 1,000,000-row remeasure for the bytes/row curve.
- Per-category breakdown (rows+vector capacity slack, idToIndex/parentToChildren
  maps, name arena, filename trigram postings + non-ASCII list, LRU metadata,
  free list). `Projection::EstimatedMemoryBytes` (Projection.cpp:89-105)
  computes exactly these categories from measured capacities; a probe will
  report the category sums at the same scale, and the residual against the
  measured private delta will be recorded as allocator overhead/fragmentation
  rather than silently absorbed.

## Task 8.2 — duplication classification (source-traced)

Long-lived copies of the same logical names/rows, per the code:

| layer | representation | lifetime | classification |
|---|---|---|---|
| SQLite `$MFT` store | on-disk rows + name text | durable | **intentional** — the durable source of truth; 14.9 MB on disk at 200k rows |
| `Projection` rows (`entries_` vector) | `ProjectionEntry` (ids, sizes, times) | engine lifetime, evictable under pressure | **intentional** — the indexed query plane |
| `Projection::NamePool` | interned UTF-16 arena (offset/length) | shared with rows | **intentional** — already deduplicates name text; the name string exists ONCE here |
| filename trigram postings + non-ASCII list | `unordered_map<u64, vector<u32>>` | engine lifetime, rebuilt on invalidation | **intentional for speed** — the secondary accelerator; per 8.3 it is not removable if it slows common selective search |
| LRU `lastAccess_` metadata | per-entry key+timestamp hash map | engine lifetime | **intentional for bounded memory** — drives eviction; not a name copy |
| export map (`std::map<wstring, SnapshotDirectory>`) | owning path keys + owning `wstring` names | transient (one publication) | **transient** — exists only between `ExportDirectorySnapshotBounded` and `MergeIndexDirectories`; copies every name out of the arena |
| serialized snapshot section | length-prefixed bytes in mapped memory | one per generation (current mapping only) | **transient/shared** — intentionally the zero-IPC read plane |
| UI parse map (`ParseSnapshot` result) | owning path keys + owning names | per parsed generation in the client | **transient** — second owning copy of every name |
| search results | per-query row references/ids | query-scoped | **transient** — ids and small result sets, not name copies |

No accidental duplication found in the name plane: the arena interns names, and
the two owning copies (export map, UI parse map) are transient by design —
they are also exactly the two per-entry allocation sites the D5 isolation
experiment measures. The one structural observation for 8.3: at any instant a
name can exist up to three times (arena + export map + parsed UI map), but never
more, and the outer two are bounded by one publication cycle each. The engine
keeps only the current mapping (SnapshotPublisher contract), so generations do
not accumulate.

8.3's retain/reject decision is deferred until the breakdown pass and the
1M-row remeasure above are complete; no memory change is justified yet.
