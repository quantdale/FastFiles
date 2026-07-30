## Context

`establish-architecture-foundation` stood up the three-process architecture, the hardened `FastFilesEngine ↔ FastFilesIndexSvc` protocol, and the degraded-mode fallback — but deliberately stubbed `StartVolumeScan`/`OpenUsnJournal` and left the durable index storage shape as an open question, precisely so that decision wouldn't block standing up the process skeleton. `instant-search` and `storage-analysis` are both scoped assuming a real, incrementally-updated, whole-volume index already exists; this change is what makes that true.

Concretely, this change has to answer four questions the foundation change left open:
1. What holds the index durably, across restarts and crashes, without corrupting itself?
2. What does the engine actually keep in RAM for zero-IPC-round-trip search/browse, and how does that stay small at millions of entries?
3. How does the in-memory copy get built at startup and stay in sync afterward, fed by the service's raw MFT/USN stream?
4. What happens when a scan is interrupted, a volume disappears, or events are missed — how does the index self-correct without a full rescan and without corrupting unrelated data?

This document also assumes the foundation change's D4 security model as fixed (allowlisted `$STANDARD_INFORMATION`/`$FILE_NAME` fields only, `FileReferenceNumber`-keyed identity, filename canonicalization at ingestion, `SeBackupPrivilege`-only raw volume access) and builds the real scanning/journaling logic on top of it rather than revisiting it.

## Goals / Non-Goals

**Goals:**
- Decide and justify the durable storage engine (SQLite/WAL vs. alternatives) for this specific workload.
- Define a compact in-memory projection design that scales to millions of entries without per-entry full-path strings.
- Define how the projection is built at startup and kept incrementally synchronized from the service's streamed scan/journal records.
- Define a reconciliation strategy that self-corrects missed events without a full manual rescan.
- Define resumable-scan semantics so an interrupted scan doesn't restart from zero.
- Define volume lifecycle handling (disconnect/reconnect) that never corrupts unrelated data.
- Define identity/dedup rules (volume identifier + `FileReferenceNumber`) that make symlink/junction/reparse-point cycles a non-issue.
- Replace `FastFilesIndexSvc`'s stubbed scan/journal responses with real MFT parsing and USN journal reads, within the already-established security boundary.

**Non-Goals:**
- Search query parsing, ranking, filters, or any UI (`instant-search`'s scope).
- Treemap/rollup size computation, category classification, or any storage-analysis UI (`storage-analysis`'s scope) — this change persists per-entry size/time/attribute data; aggregating it is a follow-up concern.
- Redesigning the Engine↔Service IPC transport, framing, or security model established in `establish-architecture-foundation` (this change extends the closed command surface minimally — e.g., an optional resume argument — it does not reopen the transport design).
- File operations (copy/move/delete/rename) of any kind.
- Enterprise/managed deployment and per-user ACL-aware index filtering (still open questions from the foundation change, untouched here).

## Decisions

### D1: SQLite (WAL mode) as the durable, crash-safe source of truth

**Decision:** `FastFilesEngine` persists the authoritative index in a single-file SQLite database, opened in WAL (write-ahead log) journal mode.

**Rationale:**
- **Embedded, no server.** Matches the product's whole posture — one bundled dependency inside an unprivileged process, no separate service, no install-time server setup, consistent with WizTree/WizFile shipping as self-contained tools.
- **Concurrent read-while-write.** WAL mode lets background readers (the reconciliation sweep, diagnostics, an eventual "show me what the persisted store thinks" debug view) run concurrently with the ingestion pipeline's writer, without blocking each other — exactly the access pattern this component needs (ingest is effectively always-on; reads must never stall behind it).
- **Batched write throughput is good enough.** WAL commits append small frames to the WAL file rather than rewriting/fsyncing the whole database; combined with explicit multi-row transactions (batching hundreds to low-thousands of USN/scan records per commit, matching how the service streams batched records over the pipe), this comfortably absorbs USN-journal-rate churn on a single desktop volume without per-row fsync cost.
- **Well-understood crash safety.** WAL replay on next open is a solved, heavily-exercised code path; `PRAGMA integrity_check` gives a cheap way to detect corruption defensively. This matters because the durable store is the thing everything else rebuilds from — its crash-safety story cannot be improvised.
- **Expressiveness for reconciliation.** Reconciliation sweeps and volume-lifecycle bookkeeping are naturally expressed as SQL (anti-joins to find rows with no on-disk counterpart, existence checks, bulk status updates) rather than hand-rolled traversal logic over a custom format.
- **Debuggability.** A single-file, SQL-queryable store can be opened with off-the-shelf tools (`sqlite3` CLI, DB Browser for SQLite) during development and support — valuable for a from-scratch native app with no other persistence layer to lean on.

**Alternatives considered:**
- **Custom flat/append-only file format.** Could plausibly beat SQLite on raw sequential write throughput and avoid SQLite's per-row overhead, but that's the wrong axis to optimize — the hard parts of a durable store (crash-consistent commit, secondary lookup by parent, safe concurrent read-during-write, corruption detection/repair) would all have to be built and hardened from scratch, for a workload that isn't write-throughput-bound in the first place (USN churn on a desktop volume is nowhere near SQLite's ceiling). High implementation/maintenance risk for a benefit this workload doesn't need.
- **LMDB.** A closer call — mmap-based reads and MVCC are a genuinely good fit for concurrent readers. Rejected because: (a) LMDB is a single-writer copy-on-write B+tree, and its write amplification under many small, scattered updates (the realistic shape of live USN churn: individual file renames/writes/deletes scattered across the key space, not appended in key order) grows the file faster than a WAL-mode SQLite database under the same workload; (b) it has no query language — every lookup pattern (children-of-parent, orphan detection for reconciliation, volume-status bulk updates) would need hand-rolled index maintenance identical in spirit to the "custom flat file" option, just with a B+tree instead of a log; (c) meaningfully smaller tooling/debugging ecosystem than SQLite for a project that will already be hand-building nearly everything else (Win32/COM/Direct2D per the foundation change's D1) and can't afford to also hand-build persistence-layer tooling.

SQLite's write-throughput ceiling and its being a linear WAL rather than a true log-structured store are real limitations in the abstract, but neither is the bottleneck for a single desktop volume's index churn — the choice is won on crash-safety maturity, concurrent-read support, and query expressiveness, not raw throughput.

### D2: Compact in-memory projection — parent-reference + interned names, not per-entry full paths

**Decision:** The in-memory snapshot that search/browse/storage-analysis actually read is a dense, struct-of-arrays-style table of fixed-size entry records, each holding a reference to its parent entry and an interned name id — never a stored full path string.

Roughly, per entry:
- `FileReferenceNumber` (wide enough for NTFS's 64-bit and ReFS's 128-bit file IDs) plus a reference to its volume's identity.
- `ParentFileReferenceNumber` — the containing directory's identity, not a path.
- `NameId` — a 32-bit index into a single, process-wide interned/deduplicated name pool (a contiguous string arena plus offset/length table, deduplicated via hash lookup at ingestion time so identical name strings anywhere in the tree — `index.js`, `.git`, `node_modules`, a given file extension — share one entry regardless of how many directories contain a same-named child).
- Size, last-write time, and attribute flags (fixed-width fields).

A full path is reconstructed on demand (walking `ParentFileReferenceNumber` links up to a volume root, concatenating interned names) only when one is actually needed — displaying a result, opening a file, building a Column View breadcrumb — never stored per entry.

**Why naive full-path-per-entry storage doesn't scale:** at the entry counts this product targets (a large NTFS volume commonly has several million to tens of millions of MFT records), storing a full path string per entry means every entry redundantly repeats its entire ancestor chain — `C:\Users\...\node_modules\...` duplicated verbatim across every one of a directory's thousands of siblings and again across every subtree that happens to share a prefix. At even a modest ~80–120 bytes average path length, 5 million entries is 400–600 MB of almost entirely redundant string data before accounting for per-entry fixed fields at all — and it still has to be rebuilt/re-scanned for the substring/prefix work search needs. Interning collapses that redundancy at the level it actually occurs (repeated *name segments*, not repeated *paths*): the unique-segment count across a real filesystem tree is a small fraction of the total entry count (the same names recur constantly — `.git`, `bin`, `node_modules`, common source/binary filenames), so the name pool stays a bounded, small structure while the per-entry cost drops to a handful of fixed-width fields (on the order of 40–50 bytes/entry) — roughly an order of magnitude smaller than per-entry full paths at this scale, and every field is fixed-size, so the entry table itself can be a flat, indexable array rather than a heap of variable-length records.

This shape also matches the access patterns the product actually needs: Column View and directory listings are "give me all children of parent X," which is a lookup keyed on `ParentFileReferenceNumber` (backed by a parent→children index built at projection time), not a string operation; only when a human needs to *read* a path (display, open, breadcrumb) does the ancestor walk happen, and it's O(depth), not O(entries).

**Alternatives considered:** storing pre-materialized full paths was rejected outright for the RAM cost above. A per-directory (rather than global) name pool was considered — it would avoid a global hash lookup at ingestion — but a global pool captures far more duplication (the same filename recurs across unrelated directories constantly) for a bounded extra cost (one hash-map insert per newly-seen distinct name, not per entry), so it wins on memory at the cost of a small amount of ingestion-time CPU.

### D3: SQLite stores denormalized rows; interning is an in-memory-only optimization

**Decision:** The SQLite schema stores each entry's name as a plain string column (with parent identity as a plain foreign-key-shaped column), not as an interned/id-referenced name — interning happens only when building or updating the in-memory projection.

**Rationale:** RAM is the scarce resource the projection is optimized for; disk is not. Interning on the SQLite side would require a disk-resident name pool with reference counting (to know when a name can be reclaimed) and would turn every ingestion write into a two-table operation (pool lookup-or-insert, then entry upsert) for a resource (disk space) that isn't under the same pressure. Keeping SQLite denormalized keeps ingestion writes to simple per-record upserts and keeps reconciliation queries (which need to compare "what's in the DB" against "what's on disk") straightforward string/identity comparisons without an extra join.

### D4: Startup rebuild and incremental synchronization

**Startup:** on `FastFilesEngine` launch, the projection is rebuilt from SQLite by streaming rows out in a single pass, allocating dense in-memory entry slots as rows arrive, interning each row's name into the pool (deduplicating), and building the parent→children index. Row counts are known up front (tracked in the `volumes` metadata — see D6), so the entry table and name pool can be pre-sized to avoid incremental reallocation during the bulk load. Each volume's rebuild is published as its own snapshot generation as soon as that volume's rows are loaded, rather than gating the very first published snapshot on every volume finishing, so a UI window opened while a large volume is still loading still gets a usable (partial) view immediately.

**Incremental sync thereafter:** every batch of records the engine ingests — whether from an initial `StartVolumeScan` or a live `OpenUsnJournal` stream — goes through one ingestion pipeline: (1) the batch is written to SQLite inside an explicit transaction; (2) only once that transaction has durably committed is the same batch applied to the in-memory projection (insert new entry / update changed fields / remove deleted entry / re-parent on move, interning any newly-seen name); (3) the projection's snapshot generation is bumped and published via the existing double-buffered memory-mapped mechanism, with subscribed UI clients notified over the control pipe exactly as `establish-architecture-foundation` already defines. Committing to SQLite strictly before applying to the projection is deliberate: if the engine crashes between steps, the worst outcome on restart is "a few already-durable records get re-applied to a freshly rebuilt projection" (a harmless no-op, since projection rebuild always starts from SQLite), never "the live view shows something the durable store never actually recorded."

### D5: Reconciliation sweeps

A low-priority, periodic (interval configurable; paced/idle-scheduled rather than running at full I/O priority) reconciliation pass runs per volume, comparing the persisted index against ground truth and correcting drift: entries present in the index but no longer resolvable (deleted, since the last observed event), entries present on disk/in a fresh scan but missing from the index (a missed create event), and stale fields (a missed rename/attribute-change event). This is the self-correction mechanism for the two situations incremental journaling can't fully cover on its own: events missed while the engine wasn't running (no journal watch active), and events lost because the USN journal itself wrapped or was recreated before the engine could resume from its last known position (see D6). Reconciliation is a backstop, not the primary update path — the incremental scan+journal pipeline (D4) is what keeps the index fresh moment-to-moment; sweeps exist to catch what that pipeline could have missed.

### D6: Volume identity, disconnect/reconnect, and journal discontinuity

**Decision:** the durable store keys each volume by a stable identity (its volume GUID, plus cached serial number) — distinct from the wire protocol's opaque, connection-scoped `VolumeId` handle, which is only valid for the lifetime of one Engine↔Service connection. The engine maps the ephemeral handle to the durable identity every time it calls `EnumerateVolumes`.

When a volume stops being enumerable (unplugged, unmounted), its `volumes` row is marked unavailable with a last-seen timestamp; its entries are **not** deleted and there is no cascading delete — they simply stop being surfaced as "live" until the volume reappears, so a transient disconnect never destroys index data for unrelated or even the same volume. Deleting a volume's index data only happens on an explicit user action, never as a side effect of disconnection.

On reconnection, the engine compares the volume's persisted USN journal identity (the journal's own ID, obtained from the service) against what's currently reported. If it matches, the engine resumes the journal from its last persisted position — no rescan. If the journal ID differs (the journal was deleted/recreated — e.g., reformat, or the journal being disabled and re-enabled) or the persisted resume position is now out of the journal's retained range (journal wrap during a long disconnection), incremental resume is unsafe, and the engine falls back to a reconciliation sweep (D5) for that volume rather than either silently missing changes or unconditionally deleting and rescanning from zero — because entries are keyed by `FileReferenceNumber` (D7), a sweep reuses and updates existing rows wherever the underlying files are unchanged, rather than treating the whole volume as new.

### D7: Identity and cycle safety — key by (volume identity, `FileReferenceNumber`), never by path

**Decision:** every entry's identity, both in SQLite and in the in-memory projection, is the pair (volume identity, `FileReferenceNumber`) — never a path string. This is what the whole scheme (D1–D6) rests on, so it's called out as its own decision rather than an implementation detail.

**Why:** junctions, symlinks, and other reparse points can make a path-based traversal revisit an ancestor of itself (a junction inside a tree pointing back up into that same tree), which would make any path-string-keyed visited-set or naive recursive walker either loop forever or double-count the same physical file under multiple apparent paths. Raw MFT scanning sidesteps this by construction — it enumerates MFT records directly, one pass, each record visited exactly once, and a record's parent reference is itself just another `FileReferenceNumber`, never a resolved/followed path — so the cycle risk in this design is really confined to two places: the degraded-mode fallback walker (`FindFirstFileEx`-based, which does walk paths and must recognize `FILE_ATTRIBUTE_REPARSE_POINT` and skip rather than follow it, per the foundation change's existing degraded-mode behavior) and any reconciliation-sweep logic that does path-based verification. A `(volume identity, FileReferenceNumber)` primary key/uniqueness constraint in SQLite, mirrored by the projection's entry table, guarantees a single row per physical file record regardless of how many directory entries might otherwise seem to reference it, and gives the projection's parent-chain walk (used for on-demand path reconstruction, D2) a well-defined, defensively cycle-detectable structure — a walk that revisits an already-seen `FileReferenceNumber` can simply stop, even though a well-formed NTFS/ReFS tree should never produce that.

**Alternatives considered:** deduplicating on normalized path string was rejected outright — it's the exact mechanism that's unsafe under junctions/symlinks (a path can be constructed to point back at itself, but a `FileReferenceNumber` cannot, since it identifies the on-disk MFT record itself, not a name that resolves to it).

### D8: Resumable scan progress

**Decision:** the store persists a per-volume scan-progress cursor — an opaque continuation token supplied by the service as `StartVolumeScan` streams batches — updated only after the corresponding batch has committed to SQLite. If `FastFilesEngine` is closed or crashes mid-scan, restart resumes `StartVolumeScan` from that persisted cursor instead of record zero.

This requires a small, additive extension to the closed command protocol (`StartVolumeScan` gains an optional resume-cursor argument) rather than a new command — kept deliberately minimal given how hardened that protocol surface already is.

Correctness doesn't actually depend on the cursor being byte-exact: because ingestion is keyed by `(volume identity, FileReferenceNumber)` (D7) and every write is an upsert, re-ingesting a record the engine already committed before the crash is a harmless no-op. The cursor is a performance optimization — it avoids re-reading and re-transferring already-seen MFT records on a very large volume — not a correctness requirement; the safety net is idempotent upsert semantics, and the cursor is what makes resumption efficient on top of that.

## Risks / Trade-offs

- **[Risk]** WAL file growth under sustained heavy write churn (e.g., a large batch import or a burst of USN activity) could grow unboundedly if never checkpointed. → **Mitigation:** scheduled passive checkpoints plus a size-triggered forced checkpoint if the WAL grows past a threshold; `synchronous=NORMAL` under WAL (safe for this workload, since only checkpoint operations need the stronger fsync guarantee, and the durable source of truth is never left inconsistent by a NORMAL-mode WAL commit followed by a crash — at worst a few of the most recent transactions are lost, which the journal/scan resume logic already tolerates).
- **[Risk]** Projection rebuild at startup is slow for very large volumes (tens of millions of rows) if done naively. → **Mitigation:** single streaming pass with pre-sized allocations (row counts known from stored volume metadata), per-volume incremental publication so partial results are usable before every volume finishes rebuilding.
- **[Risk]** Reconciliation sweeps are I/O-heavy and could make the machine feel sluggish if run at normal priority/foreground time. → **Mitigation:** idle-time scheduling and low I/O priority, paced rather than run-to-completion as fast as possible.
- **[Risk]** SQLite writes and the in-memory projection could diverge if a crash happens between "committed to SQLite" and "applied to projection." → **Mitigation:** strict ordering (commit before apply, D4) means the only possible divergence after a crash is "projection is momentarily behind," never "projection shows something SQLite never recorded" — and a restart always rebuilds the projection from SQLite, so any such gap self-heals on next launch; reconciliation sweeps are an additional backstop.
- **[Risk]** Treating a resumed journal as valid when it actually wrapped or was recreated would silently drop changes. → **Mitigation:** explicit journal-identity comparison before resuming (D6); any mismatch forces a reconciliation sweep instead of a blind resume.
- **[Risk]** The interned name pool has no live reference-counted eviction — a name that becomes unused (its last referencing entry deleted) stays in the pool until the next full rebuild. → **Mitigation:** acceptable given the pool's bounded size relative to total entries (D2); reclaimed opportunistically on projection rebuild rather than requiring live garbage collection, trading a small amount of long-run memory slack for materially simpler ingestion-path code.
- **[Risk]** Extending `StartVolumeScan` with a resume argument touches the already-hardened, closed command protocol from the foundation change. → **Mitigation:** additive-only field on an existing command (not a new, open-ended command), consistent with the protocol's existing minor-version-is-purely-additive rule; the closed-set nature of the command surface itself is unchanged.

## Migration Plan

Greenfield: no prior persisted index exists (the foundation change stubbed scanning entirely), so there is no data migration from an earlier format. Deployment is simply shipping updated `FastFilesIndexSvc` and `FastFilesEngine` binaries; on first run under this change, the engine creates its SQLite database file and begins populating it via real `StartVolumeScan`/`OpenUsnJournal` calls. The database includes a stored schema-version marker from the outset so future changes can migrate it in place rather than needing another greenfield reset.

Degraded mode remains the safety net throughout: if anything in the new scanning/ingestion path fails or the privileged service is unavailable, `FastFilesEngine` falls back to the already-implemented unprivileged enumeration path exactly as before, so the UI keeps working regardless of this change's rollout state. Rolling back to a pre-change binary simply means an already-created SQLite database is ignored by the older code (it predates this change and does not know the file exists) — no destructive rollback step is required.

## Open Questions

- Exact reconciliation sweep cadence/pacing parameters (fixed interval vs. idle-detection-driven) are left as an implementation/tuning detail, not fixed here.
- Whether ReFS's 128-bit file identifiers require any schema handling beyond sizing the identifier field wide enough (this design assumes "size it wide enough now," not a ReFS-specific code path) is left to implementation.
- Whether to persist WizTree-style allocated-vs-logical size distinction now or defer it entirely to `storage-analysis`'s own design — leaning toward persisting whatever `$STANDARD_INFORMATION`/`$FILE_NAME` already expose per entry (this change does not add new MFT attribute parsing beyond the allowlist) and leaving rollup/aggregation to `storage-analysis`, but not binding that change's design here.
- Confirm that `FastFilesEngine`'s SQLite database is strictly per-user-profile/per-engine-instance with no cross-session sharing, consistent with the foundation change's per-logon-session engine model — assumed here, not re-litigated.
