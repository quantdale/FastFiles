Task: READ-ONLY research lane C (FastFiles repo; work ONLY read-only, no shell, no edits) — Hybrid hot/cold residency. Repo: C:/Users/Michael Roy/Documents/FastFiles. Investigate: (1) the memory model — Projection::kDefaultMaximumMemoryBytes (500MiB), enforcement watermark, EnforceMemoryLimit/EnforceMemoryLimitNow, EvictSubtree/EvictVolume semantics (whole-volume cold marking), ReloadEvictedVolume/RebuildVolumeFromStore; (2) what a COMPACT always-resident candidate layer would need per entry (folded name key? trigram signature? minimal automaton?) — derive its bytes/entry arithmetic from the fixture generator's name shapes (find the fixture generator in the bench harness); (3) strict memory-bound design: how such a layer would be budgeted separately from the 500MiB projection budget without raising it, its eviction semantics, consistency with evicted rows, and behavior under memory pressure; (4) whether the compact layer can support ALL 12 query classes or only literal-eligible ones (map against the eligibility gates in the planner: single literal, folded length >= 3, candidate cap); (5) query concurrency and staleness (the residency epoch machinery — ProjectionResidency, query-visible epochs). Deliver: findings with file:line evidence, bytes/entry and bytes/1M arithmetic, a strict-bound design sketch, and the measurement plan a human should run.
grep: C:/Users/Michael Roy/Documents/FastFiles
grep: C:/Users/Michael Roy/Documents/FastFiles
tests/indexstore/test_projection.cpp:429:         reload.SetMaximumMemoryBytes(Projection::kDefaultMaximumMemoryBytes);
tests/indexstore/test_projection.cpp:523:     Check(diagnostics.configuredMaximumBytes == Projection::kDefaultMaximumMemoryBytes,
tests/indexstore/test_projection.cpp:544:     proj.SetMaximumMemoryBytes(Projection::kDefaultMaximumMemoryBytes);
src/indexstore/src/Projection.cpp:131:     maximumMemoryBytes_ = maximumBytes == 0 ? kDefaultMaximumMemoryBytes : maximumBytes;
src/indexstore/include/ffindexstore/Projection.h:103:     static constexpr size_t kDefaultMaximumMemoryBytes = 500ULL * 1024ULL * 1024ULL;
src/indexstore/include/ffindexstore/Projection.h:503:     size_t maximumMemoryBytes_ = kDefaultMaximumMemoryBytes;
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/memory-cold-decision.md:10: - `Projection::kDefaultMaximumMemoryBytes` = **500 MiB**; the early-enforcement
openspec/changes/memory-bounded-indexed-scale/design.md:6: (`kDefaultMaximumMemoryBytes` = 500 MiB; subtree eviction; `evictedVolumes_`
openspec/changes/durable-cold-search-index-and-hybrid-residency/proposal.md:14: default budget (`Projection::kDefaultMaximumMemoryBytes`; enforcement
openspec/changes/durable-cold-search-index-and-hybrid-residency/specs/index-engine/spec.md:82: (`Projection::kDefaultMaximumMemoryBytes`) SHALL NOT be raised by this change;
tests/engine/test_index_pipeline.cpp:2447: // query-visible residency change (the same ProjectionResidency epoch
src/indexstore/include/ffindexstore/Projection.h:221:     // change (the same ProjectionResidency epoch mechanism
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/adversarial-review-2026-10-21.md:15: | D P1-1 / C F1 | Memory-pressure enforcement evicts+compacts across volumes with no ProjectionResidency epoch bump; in-flight searches serve silently incomplete results as Completed and walk repacked layouts | P1 (results integrity) | `EnforceMemoryLimitNow` returns whether eviction ran; every enforcement/reload site bumps residency epochs via the existing restart machinery (scoped unrelated-mutation semantics preserved) | `TestEnforcementEvictionAdvancesQueryVisibleEpochs` |
src/engine/src/IndexPipeline.cpp:430:     // change through the SAME ProjectionResidency epoch mechanism
src/engine/src/IndexPipeline.cpp:437:         AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::ProjectionResidency);
src/engine/src/IndexPipeline.cpp:439:     AdvanceQueryVisibleEpochLocked(0, QueryMutationReason::ProjectionResidency, true);
src/engine/src/IndexPipeline.cpp:1262:             AdvanceQueryVisibleEpochLocked(metadata.rowId, QueryMutationReason::ProjectionResidency);
src/engine/src/IndexPipeline.cpp:1291:         AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::ProjectionResidency);
src/engine/src/IndexPipeline.h:383:         ProjectionResidency,
src/engine/src/IndexPipeline.h:405:     // ProjectionResidency for every known volume after a structural
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
ls: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "ffindexstore/EntryRecord.h"
#include "ffindexstore/FlatHashMap.h"
#include "ffindexstore/Identity.h"
#include "ffindexstore/NamePool.h"
#include "ffindexstore/Store.h"
namespace ffindexstore {
// FlatChildrenMap is defined in ffindexstore/FlatHashMap.h and included above.
// One entry in the in-memory projection (design.md D2): a fixed-size
// record referencing its parent by FileReferenceNumber -- the spec's
// explicitly-allowed alternative to a dense internal reference -- and its
// name by an interned NameId, never by a stored full path.
struct ProjectionEntry {
    VolumeRowId volumeRowId = 0;
    FileId frn;
    FileId parentFrn;
    NameId nameId = 0;
    uint64_t sizeBytes = 0;
    // allocated-size-storage-accuracy D5: on-disk size with an explicit
    // known flag -- zero is a valid size, never an absence sentinel.
    // Full scans set both; partial updates preserve both unless the
    // allocated-size bit is present in the update's field mask.
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
    uint64_t creationTime = 0;
    uint64_t lastModifiedTime = 0;
    uint64_t lastAccessTime = 0;
    uint32_t attributes = 0;
    bool nameAsciiLower = false;
    // Any-case ASCII classification for the prepared fold matcher; see
    // CandidateView::nameIsAscii in ffsearch for the equivalence argument.
    bool nameIsAscii = false;
};
class Projection;
// nonblocking-indexed-read-plane 4.1: a cursor carries only the current
// dense-slot or flat-map bucket position. It does not own an index copy. The
// caller may release the projection lock between bounded chunks, but must
// discard the cursor if the owning pipeline generation changes.
class ProjectionEntryCursor {
public:
    ProjectionEntryCursor() = default;
    bool Next(const ProjectionEntry*& entry);
    // large-index-lifecycle-and-search-scalability review-lane C Finding 2:
    // true when the layout this cursor walks was disturbed underneath it
    // (entries_ reallocated or reordered, or the id->index flat table grew).
    // A disturbed cursor would silently skip rows, so callers must treat it
    // as a stale generation and re-capture instead of accepting a truncated
    // iteration. Ordinary in-place updates and appends within reserved
    // capacity do not disturb it.
    bool Stale() const noexcept;
private:
    friend class Projection;
    using EntryMap = FlatHashMap<EntryKey, uint32_t, EntryKeyHash>;
    explicit ProjectionEntryCursor(const Projection& projection);
    const Projection* projection_ = nullptr;
    bool dense_ = false;
    size_t denseIndex_ = 0;
    EntryMap::Iterator iterator_;
    // Layout markers captured at construction (lane C Finding 2).
    const void* entriesData_ = nullptr;
    size_t mapBuckets_ = 0;
};
// The compact, RAM-resident view of the index that search/browse/storage-
// analysis actually read (design.md D2), rebuilt from the durable Store at
// startup (D4) and kept incrementally in sync thereafter (this is the
// consumer of Store::ApplyBatch's committed data, applied strictly after
// the corresponding commit -- see the `index-engine` ingestion-ordering
// requirement; the ordering itself is the caller's responsibility, this
// class only implements the in-memory half).
//
// Not internally synchronized -- callers (the engine's single ingestion
// pipeline thread, or a caller-held lock around startup rebuild) are
// responsible for serializing mutation, consistent with SnapshotPublisher
// already requiring the caller to serialize Publish() calls.
// performance-closure-and-multisurface-acceleration 8.3: revision of the
// projection's memory representation and layout. Revision 2 is the
// capacity-slack release after bulk rebuild plus the dense per-slot LRU
// timestamps (revision 1 was the pre-correction representation). Reports embed
// this compiled value so an artifact identifies which representation produced
// it instead of asserting a hardcoded flag.
inline constexpr uint32_t kProjectionMemoryLayoutRevision = 2;
class Projection {
public:
    static constexpr size_t kDefaultMaximumMemoryBytes = 500ULL * 1024ULL * 1024ULL;
    // A zero value restores the production default. The limit is measured
    // from the projection's owned capacity and index storage, not from the
    // database file size.
    void SetMaximumMemoryBytes(size_t maximumBytes);
    size_t MaximumMemoryBytes() const noexcept { return maximumMemoryBytes_; }
    size_t EstimatedMemoryBytes() const noexcept;
    bool IsMemoryPressureHigh() const noexcept;
    // performance-closure-and-multisurface-acceleration 8.1: narrow,
    // read-only diagnostic seam for the projection-memory decomposition. It
    // exists so the memory benchmark can attribute the projection's own
    // allocations per contributor without private-access hacks, a friend
    // test, or any production representation change.
    //
    // Measurement contract:
    //  * Every byte field is a live capacity()/size()/bucket_count()/
    //    element-count measurement of the owning container, except the two
    //    explicitly labelled `*ApproxNodeBytes` node-header terms, which are
    //    element counts * sizeof(...) mirroring EstimatedMemoryBytes()'s
    //    convention. Nothing here is derived from sizeof(object) alone.
    //  * `modeledCategoryBytes` sums exactly the categories
    //    EstimatedMemoryBytes() models; a unit test asserts the two stay
    //    equal so the estimator and this breakdown cannot silently drift.
    //  * Standard-library node links, allocator headers, and heap rounding
    //    are deliberately NOT fabricated: they stay in the
    //    process-private/allocator reconciliation as disclosed residual.
    //  * Structures the estimator does not model are reported in their own
    //    additive field (`evictedVolumeBucketBytes`), not folded into the
    //    modeled sum.
    //  * Structurally absent categories are explicit zeros with the reason:
    //    normalized names are folded on demand (no stored folded copy) and
    //    paths are reconstructed per call (no stored path field).
    struct MemoryBreakdown {
        // ---- rows -------------------------------------------------------
        size_t entryCount = 0;              // live rows (idToIndex_.size())
        size_t rowCapacity = 0;             // entries_.capacity()
        size_t rowBytes = 0;                // rowCapacity * sizeof(ProjectionEntry)
        size_t rowLiveBytes = 0;            // entryCount * sizeof(ProjectionEntry)
        size_t rowSlackBytes = 0;           // rowBytes - rowLiveBytes
        size_t freeListSlots = 0;
        size_t freeListBytes = 0;
        // ---- identifier maps --------------------------------------------
        size_t idMapElements = 0;
        size_t idMapBuckets = 0;            // keys_.size() (flat table slots)
        size_t idMapKeyBytes = 0;
        size_t idMapValueBytes = 0;
        size_t idMapStateBytes = 0;
        size_t idMapBytes = 0;
        // ---- parent/child index -----------------------------------------
        size_t childMapElements = 0;        // distinct parent keys
        size_t childMapBuckets = 0;
        // Live per-key child links (<= the flat array's length: swap-removal
        // and slice relocation can leave stale tail copies in the array).
        size_t childMapChildLinks = 0;
        size_t childMapKeyBytes = 0;
        size_t childMapChildBytes = 0;      // flat child-index array capacity
        size_t childMapStateBytes = 0;
        size_t childMapOffsetBytes = 0;
        size_t childMapCountBytes = 0;
        size_t childMapBytes = 0;
        // ---- names (interned; original case only) ------------------------
        size_t uniqueNameCount = 0;
        size_t nameArenaChars = 0;              // used UTF-16 chars
        size_t nameArenaCapacityChars = 0;
        size_t nameArenaBytes = 0;
        size_t nameArenaSlackBytes = 0;
        size_t nameTableSlotCount = 0;
        size_t nameTableBytes = 0;              // offset/length slots capacity
        size_t nameHashBucketCount = 0;
        size_t nameHashBucketBytes = 0;
        size_t nameHashNodeCount = 0;
        size_t nameHashApproxNodeBytes = 0;     // key + vector header per bucket
        size_t nameHashPayloadBytes = 0;        // bucket candidate-vector capacities (unmodeled by the estimator)
        size_t nameBytes = 0;
        // ---- secondary filename index ------------------------------------
        bool filenameIndexValid = false;
        size_t filenameIndexDistinctTrigrams = 0;
        size_t filenameIndexPostingSlots = 0;   // sum of posting capacities
        size_t filenameIndexPostingPayload = 0; // sum of posting sizes
        size_t filenameIndexPostingBytes = 0;   // postingSlots * sizeof(uint32_t)
        size_t filenameIndexPostingHeaderBytes = 0;
        size_t filenameIndexBucketCount = 0;
        size_t filenameIndexBucketBytes = 0;
        size_t filenameIndexApproxNodeBytes = 0;
        size_t filenameIndexExceptionalCount = 0;  // live entries (size, not capacity)
        size_t filenameIndexExceptionalSlots = 0;
        size_t filenameIndexExceptionalBytes = 0;
        size_t filenameIndexBytes = 0;
        // ---- LRU / eviction metadata -------------------------------------
        size_t lruSlotCapacity = 0;             // dense per-slot timestamp vector
        size_t lruSlotCount = 0;
        size_t lruTrackedCount = 0;             // slots carrying a nonzero timestamp
        size_t lruBytes = 0;                    // lruSlotCapacity * sizeof(uint64_t)
        // ---- structures the estimator does not model ---------------------
        size_t evictedVolumeCount = 0;
        size_t evictedVolumeBucketCount = 0;
        size_t evictedVolumeBucketBytes = 0;    // bucket array only; node links unmodeled
        // ---- structurally absent categories ------------------------------
        size_t normalizedNameStorageBytes = 0;  // folds computed on demand
        size_t storedPathBytes = 0;             // paths reconstructed per call
        // ---- totals -------------------------------------------------------
        size_t modeledCategoryBytes = 0;        // == EstimatedMemoryBytes()
        size_t reportedTotalBytes = 0;          // modeled + concrete unmodeled allocations
    };
    MemoryBreakdown GetMemoryBreakdown() const noexcept;
    // Applies the bounded LRU policy after a complete ingestion mutation
    // batch. Upsert deliberately does not compact per record: compaction
    // rebuilds every projection index and must not turn a large scan into an
    // O(N) operation for every record.
    // large-index-lifecycle-and-search-scalability review-lane D P2-1/P1-1:
    // enforcement loops with bounded passes until the low watermark is
    // reached or no pass makes progress (a single pass could free one small
    // victim per batch boundary in flat corpora and never converge). Returns
    // true when at least one eviction pass ran: every eviction ends in
    // CompactStorage, which repacks ALL row slots and can cold-mark any
    // volume, so callers must treat that as a query-visible structural
    // change (the same ProjectionResidency epoch mechanism
    // SetProjectionMemoryBudget / UnloadVolumeFromProjection use).
    bool EnforceMemoryLimitNow();
    // Initial/reconciliation scans are bulk mutations. Disable the expensive
    // eviction/compaction path and the optional filename accelerator while a
    // scan is active; canonical matching remains correct and the durable
    // store is authoritative. Ending the scan (false) re-enables enforcement
    // AND rebuilds the filename accelerator once (review-lane C Finding 3:
    // previously the accelerator stayed dead after every bulk scan, silently
    // disabling selective-query acceleration until the next full rebuild).
    void SetBulkScanActive(bool active) noexcept;
    // Evicted subtrees remain durable in Store. The engine calls this before
    // serving a volume-specific browse/aggregate/search operation.
    bool NeedsReload(VolumeRowId volumeRowId) const;
    bool ReloadEvictedVolume(Store& store, VolumeRowId volumeRowId);
    bool ReloadAllEvictedVolumes(Store& store);
    // memory-bounded-indexed-scale 3.1: observable memory-policy state so
    // tests can prove the ceiling is real (estimated bytes stay within the
    // configured maximum) and that eviction/reload behavior is bounded rather
    // than thrashing. Counters are monotonic for the projection's lifetime.
    struct MemoryDiagnostics {
        size_t configuredMaximumBytes = 0;
        size_t estimatedBytes = 0;
        size_t lastEvictionBytesBefore = 0;
        size_t lastEvictionBytesAfter = 0;
        uint64_t subtreeEvictionCount = 0;
        uint64_t coldReloadCount = 0;
        uint64_t coldReloadFailedCount = 0;
    };
    MemoryDiagnostics GetMemoryDiagnostics() const;
    // task 2.5: pre-size before a bulk rebuild.
    void Reserve(size_t expectedEntryCount);
    // Inserts a brand-new entry, or updates an existing one in place
    // (including re-parenting, which updates the parent->children index --
    // task 2.3).
    void Upsert(VolumeRowId volumeRowId, const EntryRecord& record);
    // Applies only the fields present in `fields`, retaining previous
    // authoritative values for absent fields. A missing entry is created with
    // zero/default values only for fields that were not present.
    void UpsertPartial(VolumeRowId volumeRowId, const EntryRecord& record,
                       EntryFieldMask fields);
    // No-op if the entry is not present.
    void Remove(VolumeRowId volumeRowId, FileId frn);
    // large-index-lifecycle-and-search-scalability 4.4: batch removal with
    // one bulk filename-accelerator update instead of per-entry sorted-vector
    // posting erases (quadratic in posting sizes for large batches).
    // keepFilenameIndexCurrent rebuilds the accelerator once after a bulk
    // removal; false leaves it invalidated for a caller that rebuilds it as
    // part of an enclosing bulk operation. Observable removal semantics are
    // identical to per-entry Remove in both modes; in keep=false mode the
    // accelerator is invalidated regardless of batch size (review-lane C
    // Finding 5: the small-batch path previously left it valid, contradicting
    // this contract).
    void RemoveBatch(VolumeRowId volumeRowId, const std::vector<FileId>& frns,
                     bool keepFilenameIndexCurrent = true);
    // tasks.md 7.4's projection half: drops every entry belonging to a
    // forgotten volume (dense-slot tombstoning, id->index and
    // parent->children rows) -- equivalent to calling Remove on each of
    // the volume's entries. Interned names are not reclaimed, exactly as
    // with individual Remove (the pool is reclaimed on the next full
    // rebuild -- design.md "Risks" name-pool eviction note).
    // large-index-lifecycle-and-search-scalability 4.4: routes through
    // RemoveBatch's bulk accelerator maintenance.
    void RemoveVolume(VolumeRowId volumeRowId, bool keepFilenameIndexCurrent = true);
#ifdef FF_INDEXSTORE_TESTING
    // large-index-lifecycle-and-search-scalability 4.2: benchmark-only
    // differential-isolation seam for the removal-cost attribution matrix
    // (bench_rebuild_isolation). Compiled only into the isolation benchmark
    // target via FF_INDEXSTORE_TESTING; production builds contain none of
    // this. RemovalMaintenanceMode::Production (the default) preserves exact
    // production removal semantics; the other modes skip ONE kind of
    // per-entry secondary maintenance so the matrix can attribute removal
    // wall-time to filename-accelerator maintenance vs. parent/child unlink
    // vs. the residual id-map/free-list work. Never set in production code.
    enum class RemovalMaintenanceMode : uint8_t {
        Production = 0,
        SkipFilenameMaintenance = 1,
        SkipChildUnlink = 2,
        SkipBoth = 3,
    };
    void SetRemovalMaintenanceMode(RemovalMaintenanceMode mode) noexcept {
        removalMaintenanceMode_ = mode;
    }
#endif
    // large-index-lifecycle-and-search-scalability review-remediation (P2-A):
    // testing-only seam that force-marks a UTF-16 unit as fold-exceptional
    // (simulates a host ordinal table that equates it with ASCII, e.g. U+017F
    // LONG S vs 's'), so the candidate-completeness machinery for the
    // exceptional-name fallback is deterministically testable on hosts whose
    // real table has no cross-ASCII fold classes. The DECLARATION is visible
    // to every build so static analysis sees the seam; the definition and its
    // override state are compiled only with FF_INDEXSTORE_TESTING (test and
    // bench targets), so production binaries contain no seam behavior. Never
    // set in production.
    static void SetTestingFoldExceptionalUnit(char16_t unit, bool exceptional);
    const ProjectionEntry* Find(VolumeRowId volumeRowId, FileId frn) const;
    ProjectionEntryCursor BeginEntryCursor() const;
    // Returns a conservative candidate set for an already ASCII-folded
    // simple literal. nullopt means the secondary accelerator is ineligible
    // or unavailable, so callers must use the canonical live-entry scan.
    // The candidate set is a CONSERVATIVE SUPERSET of the rows the canonical
    // (CompareStringOrdinal ignore-case) matcher would accept: every row the
    // matcher accepts is present, though the set may contain false positives
    // that only the canonical matcher rejects. False negatives are forbidden
    // (review-remediation P2-A enforces this for every host ordinal table).
    // Returned slot indices belong to this projection generation and are
    // valid only while the caller holds the projection's synchronization.
    std::optional<std::vector<uint32_t>> FilenameCandidateIndices(
        std::u16string_view foldedNeedle, size_t maximumCandidates = 0) const;
    // task 2.3: parent->children lookup for directory-listing/Column-View
    // style access. Returns empty span if the parent has no known children
    // (which is not the same as the parent itself being unknown).
    std::span<const uint32_t> ChildIndices(VolumeRowId volumeRowId, FileId parentFrn) const;
    // memory-bounded-indexed-scale 5.x: drops one volume's resident rows and
    // marks the volume cold -- an explicit policy hook (e.g. unloading an
    // offline volume's working set) that also gives tests deterministic
    // forced-eviction state without depending on LRU victim order. Data stays
    // durable in Store; a no-op for an unknown/empty volume.
    void EvictVolume(VolumeRowId volumeRowId);
    // file-preview-and-properties §6.2 / storage-analysis §3.1: walks the
    // in-memory subtree rooted at `parentFrn` and returns the aggregate item
    // count (all descendants, excluding the root itself) and the total
    // sizeBytes sum across every entry in that subtree. Returns std::nullopt
    // if the root is unknown, distinguishing "not indexed" from "empty folder".
    // allocated-size-storage-accuracy D7: the allocated-size subtotal covers
    // only descendants whose allocated size is known, with the completeness
    // pair (known/total counts); Complete only when every descendant is
    // known. Sums saturate at UINT64_MAX with completeness cleared on
    // saturation -- a wrapped-looking total must never be presented as exact.
    struct FolderAggregate {
        uint64_t itemCount = 0;
        uint64_t totalSizeBytes = 0;
        uint64_t totalAllocatedSizeBytes = 0;
        uint64_t allocatedBytesKnownCount = 0;
        uint64_t allocatedBytesTotalCount = 0;
        bool complete = true;
    };
    std::optional<FolderAggregate> GetFolderAggregate(VolumeRowId volumeRowId, FileId parentFrn) const;
    const ProjectionEntry& EntryAt(uint32_t index) const { return entries_[index]; }
    struct PathResult {
        std::u16string path; // backslash-joined names, root-to-leaf, leaf included
        // false if the walk stopped early because it hit an entry not
        // present in the projection, or detected revisiting an FRN already
        // seen in this same walk (task 2.4's defensive cycle detection) --
        // in both cases `path` holds whatever prefix was resolved.
        bool reachedRoot = false;
    };
    // task 2.4: walks parent references and concatenates interned names
    // on demand -- never reads a precomputed path field.
    PathResult ReconstructPath(VolumeRowId volumeRowId, FileId frn) const;
    // nonblocking-indexed-read-plane 4.3: the same bounded walk with a
    // cooperative cancellation check between parent links.
    PathResult ReconstructPathUntil(VolumeRowId volumeRowId, FileId frn,
                                     const std::function<bool()>& cancelled) const;
    // task 3.1: rebuilds this volume's portion of the projection from the
    // durable store in a single streaming pass, interning names and
    // building the parent->children index as rows arrive. Returns false when
    // the durable stream fails; the volume is left cold with no partial
    // projection so callers cannot publish an incomplete rebuild.
    // expectedEntryCount pre-sizes allocations (task 2.5/D4); the caller
    // gets this from Store::GetVolumeMetadata(...)->entryCount.
    bool RebuildVolumeFromStore(Store& store, VolumeRowId volumeRowId, uint64_t expectedEntryCount);
    size_t EntryCount() const noexcept { return idToIndex_.size(); }
    const NamePool& Names() const noexcept { return namePool_; }
    // Visits every live entry (fn(const EntryKey&, const ProjectionEntry&))
    // -- used by consumers that need to export the whole projection (e.g.
    // converting it to the directory-listing snapshot format for
    // publication). Iterates the live-entry index rather than the dense
    // array directly, so tombstoned/reused slots are never visited.
    template <typename Fn>
    void ForEachEntry(Fn&& fn) const {
        idToIndex_.ForEach([&](const EntryKey& key, const uint32_t index) {
            fn(key, entries_[index]);
        });
    }
    template <typename Fn>
    void ForEachEntryValue(Fn&& fn) const {
        if (denseIterationOrderValid_) {
            for (const ProjectionEntry& entry : entries_) fn(entry);
        } else {
            idToIndex_.ForEachValue([&](const uint32_t index) { fn(entries_[index]); });
        }
    }
    // audit-remediation FF-AUDIT-06: same live-entry traversal with a
    // cooperative stop predicate. The
    // predicate is evaluated before each callback so a superseded indexed
    // search can stop projection enumeration rather than merely discarding
    // its eventual result.
    template <typename Fn, typename Cancel>
    bool ForEachEntryValueUntil(Fn&& fn, Cancel&& cancelled) const {
        if (denseIterationOrderValid_) {
            for (const ProjectionEntry& entry : entries_) {
                if (cancelled()) return false;
                fn(entry);
            }
        } else {
            bool keepGoing = true;
            idToIndex_.ForEachValue([&](const uint32_t index) {
                if (!keepGoing || cancelled()) {
                    keepGoing = false;
                    return;
                }
                fn(entries_[index]);
            });
            if (!keepGoing) return false;
        }
        return true;
    }
    template <typename Fn, typename Cancel>
    bool ForEachEntryUntil(Fn&& fn, Cancel&& cancelled) const {
        bool keepGoing = true;
        idToIndex_.ForEach([&](const EntryKey& key, const uint32_t index) {
            if (!keepGoing || cancelled()) {
                keepGoing = false;
                return;
            }
            fn(key, entries_[index]);
        });
        return keepGoing;
    }
private:
    friend class ProjectionEntryCursor;
    // review-lane D P2-1: return whether an eviction pass ran (see the public
    // EnforceMemoryLimitNow contract).
    bool EnforceMemoryLimit();
    bool EnforceMemoryLimitTo(size_t targetBytes);
    bool EnforceMemoryLimitOnceTo(size_t targetBytes);
    std::optional<EntryKey> LeastRecentlyUsedEntry() const;
    void EvictSubtree(const EntryKey& rootKey);
    void CompactStorage();
    void TouchIndex(uint32_t index) const noexcept;
    void CompactEntriesForIterationOrder();
    void RemoveFromChildrenList(const EntryKey& parentKey, uint32_t index);
    void RebuildFilenameIndex();
    // performance-closure-and-multisurface-acceleration 8.3: releases the
    // geometric-growth capacity slack a bulk rebuild leaves behind.
    void ShrinkBulkRebuildCapacity();
    void AddFilenameIndexEntry(uint32_t index);
    void RemoveFilenameIndexEntry(uint32_t index, std::u16string_view name);
    void InvalidateFilenameIndex() noexcept;
    NamePool namePool_;
    std::vector<ProjectionEntry> entries_; // dense array; freed slots reused via freeList_
    std::vector<uint32_t> freeList_;
    FlatHashMap<EntryKey, uint32_t, EntryKeyHash> idToIndex_;
    FlatChildrenMap parentToChildren_;
    bool denseIterationOrderValid_ = false;
    std::unordered_map<uint64_t, std::vector<uint32_t>> filenameTrigramPostings_;
    // review-remediation (P2-A): rows whose names can match an ASCII needle
    // through a non-ASCII unit under the host ordinal table, plus every
    // surrogate-bearing name (conservative). Sorted slot indices; merged into
    // every ASCII candidate set so the superset invariant holds.
    std::vector<uint32_t> filenameExceptionalEntries_;
    bool filenameIndexValid_ = true;
    bool filenameIndexBuilding_ = false;
    // large-index-lifecycle-and-search-scalability review-lane C Finding 3/6:
    // tracks the bulk-scan state so scan end can revive the accelerator and
    // cold reloads do not resurrect it mid-scan.
    bool bulkScanActive_ = false;
#ifdef FF_INDEXSTORE_TESTING
    RemovalMaintenanceMode removalMaintenanceMode_ = RemovalMaintenanceMode::Production;
#endif
    size_t maximumMemoryBytes_ = kDefaultMaximumMemoryBytes;
    mutable uint64_t accessClock_ = 0;
    // performance-closure-and-multisurface-acceleration 8.3: one access
    // timestamp per dense entry slot instead of one hash-map node per entry.
    // The key -> slot map (idToIndex_) already exists, so the previous
    // std::unordered_map<EntryKey, uint64_t> was a second hashed container
    // costing about 40 bytes per entry (bucket array + node key + node link);
    // this costs 8 bytes per dense slot. Slot reuse, Remove, and
    // CompactStorage reset or remap the timestamps exactly as they did for the
    // removed map nodes, and LeastRecentlyUsedEntry reads them by slot.
    mutable std::vector<uint64_t> lastAccessTimes_;
    std::unordered_set<VolumeRowId> evictedVolumes_;
    bool enforcingMemoryLimit_ = true;
    // memory-bounded-indexed-scale 3.1: monotonic policy observability.
    uint64_t subtreeEvictionCount_ = 0;
    uint64_t coldReloadCount_ = 0;
    uint64_t coldReloadFailedCount_ = 0;
    size_t lastEvictionBytesBefore_ = 0;
    size_t lastEvictionBytesAfter_ = 0;
};
} // namespace ffindexstore
design.md
proposal.md
specs/
tasks.md
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
# Proposal: durable-cold-search-index-and-hybrid-residency
Status: **PROPOSAL / DECISION RECORD ONLY — implementation is explicitly not
started.** This change was prepared during the `large-index-lifecycle-and-
search-scalability` closure campaign (PR #14 handoff) as the separately scoped
successor that campaign deliberately deferred. It must not be folded into PR
#14 and must not begin production implementation without an explicit owner
authorization and a completed measurement phase (task group 1).
## Problem
At 2M+ indexed rows the product enters a measured **cold regime**: the
in-memory projection (~839.7 MB modeled at 2M rows) exceeds the 500 MiB
default budget (`Projection::kDefaultMaximumMemoryBytes`; enforcement
watermark 400 MiB), `SetProjectionMemoryBudget` has no production caller, and
every query then serves from the SQLite cold stream. Measured at 2M
(`evidence/search/search-matrix-before.json`, historical p50): selective
~2.5 s, broad ~27.1 s, extension ~37.6 s per keystroke-triggered query class.
The completed predecessor campaign shipped the safe, schema-free
improvements and stopped here on purpose:
- hot/projection-resident selective search is already accelerated
  (filename-trigram accelerator, 56-189x on eligible literal classes at 1M,
  rows examined 1M -> 1);
- the cold path gained a conservative SQL name prefilter
  (`ForEachEntryWithAsciiNameUntil`, 1.28-1.44x on cold literal classes at
  1M with 9.8% non-ASCII names), but the **hard floor is the name-column
  scan itself (~1.05-1.10 us/scanned-row)** — a zero-survivor literal is
  still ~1.1 s at 1M and proportionally worse at 2M-5M;
- non-literal classes (broad, extension, short_prefix, unicode) are
  prefilter-ineligible by design and remain O(N) scans on the cold path.
The largest remaining user-relevant cost at production scale is therefore
**cold candidate generation at 2M-5M+ rows**: no durable structure exists
that can answer "which rows could match this needle?" without scanning and
decoding every row of the volume.
## What this proposal investigates (measure first, decide second)
The proposal does **not** assume FTS, schema v3, or hybrid residency is the
answer. It defines a measurement campaign over alternatives, with a decision
record per alternative:
1. **Durable candidate-index alternatives** (schema v3 or equivalent):
   - SQLite FTS5 (unicode61/trigram tokenizer) as a candidate generator;
   - a durable trigram posting table mirroring the in-memory accelerator's
     semantics (folded-trigram postings + exception set);
   - covering-index / sorted-name strategies (ordinal-key prefixes);
   - hybrid hot/cold residency (Option C of the predecessor's memory
     decision: a compact, always-resident candidate subset per volume) as a
     non-schema alternative;
   - budget-policy alternatives (adaptive budget) as the control arm.
2. **Candidate completeness under Windows ordinal comparison.** Every
   candidate generator must be a **conservative superset** of the canonical
   matcher's result set under `CompareStringOrdinal(..., TRUE)` semantics on
   the host table — including the fold-exceptional class (units the host
   uppercase table equates with ASCII, e.g. U+017F on some tables) and
   surrogate names, per the predecessor's P2-A repair
   (`Projection::SetTestingFoldExceptionalUnit`, host-derived memoized
   probe, fail-closed). An FTS tokenizer that drops such rows is not
   acceptable; the differential suites (hot/cold identity + ordering,
   `TestFilenameCandidatesAreConservativeSuperset` and descendants) are the
   acceptance oracle.
3. **Query planner selection.** When does the planner consult the durable
   index vs. stream the cold path? Candidate caps, eligibility gates (single
   literal, folded length, residency), and fallback-to-scan behavior must be
   cost-based and measured at 200k/1M/2M/5M.
4. **Schema migration and rollback.** Any schema v3 must be additive and
   versioned: migration cost at 2M-5M rows, crash-consistent index updates
   (the index must never disagree with the entries table after a crash —
   transactional coupling or rebuild-on-open), rollback to v2 with no data
   loss, and disk-size tradeoffs (measured index bytes per 1M rows per
   alternative).
5. **Bounded memory footprint.** The durable index is a disk structure, but
   caches/planners built on it must stay within documented bounds; no
   alternative may silently raise the default 500 MiB projection budget.
6. **Cold/hot result equivalence.** Cold (durable-index-assisted) and hot
   (projection-accelerated) results must remain byte-identical in identity
   and ordering for every query class — the pinned contract from the
   predecessor spec delta.
7. **Startup/rebuild cost.** Index build during scan/ingestion, incremental
   maintenance per USN batch, and reopen/recovery cost must be measured;
   a durable index that doubles ingestion time is a regression.
## Performance targets (to be validated, not promised)
Measure at 200k / 1M / 2M / 5M rows, same discipline as the predecessor
campaign (idle host gating, medians for hot, disclosed single samples for
cold, machine-readable artifacts with embedded identity):
- cold selective/exact/no-match at 2M: from seconds to the sub-200 ms class
  if a durable candidate generator is adopted;
- cold non-literal classes: measured improvement or an honest documented
  floor;
- ingestion and rebuild overhead: no material regression vs. the certified
  31-34x replacement-rebuild behavior at 1M.
## Impact
- **Affected components (if adopted later):** `ffindexstore` (schema,
  migration, candidate generation), `ffengine` (planner selection, budget
  policy), `ffsearch` (query classification), tests (differential +
  crash-consistency + migration suites). No protocol, service, installer, or
  UI change is implied.
- **Risk:** schema migration on multi-million-row user indexes is the
  highest-risk operation this product would yet ship; crash-consistency and
  rollback are mandatory acceptance items, not stretch goals.
- **Non-goals:** raising default memory consumption; changing canonical match
  semantics; hot-path redesign; any change to the certified PR #14 behavior.
- **Relationship to PR #14:** none — PR #14 remains a self-contained,
  certified candidate; this successor starts from its landed state.
## Decision record pointer
`design.md` in this directory is the living decision record: alternatives,
measurement plan, acceptance gates, and the retain/reject decision per
alternative (to be filled by the future campaign — current entries record
the deferred predecessor decisions that bound this work).
# Design / decision record: durable-cold-search-index-and-hybrid-residency
Status: **decision record seeded during the PR #14 closure campaign; every
alternative is UNDECIDED pending the measurement phase (task group 1).** No
production implementation exists or is authorized by this document.
## 1. Bounding facts (inherited, measured)
These constrain the design space and come from the completed
`large-index-lifecycle-and-search-scalability` campaign's evidence
(`evidence/memory-cold-decision.md`, `evidence/lifecycle-reload-resources.md`):
- Projection budget: 500 MiB default, 400 MiB early-enforcement watermark;
  `SetProjectionMemoryBudget` has no production caller (tests only). A 1M-row
  projection models ~414 MB (straddles the watermark); 2M models ~839.7 MB
  (always cold under default policy).
- Cold stream floor: ~1.05-1.10 us per scanned row (name-column scan,
  decode, match). The shipped conservative SQL name prefilter
  (`ForEachEntryWithAsciiNameUntil`) removes the survivor-decode share for
  ASCII literals (1.28-1.44x at 1M, 9.8% non-ASCII names) but cannot beat
  the scan floor; non-literal classes are ineligible by design.
- Hot path: filename-trigram accelerator (folded postings + host-derived
  fold-exceptional set + surrogate-conservative set) gives 56-189x on
  eligible literal classes at 1M; candidate cap
  `kMaximumFilenameIndexCandidates = 16,384` is the existing cost-based
  fallback to canonical scan.
- Correctness contract: hot/cold identity+ordering equivalence is pinned by
  differential suites; candidate sets must be conservative supersets under
  `CompareStringOrdinal(..., TRUE)` host semantics (predecessor P2-A repair:
  fold-exceptional classification via a memoized host probe, fail-closed on
  API error).
## 2. Alternatives to measure (all currently UNDECIDED)
| # | Alternative | Sketch | Principal risk |
|---|---|---|---|
| A | SQLite FTS5 candidate table | FTS5 (trigram or unicode61) over names, queried for rowid candidates, verified by the canonical matcher | Tokenizer case-folding is NOT Windows ordinal folding; must prove superset incl. fold-exceptional + surrogate classes, or maintain an exception side-set |
| B | Durable trigram posting table | Persist the in-memory accelerator's structure (folded trigram -> sorted rowid postings + exception set) in SQLite; incremental maintenance per batch | Write amplification on ingestion; crash-consistency of postings vs entries; disk size |
| C | Hybrid hot/cold residency | Keep a compact per-volume candidate subset resident (e.g. folded-name keys or a minimal automaton) while rows stay cold | Memory bound must be strict and provable; consistency with evicted rows; predecessor marked this "separately scoped architecture work" |
| D | Covering ordinal-key index | Sorted index on the matcher's folded name key; range scans for prefix/substring classes | Substring/wildcard classes get little benefit; index size |
| E | Budget policy (adaptive/default raise) | Control arm: serve 2M hot by raising/deriving the budget | Competes with system memory; forbidden without safety proof; does not help 5M |
## 3. Mandatory acceptance dimensions (per alternative)
1. **Completeness:** candidate superset proof under host ordinal semantics,
   with red-first differential tests (fold-exceptional, surrogate, dotted,
   mixed-case, short-name shapes) — the predecessor's
   `TestFilenameCandidatesAreConservativeSuperset` family extended to the
   durable generator.
2. **Equivalence:** cold-with-index results byte-identical to hot results for
   all 12 query classes at 200k/1M/2M (and 5M for the selected candidate).
3. **Migration:** v2 -> v3 cost at 2M-5M; rollback path; crash-consistency
   (kill-during-update recovery; index never disagrees with entries);
   rebuild-on-open as the fallback recovery.
4. **Resources:** planner/index cache memory bounds; disk bytes per 1M rows;
   no change to the default 500 MiB projection budget.
5. **Cost:** ingestion/USN-batch maintenance overhead; reopen/recovery cost;
   measured planner decision quality (when index is consulted vs scan).
6. **Scale:** p50/p90/p95 at 200k/1M/2M/5M per query class, machine-readable
   artifacts with embedded source identity (predecessor lane E E-8 lesson:
   artifacts must embed `--source-sha`).
## 4. Decision log
| Date | Decision | Status |
|---|---|---|
| 2026-10-21 | Proposal + decision record created at PR #14 closure; alternatives A-E recorded; measurement-first mandated; no alternative selected | RECORDED |
| (future) | Task group 1 measurements complete | PENDING |
| (future) | Alternative selected/rejected with measured rationale | PENDING |
| (future) | Schema/migration/rollback design for the selected alternative | PENDING |
## 5. Explicit non-decisions
- FTS5 is **not** presumed correct: its tokenizer folding must be proven a
  superset of Windows ordinal ignore-case on this host (see dimension 1) or
  supplemented with an exception side-set maintained like the in-memory
  accelerator's.
- Schema v3 is **not** presumed necessary: alternative C (hybrid residency)
  may achieve the latency targets without a schema change, and alternative E
  is retained only as a control arm.
- The 500 MiB default budget is **not** raised by this proposal.
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
ls: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
# Tasks
Proposal-stage ledger. **Nothing here is started.** The first action of the
future campaign is task 1.1; no production implementation task may begin
before the measurement phase (group 1) has produced its decision record.
## 1. Measurement phase (mandatory before any implementation)
- [ ] 1.1 Reproduce the 2M-5M cold-regime baseline on the landed PR #14 tree (same harness discipline: idle gating, embedded source identity, machine-readable artifacts) at 200k / 1M / 2M / 5M, all 12 query classes, hot and cold.
- [ ] 1.2 Measure alternatives A-E from `design.md` as prototypes behind test-only seams (never production): candidate-generation latency, completeness status, ingestion overhead, index size, memory bounds.
- [ ] 1.3 Prove or disprove candidate completeness under Windows ordinal folding for each alternative (fold-exceptional + surrogate classes included); record red-first differential evidence.
## 2. Decision
- [ ] 2.1 Select or reject each alternative with measured rationale in the `design.md` decision log; state the schema/migration consequence explicitly.
- [ ] 2.2 If no alternative meets the targets with acceptable risk, record the measured floor and stop — an honest documented floor is an acceptable outcome.
## 3. Implementation (only after 2.x selection and owner authorization)
- [ ] 3.1 Implement the selected candidate generator incrementally (additive schema, versioned), with crash-consistent updates and rebuild-on-open fallback.
- [ ] 3.2 Wire cost-based planner selection (candidate cap + fallback to canonical scan preserved).
- [ ] 3.3 Migration + rollback tooling with measured v2->v3 cost at 2M-5M rows and crash-recovery tests.
## 4. Acceptance
- [ ] 4.1 Cold/hot result identity + ordering equivalence suites green for all query classes at 200k/1M/2M/5M.
- [ ] 4.2 Resource bounds proven (planner caches, disk bytes per 1M rows, default 500 MiB projection budget unchanged).
- [ ] 4.3 Full certification ladder at the final candidate (Debug/release-test CTest, Analyze, ASan slice, strict OpenSpec, local CI parity, E2E, UIA, visual), with the same honest classification discipline as the predecessor campaign.
index-engine/
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
grep: C:/Users/Michael Roy/Documents/FastFiles/src
Projection.cpp:138:     EnforceMemoryLimitTo(maximumMemoryBytes_);
Projection.cpp:142: bool Projection::EnforceMemoryLimitNow() {
Projection.cpp:143:     return EnforceMemoryLimit();
Projection.cpp:293:     result.evictedVolumeCount = evictedVolumes_.size();
Projection.cpp:294:     result.evictedVolumeBucketCount = evictedVolumes_.bucket_count();
Projection.cpp:295:     result.evictedVolumeBucketBytes = evictedVolumes_.bucket_count() * sizeof(void*);
Projection.cpp:316: bool Projection::NeedsReload(const VolumeRowId volumeRowId) const {
Projection.cpp:317:     return evictedVolumes_.contains(volumeRowId);
Projection.cpp:379: void Projection::EvictSubtree(const EntryKey& rootKey) {
Projection.cpp:407:     // caller's CompactStorage() invalidates the accelerator anyway, and the
Projection.cpp:415:     evictedVolumes_.insert(rootKey.volumeRowId);
Projection.cpp:419: void Projection::EvictVolume(const VolumeRowId volumeRowId) {
Projection.cpp:431:     // EvictSubtree -- the trailing CompactStorage() invalidates the
Projection.cpp:440:     evictedVolumes_.insert(volumeRowId);
Projection.cpp:442:     CompactStorage();
Projection.cpp:445: void Projection::CompactStorage() {
Projection.cpp:482: bool Projection::EnforceMemoryLimit() {
Projection.cpp:498:         if (!EnforceMemoryLimitOnceTo(lowWatermark)) break;
Projection.cpp:505: bool Projection::EnforceMemoryLimitTo(const size_t targetBytes) {
Projection.cpp:512:         EvictSubtree(*victim);
Projection.cpp:513:         CompactStorage();
Projection.cpp:524: bool Projection::EnforceMemoryLimitOnceTo(const size_t targetBytes) {
Projection.cpp:529:     EvictSubtree(*victim);
Projection.cpp:530:     CompactStorage();
Projection.cpp:538: bool Projection::ReloadEvictedVolume(Store& store, const VolumeRowId volumeRowId) {
Projection.cpp:539:     if (!NeedsReload(volumeRowId)) return true;
Projection.cpp:546:     const bool rebuilt = RebuildVolumeFromStore(store, volumeRowId, metadata->entryCount);
Projection.cpp:547:     const bool reloaded = rebuilt && !NeedsReload(volumeRowId);
Projection.cpp:554:     volumeIds.reserve(evictedVolumes_.size());
Projection.cpp:555:     for (const VolumeRowId volumeId : evictedVolumes_) {
Projection.cpp:560:         if (!ReloadEvictedVolume(store, volumeId)) {
Projection.cpp:1142: bool Projection::RebuildVolumeFromStore(Store& store, VolumeRowId volumeRowId, uint64_t expectedEntryCount) {
Projection.cpp:1149:     if (volumePresent || NeedsReload(volumeRowId)) {
Projection.cpp:1156:         CompactStorage();
Projection.cpp:1158:     evictedVolumes_.erase(volumeRowId);
Projection.cpp:1177:             evictedVolumes_.insert(volumeRowId);
Projection.cpp:1202:         CompactStorage();
Projection.cpp:1203:         evictedVolumes_.insert(volumeRowId);
Projection.cpp:1205:         EnforceMemoryLimit();
Projection.cpp:1226:     EnforceMemoryLimit();
Projection.cpp:1227:     return !NeedsReload(volumeRowId);
indexstore/src/Store.cpp:725:         Statement eligibility(db_, "SELECT available FROM volumes WHERE id=?;");
indexstore/src/Store.cpp:726:         ok = eligibility.Valid();
indexstore/src/Store.cpp:728:             sqlite3_bind_int64(eligibility, 1, static_cast<sqlite3_int64>(volumeRowId));
indexstore/src/Store.cpp:729:             ok = sqlite3_step(eligibility) == SQLITE_ROW && sqlite3_column_int(eligibility, 0) == 0;
indexstore/src/Projection.cpp:949:     if (!filenameIndexValid_ || foldedNeedle.size() < 3 || !IsAscii(foldedNeedle)) {
engine/src/IndexPipeline.h:399:     static constexpr size_t kMaximumFilenameIndexCandidates = 16 * 1024;
engine/src/IndexPipeline.cpp:365: // cold-stream prefilter eligibility. An ASCII-folded simple literal needle of
engine/src/IndexPipeline.cpp:1500:                         const bool filenameEligible = searchRequest.query.simpleLiteralName &&
engine/src/IndexPipeline.cpp:1502:                         if (filenameEligible) {
engine/src/IndexPipeline.cpp:1504:                                 searchRequest.query.simpleLiteralFolded, kMaximumFilenameIndexCandidates);
search/src/Query.cpp:212:         return foldedNeedle.size() >= 4
setup/src/AuthenticodeVerification.cpp:108:         if (length == 0 || length >= 32768) {
setup/src/PackageTrust.cpp:189:         if (length == 0 || length >= 32768) {
ui/src/NavigationChrome.cpp:228:     if (!IsChromeTabTraversalEligible(window_ != nullptr, owner_ != nullptr, editing_, focusIsShellSurface,
ui/include/ffui/ChromeTabTraversal.h:35: inline bool IsChromeTabTraversalEligible(bool chromeWindowValid, bool ownerValid, bool editing,
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
find: C:/Users/Michael Roy/Documents/FastFiles
std::vector<uint64_t> UniqueTrigrams(std::u16string_view value) {
    std::vector<uint64_t> result;
    if (value.size() < 3) return result;
    result.reserve(value.size() - 2);
    for (size_t index = 0; index + 2 < value.size(); ++index) {
        result.push_back(PackTrigram(value, index));
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
void InsertSorted(std::vector<uint32_t>& values, uint32_t value) {
    const auto it = std::lower_bound(values.begin(), values.end(), value);
    if (it == values.end() || *it != value) values.insert(it, value);
}
void EraseSorted(std::vector<uint32_t>& values, uint32_t value) {
    const auto it = std::lower_bound(values.begin(), values.end(), value);
    if (it != values.end() && *it == value) values.erase(it);
}
// large-index-lifecycle-and-search-scalability 4.4: batches above this size
// drop the filename accelerator wholesale and rebuild it once instead of
// paying per-entry sorted-vector posting erases (quadratic in index size).
// Small batches keep the per-entry path so the accelerator stays hot without
// a full O(N) rebuild; both paths leave identical observable state.
constexpr size_t kPerEntryFilenameRemovalThreshold = 256;
} // namespace
void Projection::SetMaximumMemoryBytes(const size_t maximumBytes) {
    maximumMemoryBytes_ = maximumBytes == 0 ? kDefaultMaximumMemoryBytes : maximumBytes;
    // An explicit limit change is synchronous and strict even during a bulk
    // scan; ingestion-path watermarks still defer per-record compaction but an
    // explicit caller expectation (e.g. tests or a settings change) must not be
    // silently ignored until the scan ends.
    const bool wasEnforcing = enforcingMemoryLimit_;
    enforcingMemoryLimit_ = true;
    EnforceMemoryLimitTo(maximumMemoryBytes_);
    enforcingMemoryLimit_ = wasEnforcing;
}
bool Projection::EnforceMemoryLimitNow() {
    return EnforceMemoryLimit();
}
void Projection::SetBulkScanActive(const bool active) noexcept {
    if (active) {
        InvalidateFilenameIndex();
        enforcingMemoryLimit_ = false;
    } else {
        enforcingMemoryLimit_ = true;
        // large-index-lifecycle-and-search-scalability review-lane C
        // Finding 3: ending the scan must revive the accelerator, otherwise
        // it stays dead after every bulk scan (nothing else rebuilds it) and
        // every selective query silently falls back to the canonical scan.
        try {
            RebuildFilenameIndex();
        } catch (...) {
            InvalidateFilenameIndex();
        }
    }
    bulkScanActive_ = active;
}
#ifdef FF_INDEXSTORE_TESTING
void Projection::SetTestingFoldExceptionalUnit(const char16_t unit, const bool exceptional) {
    // large-index-lifecycle-and-search-scalability review-remediation (P2-A):
    // testing-only fold-exceptional override; process-wide and idempotent.
    gTestingForceFoldExceptional[static_cast<uint16_t>(unit)].store(
        exceptional ? 1 : 0, std::memory_order_release);
}
#endif
size_t Projection::EstimatedMemoryBytes() const noexcept {
    size_t result = entries_.capacity() * sizeof(ProjectionEntry)
        + freeList_.capacity() * sizeof(uint32_t)
        + idToIndex_.MemoryUsageBytes()
        + parentToChildren_.MemoryUsageBytes()
        + namePool_.MemoryUsageBytes()
        + filenameExceptionalEntries_.capacity() * sizeof(uint32_t)
        + lastAccessTimes_.capacity() * sizeof(uint64_t);
    for (const auto& [trigram, posting] : filenameTrigramPostings_) {
        (void)trigram;
        result += sizeof(uint64_t) + sizeof(std::vector<uint32_t>)
            + posting.capacity() * sizeof(uint32_t);
    }
    result += filenameTrigramPostings_.bucket_count() * sizeof(void*);
    return result;
}
// performance-closure-and-multisurface-acceleration 8.1: read-only
// decomposition of the projection's own allocations. Each field is a live
// capacity/size/bucket-count measurement; the `modeledCategoryBytes` sum
// deliberately reproduces EstimatedMemoryBytes() exactly (asserted by
// ffindexstore_projection_tests) so the diagnostic cannot drift from the
// estimator the memory policy uses. Standard-library node links, allocator
// headers, and heap rounding are not guessed here -- they remain the
// disclosed residual in the process/allocator reconciliation.
Projection::MemoryBreakdown Projection::GetMemoryBreakdown() const noexcept {
    MemoryBreakdown result;
    // Rows: dense array plus the freed-slot reuse list.
    result.entryCount = idToIndex_.size();
    result.rowCapacity = entries_.capacity();
    result.rowBytes = result.rowCapacity * sizeof(ProjectionEntry);
    result.rowLiveBytes = result.entryCount * sizeof(ProjectionEntry);
    result.rowSlackBytes = result.rowBytes - result.rowLiveBytes;
    result.freeListSlots = freeList_.capacity();
    result.freeListBytes = result.freeListSlots * sizeof(uint32_t);
    // Identifier map (EntryKey -> dense row index), a flat open-addressing
    // table: three parallel arrays whose capacities are the allocation.
    result.idMapElements = idToIndex_.size();
    result.idMapBuckets = idToIndex_.bucket_count();
    result.idMapKeyBytes = idToIndex_.KeyCapacityBytes();
    result.idMapValueBytes = idToIndex_.ValueCapacityBytes();
    result.idMapStateBytes = idToIndex_.StateCapacityBytes();
    result.idMapBytes = idToIndex_.MemoryUsageBytes();
    // Parent -> children index: one flat map with a contiguous child-index
    // array (no per-key heap vector).
    result.childMapElements = parentToChildren_.ElementCount();
    result.childMapBuckets = parentToChildren_.size();
    result.childMapChildLinks = parentToChildren_.LiveChildLinkCount();
    result.childMapKeyBytes = parentToChildren_.KeyCapacityBytes();
    result.childMapChildBytes = parentToChildren_.ChildCapacityBytes();
    result.childMapStateBytes = parentToChildren_.StateCapacityBytes();
    result.childMapOffsetBytes = parentToChildren_.OffsetCapacityBytes();
    result.childMapCountBytes = parentToChildren_.CountCapacityBytes();
    result.childMapBytes = parentToChildren_.MemoryUsageBytes();
    // Interned names: one UTF-16 arena (original case only) plus an
    // offset/length table and a hash-bucket index of its own. There is no
    // second, normalized copy of any name anywhere in the projection.
    result.uniqueNameCount = namePool_.UniqueNameCount();
    result.nameArenaChars = namePool_.ArenaSizeChars();
    result.nameArenaCapacityChars = namePool_.ArenaCapacityChars();
    result.nameArenaBytes = namePool_.ArenaCapacityBytes();
    result.nameArenaSlackBytes =
        result.nameArenaBytes - result.nameArenaChars * sizeof(char16_t);
    result.nameTableSlotCount = namePool_.TableSlotCount();
    result.nameTableBytes = namePool_.TableSlotCapacityBytes();
    result.nameHashBucketCount = namePool_.HashBucketCount();
    result.nameHashBucketBytes = namePool_.HashBucketArrayBytes();
    result.nameHashNodeCount = result.nameHashBucketCount == 0 ? 0 : namePool_.HashBucketElementCount();
    result.nameHashApproxNodeBytes = namePool_.HashNodeApproxBytes();
    result.nameHashPayloadBytes = namePool_.HashBucketPayloadBytes();
    result.nameBytes = namePool_.MemoryUsageBytes();
    // Secondary filename accelerator: trigram -> sorted posting vectors plus
    // a sorted fold-exceptional entry list (names that could match an ASCII
    // needle through a non-ASCII unit under the host ordinal table, plus all
    // surrogate-bearing names). Posting vector capacity (not only the
    // payload size) is reported, because the vectors are push_back-built and
    // never shrunk to fit.
    result.filenameIndexValid = filenameIndexValid_;
    result.filenameIndexDistinctTrigrams = filenameTrigramPostings_.size();
    for (const auto& [trigram, posting] : filenameTrigramPostings_) {
        (void)trigram;
        result.filenameIndexPostingSlots += posting.capacity();
        result.filenameIndexPostingPayload += posting.size();
    }
    result.filenameIndexPostingBytes =
        result.filenameIndexPostingSlots * sizeof(uint32_t);
    result.filenameIndexPostingHeaderBytes =
        result.filenameIndexDistinctTrigrams * sizeof(std::vector<uint32_t>);
    result.filenameIndexBucketCount = filenameTrigramPostings_.bucket_count();
    result.filenameIndexBucketBytes = filenameTrigramPostings_.bucket_count() * sizeof(void*);
    result.filenameIndexApproxNodeBytes =
        result.filenameIndexDistinctTrigrams * sizeof(uint64_t)
        + result.filenameIndexPostingHeaderBytes;
    result.filenameIndexExceptionalCount = filenameExceptionalEntries_.size();
    result.filenameIndexExceptionalSlots = filenameExceptionalEntries_.capacity();
    result.filenameIndexExceptionalBytes =
        result.filenameIndexExceptionalSlots * sizeof(uint32_t);
    result.filenameIndexBytes = result.filenameIndexPostingBytes
        + result.filenameIndexApproxNodeBytes
        + result.filenameIndexBucketBytes
        + result.filenameIndexExceptionalBytes;
    // LRU eviction metadata: one dense uint64 timestamp per entry slot.
    result.lruSlotCapacity = lastAccessTimes_.capacity();
    result.lruSlotCount = lastAccessTimes_.size();
    result.lruTrackedCount = 0;
    for (const uint64_t timestamp : lastAccessTimes_) {
        if (timestamp != 0) ++result.lruTrackedCount;
    }
    result.lruBytes = result.lruSlotCapacity * sizeof(uint64_t);
    // Cold-volume markers. EstimatedMemoryBytes() does not model this set, so
    // only its bucket array is added to the additive reported total; the
    // per-node link remains unmodeled residual rather than a fabricated size.
    result.evictedVolumeCount = evictedVolumes_.size();
    result.evictedVolumeBucketCount = evictedVolumes_.bucket_count();
    result.evictedVolumeBucketBytes = evictedVolumes_.bucket_count() * sizeof(void*);
    // Structurally absent categories: the matcher folds a name on demand (no
    // stored folded/normalized copy) and paths are rebuilt per call from
    // parent links (ProjectionEntry has no path field).
    result.normalizedNameStorageBytes = 0;
    result.storedPathBytes = 0;
    result.modeledCategoryBytes = result.rowBytes + result.freeListBytes
        + result.idMapBytes + result.childMapBytes + result.nameBytes
        + result.filenameIndexBytes + result.lruBytes;
    result.reportedTotalBytes = result.modeledCategoryBytes
        + result.evictedVolumeBucketBytes + result.nameHashPayloadBytes;
    return result;
}
bool Projection::IsMemoryPressureHigh() const noexcept {
    if (maximumMemoryBytes_ == 0) return false;
    return EstimatedMemoryBytes() >= maximumMemoryBytes_ - maximumMemoryBytes_ / 5;
}
bool Projection::NeedsReload(const VolumeRowId volumeRowId) const {
    return evictedVolumes_.contains(volumeRowId);
}
Projection::MemoryDiagnostics Projection::GetMemoryDiagnostics() const {
    MemoryDiagnostics diagnostics;
    diagnostics.configuredMaximumBytes = maximumMemoryBytes_;
    diagnostics.estimatedBytes = EstimatedMemoryBytes();
    diagnostics.lastEvictionBytesBefore = lastEvictionBytesBefore_;
    diagnostics.lastEvictionBytesAfter = lastEvictionBytesAfter_;
    diagnostics.subtreeEvictionCount = subtreeEvictionCount_;
    diagnostics.coldReloadCount = coldReloadCount_;
    diagnostics.coldReloadFailedCount = coldReloadFailedCount_;
    return diagnostics;
}
void Projection::TouchIndex(const uint32_t index) const noexcept {
    // Dense timestamps cannot allocate, so this cannot fail and needs no
    // failure path; a slot beyond the current vector is simply not tracked
    // (it is initialized when the slot is created).
    if (index >= lastAccessTimes_.size()) return;
    if (accessClock_ == std::numeric_limits<uint64_t>::max()) {
        accessClock_ = 1;
        std::fill(lastAccessTimes_.begin(), lastAccessTimes_.end(), 0);
    } else {
        ++accessClock_;
    }
    lastAccessTimes_[index] = accessClock_;
}
std::optional<EntryKey> Projection::LeastRecentlyUsedEntry() const {
    std::optional<EntryKey> oldest;
    std::optional<EntryKey> oldestDirectory;
    std::optional<EntryKey> rootFallback;
    uint64_t oldestTimestamp = std::numeric_limits<uint64_t>::max();
    uint64_t oldestDirectoryTimestamp = std::numeric_limits<uint64_t>::max();
    uint64_t rootTimestamp = std::numeric_limits<uint64_t>::max();
    idToIndex_.ForEach([&](const EntryKey& key, const uint32_t index) {
        const ProjectionEntry& entry = entries_[index];
        const uint64_t timestamp = index < lastAccessTimes_.size()
            ? lastAccessTimes_[index] : 0;
        if (entry.parentFrn == entry.frn) {
            if (!rootFallback || timestamp < rootTimestamp) {
                rootFallback = key;
                rootTimestamp = timestamp;
            }
            return;
        }
        if ((entry.attributes & kFileAttributeDirectory) != 0) {
            if (!oldestDirectory || timestamp < oldestDirectoryTimestamp) {
                oldestDirectory = key;
                oldestDirectoryTimestamp = timestamp;
            }
            return;
        }
        if (!oldest || timestamp < oldestTimestamp) {
            oldest = key;
            oldestTimestamp = timestamp;
        }
    });
    return oldestDirectory.has_value() ? oldestDirectory : (oldest.has_value() ? oldest : rootFallback);
}
void Projection::EvictSubtree(const EntryKey& rootKey) {
    const uint32_t* rootIndex = idToIndex_.find(rootKey);
    if (rootIndex == nullptr) return;
    std::vector<EntryKey> removalOrder;
    std::vector<EntryKey> pending;
    std::unordered_set<EntryKey, EntryKeyHash> visited;
    pending.push_back(rootKey);
    while (!pending.empty()) {
        const EntryKey current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second) continue;
        const uint32_t* currentIndex = idToIndex_.find(current);
        if (currentIndex == nullptr) continue;
        removalOrder.push_back(current);
        const ProjectionEntry& entry = entries_[*currentIndex];
        if ((entry.attributes & kFileAttributeDirectory) == 0) continue;
        for (const uint32_t childIndex : parentToChildren_.find(current)) {
            if (childIndex >= entries_.size()) continue;
            const ProjectionEntry& child = entries_[childIndex];
            pending.push_back(EntryKey{child.volumeRowId, child.frn});
        }
    }
    // Remove descendants before the selected directory so every child-index
    // slice is still valid when Remove() unlinks the child.
    // large-index-lifecycle-and-search-scalability 4.4: eviction is a bulk
    // removal -- skip per-entry accelerator erases for large subtrees. The
    // caller's CompactStorage() invalidates the accelerator anyway, and the
    // memory-pressure path must not allocate a rebuild here.
    if (removalOrder.size() > kPerEntryFilenameRemovalThreshold) {
        InvalidateFilenameIndex();
    }
    for (auto it = removalOrder.rbegin(); it != removalOrder.rend(); ++it) {
        Remove(it->volumeRowId, it->frn);
    }
    evictedVolumes_.insert(rootKey.volumeRowId);
    ++subtreeEvictionCount_;
}
void Projection::EvictVolume(const VolumeRowId volumeRowId) {
    // memory-bounded-indexed-scale 5.x: whole-volume unload. A flat key
    // snapshot first, then individual removals (Remove unlinks each row's
    // child-index slice), then a single compaction -- never per-row
    // compaction, matching the batch-boundary policy.
    std::vector<EntryKey> keys;
    keys.reserve(idToIndex_.size());
    idToIndex_.ForEach([&](const EntryKey& key, const uint32_t) {
        if (key.volumeRowId == volumeRowId) keys.push_back(key);
    });
    if (keys.empty()) return;
    // large-index-lifecycle-and-search-scalability 4.4: same bulk rule as
    // EvictSubtree -- the trailing CompactStorage() invalidates the
    // accelerator regardless, so per-entry erases are pure waste above the
    // threshold.
    if (keys.size() > kPerEntryFilenameRemovalThreshold) {
        InvalidateFilenameIndex();
    }
    for (const EntryKey& key : keys) {
        Remove(key.volumeRowId, key.frn);
    }
    evictedVolumes_.insert(volumeRowId);
    ++subtreeEvictionCount_;
    CompactStorage();
}
void Projection::CompactStorage() {
    // Free the optional accelerator before allocating replacement projection
    // tables. Keeping old postings alive until the end of this copy can make
    // memory pressure compaction briefly require several times the bounded
    // projection budget; canonical search remains correct while invalidated.
    InvalidateFilenameIndex();
    std::vector<ProjectionEntry> compacted;
    compacted.reserve(idToIndex_.size());
    std::vector<uint64_t> compactedAccessTimes;
    compactedAccessTimes.reserve(idToIndex_.size());
    FlatHashMap<EntryKey, uint32_t, EntryKeyHash> compactedIds;
    compactedIds.reserve(idToIndex_.size());
    FlatChildrenMap compactedChildren;
    NamePool compactedNames;
    idToIndex_.ForEach([&](const EntryKey& key, const uint32_t oldIndex) {
        ProjectionEntry entry = entries_[oldIndex];
        entry.nameId = compactedNames.Intern(namePool_.Get(entry.nameId));
        const uint32_t newIndex = static_cast<uint32_t>(compacted.size());
        compacted.push_back(entry);
        compactedAccessTimes.push_back(oldIndex < lastAccessTimes_.size()
            ? lastAccessTimes_[oldIndex] : 0);
        compactedIds.insert(key, newIndex);
        if (entry.parentFrn != entry.frn) {
            compactedChildren.insert(EntryKey{entry.volumeRowId, entry.parentFrn}, newIndex);
        }
    });
    entries_ = std::move(compacted);
    lastAccessTimes_ = std::move(compactedAccessTimes);
    freeList_.clear();
    idToIndex_ = std::move(compactedIds);
    parentToChildren_ = std::move(compactedChildren);
    namePool_ = std::move(compactedNames);
    denseIterationOrderValid_ = true;
}
bool Projection::EnforceMemoryLimit() {
    if (!enforcingMemoryLimit_) return false;
    // Leave headroom after a compaction so the next ingestion batches do not
    // immediately re-enter the full-index rebuild path.
    const size_t lowWatermark = maximumMemoryBytes_ - maximumMemoryBytes_ / 5;
    // large-index-lifecycle-and-search-scalability review-lane D P2-1:
    // bounded multi-pass enforcement. One pass can free a single small victim
    // (flat corpora fall back to one-file victims), which never converges
    // under sustained ingestion; loop while above the watermark and passes
    // keep making progress, with a hard pass bound to keep batch latency
    // predictable (each pass is one subtree eviction + one O(N) compaction).
    constexpr size_t kMaxEnforcementPasses = 8;
    bool evictedAny = false;
    for (size_t pass = 0; pass < kMaxEnforcementPasses; ++pass) {
        if (EstimatedMemoryBytes() <= lowWatermark) break;
        const size_t before = EstimatedMemoryBytes();
        if (!EnforceMemoryLimitOnceTo(lowWatermark)) break;
        evictedAny = true;
        if (EstimatedMemoryBytes() >= before) break; // no progress guard
    }
    return evictedAny;
}
bool Projection::EnforceMemoryLimitTo(const size_t targetBytes) {
    if (!enforcingMemoryLimit_) return false;
    bool evictedAny = false;
    while (EstimatedMemoryBytes() > targetBytes) {
        const auto victim = LeastRecentlyUsedEntry();
        if (!victim.has_value()) break;
        const size_t before = EstimatedMemoryBytes();
        EvictSubtree(*victim);
        CompactStorage();
        evictedAny = true;
        // memory-bounded-indexed-scale 3.1: the multi-pass shrink path records
        // each pass exactly like the single-shot watermark path.
        lastEvictionBytesBefore_ = before;
        lastEvictionBytesAfter_ = EstimatedMemoryBytes();
        if (EstimatedMemoryBytes() >= before && idToIndex_.empty()) break;
    }
    return evictedAny;
}
bool Projection::EnforceMemoryLimitOnceTo(const size_t targetBytes) {
    if (!enforcingMemoryLimit_ || EstimatedMemoryBytes() <= targetBytes) return false;
    const auto victim = LeastRecentlyUsedEntry();
    if (!victim.has_value()) return false;
    const size_t bytesBefore = EstimatedMemoryBytes();
    EvictSubtree(*victim);
    CompactStorage();
    // memory-bounded-indexed-scale 3.1: record the policy's actual effect so
    // tests can distinguish "eviction ran" from "eviction changed nothing".
    lastEvictionBytesBefore_ = bytesBefore;
    lastEvictionBytesAfter_ = EstimatedMemoryBytes();
    return true;
}
bool Projection::ReloadEvictedVolume(Store& store, const VolumeRowId volumeRowId) {
    if (!NeedsReload(volumeRowId)) return true;
    ++coldReloadCount_;
    const auto metadata = store.GetVolumeMetadata(volumeRowId);
    if (!metadata.has_value()) {
        ++coldReloadFailedCount_;
        return false;
    }
    const bool rebuilt = RebuildVolumeFromStore(store, volumeRowId, metadata->entryCount);
    const bool reloaded = rebuilt && !NeedsReload(volumeRowId);
    if (!reloaded) ++coldReloadFailedCount_;
    return reloaded;
}
bool Projection::ReloadAllEvictedVolumes(Store& store) {
    std::vector<VolumeRowId> volumeIds;
    volumeIds.reserve(evictedVolumes_.size());
    for (const VolumeRowId volumeId : evictedVolumes_) {
        volumeIds.push_back(volumeId);
    }
    bool allReloaded = true;
    for (const VolumeRowId volumeId : volumeIds) {
        if (!ReloadEvictedVolume(store, volumeId)) {
            allReloaded = false;
        }
    }
    return allReloaded;
}
void Projection::Reserve(size_t expectedEntryCount) {
    entries_.reserve(expectedEntryCount);
    idToIndex_.reserve(expectedEntryCount);
    // Names recur constantly across a real tree (D2's rationale) -- a
    // quarter of the entry count is a reasonable rough estimate for the
    // unique-name-count hint; NamePool grows past this fine if wrong.
    namePool_.Reserve(expectedEntryCount / 4 + 16, expectedEntryCount * 12);
}
void Projection::CompactEntriesForIterationOrder() {
    if (!freeList_.empty() || entries_.empty()) return;
    std::vector<uint32_t> remap(entries_.size());
    std::vector<ProjectionEntry> reordered;
    reordered.reserve(entries_.size());
    // performance-closure-and-multisurface-acceleration 8.3: the dense LRU
    // timestamps live per entry slot, so they must be permuted with the rows.
    // Leaving them in the old slot order would make the eviction policy read
    // another row's access time after this compaction.
    std::vector<uint64_t> reorderedAccessTimes;
    reorderedAccessTimes.reserve(entries_.size());
    idToIndex_.ForEach([&](const EntryKey&, const uint32_t oldIndex) {
        remap[oldIndex] = static_cast<uint32_t>(reordered.size());
        reordered.push_back(entries_[oldIndex]);
        reorderedAccessTimes.push_back(oldIndex < lastAccessTimes_.size()
            ? lastAccessTimes_[oldIndex] : 0);
    });
    if (reordered.size() != entries_.size()) return;
    idToIndex_.RemapValues([&](const uint32_t oldIndex) { return remap[oldIndex]; });
    parentToChildren_.RemapValues([&](const uint32_t oldIndex) { return remap[oldIndex]; });
    entries_ = std::move(reordered);
    lastAccessTimes_ = std::move(reorderedAccessTimes);
[661 more lines in file. Use offset=600 to continue.]
tests/engine/bench_allocation_isolation.cpp
tests/engine/bench_first_generation_publish.cpp
tests/engine/bench_large_index_scalability.cpp
tests/engine/bench_secondary_filename_index.cpp
tests/indexstore/bench_projection_memory.cpp
tests/indexstore/bench_rebuild_isolation.cpp
tests/search/bench_literal_matcher.cpp
grep: C:/Users/Michael Roy/Documents/FastFiles/tests
fileoperations/test_file_operation_policy.cpp:203:         Check(performedStorage != nullptr, "performed-effect fixture allocation failed");
fileoperations/test_file_operation_policy.cpp:206:             Check(value != nullptr, "performed-effect fixture lock failed");
e2e/Test-E2EHarness.ps1:17: $inside = Join-Path $root 'fixture\file.txt'
e2e/Test-E2EHarness.ps1:24:     Check $false 'the assert helper rejects paths outside the fixture root'
e2e/Test-E2EHarness.ps1:26:     Check $true 'the assert helper rejects paths outside the fixture root'
indexstore/bench_rebuild_isolation.cpp:17: //   G0  diagnostic fixture population (<=2-char names: no trigrams)
indexstore/bench_rebuild_isolation.cpp:26: // Fixture shape mirrors tests/engine/bench_large_index_scalability.cpp
indexstore/bench_rebuild_isolation.cpp:271:     double fixtureGenerationMs = 0.0;
indexstore/bench_rebuild_isolation.cpp:297: bool BuildFixtureStore(ffindexstore::Store& store, size_t scale, ffindexstore::VolumeRowId& volumeId,
indexstore/bench_rebuild_isolation.cpp:376:         if (!BuildFixtureStore(store, scale, result.volumeRowId, result.fixtureGenerationMs)) {
indexstore/bench_rebuild_isolation.cpp:377:             result.failureReason = "fixture generation failed";
indexstore/bench_rebuild_isolation.cpp:385:         std::printf("fixture rows=%zu in %.2f s\n", rowCount, result.fixtureGenerationMs / 1000.0);
indexstore/bench_rebuild_isolation.cpp:593:                     "RemoveVolume on the no-trigram fixture (production semantics)",
indexstore/bench_rebuild_isolation.cpp:624:                     "RemoveVolume on the no-trigram fixture with child unlink skipped (residual ceiling)",
indexstore/bench_rebuild_isolation.cpp:657:         output << "      \"fixture_generation_ms\": " << scale.fixtureGenerationMs << ",\n";
drift/CMakeLists.txt:14:   # Adversarial fixture matrix for the semantic truth contracts (hermetic
drift/CMakeLists.txt:17:     NAME ffdoc_truth_fixture_tests
drift/CMakeLists.txt:19:             "${PROJECT_SOURCE_DIR}/tests/drift/test_doc_truth_fixtures.ps1"
drift/CMakeLists.txt:21:   set_tests_properties(ffdoc_truth_fixture_tests PROPERTIES TIMEOUT 300)
drift/CMakeLists.txt:23:   # Repository-state provenance fixtures use disposable Git repositories to
drift/CMakeLists.txt:26:     NAME ffsource_provenance_fixture_tests
drift/CMakeLists.txt:28:             "${PROJECT_SOURCE_DIR}/tests/drift/test_source_provenance_fixtures.ps1"
drift/CMakeLists.txt:30:   set_tests_properties(ffsource_provenance_fixture_tests PROPERTIES TIMEOUT 180)
e2e/README.md:20: timeouts, fixture safety, and failure evidence.
indexstore/test_store_read_session.cpp:164:     }), "read-session fault fixture commits");
indexstore/test_store_read_session.cpp:258:     }), "prefilter fixture commits");
verification/CMakeLists.txt:2: # worker-boundary fixtures are plain PowerShell tests and use a temp fixture
verification/CMakeLists.txt:60:   # process classification with controlled npm/npx fixtures; no network.
verification/CMakeLists.txt:78:   # fixture-driven through injectable probes; never touches real service,
verification/CMakeLists.txt:88:   # injectable detection fixtures and requested-vs-executed evidence shape;
fileoperations/test_file_operations_e2e.cpp:87:     Check(handle != INVALID_HANDLE_VALUE, "could not create sparse-file fixture");
fileoperations/test_file_operations_e2e.cpp:204:           "undo fixture move failed");
fileoperations/test_file_operations_e2e.cpp:412:     std::wstring longPathFixture;
fileoperations/test_file_operations_e2e.cpp:418:         longPathFixture = deepDirectory;
fileoperations/test_file_operations_e2e.cpp:419:         Check(std::filesystem::create_directories(deepDirectory), "long-path fixture directory not created");
fileoperations/test_file_operations_e2e.cpp:423:         Check(deepHandle != INVALID_HANDLE_VALUE, "long-path fixture file not created");
fileoperations/test_file_operations_e2e.cpp:436:         removeTree(longPathFixture);
fileoperations/test_file_operations_e2e.cpp:437:         Check(!std::filesystem::exists(longPathFixture.substr(4)), "long-path fixture cleanup failed");
drift/test_binary_identity_binding.ps1:72: # This targeted test binary embeds kBuildIdentity in its signed-package fixture
drift/test_binary_identity_binding.ps1:78: Check ($packageTrustText.Contains($overrideId)) 'ffsetup_package_trust_tests embeds the expected source identity in its fixture payload'
preview/test_preview.cpp:111:     Check(!error, "preview fixture cleanup failed");
preview/test_preview.cpp:145:     Check(!error, "preview controller fixture cleanup failed");
preview/test_preview.cpp:220:     Check(!error, "preview rapid-burst fixture cleanup failed");
preview/test_preview.cpp:261:     Check(!error, "utf8 fixture cleanup failed");
preview/test_preview.cpp:293:     first.path = L"C:\\fixture\\note.txt";
preview/test_preview.cpp:303:     changedPath.path = L"C:\\fixture\\other.txt";
preview/test_preview.cpp:328:     Check(fileSize > 256ull * 1024ull, "fixture must exceed the default byte ceiling");
preview/test_preview.cpp:353:     Check(!error, "preview-limit fixture cleanup failed");
drift/test_source_provenance_fixtures.ps1:10: function Invoke-FixtureGit {
drift/test_source_provenance_fixtures.ps1:13:     if ($LASTEXITCODE -ne 0) { throw "fixture git command failed: git $($GitArgs -join ' '): $($output -join ' ')" }
drift/test_source_provenance_fixtures.ps1:17: $fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('FastFiles-source-provenance-' + [guid]::NewGuid().ToString('N'))
drift/test_source_provenance_fixtures.ps1:22:     New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
drift/test_source_provenance_fixtures.ps1:24:     Invoke-FixtureGit $fixtureRoot init | Out-Null
drift/test_source_provenance_fixtures.ps1:25:     Invoke-FixtureGit $fixtureRoot config user.name 'FastFiles Fixture' | Out-Null
drift/test_source_provenance_fixtures.ps1:26:     Invoke-FixtureGit $fixtureRoot config user.email 'fixture@example.invalid' | Out-Null
drift/test_source_provenance_fixtures.ps1:27:     Set-Content -LiteralPath (Join-Path $fixtureRoot '.gitignore') -Value @('build/', 'verify/status/generated/') -Encoding utf8NoBOM
drift/test_source_provenance_fixtures.ps1:28:     Set-Content -LiteralPath (Join-Path $fixtureRoot 'tracked.txt') -Value 'committed-stale' -Encoding utf8NoBOM
drift/test_source_provenance_fixtures.ps1:29:     Invoke-FixtureGit $fixtureRoot add . | Out-Null
drift/test_source_provenance_fixtures.ps1:30:     Invoke-FixtureGit $fixtureRoot commit -m baseline | Out-Null
drift/test_source_provenance_fixtures.ps1:32:     $clean = Get-RepositorySourceState -RepoRoot $fixtureRoot -QualificationMode exact-tree
[60 matches limit reached. Use limit=120 for more, or refine pattern]
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
bench_large_index_scalability.cpp-37- #define FFBENCH_ALLOCATOR_PROBE_DEFINE
bench_large_index_scalability.cpp-38- #include "bench_allocator_probe.h"
bench_large_index_scalability.cpp-39- #include "ffsetup/BuildIdentity.h"
bench_large_index_scalability.cpp-40- 
bench_large_index_scalability.cpp:41: namespace {
bench_large_index_scalability.cpp-42- 
bench_large_index_scalability.cpp-43- using Clock = std::chrono::steady_clock;
bench_large_index_scalability.cpp-44- 
bench_large_index_scalability.cpp-45- constexpr size_t kBatchSize = 8192;
bench_large_index_scalability.cpp-164-     bool memoryReport = false;
bench_large_index_scalability.cpp-165-     size_t rebuildCycles = 4;
bench_large_index_scalability.cpp-166-     size_t exportBoundDirectories = 4096;
bench_large_index_scalability.cpp-167-     // Provenance recorded into the memory report so the artifact identifies
bench_large_index_scalability.cpp:168:     // its own build and command instead of relying on an ignored file name.
bench_large_index_scalability.cpp-169-     std::string commandLine;
bench_large_index_scalability.cpp-170-     std::string sourceState;   // caller-supplied (clean/dirty), verified by the runner
bench_large_index_scalability.cpp-171-     std::string sourceSha;     // caller-supplied git revision
bench_large_index_scalability.cpp-172-     std::string preset;        // caller-supplied configure preset
bench_large_index_scalability.cpp-277-     }
bench_large_index_scalability.cpp-278-     return {};
bench_large_index_scalability.cpp-279- }
bench_large_index_scalability.cpp-280- 
bench_large_index_scalability.cpp:281: ffprotocol::MftRecordV1 MakeMftRecord(uint64_t id, uint64_t parent, const std::wstring& name,
bench_large_index_scalability.cpp-282-                                       uint32_t attributes, uint64_t sizeBytes, uint64_t timestamp) {
bench_large_index_scalability.cpp-283-     ffprotocol::MftRecordV1 record{};
bench_large_index_scalability.cpp-284-     record.fixed.fileReferenceNumber = id;
bench_large_index_scalability.cpp-285-     record.fixed.parentFileReferenceNumber = parent;
bench_large_index_scalability.cpp-287-     record.fixed.lastModifiedTime = timestamp + 100;
bench_large_index_scalability.cpp-288-     record.fixed.lastAccessTime = timestamp + 200;
bench_large_index_scalability.cpp-289-     record.fixed.sizeBytes = sizeBytes;
bench_large_index_scalability.cpp-290-     record.fixed.fileAttributes = attributes;
bench_large_index_scalability.cpp:291:     record.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
bench_large_index_scalability.cpp-292-     record.fileName = ToUtf16(name);
bench_large_index_scalability.cpp-293-     return record;
bench_large_index_scalability.cpp-294- }
bench_large_index_scalability.cpp-295- 
bench_large_index_scalability.cpp-288-     record.fixed.lastAccessTime = timestamp + 200;
bench_large_index_scalability.cpp-289-     record.fixed.sizeBytes = sizeBytes;
bench_large_index_scalability.cpp-290-     record.fixed.fileAttributes = attributes;
bench_large_index_scalability.cpp-291-     record.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
bench_large_index_scalability.cpp:292:     record.fileName = ToUtf16(name);
bench_large_index_scalability.cpp-293-     return record;
bench_large_index_scalability.cpp-294- }
bench_large_index_scalability.cpp-295- 
bench_large_index_scalability.cpp-296- std::wstring DirectoryName(size_t index) {
bench_large_index_scalability.cpp-292-     record.fileName = ToUtf16(name);
bench_large_index_scalability.cpp-293-     return record;
bench_large_index_scalability.cpp-294- }
bench_large_index_scalability.cpp-295- 
bench_large_index_scalability.cpp:296: std::wstring DirectoryName(size_t index) {
bench_large_index_scalability.cpp-297-     return L"folder_" + std::to_wstring(index % 997) + L"_branch_" + std::to_wstring(index);
bench_large_index_scalability.cpp-298- }
bench_large_index_scalability.cpp-299- 
bench_large_index_scalability.cpp-300- std::wstring FileName(size_t index) {
bench_large_index_scalability.cpp-296- std::wstring DirectoryName(size_t index) {
bench_large_index_scalability.cpp-297-     return L"folder_" + std::to_wstring(index % 997) + L"_branch_" + std::to_wstring(index);
bench_large_index_scalability.cpp-298- }
bench_large_index_scalability.cpp-299- 
bench_large_index_scalability.cpp:300: std::wstring FileName(size_t index) {
bench_large_index_scalability.cpp-301-     const size_t pattern = index % 10;
bench_large_index_scalability.cpp-302-     if (index == 7391) return L"unique_component_7391.txt";
bench_large_index_scalability.cpp-303-     if (pattern == 0 || pattern == 1) return L"common_component_" + std::to_wstring(index) + L".txt";
bench_large_index_scalability.cpp-304-     if (pattern == 2) return L"report_" + std::to_wstring(index) + L".txt";
bench_large_index_scalability.cpp-307-     if (pattern == 5) return L"project_" + std::to_wstring(index) + L".cpp";
bench_large_index_scalability.cpp-308-     if (pattern == 6) return L"notes_" + std::to_wstring(index) + L".md";
bench_large_index_scalability.cpp-309-     if (pattern == 7) return L"common_media_" + std::to_wstring(index) + L".mp4";
bench_large_index_scalability.cpp-310-     // performance-closure-and-multisurface-acceleration 7.1: a deterministic
bench_large_index_scalability.cpp:311:     // Unicode-name subset (10% of the fixture) so the Unicode query class is
bench_large_index_scalability.cpp-312-     // measured against real matches, not only the no-result path. Escape
bench_large_index_scalability.cpp-313-     // sequences keep the source encoding-independent.
bench_large_index_scalability.cpp-314-     if (pattern == 8) return L"\u0441\u043f\u0438\u0441\u043e\u043a_" + std::to_wstring(index) + L".txt";
bench_large_index_scalability.cpp-315-     return L"backup_" + std::to_wstring(index) + L".7z";
bench_large_index_scalability.cpp-372-     bool wireTruncated = false;
bench_large_index_scalability.cpp-373-     size_t wireResultCount = 0;
bench_large_index_scalability.cpp-374-     bool cancelled = false;
bench_large_index_scalability.cpp-375-     bool unavailable = false;
bench_large_index_scalability.cpp:376:     bool filenameIndexUsed = false;
bench_large_index_scalability.cpp-377-     size_t filenameIndexCandidateCount = 0;
bench_large_index_scalability.cpp-378-     uint64_t searchGeneration = 0;
bench_large_index_scalability.cpp-379-     uint64_t queryVisibleEpoch = 0;
bench_large_index_scalability.cpp-380-     uint64_t dependencySetEpoch = 0;
bench_large_index_scalability.cpp-373-     size_t wireResultCount = 0;
bench_large_index_scalability.cpp-374-     bool cancelled = false;
bench_large_index_scalability.cpp-375-     bool unavailable = false;
bench_large_index_scalability.cpp-376-     bool filenameIndexUsed = false;
bench_large_index_scalability.cpp:377:     size_t filenameIndexCandidateCount = 0;
bench_large_index_scalability.cpp-378-     uint64_t searchGeneration = 0;
bench_large_index_scalability.cpp-379-     uint64_t queryVisibleEpoch = 0;
bench_large_index_scalability.cpp-380-     uint64_t dependencySetEpoch = 0;
bench_large_index_scalability.cpp-381-     size_t generationRestarts = 0;
bench_large_index_scalability.cpp-390-     size_t peakDurableReaders = 0;
bench_large_index_scalability.cpp-391-     double sqliteMs = 0.0;
bench_large_index_scalability.cpp-392-     double projectionTraversalMs = 0.0;
bench_large_index_scalability.cpp-393-     double projectionEnumerationMs = 0.0;
bench_large_index_scalability.cpp:394:     double namePoolLookupMs = 0.0;
bench_large_index_scalability.cpp-395-     double nameViewConstructionMs = 0.0;
bench_large_index_scalability.cpp-396-     double materializationMs = 0.0;
bench_large_index_scalability.cpp-397-     double lightweightCandidateMaterializationMs = 0.0;
bench_large_index_scalability.cpp-398-     double lazyPathMaterializationMs = 0.0;
bench_large_index_scalability.cpp-391-     double sqliteMs = 0.0;
bench_large_index_scalability.cpp-392-     double projectionTraversalMs = 0.0;
bench_large_index_scalability.cpp-393-     double projectionEnumerationMs = 0.0;
bench_large_index_scalability.cpp-394-     double namePoolLookupMs = 0.0;
bench_large_index_scalability.cpp:395:     double nameViewConstructionMs = 0.0;
bench_large_index_scalability.cpp-396-     double materializationMs = 0.0;
bench_large_index_scalability.cpp-397-     double lightweightCandidateMaterializationMs = 0.0;
bench_large_index_scalability.cpp-398-     double lazyPathMaterializationMs = 0.0;
bench_large_index_scalability.cpp-399-     size_t lazyPathMaterializationCount = 0;
bench_large_index_scalability.cpp-419-     size_t coldRowsExamined = 0;
bench_large_index_scalability.cpp-420-     size_t coldVolumesSearched = 0;
bench_large_index_scalability.cpp-421-     double storeStreamMs = 0.0;
bench_large_index_scalability.cpp-422-     bool coldSearchUsed = false;
bench_large_index_scalability.cpp:423:     size_t namePoolLookups = 0;
bench_large_index_scalability.cpp-424-     size_t emptyNamesRejected = 0;
bench_large_index_scalability.cpp-425-     size_t nameViewsConstructed = 0;
bench_large_index_scalability.cpp-426-     size_t queryPredicateEvaluations = 0;
bench_large_index_scalability.cpp-427-     size_t queryTermEvaluations = 0;
bench_large_index_scalability.cpp-420-     size_t coldVolumesSearched = 0;
bench_large_index_scalability.cpp-421-     double storeStreamMs = 0.0;
bench_large_index_scalability.cpp-422-     bool coldSearchUsed = false;
bench_large_index_scalability.cpp-423-     size_t namePoolLookups = 0;
bench_large_index_scalability.cpp:424:     size_t emptyNamesRejected = 0;
bench_large_index_scalability.cpp-425-     size_t nameViewsConstructed = 0;
bench_large_index_scalability.cpp-426-     size_t queryPredicateEvaluations = 0;
bench_large_index_scalability.cpp-427-     size_t queryTermEvaluations = 0;
bench_large_index_scalability.cpp-428-     size_t caseInsensitiveComparisons = 0;
bench_large_index_scalability.cpp-421-     double storeStreamMs = 0.0;
bench_large_index_scalability.cpp-422-     bool coldSearchUsed = false;
bench_large_index_scalability.cpp-423-     size_t namePoolLookups = 0;
bench_large_index_scalability.cpp-424-     size_t emptyNamesRejected = 0;
bench_large_index_scalability.cpp:425:     size_t nameViewsConstructed = 0;
bench_large_index_scalability.cpp-426-     size_t queryPredicateEvaluations = 0;
bench_large_index_scalability.cpp-427-     size_t queryTermEvaluations = 0;
bench_large_index_scalability.cpp-428-     size_t caseInsensitiveComparisons = 0;
bench_large_index_scalability.cpp-429-     size_t caseFoldOperations = 0;
bench_large_index_scalability.cpp-437-     size_t pathNeededDecisions = 0;
bench_large_index_scalability.cpp-438-     size_t pathLookups = 0;
bench_large_index_scalability.cpp-439-     size_t matchedRows = 0;
bench_large_index_scalability.cpp-440-     size_t candidateObjectsMaterialized = 0;
bench_large_index_scalability.cpp:441:     size_t candidateNameCopies = 0;
bench_large_index_scalability.cpp-442-     size_t searchResultObjectsMaterialized = 0;
bench_large_index_scalability.cpp-443-     size_t wireBytes = 0;
bench_large_index_scalability.cpp-444-     MemorySample memory;
bench_large_index_scalability.cpp-445- };
bench_large_index_scalability.cpp-491-     std::string databaseStorage;
bench_large_index_scalability.cpp-492- };
bench_large_index_scalability.cpp-493- 
bench_large_index_scalability.cpp-494- struct QueryCase {
bench_large_index_scalability.cpp:495:     const char* name;
bench_large_index_scalability.cpp-496-     const wchar_t* query;
bench_large_index_scalability.cpp-497-     uint8_t sortField;
bench_large_index_scalability.cpp-498-     uint32_t maxResults = 10000;
bench_large_index_scalability.cpp-499- };
bench_large_index_scalability.cpp-497-     uint8_t sortField;
bench_large_index_scalability.cpp-498-     uint32_t maxResults = 10000;
bench_large_index_scalability.cpp-499- };
bench_large_index_scalability.cpp-500- 
bench_large_index_scalability.cpp:501: std::string ReadCpuName() {
bench_large_index_scalability.cpp-502-     int registers[4]{};
bench_large_index_scalability.cpp-503-     __cpuid(registers, 0x80000000);
bench_large_index_scalability.cpp-504-     const unsigned int maxExtendedLeaf = static_cast<unsigned int>(registers[0]);
bench_large_index_scalability.cpp-505-     if (maxExtendedLeaf < 0x80000004U) {
bench_large_index_scalability.cpp-532-     GetNativeSystemInfo(&systemInfo);
bench_large_index_scalability.cpp-533-     MEMORYSTATUSEX memory{};
bench_large_index_scalability.cpp-534-     memory.dwLength = sizeof(memory);
bench_large_index_scalability.cpp-535-     GlobalMemoryStatusEx(&memory);
bench_large_index_scalability.cpp:536:     return {ReadCpuName(), systemInfo.dwNumberOfProcessors, memory.ullTotalPhys, ReadWindowsVersion(),
bench_large_index_scalability.cpp-537-             "MSVC " + std::to_string(_MSC_VER) + "." + std::to_string(_MSC_FULL_VER),
bench_large_index_scalability.cpp-538- #ifdef NDEBUG
bench_large_index_scalability.cpp-539-             "Release",
bench_large_index_scalability.cpp-540- #else
bench_large_index_scalability.cpp-602-     batch.push_back(MakeMftRecord(5, 5, L"", 0x10, 0, 1));
bench_large_index_scalability.cpp-603-     for (size_t index = 0; index < directoryCount && 1 + index < scale; ++index) {
bench_large_index_scalability.cpp-604-         const uint64_t id = 100 + static_cast<uint64_t>(index);
bench_large_index_scalability.cpp-605-         const uint64_t parent = index < 16 ? 5 : 100 + static_cast<uint64_t>((index - 1) / 16);
bench_large_index_scalability.cpp:606:         batch.push_back(MakeMftRecord(id, parent, DirectoryName(index), 0x10, 0, 10 + index));
bench_large_index_scalability.cpp-607-         if (batch.size() == kBatchSize && !flush()) return false;
bench_large_index_scalability.cpp-608-     }
bench_large_index_scalability.cpp-609- 
bench_large_index_scalability.cpp-610-     const size_t filesToCreate = scale > 1 + directoryCount ? scale - 1 - directoryCount : 0;
bench_large_index_scalability.cpp-612-     for (size_t index = 0; index < filesToCreate; ++index) {
bench_large_index_scalability.cpp-613-         const uint64_t parent = 100 + static_cast<uint64_t>(index % directoryCount);
bench_large_index_scalability.cpp-614-         const uint64_t id = fileIdBase + static_cast<uint64_t>(index);
bench_large_index_scalability.cpp-615-         const uint64_t sizeBytes = 1024 + static_cast<uint64_t>((index * 7919) % (64 * 1024 * 1024));
bench_large_index_scalability.cpp:616:         batch.push_back(MakeMftRecord(id, parent, FileName(index), 0, sizeBytes, 1000 + index));
bench_large_index_scalability.cpp-617-         if (batch.size() == kBatchSize && !flush()) return false;
bench_large_index_scalability.cpp-618-     }
bench_large_index_scalability.cpp-619-     if (!flush()) return false;
bench_large_index_scalability.cpp-620-     generationMs = ElapsedMs(generationStart);
bench_large_index_scalability.cpp-695-             const auto materializationStart = Clock::now();
bench_large_index_scalability.cpp-696-             std::vector<ffsearch::Candidate> uiCandidates;
bench_large_index_scalability.cpp-697-             uiCandidates.reserve(parsed->candidates.size());
bench_large_index_scalability.cpp-698-             for (const auto& candidate : parsed->candidates) {
bench_large_index_scalability.cpp:699:                 uiCandidates.push_back({candidate.name, candidate.folder, candidate.sizeBytes,
bench_large_index_scalability.cpp-700-                                         candidate.modifiedTime, candidate.isDirectory, candidate.createdTime,
bench_large_index_scalability.cpp-701-                                         candidate.volumeId, candidate.id, candidate.parentId,
bench_large_index_scalability.cpp-702-                                         candidate.persistentIndexed});
bench_large_index_scalability.cpp-703-             }
bench_large_index_scalability.cpp-708-         }
bench_large_index_scalability.cpp-709-     }
bench_large_index_scalability.cpp-710- 
bench_large_index_scalability.cpp-711-     outcome.run.scale = scale;
bench_large_index_scalability.cpp:712:     outcome.run.queryClass = queryCase.name;
bench_large_index_scalability.cpp-713-     outcome.run.query = ToUtf8(queryCase.query);
bench_large_index_scalability.cpp-714-     outcome.run.temperature = temperature;
bench_large_index_scalability.cpp-715-     outcome.run.iteration = iteration;
bench_large_index_scalability.cpp-716-     outcome.run.resultCount = outcome.timings.resultCount;
bench_large_index_scalability.cpp-717-     outcome.run.cancelled = response.status == ffprotocol::IndexedSearchStatus::Cancelled;
bench_large_index_scalability.cpp-718-     outcome.run.unavailable = response.status == ffprotocol::IndexedSearchStatus::Unavailable;
bench_large_index_scalability.cpp-719-     outcome.run.candidateCount = outcome.timings.candidateCount;
bench_large_index_scalability.cpp-720-     outcome.run.candidatesVisited = outcome.timings.candidatesVisited;
bench_large_index_scalability.cpp:721:     outcome.run.filenameIndexUsed = outcome.timings.filenameIndexUsed;
bench_large_index_scalability.cpp-722-     outcome.run.filenameIndexCandidateCount = outcome.timings.filenameIndexCandidateCount;
bench_large_index_scalability.cpp-723-     outcome.run.searchGeneration = outcome.timings.searchGeneration;
bench_large_index_scalability.cpp-724-     outcome.run.queryVisibleEpoch = outcome.timings.queryVisibleEpoch;
bench_large_index_scalability.cpp-725-     outcome.run.dependencySetEpoch = outcome.timings.dependencySetEpoch;
bench_large_index_scalability.cpp-718-     outcome.run.unavailable = response.status == ffprotocol::IndexedSearchStatus::Unavailable;
bench_large_index_scalability.cpp-719-     outcome.run.candidateCount = outcome.timings.candidateCount;
bench_large_index_scalability.cpp-720-     outcome.run.candidatesVisited = outcome.timings.candidatesVisited;
bench_large_index_scalability.cpp-721-     outcome.run.filenameIndexUsed = outcome.timings.filenameIndexUsed;
bench_large_index_scalability.cpp:722:     outcome.run.filenameIndexCandidateCount = outcome.timings.filenameIndexCandidateCount;
bench_large_index_scalability.cpp-723-     outcome.run.searchGeneration = outcome.timings.searchGeneration;
bench_large_index_scalability.cpp-724-     outcome.run.queryVisibleEpoch = outcome.timings.queryVisibleEpoch;
bench_large_index_scalability.cpp-725-     outcome.run.dependencySetEpoch = outcome.timings.dependencySetEpoch;
bench_large_index_scalability.cpp-726-     outcome.run.generationRestarts = outcome.timings.generationRestarts;
bench_large_index_scalability.cpp-734-     outcome.run.truncated = outcome.timings.truncated;
bench_large_index_scalability.cpp-735-     outcome.run.sqliteMs = outcome.timings.sqliteMetadataMs;
bench_large_index_scalability.cpp-736-     outcome.run.projectionTraversalMs = outcome.timings.projectionTraversalMs;
bench_large_index_scalability.cpp-737-     outcome.run.projectionEnumerationMs = outcome.timings.projectionEnumerationMs;
bench_large_index_scalability.cpp:738:     outcome.run.namePoolLookupMs = outcome.timings.namePoolLookupMs;
bench_large_index_scalability.cpp-739-     outcome.run.nameViewConstructionMs = outcome.timings.nameViewConstructionMs;
bench_large_index_scalability.cpp-740-     outcome.run.materializationMs = outcome.timings.candidateMaterializationMs;
bench_large_index_scalability.cpp-741-     outcome.run.lightweightCandidateMaterializationMs = outcome.timings.lightweightCandidateMaterializationMs;
bench_large_index_scalability.cpp-742-     outcome.run.lazyPathMaterializationMs = outcome.timings.lazyPathMaterializationMs;
bench_large_index_scalability.cpp-735-     outcome.run.sqliteMs = outcome.timings.sqliteMetadataMs;
bench_large_index_scalability.cpp-736-     outcome.run.projectionTraversalMs = outcome.timings.projectionTraversalMs;
bench_large_index_scalability.cpp-737-     outcome.run.projectionEnumerationMs = outcome.timings.projectionEnumerationMs;
bench_large_index_scalability.cpp-738-     outcome.run.namePoolLookupMs = outcome.timings.namePoolLookupMs;
bench_large_index_scalability.cpp:739:     outcome.run.nameViewConstructionMs = outcome.timings.nameViewConstructionMs;
bench_large_index_scalability.cpp-740-     outcome.run.materializationMs = outcome.timings.candidateMaterializationMs;
bench_large_index_scalability.cpp-741-     outcome.run.lightweightCandidateMaterializationMs = outcome.timings.lightweightCandidateMaterializationMs;
bench_large_index_scalability.cpp-742-     outcome.run.lazyPathMaterializationMs = outcome.timings.lazyPathMaterializationMs;
bench_large_index_scalability.cpp-743-     outcome.run.lazyPathMaterializationCount = outcome.timings.lazyPathMaterializationCount;
bench_large_index_scalability.cpp-762-     outcome.run.coldRowsExamined = outcome.timings.coldRowsExamined;
bench_large_index_scalability.cpp-763-     outcome.run.coldVolumesSearched = outcome.timings.coldVolumesSearched;
bench_large_index_scalability.cpp-764-     outcome.run.storeStreamMs = outcome.timings.storeStreamMs;
bench_large_index_scalability.cpp-765-     outcome.run.coldSearchUsed = outcome.timings.coldSearchUsed;
bench_large_index_scalability.cpp:766:     outcome.run.namePoolLookups = outcome.timings.namePoolLookups;
bench_large_index_scalability.cpp-767-     outcome.run.emptyNamesRejected = outcome.timings.emptyNamesRejected;
bench_large_index_scalability.cpp-768-     outcome.run.nameViewsConstructed = outcome.timings.nameViewsConstructed;
bench_large_index_scalability.cpp-769-     outcome.run.queryPredicateEvaluations = outcome.timings.queryPredicateEvaluations;
bench_large_index_scalability.cpp-770-     outcome.run.queryTermEvaluations = outcome.timings.queryTermEvaluations;
bench_large_index_scalability.cpp-763-     outcome.run.coldVolumesSearched = outcome.timings.coldVolumesSearched;
bench_large_index_scalability.cpp-764-     outcome.run.storeStreamMs = outcome.timings.storeStreamMs;
bench_large_index_scalability.cpp-765-     outcome.run.coldSearchUsed = outcome.timings.coldSearchUsed;
bench_large_index_scalability.cpp-766-     outcome.run.namePoolLookups = outcome.timings.namePoolLookups;
bench_large_index_scalability.cpp:767:     outcome.run.emptyNamesRejected = outcome.timings.emptyNamesRejected;
bench_large_index_scalability.cpp-768-     outcome.run.nameViewsConstructed = outcome.timings.nameViewsConstructed;
bench_large_index_scalability.cpp-769-     outcome.run.queryPredicateEvaluations = outcome.timings.queryPredicateEvaluations;
bench_large_index_scalability.cpp-770-     outcome.run.queryTermEvaluations = outcome.timings.queryTermEvaluations;
bench_large_index_scalability.cpp-771-     outcome.run.caseInsensitiveComparisons = outcome.timings.caseInsensitiveComparisons;
bench_large_index_scalability.cpp-764-     outcome.run.storeStreamMs = outcome.timings.storeStreamMs;
bench_large_index_scalability.cpp-765-     outcome.run.coldSearchUsed = outcome.timings.coldSearchUsed;
bench_large_index_scalability.cpp-766-     outcome.run.namePoolLookups = outcome.timings.namePoolLookups;
bench_large_index_scalability.cpp-767-     outcome.run.emptyNamesRejected = outcome.timings.emptyNamesRejected;
bench_large_index_scalability.cpp:768:     outcome.run.nameViewsConstructed = outcome.timings.nameViewsConstructed;
bench_large_index_scalability.cpp-769-     outcome.run.queryPredicateEvaluations = outcome.timings.queryPredicateEvaluations;
bench_large_index_scalability.cpp-770-     outcome.run.queryTermEvaluations = outcome.timings.queryTermEvaluations;
bench_large_index_scalability.cpp-771-     outcome.run.caseInsensitiveComparisons = outcome.timings.caseInsensitiveComparisons;
bench_large_index_scalability.cpp-772-     outcome.run.caseFoldOperations = outcome.timings.caseFoldOperations;
bench_large_index_scalability.cpp-780-     outcome.run.pathNeededDecisions = outcome.timings.pathNeededDecisions;
bench_large_index_scalability.cpp-781-     outcome.run.pathLookups = outcome.timings.pathLookups;
bench_large_index_scalability.cpp-782-     outcome.run.matchedRows = outcome.timings.matchedRows;
bench_large_index_scalability.cpp-783-     outcome.run.candidateObjectsMaterialized = outcome.timings.candidateObjectsMaterialized;
bench_large_index_scalability.cpp:784:     outcome.run.candidateNameCopies = outcome.timings.candidateNameCopies;
bench_large_index_scalability.cpp-785-     outcome.run.searchResultObjectsMaterialized = outcome.timings.searchResultObjectsMaterialized;
bench_large_index_scalability.cpp-786-     outcome.run.wireBytes = payload.size();
bench_large_index_scalability.cpp-787-     outcome.run.memory = ReadMemory();
bench_large_index_scalability.cpp-788-     const auto readPlane = pipeline.GetReadPlaneDiagnostics();
bench_large_index_scalability.cpp-802-                       const std::vector<SearchRun>& runs) {
bench_large_index_scalability.cpp-803-     std::vector<double> values;
bench_large_index_scalability.cpp-804-     size_t resultCount = 0;
bench_large_index_scalability.cpp-805-     for (const auto& run : runs) {
bench_large_index_scalability.cpp:806:         if (run.scale == scale && run.queryClass == queryCase.name && run.temperature == "warm") {
bench_large_index_scalability.cpp-807-             values.push_back(run.totalMs);
bench_large_index_scalability.cpp-808-             resultCount = run.resultCount;
bench_large_index_scalability.cpp-809-         }
bench_large_index_scalability.cpp-810-     }
bench_large_index_scalability.cpp-809-         }
bench_large_index_scalability.cpp-810-     }
bench_large_index_scalability.cpp-811-     if (values.empty()) return;
bench_large_index_scalability.cpp-812-     std::sort(values.begin(), values.end());
bench_large_index_scalability.cpp:813:     summaries.push_back({scale, queryCase.name, resultCount, values.front(),
bench_large_index_scalability.cpp-814-                          Percentile(values, 0.50), Percentile(values, 0.90), values.back()});
bench_large_index_scalability.cpp-815- }
bench_large_index_scalability.cpp-816- 
bench_large_index_scalability.cpp-817- class ContentionBarrier {
bench_large_index_scalability.cpp-1064-     directory.parentIdLow = 5;
bench_large_index_scalability.cpp-1065-     directory.entries.reserve(rows);
bench_large_index_scalability.cpp-1066-     for (size_t index = 0; index < rows; ++index) {
bench_large_index_scalability.cpp-1067-         ffprotocol::SnapshotDirectoryEntry entry;
bench_large_index_scalability.cpp:1068:         entry.name = L"snapshot_item_" + std::to_wstring(index) + ((index % 5 == 0) ? L".txt" : L".bin");
bench_large_index_scalability.cpp-1069-         entry.sizeBytes = 4096 + index * 17;
bench_large_index_scalability.cpp-1070-         entry.attributes = 0;
bench_large_index_scalability.cpp-1071-         entry.creationTime = 10 + index;
bench_large_index_scalability.cpp-1072-         entry.lastModifiedTime = 20 + index;
bench_large_index_scalability.cpp-1090-         kStatusBytes + kDirectoryIdentifierBytes + sizeof(uint32_t);
bench_large_index_scalability.cpp-1091-     for (size_t index = 0; index < rows; ++index) {
bench_large_index_scalability.cpp-1092-         size_t digits = 1;
bench_large_index_scalability.cpp-1093-         for (size_t value = index; value >= 10; value /= 10) ++digits;
bench_large_index_scalability.cpp:1094:         const size_t nameChars = 18 + digits; // snapshot_item_ + decimal index + .txt/.bin
bench_large_index_scalability.cpp-1095-         total += kEntryFixedBytes + sizeof(uint32_t) + nameChars * sizeof(wchar_t);
bench_large_index_scalability.cpp-1096-     }
bench_large_index_scalability.cpp-1097-     return total;
bench_large_index_scalability.cpp-1098- }
bench_large_index_scalability.cpp-1091-     for (size_t index = 0; index < rows; ++index) {
bench_large_index_scalability.cpp-1092-         size_t digits = 1;
bench_large_index_scalability.cpp-1093-         for (size_t value = index; value >= 10; value /= 10) ++digits;
bench_large_index_scalability.cpp-1094-         const size_t nameChars = 18 + digits; // snapshot_item_ + decimal index + .txt/.bin
bench_large_index_scalability.cpp:1095:         total += kEntryFixedBytes + sizeof(uint32_t) + nameChars * sizeof(wchar_t);
bench_large_index_scalability.cpp-1096-     }
bench_large_index_scalability.cpp-1097-     return total;
bench_large_index_scalability.cpp-1098- }
bench_large_index_scalability.cpp-1099- 
bench_large_index_scalability.cpp-1117-     size_t rows = 0;
bench_large_index_scalability.cpp-1118-     bool parsed = false;
bench_large_index_scalability.cpp-1119- };
bench_large_index_scalability.cpp-1120- 
bench_large_index_scalability.cpp:1121: PublishedRead ReadPublishedSnapshot(const std::wstring& sectionName) {
bench_large_index_scalability.cpp-1122-     const auto start = Clock::now();
bench_large_index_scalability.cpp-1123-     PublishedRead result;
bench_large_index_scalability.cpp-1124-     const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, sectionName.c_str());
bench_large_index_scalability.cpp-1125-     if (mapping == nullptr) {
bench_large_index_scalability.cpp-1120- 
bench_large_index_scalability.cpp-1121- PublishedRead ReadPublishedSnapshot(const std::wstring& sectionName) {
bench_large_index_scalability.cpp-1122-     const auto start = Clock::now();
bench_large_index_scalability.cpp-1123-     PublishedRead result;
bench_large_index_scalability.cpp:1124:     const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, sectionName.c_str());
bench_large_index_scalability.cpp-1125-     if (mapping == nullptr) {
bench_large_index_scalability.cpp-1126-         result.elapsedMs = ElapsedMs(start);
bench_large_index_scalability.cpp-1127-         return result;
bench_large_index_scalability.cpp-1128-     }
bench_large_index_scalability.cpp-1169-     result.oversize = !result.publishSucceeded && timings.serializedBytes > ffprotocol::kSnapshotMaximumPayloadBytes;
bench_large_index_scalability.cpp-1170-     result.generationAfter = publisher.CurrentGeneration();
bench_large_index_scalability.cpp-1171-     result.memoryAfterPublish = ReadMemory();
bench_large_index_scalability.cpp-1172-     if (result.publishSucceeded) {
bench_large_index_scalability.cpp:1173:         const PublishedRead read = ReadPublishedSnapshot(publisher.CurrentSectionName());
bench_large_index_scalability.cpp-1174-         result.uiMappingAndParsingMs = read.elapsedMs;
bench_large_index_scalability.cpp-1175-         result.uiParsedRows = read.rows;
bench_large_index_scalability.cpp-1176-         if (!read.parsed || read.rows != rows) {
bench_large_index_scalability.cpp-1177-             std::fprintf(stderr, "warning: published snapshot did not round-trip all rows\n");
bench_large_index_scalability.cpp-1310-            << ",\"cancelled\":" << (run.cancelled ? "true" : "false")
bench_large_index_scalability.cpp-1311-            << ",\"unavailable\":" << (run.unavailable ? "true" : "false")
bench_large_index_scalability.cpp-1312-            << ",\"wire_result_count\":" << run.wireResultCount
bench_large_index_scalability.cpp-1313-            << ",\"wire_truncated\":" << (run.wireTruncated ? "true" : "false")
bench_large_index_scalability.cpp:1314:            << ",\"filename_index_used\":" << (run.filenameIndexUsed ? "true" : "false")
bench_large_index_scalability.cpp-1315-            << ",\"filename_index_candidate_count\":" << run.filenameIndexCandidateCount;
bench_large_index_scalability.cpp-1316-     output << ",\"search_generation\":" << run.searchGeneration
bench_large_index_scalability.cpp-1317-            << ",\"query_visible_epoch\":" << run.queryVisibleEpoch
bench_large_index_scalability.cpp-1318-            << ",\"dependency_set_epoch\":" << run.dependencySetEpoch
bench_large_index_scalability.cpp-1311-            << ",\"unavailable\":" << (run.unavailable ? "true" : "false")
bench_large_index_scalability.cpp-1312-            << ",\"wire_result_count\":" << run.wireResultCount
bench_large_index_scalability.cpp-1313-            << ",\"wire_truncated\":" << (run.wireTruncated ? "true" : "false")
bench_large_index_scalability.cpp-1314-            << ",\"filename_index_used\":" << (run.filenameIndexUsed ? "true" : "false")
bench_large_index_scalability.cpp:1315:            << ",\"filename_index_candidate_count\":" << run.filenameIndexCandidateCount;
bench_large_index_scalability.cpp-1316-     output << ",\"search_generation\":" << run.searchGeneration
bench_large_index_scalability.cpp-1317-            << ",\"query_visible_epoch\":" << run.queryVisibleEpoch
bench_large_index_scalability.cpp-1318-            << ",\"dependency_set_epoch\":" << run.dependencySetEpoch
bench_large_index_scalability.cpp-1319-            << ",\"generation_restarts\":" << run.generationRestarts
bench_large_index_scalability.cpp-1330-            << ",\"peak_durable_readers\":" << run.peakDurableReaders;
bench_large_index_scalability.cpp-1331-     output << ",\"sqlite_ms\":"; WriteJsonDouble(output, run.sqliteMs);
bench_large_index_scalability.cpp-1332-     output << ",\"projection_traversal_ms\":"; WriteJsonDouble(output, run.projectionTraversalMs);
bench_large_index_scalability.cpp-1333-     output << ",\"projection_enumeration_ms\":"; WriteJsonDouble(output, run.projectionEnumerationMs);
bench_large_index_scalability.cpp:1334:     output << ",\"name_pool_lookup_ms\":"; WriteJsonDouble(output, run.namePoolLookupMs);
bench_large_index_scalability.cpp-1335-     output << ",\"name_view_construction_ms\":"; WriteJsonDouble(output, run.nameViewConstructionMs);
bench_large_index_scalability.cpp-1336-     output << ",\"materialization_ms\":"; WriteJsonDouble(output, run.materializationMs);
bench_large_index_scalability.cpp-1337-     output << ",\"lightweight_candidate_materialization_ms\":";
bench_large_index_scalability.cpp-1338-     WriteJsonDouble(output, run.lightweightCandidateMaterializationMs);
bench_large_index_scalability.cpp-1331-     output << ",\"sqlite_ms\":"; WriteJsonDouble(output, run.sqliteMs);
bench_large_index_scalability.cpp-1332-     output << ",\"projection_traversal_ms\":"; WriteJsonDouble(output, run.projectionTraversalMs);
bench_large_index_scalability.cpp-1333-     output << ",\"projection_enumeration_ms\":"; WriteJsonDouble(output, run.projectionEnumerationMs);
bench_large_index_scalability.cpp-1334-     output << ",\"name_pool_lookup_ms\":"; WriteJsonDouble(output, run.namePoolLookupMs);
bench_large_index_scalability.cpp:1335:     output << ",\"name_view_construction_ms\":"; WriteJsonDouble(output, run.nameViewConstructionMs);
bench_large_index_scalability.cpp-1336-     output << ",\"materialization_ms\":"; WriteJsonDouble(output, run.materializationMs);
bench_large_index_scalability.cpp-1337-     output << ",\"lightweight_candidate_materialization_ms\":";
bench_large_index_scalability.cpp-1338-     WriteJsonDouble(output, run.lightweightCandidateMaterializationMs);
bench_large_index_scalability.cpp-1339-     output << ",\"lazy_path_materialization_ms\":";
bench_large_index_scalability.cpp-1359-             output << ",\"hot_rows_examined\":" << run.hotRowsExamined;
bench_large_index_scalability.cpp-1360-             output << ",\"cold_rows_examined\":" << run.coldRowsExamined;
bench_large_index_scalability.cpp-1361-             output << ",\"cold_volumes_searched\":" << run.coldVolumesSearched;
bench_large_index_scalability.cpp-1362-             output << ",\"store_stream_ms\":" << run.storeStreamMs;
bench_large_index_scalability.cpp:1363:     output << ",\"name_pool_lookups\":" << run.namePoolLookups;
bench_large_index_scalability.cpp-1364-     output << ",\"empty_names_rejected\":" << run.emptyNamesRejected;
bench_large_index_scalability.cpp-1365-     output << ",\"name_views_constructed\":" << run.nameViewsConstructed;
bench_large_index_scalability.cpp-1366-     output << ",\"query_predicate_evaluations\":" << run.queryPredicateEvaluations;
bench_large_index_scalability.cpp-1367-     output << ",\"query_term_evaluations\":" << run.queryTermEvaluations;
bench_large_index_scalability.cpp-1360-             output << ",\"cold_rows_examined\":" << run.coldRowsExamined;
bench_large_index_scalability.cpp-1361-             output << ",\"cold_volumes_searched\":" << run.coldVolumesSearched;
bench_large_index_scalability.cpp-1362-             output << ",\"store_stream_ms\":" << run.storeStreamMs;
bench_large_index_scalability.cpp-1363-     output << ",\"name_pool_lookups\":" << run.namePoolLookups;
bench_large_index_scalability.cpp:1364:     output << ",\"empty_names_rejected\":" << run.emptyNamesRejected;
bench_large_index_scalability.cpp-1365-     output << ",\"name_views_constructed\":" << run.nameViewsConstructed;
bench_large_index_scalability.cpp-1366-     output << ",\"query_predicate_evaluations\":" << run.queryPredicateEvaluations;
bench_large_index_scalability.cpp-1367-     output << ",\"query_term_evaluations\":" << run.queryTermEvaluations;
bench_large_index_scalability.cpp-1368-     output << ",\"case_insensitive_comparisons\":" << run.caseInsensitiveComparisons;
bench_large_index_scalability.cpp-1361-             output << ",\"cold_volumes_searched\":" << run.coldVolumesSearched;
bench_large_index_scalability.cpp-1362-             output << ",\"store_stream_ms\":" << run.storeStreamMs;
bench_large_index_scalability.cpp-1363-     output << ",\"name_pool_lookups\":" << run.namePoolLookups;
bench_large_index_scalability.cpp-1364-     output << ",\"empty_names_rejected\":" << run.emptyNamesRejected;
bench_large_index_scalability.cpp:1365:     output << ",\"name_views_constructed\":" << run.nameViewsConstructed;
bench_large_index_scalability.cpp-1366-     output << ",\"query_predicate_evaluations\":" << run.queryPredicateEvaluations;
bench_large_index_scalability.cpp-1367-     output << ",\"query_term_evaluations\":" << run.queryTermEvaluations;
bench_large_index_scalability.cpp-1368-     output << ",\"case_insensitive_comparisons\":" << run.caseInsensitiveComparisons;
bench_large_index_scalability.cpp-1369-     output << ",\"case_fold_operations\":" << run.caseFoldOperations;
bench_large_index_scalability.cpp-1377-     output << ",\"path_needed_decisions\":" << run.pathNeededDecisions;
bench_large_index_scalability.cpp-1378-     output << ",\"path_lookups\":" << run.pathLookups;
bench_large_index_scalability.cpp-1379-     output << ",\"matched_rows\":" << run.matchedRows;
bench_large_index_scalability.cpp-1380-     output << ",\"candidate_objects_materialized\":" << run.candidateObjectsMaterialized;
bench_large_index_scalability.cpp:1381:     output << ",\"candidate_name_copies\":" << run.candidateNameCopies;
bench_large_index_scalability.cpp-1382-     output << ",\"search_result_objects_materialized\":" << run.searchResultObjectsMaterialized;
bench_large_index_scalability.cpp-1383-     output << ",\"wire_bytes\":" << run.wireBytes << ",\"memory\":";
bench_large_index_scalability.cpp-1384-     WriteMemory(output, run.memory);
bench_large_index_scalability.cpp-1385-     output << '}';
bench_large_index_scalability.cpp-1516-     return hash;
bench_large_index_scalability.cpp-1517- }
bench_large_index_scalability.cpp-1518- 
bench_large_index_scalability.cpp-1519- struct MemoryCapturePoint {
bench_large_index_scalability.cpp:1520:     std::string name;
bench_large_index_scalability.cpp-1521-     MemorySample memory;
bench_large_index_scalability.cpp-1522-     uint64_t peakWorkingSetBytes = 0;
bench_large_index_scalability.cpp-1523-     uint64_t peakPrivateBytes = 0;
bench_large_index_scalability.cpp-1524-     WorkingSetSplit workingSetSplit;
bench_large_index_scalability.cpp-1528-     ffindexstore::Projection::MemoryBreakdown breakdown;
bench_large_index_scalability.cpp-1529-     size_t estimatedBytes = 0;
bench_large_index_scalability.cpp-1530- };
bench_large_index_scalability.cpp-1531- 
bench_large_index_scalability.cpp:1532: MemoryCapturePoint CaptureMemoryPoint(const std::string& name, const double elapsedMs) {
bench_large_index_scalability.cpp-1533-     MemoryCapturePoint point;
bench_large_index_scalability.cpp-1534-     point.name = name;
bench_large_index_scalability.cpp-1535-     PROCESS_MEMORY_COUNTERS_EX counters{};
bench_large_index_scalability.cpp-1536-     counters.cb = sizeof(counters);
bench_large_index_scalability.cpp-1530- };
bench_large_index_scalability.cpp-1531- 
bench_large_index_scalability.cpp-1532- MemoryCapturePoint CaptureMemoryPoint(const std::string& name, const double elapsedMs) {
bench_large_index_scalability.cpp-1533-     MemoryCapturePoint point;
bench_large_index_scalability.cpp:1534:     point.name = name;
bench_large_index_scalability.cpp-1535-     PROCESS_MEMORY_COUNTERS_EX counters{};
bench_large_index_scalability.cpp-1536-     counters.cb = sizeof(counters);
bench_large_index_scalability.cpp-1537-     if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
bench_large_index_scalability.cpp-1538-                              sizeof(counters))) {
bench_large_index_scalability.cpp-1546-     point.elapsedMs = elapsedMs;
bench_large_index_scalability.cpp-1547-     return point;
bench_large_index_scalability.cpp-1548- }
bench_large_index_scalability.cpp-1549- 
bench_large_index_scalability.cpp:1550: MemoryCapturePoint CaptureProjectionPoint(const std::string& name, ffengine::IndexPipeline& pipeline,
bench_large_index_scalability.cpp-1551-                                           const double elapsedMs) {
bench_large_index_scalability.cpp-1552-     MemoryCapturePoint point = CaptureMemoryPoint(name, elapsedMs);
bench_large_index_scalability.cpp-1553-     point.breakdown = pipeline.GetProjectionMemoryBreakdown();
bench_large_index_scalability.cpp-1554-     point.estimatedBytes = pipeline.GetProjectionMemoryDiagnostics().estimatedBytes;
bench_large_index_scalability.cpp-1548- }
bench_large_index_scalability.cpp-1549- 
bench_large_index_scalability.cpp-1550- MemoryCapturePoint CaptureProjectionPoint(const std::string& name, ffengine::IndexPipeline& pipeline,
bench_large_index_scalability.cpp-1551-                                           const double elapsedMs) {
bench_large_index_scalability.cpp:1552:     MemoryCapturePoint point = CaptureMemoryPoint(name, elapsedMs);
bench_large_index_scalability.cpp-1553-     point.breakdown = pipeline.GetProjectionMemoryBreakdown();
bench_large_index_scalability.cpp-1554-     point.estimatedBytes = pipeline.GetProjectionMemoryDiagnostics().estimatedBytes;
bench_large_index_scalability.cpp-1555-     point.hasProjection = true;
bench_large_index_scalability.cpp-1556-     return point;
bench_large_index_scalability.cpp-1582-     unsigned long long armAllocationCount = 0;
bench_large_index_scalability.cpp-1583- };
bench_large_index_scalability.cpp-1584- 
bench_large_index_scalability.cpp-1585- struct ExportArm {
bench_large_index_scalability.cpp:1586:     std::string name;
bench_large_index_scalability.cpp-1587-     bool executed = false;
bench_large_index_scalability.cpp-1588-     std::vector<ExportArmPass> passes;
bench_large_index_scalability.cpp-1589- };
bench_large_index_scalability.cpp-1590- 
bench_large_index_scalability.cpp-1764- 
bench_large_index_scalability.cpp-1765-             // Counterbalanced export weighing: pooled (retained production path),
bench_large_index_scalability.cpp-1766-             // owning (reference path), owning, pooled.
bench_large_index_scalability.cpp-1767-             ExportArm pooledArm;
bench_large_index_scalability.cpp:1768:             pooledArm.name = "pooled-export-publication";
bench_large_index_scalability.cpp-1769-             ExportArm owningArm;
bench_large_index_scalability.cpp-1770-             owningArm.name = "owning-export-publication";
bench_large_index_scalability.cpp-1771-             for (int pass = 0; pass < 4; ++pass) {
bench_large_index_scalability.cpp-1772-                 if (pass == 0 || pass == 3) {
bench_large_index_scalability.cpp-1766-             // owning (reference path), owning, pooled.
bench_large_index_scalability.cpp-1767-             ExportArm pooledArm;
bench_large_index_scalability.cpp-1768-             pooledArm.name = "pooled-export-publication";
bench_large_index_scalability.cpp-1769-             ExportArm owningArm;
bench_large_index_scalability.cpp:1770:             owningArm.name = "owning-export-publication";
bench_large_index_scalability.cpp-1771-             for (int pass = 0; pass < 4; ++pass) {
bench_large_index_scalability.cpp-1772-                 if (pass == 0 || pass == 3) {
bench_large_index_scalability.cpp-1773-                     pooledArm.passes.push_back(RunPooledExportPass(
bench_large_index_scalability.cpp-1774-                         pipeline, volumeId, options.exportBoundDirectories, publisher));
bench_large_index_scalability.cpp-1850-            << ",\"child_map_state_bytes\":" << b.childMapStateBytes
bench_large_index_scalability.cpp-1851-            << ",\"child_map_offset_bytes\":" << b.childMapOffsetBytes
bench_large_index_scalability.cpp-1852-            << ",\"child_map_count_bytes\":" << b.childMapCountBytes
bench_large_index_scalability.cpp-1853-            << ",\"child_map_bytes\":" << b.childMapBytes
bench_large_index_scalability.cpp:1854:            << ",\"unique_name_count\":" << b.uniqueNameCount
bench_large_index_scalability.cpp-1855-            << ",\"name_arena_chars\":" << b.nameArenaChars
bench_large_index_scalability.cpp-1856-            << ",\"name_arena_capacity_chars\":" << b.nameArenaCapacityChars
bench_large_index_scalability.cpp-1857-            << ",\"name_arena_bytes\":" << b.nameArenaBytes
bench_large_index_scalability.cpp-1858-            << ",\"name_arena_slack_bytes\":" << b.nameArenaSlackBytes
bench_large_index_scalability.cpp-1851-            << ",\"child_map_offset_bytes\":" << b.childMapOffsetBytes
bench_large_index_scalability.cpp-1852-            << ",\"child_map_count_bytes\":" << b.childMapCountBytes
bench_large_index_scalability.cpp-1853-            << ",\"child_map_bytes\":" << b.childMapBytes
bench_large_index_scalability.cpp-1854-            << ",\"unique_name_count\":" << b.uniqueNameCount
bench_large_index_scalability.cpp:1855:            << ",\"name_arena_chars\":" << b.nameArenaChars
bench_large_index_scalability.cpp-1856-            << ",\"name_arena_capacity_chars\":" << b.nameArenaCapacityChars
bench_large_index_scalability.cpp-1857-            << ",\"name_arena_bytes\":" << b.nameArenaBytes
bench_large_index_scalability.cpp-1858-            << ",\"name_arena_slack_bytes\":" << b.nameArenaSlackBytes
bench_large_index_scalability.cpp-1859-            << ",\"name_table_slot_count\":" << b.nameTableSlotCount
bench_large_index_scalability.cpp-1852-            << ",\"child_map_count_bytes\":" << b.childMapCountBytes
bench_large_index_scalability.cpp-1853-            << ",\"child_map_bytes\":" << b.childMapBytes
bench_large_index_scalability.cpp-1854-            << ",\"unique_name_count\":" << b.uniqueNameCount
bench_large_index_scalability.cpp-1855-            << ",\"name_arena_chars\":" << b.nameArenaChars
bench_large_index_scalability.cpp:1856:            << ",\"name_arena_capacity_chars\":" << b.nameArenaCapacityChars
bench_large_index_scalability.cpp-1857-            << ",\"name_arena_bytes\":" << b.nameArenaBytes
bench_large_index_scalability.cpp-1858-            << ",\"name_arena_slack_bytes\":" << b.nameArenaSlackBytes
bench_large_index_scalability.cpp-1859-            << ",\"name_table_slot_count\":" << b.nameTableSlotCount
bench_large_index_scalability.cpp-1860-            << ",\"name_table_bytes\":" << b.nameTableBytes
bench_large_index_scalability.cpp-1853-            << ",\"child_map_bytes\":" << b.childMapBytes
bench_large_index_scalability.cpp-1854-            << ",\"unique_name_count\":" << b.uniqueNameCount
bench_large_index_scalability.cpp-1855-            << ",\"name_arena_chars\":" << b.nameArenaChars
bench_large_index_scalability.cpp-1856-            << ",\"name_arena_capacity_chars\":" << b.nameArenaCapacityChars
bench_large_index_scalability.cpp:1857:            << ",\"name_arena_bytes\":" << b.nameArenaBytes
bench_large_index_scalability.cpp-1858-            << ",\"name_arena_slack_bytes\":" << b.nameArenaSlackBytes
bench_large_index_scalability.cpp-1859-            << ",\"name_table_slot_count\":" << b.nameTableSlotCount
bench_large_index_scalability.cpp-1860-            << ",\"name_table_bytes\":" << b.nameTableBytes
bench_large_index_scalability.cpp-1861-            << ",\"name_hash_bucket_count\":" << b.nameHashBucketCount
bench_large_index_scalability.cpp-1854-            << ",\"unique_name_count\":" << b.uniqueNameCount
bench_large_index_scalability.cpp-1855-            << ",\"name_arena_chars\":" << b.nameArenaChars
bench_large_index_scalability.cpp-1856-            << ",\"name_arena_capacity_chars\":" << b.nameArenaCapacityChars
bench_large_index_scalability.cpp-1857-            << ",\"name_arena_bytes\":" << b.nameArenaBytes
bench_large_index_scalability.cpp:1858:            << ",\"name_arena_slack_bytes\":" << b.nameArenaSlackBytes
bench_large_index_scalability.cpp-1859-            << ",\"name_table_slot_count\":" << b.nameTableSlotCount
bench_large_index_scalability.cpp-1860-            << ",\"name_table_bytes\":" << b.nameTableBytes
bench_large_index_scalability.cpp-1861-            << ",\"name_hash_bucket_count\":" << b.nameHashBucketCount
bench_large_index_scalability.cpp-1862-            << ",\"name_hash_bucket_bytes\":" << b.nameHashBucketBytes
bench_large_index_scalability.cpp-1855-            << ",\"name_arena_chars\":" << b.nameArenaChars
bench_large_index_scalability.cpp-1856-            << ",\"name_arena_capacity_chars\":" << b.nameArenaCapacityChars
bench_large_index_scalability.cpp-1857-            << ",\"name_arena_bytes\":" << b.nameArenaBytes
bench_large_index_scalability.cpp-1858-            << ",\"name_arena_slack_bytes\":" << b.nameArenaSlackBytes
bench_large_index_scalability.cpp:1859:            << ",\"name_table_slot_count\":" << b.nameTableSlotCount
bench_large_index_scalability.cpp-1860-            << ",\"name_table_bytes\":" << b.nameTableBytes
bench_large_index_scalability.cpp-1861-            << ",\"name_hash_bucket_count\":" << b.nameHashBucketCount
bench_large_index_scalability.cpp-1862-            << ",\"name_hash_bucket_bytes\":" << b.nameHashBucketBytes
bench_large_index_scalability.cpp-1863-            << ",\"name_hash_node_count\":" << b.nameHashNodeCount
bench_large_index_scalability.cpp-1856-            << ",\"name_arena_capacity_chars\":" << b.nameArenaCapacityChars
bench_large_index_scalability.cpp-1857-            << ",\"name_arena_bytes\":" << b.nameArenaBytes
bench_large_index_scalability.cpp-1858-            << ",\"name_arena_slack_bytes\":" << b.nameArenaSlackBytes
bench_large_index_scalability.cpp-1859-            << ",\"name_table_slot_count\":" << b.nameTableSlotCount
bench_large_index_scalability.cpp:1860:            << ",\"name_table_bytes\":" << b.nameTableBytes
bench_large_index_scalability.cpp-1861-            << ",\"name_hash_bucket_count\":" << b.nameHashBucketCount
bench_large_index_scalability.cpp-1862-            << ",\"name_hash_bucket_bytes\":" << b.nameHashBucketBytes
[80 matches limit reached. Use limit=160 for more, or refine pattern. 50.0KB limit reached]
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
No matches found
    }
    CounterLease(const CounterLease&) = delete;
    CounterLease& operator=(const CounterLease&) = delete;
private:
    std::atomic<size_t>* active_;
    std::atomic<size_t>* peak_;
};
// large-index-lifecycle-and-search-scalability review-remediation 18.x:
// cold-stream prefilter eligibility. An ASCII-folded simple literal needle of
// >= 3 units can use the conservative SQL name prefilter
// (StoreReadSession::ForEachEntryWithAsciiNameUntil): LIKE folds exactly the
// ASCII case the superset proof needs and the query's own fold is already
// ASCII-lowercase. Non-ASCII needles return nullopt and keep the full stream
// because SQLite LIKE cannot fold non-ASCII case variants.
std::optional<std::string> ColdNamePrefilterNeedle(const ffsearch::Query& query) {
    if (!query.simpleLiteralName) return std::nullopt;
    const std::u16string& folded = query.simpleLiteralFolded;
    if (folded.size() < 3) return std::nullopt;
    std::string needle;
    needle.reserve(folded.size());
    for (const char16_t ch : folded) {
        if (ch > 0x7f) return std::nullopt;
        needle.push_back(static_cast<char>(ch));
    }
    return needle;
}
} // namespace
bool IndexPipeline::VolumeSettingsEqual(const ffprotocol::VolumeSetting& left,
                                        const ffprotocol::VolumeSetting& right) {
    if (left.key != right.key || left.enabled != right.enabled || left.rules.size() != right.rules.size()) {
        return false;
    }
    for (size_t index = 0; index < left.rules.size(); ++index) {
        if (left.rules[index].path != right.rules[index].path ||
            left.rules[index].include != right.rules[index].include) {
            return false;
        }
    }
    return true;
}
void IndexPipeline::AdvanceQueryVisibleEpochLocked(const ffindexstore::VolumeRowId volumeId,
                                                    const QueryMutationReason reason,
                                                    const bool dependencySetChanged) {
    const uint64_t nextEpoch = queryVisibleEpoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (volumeId != 0) {
        volumeDataEpochs_[volumeId] = nextEpoch;
        if (reason == QueryMutationReason::VolumeConfiguration ||
            reason == QueryMutationReason::VolumeVisibility ||
            reason == QueryMutationReason::Forget) {
            volumeConfigurationEpochs_[volumeId] = nextEpoch;
        }
    }
    if (dependencySetChanged || reason == QueryMutationReason::Forget) {
        dependencySetEpoch_.fetch_add(1, std::memory_order_acq_rel);
    }
    queryVisibleInvalidations_.fetch_add(1, std::memory_order_relaxed);
}
void IndexPipeline::RecordMetadataOnlyUpdateLocked() {
    metadataOnlyUpdates_.fetch_add(1, std::memory_order_relaxed);
    metadataOnlyInvalidationsAvoided_.fetch_add(1, std::memory_order_relaxed);
}
void IndexPipeline::AdvanceResidencyEpochsForStructuralChangeLocked() {
    // large-index-lifecycle-and-search-scalability review-lane D P1-1 / lane C
    // Finding 1: memory-pressure enforcement and cold reload are structural
    // projection events -- EvictSubtree can cold-mark ANY volume (the LRU
    // victim is a global pick) and CompactStorage/RebuildVolumeFromStore
    // repack every row slot, so an in-flight search's captured cursor,
    // candidate slots, and cold-set can all be wrong afterwards. Surface the
    // change through the SAME ProjectionResidency epoch mechanism
    // SetProjectionMemoryBudget / UnloadVolumeFromProjection use, so searches
    // restart with a fresh capture instead of silently serving incomplete
    // results as Completed. Ordinary per-volume mutations never route through
    // here (scoped searches stay insensitive to unrelated writes).
    for (const auto& [volumeId, epoch] : volumeDataEpochs_) {
        (void)epoch;
        AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::ProjectionResidency);
    }
    AdvanceQueryVisibleEpochLocked(0, QueryMutationReason::ProjectionResidency, true);
}
bool IndexPipeline::Open(const std::string& dbPathUtf8, bool* outIntegrityFailed) {
    lifecycleEpoch_.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> lock(mutex_);
    recordsSinceAnalyze_ = 0;
    projectionMemoryWarningIssued_ = false;
    if (!store_.Open(dbPathUtf8, outIntegrityFailed)) {
        return false;
    }
    volumeDataEpochs_.clear();
    volumeConfigurationEpochs_.clear();
    AdvanceQueryVisibleEpochLocked(0, QueryMutationReason::Lifecycle, true);
    return true;
}
void IndexPipeline::Close() {
    lifecycleEpoch_.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> lock(mutex_);
    store_.Close();
    volumeDataEpochs_.clear();
    volumeConfigurationEpochs_.clear();
    AdvanceQueryVisibleEpochLocked(0, QueryMutationReason::Lifecycle, true);
    recordsSinceAnalyze_ = 0;
    projectionMemoryWarningIssued_ = false;
}
void IndexPipeline::SetSearchTestHooks(SearchTestHooks hooks) {
    std::lock_guard<std::mutex> lock(mutex_);
    searchTestHooks_ = std::move(hooks);
}
void IndexPipeline::SetReadSessionFaultInjector(ffindexstore::ReadSessionFaultInjector injector) {
    std::lock_guard<std::mutex> lock(mutex_);
    readSessionFaultInjector_ = std::move(injector);
[1730 more lines in file. Use offset=475 to continue.]
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/search
bench_large_index_scalability.cpp-491-     std::string databaseStorage;
bench_large_index_scalability.cpp-492- };
bench_large_index_scalability.cpp-493- 
bench_large_index_scalability.cpp:494: struct QueryCase {
bench_large_index_scalability.cpp-495-     const char* name;
bench_large_index_scalability.cpp-496-     const wchar_t* query;
bench_large_index_scalability.cpp-497-     uint8_t sortField;
bench_large_index_scalability.cpp-662-     SearchRun run;
bench_large_index_scalability.cpp-663- };
bench_large_index_scalability.cpp-664- 
bench_large_index_scalability.cpp:665: SearchOutcome RunSearch(ffengine::IndexPipeline& pipeline, size_t scale, const QueryCase& queryCase,
bench_large_index_scalability.cpp-666-                         size_t iteration, const char* temperature, bool detailedDiagnostics,
bench_large_index_scalability.cpp-667-                         bool projectionOnly = false) {
bench_large_index_scalability.cpp-668-     ffprotocol::IndexedSearchRequest request;
bench_large_index_scalability.cpp-798-     return values[index];
bench_large_index_scalability.cpp-799- }
bench_large_index_scalability.cpp-800- 
bench_large_index_scalability.cpp:801: void AddSearchSummary(std::vector<SearchSummary>& summaries, size_t scale, const QueryCase& queryCase,
bench_large_index_scalability.cpp-802-                       const std::vector<SearchRun>& runs) {
bench_large_index_scalability.cpp-803-     std::vector<double> values;
bench_large_index_scalability.cpp-804-     size_t resultCount = 0;
bench_large_index_scalability.cpp-2123-     std::vector<SearchSummary> searchSummaries;
bench_large_index_scalability.cpp-2124-     std::vector<IncrementalRow> incremental;
bench_large_index_scalability.cpp-2125- 
bench_large_index_scalability.cpp:2126:     const std::vector<QueryCase> queryCases{
bench_large_index_scalability.cpp-2127-         {"selective", L"unique_component_7391", 0},
bench_large_index_scalability.cpp-2128-         {"moderate", L"report", 0},
bench_large_index_scalability.cpp-2129-         {"broad", L"common", 0},
bench_large_index_scalability.cpp-2216-         }
bench_large_index_scalability.cpp-2217- 
bench_large_index_scalability.cpp-2218-         if (options.detailedDiagnostics) {
bench_large_index_scalability.cpp:2219:             const QueryCase projectionProbe{"projection_probe", L"zzzz_never_present", 0};
bench_large_index_scalability.cpp-2220-             const SearchOutcome probe = RunSearch(pipeline, scale, projectionProbe, 0,
bench_large_index_scalability.cpp-2221-                                                   "diagnostic", false, true);
bench_large_index_scalability.cpp-2222-             searchRuns.push_back(probe.run);
bench_large_index_scalability.cpp-2226-         }
bench_large_index_scalability.cpp-2227- 
bench_large_index_scalability.cpp-2228-         for (size_t caseIndex = 0; caseIndex < queryCases.size(); ++caseIndex) {
bench_large_index_scalability.cpp:2229:             const QueryCase& queryCase = queryCases[caseIndex];
bench_large_index_scalability.cpp-2230-             const size_t warmRuns = scale >= kLargeSearchThreshold ? kWarmRunsLarge : kWarmRunsNormal;
bench_large_index_scalability.cpp-2231-             if (caseIndex == 0) {
bench_large_index_scalability.cpp-2232-                 const SearchOutcome first = RunSearch(pipeline, scale, queryCase, 0,
bench_large_index_scalability.cpp-2276-         // evidence to the first requested scale so the default multi-million
bench_large_index_scalability.cpp-2277-         // row run remains useful without multiplying full-dataset churn.
bench_large_index_scalability.cpp-2278-         if (scale == options.scales.front()) {
bench_large_index_scalability.cpp:2279:             const QueryCase sustainedCase{"sustained_write_broad", L"common", 0};
bench_large_index_scalability.cpp-2280-             std::atomic<size_t> validationCount{0};
bench_large_index_scalability.cpp-2281-             pipeline.SetSearchTestHooks({[&](const ffengine::IndexPipeline::SearchPhase phase) {
bench_large_index_scalability.cpp-2282-                 if (phase != ffengine::IndexPipeline::SearchPhase::GenerationValidation) return;
bench_large_index_scalability.cpp-2303-         if (scale == 1'000'000) {
bench_large_index_scalability.cpp-2304-             const std::vector<std::wstring> steps{L"p", L"ph", L"pho", L"phot", L"photo"};
bench_large_index_scalability.cpp-2305-             for (const auto& step : steps) {
bench_large_index_scalability.cpp:2306:                 QueryCase stepCase{"incremental", step.c_str(), 0};
bench_large_index_scalability.cpp-2307-                 std::vector<double> values;
bench_large_index_scalability.cpp-2308-                 size_t resultCount = 0;
bench_large_index_scalability.cpp-2309-                 RunSearch(pipeline, scale, stepCase, 0, "warmup", options.detailedDiagnostics);
src/Query.cpp-564-         if (diagnostics != nullptr) ++diagnostics->predicateMatches;
src/Query.cpp-565-     }
src/Query.cpp-566-     return true;
src/Query.cpp-567- }
src/Query.cpp-568- 
src/Query.cpp-569- bool MatchSimpleLiteral(const Query& query, const CandidateView& candidate) {
src/Query.cpp:570:     return OrdinalContains16Prepared(candidate.name, query.simpleLiteralTerm, query.simpleLiteralFolded,
src/Query.cpp-571-                                      query.simpleLiteralShifts, candidate.nameIsAscii,
src/Query.cpp-572-                                      query.simpleLiteralAnchor, query.simpleLiteralAnchorIndex);
src/Query.cpp-573- }
src/Query.cpp-574- 
src/Query.cpp-575- bool EvaluateSimpleLiteral(const Query& query, const CandidateView& candidate,
src/Query.cpp-576-                            MatchDiagnostics* diagnostics) {
src/Query.cpp-593-     return matched;
src/Query.cpp-594- }
src/Query.cpp-595- 
src/Query.cpp-596- } // namespace
src/Query.cpp-597- 
src/Query.cpp-598- bool Query::MatchesView(const CandidateView& candidate, MatchDiagnostics* diagnostics) const {
src/Query.cpp:599:     if (viewCompatible && simpleLiteralName) return EvaluateSimpleLiteral(*this, candidate, diagnostics);
src/Query.cpp-600-     return viewCompatible && EvaluateViewPredicates(viewPredicates, viewPredicateKinds, candidate, diagnostics);
src/Query.cpp-601- }
src/Query.cpp-602- bool Query::MatchesViewWithoutFolder(const CandidateView& candidate, MatchDiagnostics* diagnostics) const {
src/Query.cpp-603-     if (viewCompatible && simpleLiteralName) return EvaluateSimpleLiteral(*this, candidate, diagnostics);
src/Query.cpp-604-     return viewCompatible && EvaluateViewPredicates(viewPredicatesWithoutFolder,
src/Query.cpp-605-                                                     viewPredicateWithoutFolderKinds, candidate, diagnostics);
src/Query.cpp-597- 
src/Query.cpp-598- bool Query::MatchesView(const CandidateView& candidate, MatchDiagnostics* diagnostics) const {
src/Query.cpp-599-     if (viewCompatible && simpleLiteralName) return EvaluateSimpleLiteral(*this, candidate, diagnostics);
src/Query.cpp-600-     return viewCompatible && EvaluateViewPredicates(viewPredicates, viewPredicateKinds, candidate, diagnostics);
src/Query.cpp-601- }
src/Query.cpp-602- bool Query::MatchesViewWithoutFolder(const CandidateView& candidate, MatchDiagnostics* diagnostics) const {
src/Query.cpp:603:     if (viewCompatible && simpleLiteralName) return EvaluateSimpleLiteral(*this, candidate, diagnostics);
src/Query.cpp-604-     return viewCompatible && EvaluateViewPredicates(viewPredicatesWithoutFolder,
src/Query.cpp-605-                                                     viewPredicateWithoutFolderKinds, candidate, diagnostics);
src/Query.cpp-606- }
src/Query.cpp-607- bool OrdinalContains(std::wstring_view h, std::wstring_view n) { if (n.empty()) return true; for (size_t i = 0; i + n.size() <= h.size(); ++i) if (CompareStringOrdinal(h.data() + i, static_cast<int>(n.size()), n.data(), static_cast<int>(n.size()), TRUE) == CSTR_EQUAL) return true; return false; }
src/Query.cpp-608- bool GlobMatches(std::wstring_view text, std::wstring_view pattern) {
src/Query.cpp-609-     const auto width = [](std::wstring_view value, size_t offset) -> size_t {
src/Query.cpp-745-                          PredicateKind::LiteralName);
src/Query.cpp-746-         }
src/Query.cpp-747-     }
src/Query.cpp-748-     if (query.viewCompatible && !query.requiresFolder && query.predicates.size() == 1 &&
src/Query.cpp-749-         query.viewPredicateKinds.size() == 1 && query.viewPredicateKinds.front() == PredicateKind::LiteralName &&
src/Query.cpp-750-         !simpleLiteralTerm.empty()) {
src/Query.cpp:751:         query.simpleLiteralName = true;
src/Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
src/Query.cpp-753-         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
src/Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
src/Query.cpp-755-         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
src/Query.cpp-756-         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
src/Query.cpp-757-             ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
src/Query.cpp-747-     }
src/Query.cpp-748-     if (query.viewCompatible && !query.requiresFolder && query.predicates.size() == 1 &&
src/Query.cpp-749-         query.viewPredicateKinds.size() == 1 && query.viewPredicateKinds.front() == PredicateKind::LiteralName &&
src/Query.cpp-750-         !simpleLiteralTerm.empty()) {
src/Query.cpp-751-         query.simpleLiteralName = true;
src/Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
src/Query.cpp:753:         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
src/Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
src/Query.cpp-755-         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
src/Query.cpp-756-         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
src/Query.cpp-757-             ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
src/Query.cpp-758-         query.simpleLiteralShifts = BuildAsciiShifts(query.simpleLiteralFolded);
src/Query.cpp-759-     }
src/Query.cpp-749-         query.viewPredicateKinds.size() == 1 && query.viewPredicateKinds.front() == PredicateKind::LiteralName &&
src/Query.cpp-750-         !simpleLiteralTerm.empty()) {
src/Query.cpp-751-         query.simpleLiteralName = true;
src/Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
src/Query.cpp-753-         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
src/Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
src/Query.cpp:755:         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
src/Query.cpp-756-         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
src/Query.cpp-757-             ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
src/Query.cpp-758-         query.simpleLiteralShifts = BuildAsciiShifts(query.simpleLiteralFolded);
src/Query.cpp-759-     }
src/Query.cpp-760-     return query;
src/Query.cpp-761- }
src/Query.cpp-750-         !simpleLiteralTerm.empty()) {
src/Query.cpp-751-         query.simpleLiteralName = true;
src/Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
src/Query.cpp-753-         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
src/Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
src/Query.cpp-755-         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
src/Query.cpp:756:         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
src/Query.cpp-757-             ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
src/Query.cpp-758-         query.simpleLiteralShifts = BuildAsciiShifts(query.simpleLiteralFolded);
src/Query.cpp-759-     }
src/Query.cpp-760-     return query;
src/Query.cpp-761- }
src/Query.cpp-762- 
src/Query.cpp-751-         query.simpleLiteralName = true;
src/Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
src/Query.cpp-753-         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
src/Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
src/Query.cpp-755-         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
src/Query.cpp-756-         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
src/Query.cpp:757:             ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
src/Query.cpp-758-         query.simpleLiteralShifts = BuildAsciiShifts(query.simpleLiteralFolded);
src/Query.cpp-759-     }
src/Query.cpp-760-     return query;
src/Query.cpp-761- }
src/Query.cpp-762- 
src/Query.cpp-763- int CompareOrdinal16(std::u16string_view left, std::wstring_view right) {
src/Query.cpp-752-         query.simpleLiteralTerm = std::move(simpleLiteralTerm);
src/Query.cpp-753-         query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
src/Query.cpp-754-             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
src/Query.cpp-755-         query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
src/Query.cpp-756-         query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
src/Query.cpp-757-             ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
src/Query.cpp:758:         query.simpleLiteralShifts = BuildAsciiShifts(query.simpleLiteralFolded);
src/Query.cpp-759-     }
src/Query.cpp-760-     return query;
src/Query.cpp-761- }
src/Query.cpp-762- 
src/Query.cpp-763- int CompareOrdinal16(std::u16string_view left, std::wstring_view right) {
src/Query.cpp-764-     if (gActiveDiagnostics != nullptr) ++gActiveDiagnostics->caseInsensitiveComparisons;
include/ffsearch/Query.h-139-     std::vector<PredicateKind> viewPredicateKinds;
include/ffsearch/Query.h-140-     std::vector<PredicateKind> viewPredicateWithoutFolderKinds;
include/ffsearch/Query.h-141-     std::vector<std::wstring> unrecognizedKeys;
include/ffsearch/Query.h-142-     std::vector<std::wstring> invalidFilters;
include/ffsearch/Query.h-143-     std::wstring primaryTerm;
include/ffsearch/Query.h-144-     std::wstring simpleLiteralTerm;
include/ffsearch/Query.h:145:     std::u16string simpleLiteralFolded;
include/ffsearch/Query.h-146-     char16_t simpleLiteralAnchor = 0;
include/ffsearch/Query.h-147-     size_t simpleLiteralAnchorIndex = 0;
include/ffsearch/Query.h-148-     std::array<size_t, 128> simpleLiteralShifts{};
include/ffsearch/Query.h-149-     bool requiresFolder = false;
include/ffsearch/Query.h-150-     bool viewCompatible = true;
include/ffsearch/Query.h-151-     bool simpleLiteralName = false;
include/ffsearch/Query.h-145-     std::u16string simpleLiteralFolded;
include/ffsearch/Query.h-146-     char16_t simpleLiteralAnchor = 0;
include/ffsearch/Query.h-147-     size_t simpleLiteralAnchorIndex = 0;
include/ffsearch/Query.h-148-     std::array<size_t, 128> simpleLiteralShifts{};
include/ffsearch/Query.h-149-     bool requiresFolder = false;
include/ffsearch/Query.h-150-     bool viewCompatible = true;
include/ffsearch/Query.h:151:     bool simpleLiteralName = false;
include/ffsearch/Query.h-152-     bool Matches(const Candidate& candidate) const;
include/ffsearch/Query.h-153-     bool MatchesWithoutFolder(const Candidate& candidate) const;
include/ffsearch/Query.h-154-     bool MatchesView(const CandidateView& candidate, MatchDiagnostics* diagnostics = nullptr) const;
include/ffsearch/Query.h-155-     bool MatchesViewWithoutFolder(const CandidateView& candidate, MatchDiagnostics* diagnostics = nullptr) const;
include/ffsearch/Query.h-156- };
include/ffsearch/Query.h-157- 
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
    std::vector<SearchSummary> searchSummaries;
    std::vector<IncrementalRow> incremental;
    const std::vector<QueryCase> queryCases{
        {"selective", L"unique_component_7391", 0},
        {"moderate", L"report", 0},
        {"broad", L"common", 0},
        {"no_match", L"zzzz_never_present", 0},
        {"prefix", L"photo", 0},
        {"extension", L"ext:txt", 3},
        {"worst_substring", L"component", 3},
        // performance-closure-and-multisurface-acceleration 7.1 additions:
        {"exact_name", L"unique_component_7391.txt", 0},
        {"short_prefix", L"co", 0},
        {"mixed_case", L"CoMmOn_CoMpOnEnT", 0},
        {"unicode", L"\u0441\u043f\u0438\u0441\u043e\u043a", 0},
        {"high_limit_broad", L"common", 0, 50000},
    };
    ffengine::SnapshotPublisher publisher;
    if (!publisher.Start(GetCurrentProcessId())) {
        std::fprintf(stderr, "snapshot publisher start failed\n");
        return 1;
    }
    std::printf("FastFiles large-index scalability benchmark (%zu dataset scales)\n", options.scales.size());
    std::fflush(stdout);
    for (const size_t scale : options.scales) {
        std::printf("Building %zu synthetic entries...\n", scale);
        std::fflush(stdout);
        const std::filesystem::path temporaryDirectory = MakeTemporaryDirectory(scale);
[267 more lines in file. Use offset=2153 to continue.]
[pi-lens: read widened to the enclosing method "ProjectionEntryCursor::Next" (symbol boundary): you asked for lines 930-1019, this shows lines 926-1019. Re-request with limit > 100 for the exact range.]
bool ProjectionEntryCursor::Next(const ProjectionEntry*& entry) {
    entry = nullptr;
    if (projection_ == nullptr) return false;
    if (Stale()) return false;
    if (dense_) {
        if (denseIndex_ >= projection_->entries_.size()) return false;
        entry = &projection_->entries_[denseIndex_++];
        return true;
    }
    if (!(iterator_ != projection_->idToIndex_.end())) return false;
    const auto [key, index] = *iterator_;
    (void)key;
    ++iterator_;
    entry = &projection_->entries_[index];
    return true;
}
ProjectionEntryCursor Projection::BeginEntryCursor() const {
    return ProjectionEntryCursor(*this);
}
std::optional<std::vector<uint32_t>> Projection::FilenameCandidateIndices(
    const std::u16string_view foldedNeedle, const size_t maximumCandidates) const {
    if (!filenameIndexValid_ || foldedNeedle.size() < 3 || !IsAscii(foldedNeedle)) {
        return std::nullopt;
    }
    try {
        std::vector<uint32_t> result;
        const std::vector<uint64_t> trigrams = UniqueTrigrams(foldedNeedle);
        std::vector<const std::vector<uint32_t>*> postings;
        postings.reserve(trigrams.size());
        bool anyTrigramMissing = false;
        for (const uint64_t trigram : trigrams) {
            const auto it = filenameTrigramPostings_.find(trigram);
            if (it == filenameTrigramPostings_.end()) {
                // large-index-lifecycle-and-search-scalability review-lane C
                // Finding 7: one missing needle trigram proves no row can
                // match through its ASCII fold -- the candidate set is then
                // exactly the exceptional merge of an empty intersection,
                // not a re-intersection over the postings that happened to
                // exist.
                anyTrigramMissing = true;
                break;
            }
            postings.push_back(&it->second);
        }
        if (!anyTrigramMissing && !postings.empty()) {
            std::sort(postings.begin(), postings.end(), [](const auto* left, const auto* right) {
                return left->size() < right->size();
            });
            if (maximumCandidates != 0 && postings.front()->size() > maximumCandidates) {
                return std::nullopt;
            }
            result = *postings.front();
            std::vector<uint32_t> scratch;
            for (size_t index = 1; index < postings.size() && !result.empty(); ++index) {
                scratch.clear();
                std::set_intersection(result.begin(), result.end(), postings[index]->begin(), postings[index]->end(),
                                      std::back_inserter(scratch));
                if (maximumCandidates != 0 && scratch.size() > maximumCandidates) {
                    return std::nullopt;
                }
                result.swap(scratch);
            }
        }
        if (maximumCandidates != 0 && result.size() > maximumCandidates) {
            return std::nullopt;
        }
        // large-index-lifecycle-and-search-scalability 6.3 + review-
        // remediation (P2-A): the candidate set is the posting intersection
        // MERGED with the fold-exceptional rows. For a name that contains no
        // exceptional unit, a canonical match keeps its aligned span entirely
        // ASCII (a non-ASCII unit there would have to fold to the aligned
        // ASCII needle unit -- which is exactly what makes it exceptional),
        // so the span's ASCII-folded trigrams are in the name's folded
        // multiset and the posting intersection already retains the row.
        // Every remaining canonical match goes through a non-ASCII unit the
        // HOST ordinal table equates with ASCII (or a surrogate), i.e. an
        // exceptional name -- retained by this merge. The pre-6.3 code unioned
        // ALL non-ASCII rows, which added non-exceptional false positives and
        // at any Unicode population above the caller's cap forced every lookup
        // to nullopt (the accelerator was unusable at 200k+ rows).
        // CanonicalMatches(query) is therefore a subset of this candidate set
        // for every host ordinal table; false positives here are verified away
        // by the authoritative canonical matcher.
        if (!filenameExceptionalEntries_.empty()) {
            const size_t maximumSize = (std::numeric_limits<size_t>::max)();
            const size_t exceptionalCount = filenameExceptionalEntries_.size();
            const size_t combinedSize = exceptionalCount > maximumSize - result.size()
                ? maximumSize : result.size() + exceptionalCount;
            std::vector<uint32_t> merged;
            merged.reserve(maximumCandidates == 0 ? combinedSize
                : (std::min)(maximumCandidates == maximumSize ? maximumSize : maximumCandidates + 1,
                             combinedSize));
[241 more lines in file. Use offset=1020 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/src/search/src/Q...
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
[pi-lens: read widened to the enclosing function_definition "function_definition" (symbol boundary): you asked for lines 180-239, this shows lines 175-239. Re-request with limit > 100 for the exact range.]
bool AnchorContainsFoldWide(std::wstring_view haystack, const AsciiFoldNeedle& needle) {
    const std::u16string_view folded(needle.folded);
    for (size_t position = needle.anchorIndex; position < haystack.size(); ++position) {
        if (static_cast<char16_t>(FoldAsciiWideChar(haystack[position])) != needle.anchor) continue;
        const size_t start = position - needle.anchorIndex;
        if (start + folded.size() > haystack.size()) return false;
        if (FoldedWindowEqualsWide(haystack, start, folded)) return true;
    }
    return false;
}
bool BoyerMooreHorspoolContainsWide(std::wstring_view haystack, const AsciiFoldNeedle& needle) {
    const std::u16string_view folded(needle.folded);
    const size_t lastIndex = folded.size() - 1;
    size_t position = lastIndex;
    while (position < haystack.size()) {
        const wchar_t rawTail = haystack[position];
        if (static_cast<char16_t>(FoldAsciiWideChar(rawTail)) == folded.back() &&
            FoldedWindowEqualsWide(haystack, position - lastIndex, folded)) {
            return true;
        }
        const size_t raw = static_cast<size_t>(rawTail);
        position += raw < needle.shifts.size() ? needle.shifts[raw] : folded.size();
    }
    return false;
}
// Single dispatch point for prepared literal matching over projection views.
// `shifts` must be built once by the caller via BuildAsciiShifts(foldedNeedle).
bool OrdinalContains16Prepared(std::u16string_view haystack, std::wstring_view needle,
                               std::u16string_view foldedNeedle,
                               const std::array<size_t, 128>& shifts,
                               bool nameIsAscii,
                               char16_t anchor, size_t anchorIndex) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    if (!foldedNeedle.empty() && nameIsAscii) {
        return foldedNeedle.size() >= 4
            ? BoyerMooreHorspoolContains(haystack, foldedNeedle, shifts)
            : AnchorContainsFold(haystack, foldedNeedle, anchor, anchorIndex);
    }
    for (size_t index = 0; index + needle.size() <= haystack.size(); ++index) {
        if (CompareOrdinal16(haystack.substr(index, needle.size()), needle) == 0) return true;
    }
    return false;
}
bool OrdinalContains16(std::u16string_view haystack, std::wstring_view needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    for (size_t index = 0; index + needle.size() <= haystack.size(); ++index) {
        if (CompareOrdinal16(haystack.substr(index, needle.size()), needle) == 0) return true;
    }
    return false;
}
bool GlobMatches16(std::u16string_view text, std::wstring_view pattern) {
    const auto width = [](std::u16string_view value, size_t offset) -> size_t {
        if (offset + 1 < value.size() && IS_HIGH_SURROGATE(value[offset]) && IS_LOW_SURROGATE(value[offset + 1])) return 2;
        return 1;
    };
    size_t textIndex = 0, patternIndex = 0, star = std::wstring_view::npos, saved = 0;
    while (textIndex < text.size()) {
        if (patternIndex < pattern.size() && pattern[patternIndex] == L'?') {
            textIndex += width(text, textIndex);
[600 more lines in file. Use offset=240 to continue.]
                if (disabledVolumes.contains(metadataEntry.rowId)) continue;
                std::wstring prefix;
                const auto configuredPrefix = configuredPrefixes.find(metadataEntry.rowId);
                if (configuredPrefix != configuredPrefixes.end()) {
                    prefix = configuredPrefix->second;
                } else {
                    const wchar_t driveLetter = ResolveDriveLetterForVolumeKey(metadataEntry.key);
                    if (driveLetter != L'\0') {
                        prefix.push_back(static_cast<wchar_t>(towupper(driveLetter)));
                        prefix.push_back(L':');
                    }
                }
                if (prefix.empty() || (!context.allVolumes &&
                                       !ffsearch::SameVolume(searchRequest.scopePath, prefix))) {
                    continue;
                }
                context.prefixes.emplace(metadataEntry.rowId, std::move(prefix));
                const auto ruleIt = volumeRules_.find(metadataEntry.rowId);
                if (ruleIt != volumeRules_.end()) {
                    context.rules.emplace(metadataEntry.rowId, ruleIt->second);
                    if (!ruleIt->second.rules.empty()) context.ruleFilterActive = true;
                }
                context.searchableVolumes.push_back(metadataEntry.rowId);
            }
            std::sort(context.searchableVolumes.begin(), context.searchableVolumes.end());
            if (context.searchableVolumes.empty()) {
                unavailable = true;
            } else {
                context.dependencies.reserve(context.searchableVolumes.size());
                for (const auto volumeId : context.searchableVolumes) {
                    const auto dataIt = volumeDataEpochs_.find(volumeId);
                    const auto configurationIt = volumeConfigurationEpochs_.find(volumeId);
                    context.dependencies.push_back({
                        volumeId,
                        dataIt == volumeDataEpochs_.end() ? 0 : dataIt->second,
                        configurationIt == volumeConfigurationEpochs_.end() ? 0 : configurationIt->second});
                    if (prepareProjection && projection_.NeedsReload(volumeId)) {
                        context.coldVolumes.insert(volumeId);
                    }
                }
                context.orderedCold.assign(context.coldVolumes.begin(), context.coldVolumes.end());
                std::sort(context.orderedCold.begin(), context.orderedCold.end());
                searchedColdVolumes = searchedColdVolumes || !context.orderedCold.empty();
                context.reader = store_.OpenReadSession({readSessionFaultInjector_});
                if (!context.reader) {
                    readerFailures_.fetch_add(1, std::memory_order_relaxed);
                    unavailable = true;
                } else {
                    if (prepareProjection) {
                        context.cursor = projection_.BeginEntryCursor();
                        const bool filenameEligible = searchRequest.query.simpleLiteralName &&
                            searchRequest.query.simpleLiteralFolded.size() >= 3 && context.coldVolumes.empty();
                        if (filenameEligible) {
                            context.filenameCandidates = projection_.FilenameCandidateIndices(
                                searchRequest.query.simpleLiteralFolded, kMaximumFilenameIndexCandidates);
                        }
                        if (timings != nullptr) {
                            timings->filenameIndexUsed = context.filenameCandidates.has_value();
                            timings->filenameIndexCandidateCount = context.filenameCandidates.has_value()
                                ? context.filenameCandidates->size() : 0;
                        }
                    }
                    ready = true;
                }
            }
        }
        const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
        maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
        lock.unlock();
        return ready;
    };
    const auto isContextCurrentLocked = [&](const SearchContext& context, const bool includeDataEpochs) {
        if (!store_.IsOpen()) return false;
        if (context.allVolumes && dependencySetEpoch_.load(std::memory_order_acquire) !=
                                     context.dependencySetEpoch) return false;
        for (const auto& dependency : context.dependencies) {
            const auto dataIt = volumeDataEpochs_.find(dependency.volumeId);
            const auto configurationIt = volumeConfigurationEpochs_.find(dependency.volumeId);
            const uint64_t dataEpoch = dataIt == volumeDataEpochs_.end() ? 0 : dataIt->second;
            const uint64_t configurationEpoch =
                configurationIt == volumeConfigurationEpochs_.end() ? 0 : configurationIt->second;
            if (configurationEpoch != dependency.configurationEpoch ||
                (includeDataEpochs && dataEpoch != dependency.dataEpoch)) {
                return false;
            }
        }
        return true;
    };
    for (size_t restart = 0; restart <= kMaxGenerationRestarts; ++restart) {
        if (isCancelled()) {
            finishCancelled();
            return result;
        }
        SearchContext context;
        bool contextReady = false;
        contextReady = captureContext(context, true);
        if (unavailable || !contextReady) break;
        if (!context.ruleFilterActive) {
            searchRequest.candidateFilter = {};
            searchRequest.candidateFilterRequiresFolder = false;
        } else {
            const auto capturedRules = context.rules;
            searchRequest.candidateFilter = [capturedRules](const ffsearch::Candidate& candidate) {
                return CandidateIncludedByRules(capturedRules, candidate);
            };
            searchRequest.candidateFilterRequiresFolder = true;
        }
        CounterLease durableReaderLease(activeDurableReaders_, peakDurableReaders_);
        (void)durableReaderLease;
        if (context.phaseHook) context.phaseHook(SearchPhase::WorkStarted);
        bool stale = false;
        bool streamFailed = false;
        bool matchingHookCalled = false;
[635 more lines in file. Use offset=1570 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
[pi-lens: read widened to the enclosing function_definition "function_definition" (symbol boundary): you asked for lines 370-449, this shows lines 196-473. Re-request with limit > 100 for the exact range.]
    ReadPlaneDiagnostics GetReadPlaneDiagnostics() const noexcept;
    // task 1.6: scheduled WAL maintenance, intended to be called on a
    // low-frequency timer (the engine calls it from VolumeSessionManager's
    // ReconciliationSchedulerLoop poll). Runs a cheap passive checkpoint
    // every call and escalates to a forced checkpoint once the WAL has
    // grown past the threshold (design.md "Risks": unbounded WAL growth
    // under sustained write bursts). No-op if the store was never opened.
    void RunStoreMaintenance(bool userIdle = false, uint64_t vacuumIntervalHours = 24,
                             uint64_t vacuumSizeGrowthPercent = 10);
    // task 3.1/3.2: rebuilds every volume already known to the durable
    // store, invoking onVolumeRebuilt once per volume whose rebuild and rule
    // pruning both complete successfully. A volume whose pruning transaction
    // fails remains retryable and is deliberately not reported as complete.
    using VolumeRebuiltCallback = std::function<void(ffindexstore::VolumeRowId)>;
    void RebuildAll(const VolumeRebuiltCallback& onVolumeRebuilt);
    // close-independent-validation-gaps 4.6: initial and reconciliation
    // scans are bulk projection mutations. Their durable writes continue
    // normally, while the projection defers compaction and its optional
    // filename accelerator until the bulk scan is no longer active.
    void SetBulkScanActive(bool active);
    ffindexstore::VolumeRowId ResolveVolume(const ffindexstore::VolumeKey& key);
    std::optional<ffindexstore::VolumeMetadata> GetVolumeMetadata(ffindexstore::VolumeRowId id);
    std::vector<ffindexstore::VolumeMetadata> GetAllVolumes();
    // zero-touch-autonomous-engineering (settings-and-appearance 2.5 /
    // subtree gating): stores the per-volume include/exclude rules + drive-
    // letter prefix the ingestion pipeline uses to decide IsPathIncluded.
    // A volume with no rules (or not yet configured) includes everything,
    // preserving the behavior prior to this change. Called by
    // VolumeSessionManager whenever a session is established or the persisted
    // configuration changes (ReloadConfiguration), so rule changes take effect
    // on subsequent ingestion and the next reconciliation pass without an
    // engine restart.
    void SetVolumeRules(ffindexstore::VolumeRowId volumeId, const ffprotocol::VolumeSetting& setting);
    // tasks.md 3.3/3.4/6.2: commits the batch to the durable store first;
    // only applies it to the projection once that commit succeeds. On a
    // failed commit, the projection is left untouched and false is
    // returned so the caller can retry the same batch later -- it is
    // never partially applied. Entries are always linked by
    // FileReferenceNumber (never a re-derived path), which is what tasks.md
    // 6.2 calls "filename canonicalization ... keyed off
    // FileReferenceNumber" in practice: EntryRecord.parentId always comes
    // from the wire record's own parent FRN field, never from a computed
    // path.
    bool ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV1>& records);
    // allocated-size-storage-accuracy 5.3: V2 scan records (additive
    // allocated size) applied with the full-record mask, exactly like V1.
    bool ApplyMftBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::MftRecordV2>& records);
    // USN_REASON_FILE_DELETE-flagged records are applied as removals;
    // everything else is an upsert (tasks.md 6.1's engine-side consumer).
    bool ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV1>& records);
    bool ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV2>& records);
    // allocated-size-storage-accuracy 5.3: V3 USN records (additive
    // allocated-size presence bit) applied with per-field presence masks.
    bool ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV3>& records);
private:
    // Shared commit-then-apply tail for scan batches (allocated-size-
    // storage-accuracy refactor: the V1/V2 overloads differ only in record
    // conversion, so the durable-commit/projection/reconciliation logic
    // lives here exactly once).
    bool ApplyMftBatchChanges(ffindexstore::VolumeRowId volumeId,
                              std::vector<ffindexstore::EntryChange>&& changes);
    // Shared commit-then-apply tail for USN batches.
    bool ApplyUsnBatchChanges(ffindexstore::VolumeRowId volumeId,
                              std::vector<ffindexstore::EntryChange>&& changes);
#ifdef FF_INDEX_PIPELINE_TESTING
    bool ShouldInjectSecondaryPruningFailureLocked(
        SecondaryPruningBoundary boundary, ffindexstore::VolumeRowId volumeId,
        const std::vector<ffindexstore::EntryChange>& changes);
#endif
    ffprotocol::IndexedSearchResponse SearchIndexedReadPlane(
        const ffprotocol::IndexedSearchRequest& request,
        SearchTimings* timings, const std::function<bool()>& cancelled);
public:
    bool SetVolumeAvailable(ffindexstore::VolumeRowId id, bool available, uint64_t nowTimestamp);
    bool SetScanCursor(ffindexstore::VolumeRowId id, const std::vector<uint8_t>& cursor);
    bool MarkScanComplete(ffindexstore::VolumeRowId id);
    bool SetJournalPosition(ffindexstore::VolumeRowId id, uint64_t journalId, uint64_t resumeUsn);
    bool SetLastReconciliationTime(ffindexstore::VolumeRowId id, uint64_t nowTimestamp);
    bool ForgetVolume(ffindexstore::VolumeRowId id);
    // tasks.md section 8: a from-scratch scan (StartVolumeScan issued
    // with no resume cursor) doubles as a reconciliation pass once it
    // runs to completion, since a full MFT enumeration is ground truth
    // for what currently exists. BeginReconciliationPass starts tracking
    // which (already-known) entries this pass observes; every Upsert
    // applied via ApplyMftBatch while a pass is active for that volume is
    // recorded; FinishReconciliationPass (call on ScanComplete) removes
    // any previously-persisted entry that the pass never observed --
    // tasks.md 8.1's "entries present in the index but no longer
    // resolvable". Scoped to a single continuous engine session: if the
    // engine restarts mid-pass, the in-memory observed-set is lost and
    // the next *fully-completed* pass reconciles correctly rather than
    // acting on a partial view.
    void BeginReconciliationPass(ffindexstore::VolumeRowId id);
    void FinishReconciliationPass(ffindexstore::VolumeRowId id);
    // index-durability-false-success-closure D3/D5: discard an in-flight
    // pass without finalizing -- no removals, no epoch change. Used when
    // the scan that was supplying the pass's seen-set turned out not to
    // have run to completion (ScanFailed, batch-apply failure, or pause),
    // so its partial coverage can never be mistaken for the whole volume.
    void AbortReconciliationPass(ffindexstore::VolumeRowId id);
    bool IsReconciliationPassActive(ffindexstore::VolumeRowId id);
    // Converts the current projection state for one volume into the
    // existing directory-listing snapshot wire format (SnapshotFormat.h),
    // keyed by full reconstructed path under `rootPathPrefix` (e.g.
    // L"C:"), so it can be published through the already-established
    // committed-generation snapshot mechanism (tasks.md 3.5) without
    // inventing a new wire format for this change.
    std::map<std::wstring, ffprotocol::SnapshotDirectory> ExportDirectorySnapshot(
        ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix);
    // memory-bounded-indexed-scale 5.1: bulk indexed publication (startup
    // rebuild, scan-complete) exports at most maxDirectories entries in the
    // map's deterministic path-sorted order -- i.e., the lexicographically
    // shallowest/top-of-tree directories survive. Deeper directories remain
    // durable and re-materialize through the normal request path, so published
    // bytes scale with the browse working set rather than the whole tree.
    static constexpr size_t kInitialIndexedExportDirectories = 4096;
    std::map<std::wstring, ffprotocol::SnapshotDirectory> ExportDirectorySnapshotBounded(
        ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix,
        size_t maxDirectories);
    // performance-closure-and-multisurface-acceleration 6.1: pooled-name
    // export. Same traversal, ordering, and bounded-eviction semantics as
    // ExportDirectorySnapshotBounded (the owning exports above are converting
    // wrappers over this core), but entry names land in per-directory pools
    // instead of per-entry owning std::wstring allocations. Measured on the
    // 200k-entry D5 isolation matrix: the owning build+serialize stages cost
    // 59% more at byte-identical payload. Callers that publish must pass the
    // pooled result straight through so the eliminated allocations never
    // reappear at a merge boundary.
    ffprotocol::PooledSnapshotMap ExportDirectorySnapshotBoundedPooled(
        ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix,
        size_t maxDirectories);
    // file-preview-and-properties §6.2 / storage-analysis §3.1: returns the
    // subtree aggregate for `parentFrn` from the in-memory projection if
    // the folder is known, or std::nullopt if it is not.
    std::optional<ffindexstore::Projection::FolderAggregate> GetFolderAggregate(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn);
    // memory-bounded-indexed-scale 3.x: bounded-projection seams. The budget
    // override applies synchronously and strictly (like
    // Projection::SetMaximumMemoryBytes), so tests -- and any future settings
    // surface -- can force deterministic eviction. The diagnostics/volume
    // residency accessors back observability without exposing the projection.
    void SetProjectionMemoryBudget(size_t maximumBytes);
    ffindexstore::Projection::MemoryDiagnostics GetProjectionMemoryDiagnostics() const;
    // performance-closure-and-multisurface-acceleration 8.1: read-only
    // per-category projection memory decomposition (see
    // Projection::MemoryBreakdown). Diagnostic only; no representation or
    // policy change.
    ffindexstore::Projection::MemoryBreakdown GetProjectionMemoryBreakdown() const;
    bool IsVolumeEvicted(ffindexstore::VolumeRowId volumeId) const;
    // memory-bounded-indexed-scale 5.x: explicit policy hook dropping one
    // volume's resident rows (durable data untouched). Also the deterministic
    // forced-eviction seam for the bounded-scale test suite.
    void UnloadVolumeFromProjection(ffindexstore::VolumeRowId volumeId);
    // Executes a bounded query against the durable in-memory projection. The
    // returned candidates carry reconstructed parent folders so the UI can
    // navigate a result without requiring the entire index over IPC. The
    // projection lock is held while copying lightweight candidate data;
    // matching and sorting happen after that copy, and lazy folder
    // reconstruction reacquires the lock only for path-dependent survivors.
    using SearchCancellation = std::function<bool()>;
    ffprotocol::IndexedSearchResponse SearchIndexed(const ffprotocol::IndexedSearchRequest& request,
                                                    SearchTimings* timings = nullptr,
                                                    const SearchCancellation& cancelled = {});
private:
    enum class QueryMutationReason : uint8_t {
        EntryMutation,
        VolumeConfiguration,
        VolumeVisibility,
        Rebuild,
        Forget,
        ProjectionResidency,
        Lifecycle,
    };
    struct SearchDependency {
        ffindexstore::VolumeRowId volumeId = 0;
        uint64_t dataEpoch = 0;
        uint64_t configurationEpoch = 0;
    };
    static constexpr size_t kHotCandidateChunkRows = 256;
    // indexed-search-zero-copy-hot-path: batch size for the lightweight
    // view-first capture chunk (mirrors kHotCandidateChunkRows).
    static constexpr size_t kViewCandidateChunkRows = kHotCandidateChunkRows;
    static constexpr size_t kMaxGenerationRestarts = 3;
    static constexpr size_t kMaxDurableFallbackConfigurationRetries = 2;
    static constexpr size_t kMaximumFilenameIndexCandidates = 16 * 1024;
    void AdvanceQueryVisibleEpochLocked(ffindexstore::VolumeRowId volumeId,
                                        QueryMutationReason reason,
                                        bool dependencySetChanged = false);
    // large-index-lifecycle-and-search-scalability review-lane D P1-1: bumps
    // ProjectionResidency for every known volume after a structural
    // projection change (memory-pressure eviction + compaction, cold reload)
    // so in-flight searches restart instead of serving silently incomplete
    // results.
    void AdvanceResidencyEpochsForStructuralChangeLocked();
    void RecordMetadataOnlyUpdateLocked();
    static bool VolumeSettingsEqual(const ffprotocol::VolumeSetting& left,
                                    const ffprotocol::VolumeSetting& right);
    mutable std::mutex mutex_;
    ffindexstore::Store store_;
    ffindexstore::Projection projection_;
    SearchTestHooks searchTestHooks_;
    ffindexstore::ReadSessionFaultInjector readSessionFaultInjector_;
#ifdef FF_INDEX_PIPELINE_TESTING
    SecondaryPruningFaultInjector secondaryPruningFaultInjector_;
#endif
    std::atomic<uint64_t> queryVisibleEpoch_{1};
    std::atomic<uint64_t> dependencySetEpoch_{1};
    std::unordered_map<ffindexstore::VolumeRowId, uint64_t> volumeDataEpochs_;
    std::unordered_map<ffindexstore::VolumeRowId, uint64_t> volumeConfigurationEpochs_;
    std::atomic<size_t> activeSearches_{0};
    std::atomic<size_t> peakSearches_{0};
    std::atomic<size_t> activeDurableReaders_{0};
    std::atomic<size_t> peakDurableReaders_{0};
    std::atomic<uint64_t> completedSearches_{0};
    std::atomic<uint64_t> cancelledSearches_{0};
    std::atomic<uint64_t> unavailableSearches_{0};
    std::atomic<uint64_t> readerFailures_{0};
    std::atomic<uint64_t> generationRestarts_{0};
    std::atomic<uint64_t> queryVisibleInvalidations_{0};
    std::atomic<uint64_t> metadataOnlyUpdates_{0};
    std::atomic<uint64_t> metadataOnlyInvalidationsAvoided_{0};
    std::atomic<uint64_t> durableFallbacks_{0};
    std::atomic<uint64_t> successfulDurableFallbacks_{0};
    std::atomic<uint64_t> failedDurableFallbacks_{0};
    std::atomic<uint64_t> durableFallbackRowsStreamed_{0};
    std::atomic<uint64_t> lifecycleEpoch_{1};
    // zero-touch-autonomous-engineering (subtree gating): per-volume rules +
    // drive-letter prefix (VolumeSetting.key, e.g. "C:") used to evaluate
    // ffprotocol::IsPathIncluded against reconstructed canonical paths.
    std::unordered_map<ffindexstore::VolumeRowId, ffprotocol::VolumeSetting> volumeRules_;
    // Returns true if the entry should be kept (included) under the volume's
    // configured rules. Caller MUST hold mutex_. Returns true (include) when
    // no rules are configured for the volume (preserves pre-change behavior),
    // or when the entry's parent chain cannot yet be resolved in the
    // projection (deferred: decided on the next reconciliation pass rather
    // than risk dropping a record whose inclusion is genuinely undecided).
    bool IsEntryIncludedLocked(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId frn);
    // address-engineering-audit-findings 5.1: keep planner statistics fresh
    // without adding ANALYZE work to every small journal update. The counter
    // is retained when ANALYZE fails so a later ingestion batch retries it.
    static constexpr uint64_t kAnalyzeRecordThreshold = 10'000;
    void MaybeAnalyzeAfterBulkInsertLocked(size_t recordCount);
    void MaybeReportProjectionMemoryPressureLocked();
    uint64_t recordsSinceAnalyze_ = 0;
    bool projectionMemoryWarningIssued_ = false;
    struct FileIdHash {
        size_t operator()(const ffindexstore::FileId& id) const noexcept {
            return std::hash<uint64_t>{}(id.low) ^ (std::hash<uint64_t>{}(id.high) << 1);
        }
    };
    std::unordered_map<ffindexstore::VolumeRowId, std::unordered_set<ffindexstore::FileId, FileIdHash>>
        reconciliationSeen_;
};
[3 more lines in file. Use offset=474 to continue.]
IndexPipeline.cpp-421- }
IndexPipeline.cpp-422- 
IndexPipeline.cpp-423- void IndexPipeline::AdvanceResidencyEpochsForStructuralChangeLocked() {
IndexPipeline.cpp-424-     // large-index-lifecycle-and-search-scalability review-lane D P1-1 / lane C
IndexPipeline.cpp-425-     // Finding 1: memory-pressure enforcement and cold reload are structural
IndexPipeline.cpp-426-     // projection events -- EvictSubtree can cold-mark ANY volume (the LRU
IndexPipeline.cpp-427-     // victim is a global pick) and CompactStorage/RebuildVolumeFromStore
IndexPipeline.cpp-428-     // repack every row slot, so an in-flight search's captured cursor,
IndexPipeline.cpp-429-     // candidate slots, and cold-set can all be wrong afterwards. Surface the
IndexPipeline.cpp-430-     // change through the SAME ProjectionResidency epoch mechanism
IndexPipeline.cpp:431:     // SetProjectionMemoryBudget / UnloadVolumeFromProjection use, so searches
IndexPipeline.cpp-432-     // restart with a fresh capture instead of silently serving incomplete
IndexPipeline.cpp-433-     // results as Completed. Ordinary per-volume mutations never route through
IndexPipeline.cpp-434-     // here (scoped searches stay insensitive to unrelated writes).
IndexPipeline.cpp-435-     for (const auto& [volumeId, epoch] : volumeDataEpochs_) {
IndexPipeline.cpp-436-         (void)epoch;
IndexPipeline.cpp-437-         AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::ProjectionResidency);
IndexPipeline.cpp-438-     }
IndexPipeline.cpp-439-     AdvanceQueryVisibleEpochLocked(0, QueryMutationReason::ProjectionResidency, true);
IndexPipeline.cpp-440- }
IndexPipeline.cpp-441- 
IndexPipeline.cpp-778-             for (const auto& removal : ruleExcluded) {
IndexPipeline.cpp-779-                 excludedIds.push_back(removal.record.id);
IndexPipeline.cpp-780-             }
IndexPipeline.cpp-781-             // large-index-lifecycle-and-search-scalability 4.4: one bulk
IndexPipeline.cpp-782-             // accelerator update instead of per-entry sorted-vector posting
IndexPipeline.cpp-783-             // erases on mass rule removal.
IndexPipeline.cpp-784-             projection_.RemoveBatch(volumeId, excludedIds);
IndexPipeline.cpp-785-         } else {
IndexPipeline.cpp-786-             // Secondary pruning failed: durable and projection remain consistent (both retain the excluded rows),
IndexPipeline.cpp-787-             // but do not report success. Caller can retry the batch; the next attempt will re-evaluate pruning.
IndexPipeline.cpp:788:             if (projection_.EnforceMemoryLimitNow()) {
IndexPipeline.cpp-789-                 AdvanceResidencyEpochsForStructuralChangeLocked();
IndexPipeline.cpp-790-             }
IndexPipeline.cpp-791-             MaybeReportProjectionMemoryPressureLocked();
IndexPipeline.cpp-792-             return false;
IndexPipeline.cpp-793-         }
IndexPipeline.cpp-794-     }
IndexPipeline.cpp-795-     // invoking it from Projection::Upsert turns a large volume scan into an
IndexPipeline.cpp-796-     // O(N) compaction storm once the bounded memory policy is reached.
IndexPipeline.cpp-797-     if (projection_.EnforceMemoryLimitNow()) {
IndexPipeline.cpp-798-         AdvanceResidencyEpochsForStructuralChangeLocked();
IndexPipeline.cpp-787-             // but do not report success. Caller can retry the batch; the next attempt will re-evaluate pruning.
IndexPipeline.cpp-788-             if (projection_.EnforceMemoryLimitNow()) {
IndexPipeline.cpp-789-                 AdvanceResidencyEpochsForStructuralChangeLocked();
IndexPipeline.cpp-790-             }
IndexPipeline.cpp-791-             MaybeReportProjectionMemoryPressureLocked();
IndexPipeline.cpp-792-             return false;
IndexPipeline.cpp-793-         }
IndexPipeline.cpp-794-     }
IndexPipeline.cpp-795-     // invoking it from Projection::Upsert turns a large volume scan into an
IndexPipeline.cpp-796-     // O(N) compaction storm once the bounded memory policy is reached.
IndexPipeline.cpp:797:     if (projection_.EnforceMemoryLimitNow()) {
IndexPipeline.cpp-798-         AdvanceResidencyEpochsForStructuralChangeLocked();
IndexPipeline.cpp-799-     }
IndexPipeline.cpp-800-     MaybeReportProjectionMemoryPressureLocked();
IndexPipeline.cpp-801-     if (!changes.empty()) {
IndexPipeline.cpp-802-         AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::EntryMutation);
IndexPipeline.cpp-803-     }
IndexPipeline.cpp-804-     return true;
IndexPipeline.cpp-805- }
IndexPipeline.cpp-806- 
IndexPipeline.cpp-807- bool IndexPipeline::ApplyUsnBatch(ffindexstore::VolumeRowId volumeId, const std::vector<ffprotocol::UsnDeltaV1>& records) {
IndexPipeline.cpp-918-             std::vector<ffindexstore::FileId> excludedIds;
IndexPipeline.cpp-919-             excludedIds.reserve(ruleExcluded.size());
IndexPipeline.cpp-920-             for (const auto& removal : ruleExcluded) {
IndexPipeline.cpp-921-                 excludedIds.push_back(removal.record.id);
IndexPipeline.cpp-922-             }
IndexPipeline.cpp-923-             // large-index-lifecycle-and-search-scalability 4.4: one bulk
IndexPipeline.cpp-924-             // accelerator update instead of per-entry sorted-vector posting
IndexPipeline.cpp-925-             // erases on mass rule removal.
IndexPipeline.cpp-926-             projection_.RemoveBatch(volumeId, excludedIds);
IndexPipeline.cpp-927-         } else {
IndexPipeline.cpp:928:             if (projection_.EnforceMemoryLimitNow()) {
IndexPipeline.cpp-929-                 AdvanceResidencyEpochsForStructuralChangeLocked();
IndexPipeline.cpp-930-             }
IndexPipeline.cpp-931-             MaybeReportProjectionMemoryPressureLocked();
IndexPipeline.cpp-932-             return false;
IndexPipeline.cpp-933-         }
IndexPipeline.cpp-934-     }
IndexPipeline.cpp-935-     if (projection_.EnforceMemoryLimitNow()) {
IndexPipeline.cpp-936-         AdvanceResidencyEpochsForStructuralChangeLocked();
IndexPipeline.cpp-937-     }
IndexPipeline.cpp-938-     MaybeReportProjectionMemoryPressureLocked();
IndexPipeline.cpp-925-             // erases on mass rule removal.
IndexPipeline.cpp-926-             projection_.RemoveBatch(volumeId, excludedIds);
IndexPipeline.cpp-927-         } else {
IndexPipeline.cpp-928-             if (projection_.EnforceMemoryLimitNow()) {
IndexPipeline.cpp-929-                 AdvanceResidencyEpochsForStructuralChangeLocked();
IndexPipeline.cpp-930-             }
IndexPipeline.cpp-931-             MaybeReportProjectionMemoryPressureLocked();
IndexPipeline.cpp-932-             return false;
IndexPipeline.cpp-933-         }
IndexPipeline.cpp-934-     }
IndexPipeline.cpp:935:     if (projection_.EnforceMemoryLimitNow()) {
IndexPipeline.cpp-936-         AdvanceResidencyEpochsForStructuralChangeLocked();
IndexPipeline.cpp-937-     }
IndexPipeline.cpp-938-     MaybeReportProjectionMemoryPressureLocked();
IndexPipeline.cpp-939-     if (!changes.empty()) {
IndexPipeline.cpp-940-         AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::EntryMutation);
IndexPipeline.cpp-941-     }
IndexPipeline.cpp-942-     return true;
IndexPipeline.cpp-943- }
IndexPipeline.cpp-944- 
IndexPipeline.cpp-945- bool IndexPipeline::SetVolumeAvailable(ffindexstore::VolumeRowId id, bool available, uint64_t nowTimestamp) {
IndexPipeline.cpp-1239-     ffindexstore::Projection::FolderAggregate result;
IndexPipeline.cpp-1240-     result.itemCount = durable.itemCount;
IndexPipeline.cpp-1241-     result.totalSizeBytes = durable.totalSizeBytes;
IndexPipeline.cpp-1242-     result.totalAllocatedSizeBytes = durable.totalAllocatedSizeBytes;
IndexPipeline.cpp-1243-     result.allocatedBytesKnownCount = durable.allocatedBytesKnownCount;
IndexPipeline.cpp-1244-     result.allocatedBytesTotalCount = durable.allocatedBytesTotalCount;
IndexPipeline.cpp-1245-     result.complete = durable.allocatedBytesKnownCount == durable.allocatedBytesTotalCount;
IndexPipeline.cpp-1246-     return result;
IndexPipeline.cpp-1247- }
IndexPipeline.cpp-1248- 
IndexPipeline.cpp:1249: void IndexPipeline::SetProjectionMemoryBudget(const size_t maximumBytes) {
IndexPipeline.cpp-1250-     std::lock_guard<std::mutex> lock(mutex_);
IndexPipeline.cpp-1251-     std::unordered_map<ffindexstore::VolumeRowId, bool> before;
IndexPipeline.cpp-1252-     for (const auto& metadata : store_.GetAllVolumesWithoutEntryCounts()) {
IndexPipeline.cpp-1253-         before.emplace(metadata.rowId, projection_.NeedsReload(metadata.rowId));
IndexPipeline.cpp-1254-     }
IndexPipeline.cpp-1255-     projection_.SetMaximumMemoryBytes(maximumBytes);
IndexPipeline.cpp-1256-     bool residencyChanged = false;
IndexPipeline.cpp-1257-     for (const auto& metadata : store_.GetAllVolumesWithoutEntryCounts()) {
IndexPipeline.cpp-1258-         const bool after = projection_.NeedsReload(metadata.rowId);
IndexPipeline.cpp-1259-         const auto beforeIt = before.find(metadata.rowId);
IndexPipeline.cpp-1276- ffindexstore::Projection::MemoryBreakdown IndexPipeline::GetProjectionMemoryBreakdown() const {
IndexPipeline.cpp-1277-     std::lock_guard<std::mutex> lock(mutex_);
IndexPipeline.cpp-1278-     return projection_.GetMemoryBreakdown();
IndexPipeline.cpp-1279- }
IndexPipeline.cpp-1280- 
IndexPipeline.cpp-1281- bool IndexPipeline::IsVolumeEvicted(const ffindexstore::VolumeRowId volumeId) const {
IndexPipeline.cpp-1282-     std::lock_guard<std::mutex> lock(mutex_);
IndexPipeline.cpp-1283-     return projection_.NeedsReload(volumeId);
IndexPipeline.cpp-1284- }
IndexPipeline.cpp-1285- 
IndexPipeline.cpp:1286: void IndexPipeline::UnloadVolumeFromProjection(const ffindexstore::VolumeRowId volumeId) {
IndexPipeline.cpp-1287-     std::lock_guard<std::mutex> lock(mutex_);
IndexPipeline.cpp-1288-     const bool wasEvicted = projection_.NeedsReload(volumeId);
IndexPipeline.cpp-1289-     projection_.EvictVolume(volumeId);
IndexPipeline.cpp-1290-     if (!wasEvicted && projection_.NeedsReload(volumeId)) {
IndexPipeline.cpp-1291-         AdvanceQueryVisibleEpochLocked(volumeId, QueryMutationReason::ProjectionResidency);
IndexPipeline.cpp-1292-     } else {
IndexPipeline.cpp-1293-         RecordMetadataOnlyUpdateLocked();
IndexPipeline.cpp-1294-     }
IndexPipeline.cpp-1295- }
IndexPipeline.cpp-1296- 
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
# Memory budget & cold-query characterization (tasks 8.2/19) — 2026-10-21
Measurement host: same campaign host, idle, release-test binaries at
`b6df9ef` (after) vs `420792d` (before). Same harness/command both sides:
`ffengine_bench_large_index_scalability --scales 200000,1000000 --diagnostics`
(`build/campaign/cold-profile-*.json` + console logs).
## 1. Policy map (verified against source; full map in the Agent-C audit)
- `Projection::kDefaultMaximumMemoryBytes` = **500 MiB**; the early-enforcement
  watermark (`EnforceMemoryLimit` at batch boundaries, `IsMemoryPressureHigh`)
  is `max - max/5` = **400 MiB**. Only `SetMaximumMemoryBytes` enforces the
  strict 500 MiB ceiling synchronously.
- `IndexPipeline::SetProjectionMemoryBudget` has **no production caller**
  (repo-wide grep: tests only). The production budget is therefore the fixed
  500 MiB default.
- Eviction: subtree victims preferred (oldest directory subtree), and
  `EvictSubtree` marks the **whole volume** cold. Reload
  (`ReloadEvictedVolume` -> `RebuildVolumeFromStore`) is a full-volume
  re-stream under the pipeline mutex (snapshot export path only; search never
  reloads). A volume larger than the low watermark can never reload (tested
  thrash behavior).
- Cold search streams `SELECT ... FROM entries WHERE volume_id=?` with no
  name predicate and no LIMIT; every cold row was decoded and matched in C++.
## 2. Cold-path profiling (the bottleneck, proven before changing anything)
At 1M rows (forced eviction; `coldRows` = rows reaching the C++ matcher):
| class | before: COLD ms / rows | after: COLD ms / rows | delta |
|---|---:|---:|---:|
| selective | 1470 / 1,000,000 | 1141 / 98,438 | 1.29x |
| no_match | 1413 / 1,000,000 | 1101 / 98,437 | 1.28x |
| exact_name | 1601 / 1,000,000 | 1115 / 98,438 | 1.44x |
| moderate | 6261 / 1,000,000 | 5817 / 196,875 | 1.08x |
| prefix | 6087 / 1,000,000 | 6449 / 196,875 | ~1.0 (noise) |
| broad | 17,680 / 1,000,000 | 14,885 / 393,749 | 1.19x |
| worst_substring | 10,921 / 1,000,000 | 10,498 / 295,313 | 1.04x |
| mixed_case | 11,891 / 1,000,000 | 11,476 / 295,312 | 1.04x |
| high_limit_broad | 16,337 / 1,000,000 | 16,120 / 393,749 | 1.01x |
| short_prefix | 16,354 / 1,000,000 | 15,357 / 1,000,000 | prefilter ineligible (<3) |
| extension | 19,971 / 1,000,000 | 21,758 / 1,000,000 | prefilter ineligible (predicate) |
| unicode | 6437 / 1,000,000 | 6237 / 1,000,000 | prefilter ineligible (non-ASCII needle) |
Before the change `stream` (the SQLite stream + decode + matcher) was >99.8%
of every cold class (e.g. selective 1468.5 of 1469.7 ms); hot selective stayed
2.5 ms (accelerated). Review-lane E E-6 correction: that percentage is a
timer-coverage tautology (the `storeStreamMs` timer wraps the entire
stream+match loop), not a scan/decode/match localization -- no such split was
instrumented. The prefilter case rests on the row-count mechanism
(decode/materialize 1M -> 98k rows), the before/after delta, and the
below-subtraction floor, not on the attribution percentage.
Measurement-discipline corrections (review-lane E E-1/E-2/E-3):
- Every cold cell is a **single measured sample** per class per scale (the
  bench runs one measured cold query after one warmup; hot figures are
  medians over 10 (200k) / 5 (1M) warm runs). "COLD" here means
  cold-projection with a warm SQLite page cache.
- The two runs were **not host-comparable on CPU-bound paths**: untouched hot
  medians moved 1.2-2.8x at 1M and up to 1.4x slower at 200k. The defensible
  noise control is the prefilter-ineligible cold classes measured in the SAME
  block (extension 19,971->21,758 ms; short_prefix 16,354->15,357; unicode
  6,437->6,237: -9%..+6%). Against that control the 1.28-1.44x literal-class
  deltas exceed observed drift and the row-count mechanism supports the
  direction, but each cell is one sample and the effect carries ~±10%
  uncertainty.
- Two-point subtraction on the no_match pair gives a survivor cost of ~0.35
  us/row and a **scan floor of ~1.05-1.10 us/scanned-row** (consistent at both
  scales) -- not the ~0.8-0.9 us/row previously written. Even a zero-survivor
  literal is therefore ~1.05-1.1 s at 1M; on a 1-2% non-ASCII corpus (an
  unmeasured assumption; CJK-heavy corpora fall back toward the measured
  9.8%-survivor case) cold selective at 1M is ~1.1 s vs 1.47 s before
  (~1.3-1.4x).
- Provenance note (lane E E-8): the two cold-profile artifacts do not embed
  source identity; the `420792d`/`b6df9ef` pairing is asserted provenance
  (consistent with observable data such as the durable-fallback row drop),
  and future runs should embed `--source-sha` like the memory-report does.
The survivors at 1M are exactly the fixture's non-ASCII name population
(98,438 = 9.8%): the conservative GLOB disjunct admits every name carrying
non-ASCII bytes (host-fold safety, see review-remediation P2-A). The hard
floor is the SQLite name-column scan itself (~1.05-1.10 us/row, see the
corrections above), so expected real-corpus cold selective latency at 1M is
~1.1 s vs 1.47 s before -- about 1.3-1.4x. Run-to-run noise between the two
runs reached 2.8x on untouched hot paths (host state differed); the
same-block ineligible-class control bounds the drift at -9%..+6% for the
cold table.
## 3. The 2M regime (task 8.2 finding, characterized)
The 2M-row projection models ~839.7 MB > the 500 MiB default, so every 2M
query serves from the cold stream under the default budget (historical
search-matrix p50 at 2M: selective ~2.5 s, broad ~27.1 s, extension ~37.6 s;
the prefilter does not change non-literal classes and removes only the
survivor decode share for literals). Serving 2M rows hot would require a
budget near 1 GiB -- not raised here (see decision).
## 4. Product decision (mission 19): **Option A + E, with the cold prefilter
## shipped as the evidence-backed improvement**
- **Option A (retained):** keep the 500 MiB default; accelerate the cold path
  within existing semantics. Shipped: `ForEachEntryWithAsciiNameUntil`
  conservative SQL name prefilter (commit `b6df9ef`), superset-proven
  (LIKE-ASCII-fold + non-ASCII-bytes disjunct; red-first on the LIKE-escape
  under-match class) and measured above. No schema change, no new memory.
- **Option B (adaptive budget) — rejected for this change:** an adaptive
  policy needs floors/ceilings and behavior proofs under changing available
  RAM; none of the measured evidence requires it yet, and the mission forbids
  raising default consumption without safety proof.
- **Option C (hybrid hot/cold residency) — deferred as separately scoped
  architecture work:** eviction currently marks a whole volume cold and the
  reload path re-streams under the mutex; a compact searchable subset would
  need strict consistency + memory-bound proofs (a new OpenSpec change).
- **Option D (explicit setting) — rejected:** `SetProjectionMemoryBudget` has
  no production caller and no settings surface/persistence semantics exist;
  a half-wired setting would be a worse product than none.
- **Option E (document the limitation) — adopted:** the 2M-entry cold regime
  is documented precisely above; the remaining candidate improvement (a
  durable trigram/FTS candidate table, schema v3 + migration + superset proof)
  is deliberately NOT taken here because the mission requires separate schema
  justification and the measured 1.3-1.7x cold-literal gain does not justify
  that risk inside this campaign.
## 5. Retention classification (mission 23) for the one performance change
- Candidate: cold SQL name prefilter (`b6df9ef`), baseline `420792d`.
- Fixture: same synthetic 200k/1M corpora (10% non-ASCII names), same bench.
  Hot figures are medians over the harness's warm runs; every cold figure is
  a single measured sample per class per scale (lane E E-2), on runs whose
  CPU-bound paths were not host-comparable (E-1); see the corrections in §2.
- Correctness: hot-vs-cold identity+ordering signatures (extended
  `TestForcedEvictionSearchEquivalence`), store-level superset + escaping
  red-first (`build/campaign/prefilter-red-run.log`), full suites green.
- Decision: **RETAIN** (correct; 1.28-1.44x on cold literal classes at 10%
  non-ASCII vs a -9%..+6% same-block control, ~1.3-1.4x expected on real
  corpora; zero schema/memory risk). Scope conditions on the prior hot
  acceleration (lane E E-5): the 56-189x figure is hot/projection-resident,
  planner-eligible literal needles only (best case 189x = 1-result ASCII
  literal at 1M; rows examined 1M->1), it repairs a planner bug that had
  disabled the accelerator entirely, and it does NOT apply to cold queries
  or the >=2M regime, where all queries serve from the durable stream and
  latency is governed by the scan floor above. Lane E E-7: the 1M cold table
  characterizes the production cold-stream cost model under a state that at
  1M rows requires multi-volume memory pressure to reach (the 1M projection
  models ~318 MiB, below the 400 MiB watermark); the common production cold
  regime is >=2M rows.
Identity.h:12: struct FileId {
Identity.h:13:     uint64_t low = 0;
Identity.h:14:     uint64_t high = 0;
Identity.h:29: struct VolumeKey {
Identity.h:30:     std::array<uint8_t, 16> volumeGuid{};
Identity.h:31:     uint32_t serialNumber = 0;
Identity.h:45: using VolumeRowId = int64_t;
Identity.h:49: struct EntryKey {
Identity.h:59: struct EntryKeyHash {
Identity.h:64:         uint64_t h = static_cast<uint64_t>(key.volumeRowId);
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
read: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/s...
NamePool.h:9: using NameId = uint32_t;
NamePool.h:25:     void Reserve(size_t expectedUniqueNames, size_t expectedTotalChars);
NamePool.h:41:     size_t UniqueNameCount() const noexcept { return table_.size(); }
NamePool.h:42:     size_t ArenaSizeChars() const noexcept { return arena_.size(); }
NamePool.h:50:     size_t ArenaCapacityChars() const noexcept { return arena_.capacity(); }
NamePool.h:51:     size_t ArenaCapacityBytes() const noexcept { return arena_.capacity() * sizeof(char16_t); }
NamePool.h:52:     size_t TableSlotCount() const noexcept { return table_.size(); }
NamePool.h:53:     size_t TableSlotCapacityBytes() const noexcept { return table_.capacity() * sizeof(Slot); }
NamePool.h:54:     size_t HashBucketCount() const noexcept { return hashBuckets_.bucket_count(); }
NamePool.h:55:     size_t HashBucketElementCount() const noexcept { return hashBuckets_.size(); }
NamePool.h:61:     size_t HashBucketPayloadBytes() const noexcept {
NamePool.h:62:         size_t result = 0;
NamePool.h:72:     size_t GrowthSlackBytes() const noexcept {
NamePool.h:76:     size_t HashBucketArrayBytes() const noexcept { return hashBuckets_.bucket_count() * sizeof(void*); }
NamePool.h:77:     size_t HashNodeApproxBytes() const noexcept {
NamePool.h:80:     size_t MemoryUsageBytes() const noexcept {
NamePool.h:86:     struct Slot {
NamePool.h:87:         uint32_t offset = 0;
NamePool.h:88:         uint32_t length = 0;
[pi-lens: read widened to the enclosing method "Projection::ReconstructPathUntil" (symbol boundary): you asked for lines 1136-1235, this shows lines 1096-1235. Re-request with limit > 100 for the exact range.]
Projection::PathResult Projection::ReconstructPathUntil(
    VolumeRowId volumeRowId, FileId frn, const std::function<bool()>& cancelled) const {
    PathResult result;
    std::vector<NameId> partsReversed;
    std::unordered_set<EntryKey, EntryKeyHash> visited;
    FileId current = frn;
    for (size_t depth = 0; depth < kMaxProjectionPathDepth; ++depth) {
        if (cancelled && cancelled()) break;
        const EntryKey currentKey{volumeRowId, current};
        // task 2.4: a walk that revisits an FRN already seen in *this*
        // walk stops rather than looping -- defensive only, a well-formed
        // volume should never produce this (design.md D7).
        if (!visited.insert(currentKey).second) {
            break;
        }
        const ProjectionEntry* entry = Find(volumeRowId, current);
        if (entry == nullptr) {
            break; // dangling/unknown parent -- stop, incomplete
        }
        partsReversed.push_back(entry->nameId);
        if (entry->parentFrn == current) {
            // Self-referential parent: the volume root (NTFS record 5's
            // parent reference is itself) -- walk terminates successfully.
            result.reachedRoot = true;
            break;
        }
        current = entry->parentFrn;
    }
    for (auto it = partsReversed.rbegin(); it != partsReversed.rend(); ++it) {
        if (cancelled && cancelled()) {
            result.reachedRoot = false;
            break;
        }
        if (!result.path.empty()) {
            result.path.push_back(u'\\');
        }
        auto name = namePool_.Get(*it);
        result.path.append(name.begin(), name.end());
    }
    return result;
}
bool Projection::RebuildVolumeFromStore(Store& store, VolumeRowId volumeRowId, uint64_t expectedEntryCount) {
    const bool wasEnforcingMemoryLimit = enforcingMemoryLimit_;
    enforcingMemoryLimit_ = false;
    bool volumePresent = false;
    idToIndex_.ForEach([&](const EntryKey& key, const uint32_t) {
        if (key.volumeRowId == volumeRowId) volumePresent = true;
    });
    if (volumePresent || NeedsReload(volumeRowId)) {
        // large-index-lifecycle-and-search-scalability 4.4: the rebuild
        // discards and re-streams every row and rebuilds the accelerator
        // once at the end, so per-entry accelerator maintenance during the
        // removal is pure waste (the measured 40x replacement-rebuild
        // pathology). keepFilenameIndexCurrent=false skips the double rebuild.
        RemoveVolume(volumeRowId, false);
        CompactStorage();
    }
    evictedVolumes_.erase(volumeRowId);
    denseIterationOrderValid_ = false;
    filenameIndexValid_ = false;
    // large-index-lifecycle-and-search-scalability review-lane D P1-2:
    // fail fast when the reloaded volume cannot survive the trailing
    // enforcement. Without this guard an oversized volume is streamed in
    // full (unbounded transient above the watermark, under the pipeline
    // mutex) only to have enforcement evict it again -- returning false and
    // an empty export, repeatedly. The estimate uses this projection's own
    // measured average row cost against the early-enforcement watermark.
    {
        const size_t lowWatermark = maximumMemoryBytes_ - maximumMemoryBytes_ / 5;
        const size_t baseline = EstimatedMemoryBytes();
        const size_t rowCount = EntryCount();
        const size_t averageRowBytes = rowCount == 0 ? 512 : baseline / rowCount;
        const size_t projected = expectedEntryCount > (lowWatermark / (averageRowBytes == 0 ? 1 : averageRowBytes))
            ? lowWatermark + 1
            : baseline + expectedEntryCount * averageRowBytes;
        if (projected > lowWatermark) {
            evictedVolumes_.insert(volumeRowId);
            enforcingMemoryLimit_ = wasEnforcingMemoryLimit;
            return false;
        }
    }
    Reserve(EntryCount() + expectedEntryCount);
    // large-index-lifecycle-and-search-scalability review-lane D P2-2:
    // allocation failure mid-stream must fall into the same cleanup as a
    // failed durable read -- a partial stream left the volume looking hot and
    // complete, silently missing rows forever. Store::ForEachEntry does not
    // catch visitor exceptions.
    bool streamed = false;
    try {
        streamed = store.ForEachEntry(volumeRowId, [&](const EntryRecord& record) {
            Upsert(volumeRowId, record);
        });
    } catch (...) {
        streamed = false;
    }
    if (!streamed) {
        // completion-integrity-hardening-and-release-certification 4.1.2:
        // a failed durable read may have delivered a prefix of the rows. Do
        // not expose that prefix as a resident volume; retain the cold marker
        // so a later rebuild can retry from the durable source.
        RemoveVolume(volumeRowId, false);
        CompactStorage();
        evictedVolumes_.insert(volumeRowId);
        enforcingMemoryLimit_ = wasEnforcingMemoryLimit;
        EnforceMemoryLimit();
        return false;
    }
    CompactEntriesForIterationOrder();
    // large-index-lifecycle-and-search-scalability review-lane C Finding 6:
    // do not resurrect the accelerator while a bulk scan deliberately keeps
    // it disabled (the incremental Upsert path would then pay quadratic
    // posting inserts for the rest of the scan).
    if (!bulkScanActive_) {
        RebuildFilenameIndex();
    }
    // performance-closure-and-multisurface-acceleration 8.3: a bulk rebuild
    // grows the name arena, the offset table, the flat child-index array, and
    // the trigram posting vectors by geometric reallocation, so each holds
    // unused capacity afterwards. Release that slack once the bulk load is
    // complete; content is unchanged and the next incremental mutation grows
    // the storage again with the usual amortized behavior. The projection's
    // id->index table is deliberately NOT shrunk: its 2x flat-open-addressing
    // sizing is the designed <=75% occupancy bound, not slack.
    ShrinkBulkRebuildCapacity();
    enforcingMemoryLimit_ = wasEnforcingMemoryLimit;
    EnforceMemoryLimit();
    return !NeedsReload(volumeRowId);
}
void Projection::ShrinkBulkRebuildCapacity() {
    // Release slack only when it is material. A single-volume reload into a
    // projection that is already near its live size must not pay a
    // reallocation to return a few bytes, and repeated reloads must not
    // thrash the allocator.
    constexpr size_t kMaterialSlackBytes = 256 * 1024;
[25 more lines in file. Use offset=1236 to continue.]
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
src/IndexPipeline.cpp-421- }
src/IndexPipeline.cpp-422- 
src/IndexPipeline.cpp-423- void IndexPipeline::AdvanceResidencyEpochsForStructuralChangeLocked() {
src/IndexPipeline.cpp-424-     // large-index-lifecycle-and-search-scalability review-lane D P1-1 / lane C
src/IndexPipeline.cpp-425-     // Finding 1: memory-pressure enforcement and cold reload are structural
src/IndexPipeline.cpp-426-     // projection events -- EvictSubtree can cold-mark ANY volume (the LRU
src/IndexPipeline.cpp:427:     // victim is a global pick) and CompactStorage/RebuildVolumeFromStore
src/IndexPipeline.cpp-428-     // repack every row slot, so an in-flight search's captured cursor,
src/IndexPipeline.cpp-429-     // candidate slots, and cold-set can all be wrong afterwards. Surface the
src/IndexPipeline.cpp-430-     // change through the SAME ProjectionResidency epoch mechanism
src/IndexPipeline.cpp-431-     // SetProjectionMemoryBudget / UnloadVolumeFromProjection use, so searches
src/IndexPipeline.cpp-432-     // restart with a fresh capture instead of silently serving incomplete
src/IndexPipeline.cpp-433-     // results as Completed. Ordinary per-volume mutations never route through
src/IndexPipeline.cpp-572- void IndexPipeline::RebuildAll(const VolumeRebuiltCallback& onVolumeRebuilt) {
src/IndexPipeline.cpp-573-     std::vector<ffindexstore::VolumeRowId> volumeIds;
src/IndexPipeline.cpp-574-     {
src/IndexPipeline.cpp-575-         std::lock_guard<std::mutex> lock(mutex_);
src/IndexPipeline.cpp-576-         for (const auto& meta : store_.GetAllVolumes()) {
src/IndexPipeline.cpp-577-             bool rebuildSucceeded =
src/IndexPipeline.cpp:578:                 projection_.RebuildVolumeFromStore(store_, meta.rowId, meta.entryCount);
src/IndexPipeline.cpp-579-             // zero-touch-autonomous-engineering (subtree gating): after the
src/IndexPipeline.cpp-580-             // projection is rebuilt from the durable store (parent chains
src/IndexPipeline.cpp-581-             // fully present), prune any excluded subtrees from store +
src/IndexPipeline.cpp-582-             // projection so the rebuilt index honors the configured rules.
src/IndexPipeline.cpp-583-             // This mirrors the ingestion prune and the reconciliation catch-
src/IndexPipeline.cpp-584-             // all, ensuring rules are honored on every equivalent ingestion
src/IndexPipeline.cpp-1101-     std::lock_guard<std::mutex> lock(mutex_);
src/IndexPipeline.cpp-1102-     // large-index-lifecycle-and-search-scalability review-lane D P1-1: the
src/IndexPipeline.cpp-1103-     // reload (and its trailing enforcement) is a structural change -- bump
src/IndexPipeline.cpp-1104-     // residency epochs so in-flight searches re-capture instead of reading a
src/IndexPipeline.cpp-1105-     // replaced layout.
src/IndexPipeline.cpp-1106-     const bool wasCold = projection_.NeedsReload(id);
src/IndexPipeline.cpp:1107:     const bool reloaded = projection_.ReloadEvictedVolume(store_, id);
src/IndexPipeline.cpp-1108-     if (wasCold) {
src/IndexPipeline.cpp-1109-         AdvanceResidencyEpochsForStructuralChangeLocked();
src/IndexPipeline.cpp-1110-     }
src/IndexPipeline.cpp-1111-     if (!reloaded) return {};
src/IndexPipeline.cpp-1112-     ffprotocol::PooledSnapshotMap result;
src/IndexPipeline.cpp-1113-     projection_.ForEachEntry([&](const ffindexstore::EntryKey& key, const ffindexstore::ProjectionEntry& entry) {
std::wstring FileName(size_t index) {
    const size_t pattern = index % 10;
    if (index == 7391) return L"unique_component_7391.txt";
    if (pattern == 0 || pattern == 1) return L"common_component_" + std::to_wstring(index) + L".txt";
    if (pattern == 2) return L"report_" + std::to_wstring(index) + L".txt";
    if (pattern == 3) return L"photo_" + std::to_wstring(index) + L".jpg";
    if (pattern == 4) return L"archive_" + std::to_wstring(index) + L".zip";
    if (pattern == 5) return L"project_" + std::to_wstring(index) + L".cpp";
    if (pattern == 6) return L"notes_" + std::to_wstring(index) + L".md";
    if (pattern == 7) return L"common_media_" + std::to_wstring(index) + L".mp4";
    // performance-closure-and-multisurface-acceleration 7.1: a deterministic
    // Unicode-name subset (10% of the fixture) so the Unicode query class is
    // measured against real matches, not only the no-result path. Escape
    // sequences keep the source encoding-independent.
    if (pattern == 8) return L"\u0441\u043f\u0438\u0441\u043e\u043a_" + std::to_wstring(index) + L".txt";
    return L"backup_" + std::to_wstring(index) + L".7z";
}
[2102 more lines in file. Use offset=318 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/tes...
# index-engine (delta): durable cold-search candidate generation and hybrid residency
Proposal-stage acceptance invariants for `durable-cold-search-index-and-
hybrid-residency`. These requirements pin the correctness and resource
contract of whatever alternative the campaign's measurement phase selects;
they deliberately do not prescribe the implementation (FTS table, durable
trigram postings, hybrid residency, or another measured alternative).
## ADDED Requirements
### Requirement: Cold candidate generation shall be a conservative superset under Windows ordinal folding
Any durable candidate structure consulted by the cold path SHALL return a
conservative superset of every row the canonical matcher would accept under
`CompareStringOrdinal(..., TRUE)` host semantics, including rows whose names
contain units the host uppercase table equates with ASCII (the fold-exceptional
class) and rows containing surrogate units. Where a candidate generator cannot
prove this property structurally, the engine SHALL maintain a conservative
exception side-set equivalent to the in-memory accelerator's
fold-exceptional/surrogate handling, derived from the host table and fail-closed
on API error.
#### Scenario: Fold-exceptional name is not dropped by the candidate generator
- **WHEN** a query whose folded needle matches a fold-exceptional name (for
  example a name containing a unit the host table folds to ASCII) executes on
  the cold path with the durable candidate structure available
- **THEN** the candidate set contains that row
- **AND** the final result set is identical to the canonical scan's result set
#### Scenario: Generator failure falls back to the canonical scan
- **WHEN** the candidate structure is missing, stale, or cannot establish
  superset coverage for a query shape
- **THEN** the engine serves the query from the canonical cold scan
- **AND** results remain correct (possibly slower)
### Requirement: Cold-with-index results shall equal hot results
For every query class, results served with durable-index assistance on the
cold path SHALL be identical in identity and ordering to results served by the
hot projection path. This extends the pinned hot/cold equivalence contract to
the durable generator.
#### Scenario: Differential identity across scales
- **WHEN** the same corpus and query set are executed hot and cold-with-index
  at 200k, 1M, 2M, and 5M rows
- **THEN** result identities and orderings match exactly for all query classes
### Requirement: Index updates shall be crash-consistent with the entries table
The durable candidate structure SHALL never disagree with the entries table
after a process or machine crash. Updates SHALL be transactionally coupled to
entry mutations or carry a validity/version marker that forces rebuild-on-open
after an unclean shutdown.
#### Scenario: Kill during ingestion batch
- **WHEN** the process is terminated in the middle of an ingestion or USN
  batch that mutates both entries and the candidate structure
- **THEN** after restart the index is either fully consistent with the entries
  recorded at the last committed transaction or is rebuilt before use
- **AND** no query observes candidates for rows that do not exist
### Requirement: Schema migration and rollback shall be measured and reversible
Any schema addition SHALL be versioned and additive, with a measured migration
cost at 2M-5M rows, a documented rollback that loses no indexed data, and a
rebuild-on-open fallback when migration is interrupted.
#### Scenario: Interrupted migration
- **WHEN** migration from the previous schema version is interrupted by a crash
- **THEN** the store opens at the previous schema version with all entries
  intact and either resumes or rebuilds the candidate structure
### Requirement: The durable index shall not raise the default memory budget
Planner caches and candidate structures SHALL have documented, bounded memory
footprints. The default 500 MiB projection budget
(`Projection::kDefaultMaximumMemoryBytes`) SHALL NOT be raised by this change;
any future budget change requires its own measured safety case.
#### Scenario: Bounded caches under sustained query load
- **WHEN** sustained query traffic runs against a 5M-row store
- **THEN** documented cache bounds are not exceeded
- **AND** private bytes show no monotonic growth across query cycles
### Requirement: Planner selection shall be cost-based with canonical fallback
The engine SHALL choose between durable-index candidate generation and the
canonical scan by measured cost rules (needle eligibility, candidate cap,
structure availability), preserving the existing fallback-to-scan behavior for
ineligible query shapes.
#### Scenario: Broad query keeps the canonical scan
- **WHEN** a broad or ineligible query shape executes
- **THEN** the planner does not force candidate generation that exceeds its cap
- **AND** the canonical scan serves the query with correct results
test_index_pipeline.cpp-2443- 
test_index_pipeline.cpp-2444- // large-index-lifecycle-and-search-scalability review-lane D P1-1 / lane C
test_index_pipeline.cpp-2445- // Finding 1: memory-pressure enforcement triggered by an UNRELATED batch can
test_index_pipeline.cpp-2446- // evict and compact any volume. That structural change must surface as a
test_index_pipeline.cpp-2447- // query-visible residency change (the same ProjectionResidency epoch
test_index_pipeline.cpp-2448- // mechanism UnloadVolumeFromProjection uses), so in-flight and future
test_index_pipeline.cpp-2449- // searches re-capture or cold-stream instead of silently serving incomplete
test_index_pipeline.cpp-2450- // results as Completed.
test_index_pipeline.cpp:2451: void TestEnforcementEvictionAdvancesQueryVisibleEpochs() {
test_index_pipeline.cpp-2452-     IndexPipeline pipeline;
test_index_pipeline.cpp-2453-     Check(pipeline.Open(FreshDbPath("engine_test_enforcement_epochs.db")),
test_index_pipeline.cpp-2454-           "enforcement-epoch pipeline opens");
test_index_pipeline.cpp-2455-     VolumeKey firstKey;
test_index_pipeline.cpp-2456-     firstKey.serialNumber = 313;
test_index_pipeline.cpp-2457-     const auto firstVolume = pipeline.ResolveVolume(firstKey);
test_index_pipeline.cpp-2458-     ffprotocol::VolumeSetting firstSetting;
test_index_pipeline.cpp-2459-     firstSetting.key = L"P:";
test_index_pipeline.cpp-2516-     TestVolumeAvailabilityAndForget();
test_index_pipeline.cpp-2517-     TestForgetVolumePurgesProjection();
test_index_pipeline.cpp-2518-     TestIndexedSearchUsesProjectionPathsAndScopes();
test_index_pipeline.cpp-2519-     TestIndexedSearchViewFirstCaptureReducesMaterialization();
test_index_pipeline.cpp-2520-     TestIndexedSearchCancellationStopsProjectionWork();
test_index_pipeline.cpp-2521-         TestIndexedSearchPropagatesAllocatedSize();
test_index_pipeline.cpp-2522-         TestIndexedSearchOndiskFilter();
test_index_pipeline.cpp-2523-         TestForcedEvictionSearchEquivalence();
test_index_pipeline.cpp:2524:         TestEnforcementEvictionAdvancesQueryVisibleEpochs();
test_index_pipeline.cpp-2525-     TestCappedRootSearchMatchesColdAndRetainsTopN();
test_index_pipeline.cpp-2526-         TestRepeatedColdSearchesDoNotThrashMemory();
test_index_pipeline.cpp-2527-         TestColdMutationsAndRestartEquivalence();
test_index_pipeline.cpp-2528-         TestColdFolderAggregateServedFromStore();
test_index_pipeline.cpp-2529-         TestCancellationDuringColdStream();
test_index_pipeline.cpp-2530-     TestMftSecondaryFailureIsFailClosedAndRetryable();
test_index_pipeline.cpp-2531-     TestUsnSecondaryFailureIsBoundarySpecificAndRetryable();
test_index_pipeline.cpp-2532-     TestRebuildSecondaryFailureDoesNotPublishSuccess();
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
[pi-lens: read widened to the enclosing function "BuildSyntheticDatabase" (symbol boundary): you asked for lines 588-622, this shows lines 557-629. Re-request with limit > 100 for the exact range.]
bool BuildSyntheticDatabase(const std::filesystem::path& database, size_t scale, double& generationMs,
                            ffindexstore::VolumeRowId& volumeId, MemorySample& before,
                            MemorySample& after, bool detailedDiagnostics) {
    before = ReadMemory();
    const auto openStart = Clock::now();
    ffengine::IndexPipeline pipeline;
    if (!pipeline.Open(PathToUtf8(database))) return false;
    if (detailedDiagnostics) {
        std::printf("  [%zu] fixture open %.2f ms\n", scale, ElapsedMs(openStart));
        std::printf("  [%zu] durable insert start (target %zu records)\n", scale, scale);
        std::fflush(stdout);
    }
    ffindexstore::VolumeKey key;
    key.serialNumber = static_cast<uint32_t>(scale ^ 0xF457F17u);
    volumeId = pipeline.ResolveVolume(key);
    if (volumeId == 0) return false;
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    pipeline.SetVolumeRules(volumeId, setting);
    const size_t directoryCount = (std::max)(size_t{1}, scale / 64);
    const auto generationStart = Clock::now();
    std::vector<ffprotocol::MftRecordV1> batch;
    batch.reserve(kBatchSize);
    size_t insertedRecords = 0;
    size_t nextProgress = 100'000;
    auto flush = [&] {
        if (batch.empty()) return true;
        const size_t batchRecords = batch.size();
        const bool applied = pipeline.ApplyMftBatch(volumeId, batch);
        batch.clear();
        if (applied) {
            insertedRecords += batchRecords;
            if (detailedDiagnostics && (insertedRecords >= nextProgress || insertedRecords == scale)) {
                std::printf("  [%zu] durable insert progress %zu/%zu (%.2f s)\n", scale,
                            insertedRecords, scale, ElapsedMs(generationStart) / 1000.0);
                std::fflush(stdout);
                while (nextProgress <= insertedRecords) nextProgress += 100'000;
            }
        }
        return applied;
    };
    batch.push_back(MakeMftRecord(5, 5, L"", 0x10, 0, 1));
    for (size_t index = 0; index < directoryCount && 1 + index < scale; ++index) {
        const uint64_t id = 100 + static_cast<uint64_t>(index);
        const uint64_t parent = index < 16 ? 5 : 100 + static_cast<uint64_t>((index - 1) / 16);
        batch.push_back(MakeMftRecord(id, parent, DirectoryName(index), 0x10, 0, 10 + index));
        if (batch.size() == kBatchSize && !flush()) return false;
    }
    const size_t filesToCreate = scale > 1 + directoryCount ? scale - 1 - directoryCount : 0;
    const uint64_t fileIdBase = 100 + static_cast<uint64_t>(directoryCount);
    for (size_t index = 0; index < filesToCreate; ++index) {
        const uint64_t parent = 100 + static_cast<uint64_t>(index % directoryCount);
        const uint64_t id = fileIdBase + static_cast<uint64_t>(index);
        const uint64_t sizeBytes = 1024 + static_cast<uint64_t>((index * 7919) % (64 * 1024 * 1024));
        batch.push_back(MakeMftRecord(id, parent, FileName(index), 0, sizeBytes, 1000 + index));
        if (batch.size() == kBatchSize && !flush()) return false;
    }
    if (!flush()) return false;
    generationMs = ElapsedMs(generationStart);
    after = ReadMemory();
    if (detailedDiagnostics) {
        std::printf("  [%zu] durable insert complete %.2f s (%zu records)\n", scale,
                    generationMs / 1000.0, insertedRecords);
        std::fflush(stdout);
    }
    pipeline.Close();
    return true;
}
[1790 more lines in file. Use offset=630 to continue.]
IndexPipeline.cpp-1535-             }
IndexPipeline.cpp-1536-         }
IndexPipeline.cpp-1537-         return true;
IndexPipeline.cpp-1538-     };
IndexPipeline.cpp-1539- 
IndexPipeline.cpp:1540:     for (size_t restart = 0; restart <= kMaxGenerationRestarts; ++restart) {
IndexPipeline.cpp-1541-         if (isCancelled()) {
IndexPipeline.cpp-1542-             finishCancelled();
IndexPipeline.cpp-1543-             return result;
IndexPipeline.cpp-1544-         }
IndexPipeline.cpp-1545- 
IndexPipeline.cpp-1571-         const auto markStaleIfChanged = [&] {
IndexPipeline.cpp-1572-             const auto waitStart = Clock::now();
IndexPipeline.cpp-1573-             std::unique_lock<std::mutex> lock(mutex_);
IndexPipeline.cpp-1574-             lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
IndexPipeline.cpp-1575-             const auto holdStart = Clock::now();
IndexPipeline.cpp:1576:             const bool changed = !isContextCurrentLocked(context, true);
IndexPipeline.cpp-1577-             const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
IndexPipeline.cpp-1578-             maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
IndexPipeline.cpp-1579-             lock.unlock();
IndexPipeline.cpp-1580-             if (changed) {
IndexPipeline.cpp-1581-                 stale = true;
IndexPipeline.cpp-1603-             {
IndexPipeline.cpp-1604-                 const auto waitStart = Clock::now();
IndexPipeline.cpp-1605-                 std::unique_lock<std::mutex> lock(mutex_);
IndexPipeline.cpp-1606-                 lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
IndexPipeline.cpp-1607-                 const auto holdStart = Clock::now();
IndexPipeline.cpp:1608:                 if (isContextCurrentLocked(context, true)) {
IndexPipeline.cpp-1609-                     materialized = MaterializeCandidateFolder(projection_, context.prefixes, candidate, isCancelled);
IndexPipeline.cpp-1610-                 } else {
IndexPipeline.cpp-1611-                     stale = true;
IndexPipeline.cpp-1612-                 }
IndexPipeline.cpp-1613-                 const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
IndexPipeline.cpp-1653-                 {
IndexPipeline.cpp-1654-                     const auto waitStart = Clock::now();
IndexPipeline.cpp-1655-                     std::unique_lock<std::mutex> lock(mutex_);
IndexPipeline.cpp-1656-                     lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
IndexPipeline.cpp-1657-                     const auto holdStart = Clock::now();
IndexPipeline.cpp:1658:                     if (!isContextCurrentLocked(context, true)) {
IndexPipeline.cpp-1659-                         stale = true;
IndexPipeline.cpp-1660-                     } else {
IndexPipeline.cpp-1661-                         const auto captureStart = Clock::now();
IndexPipeline.cpp-1662-                         while (rows.size() < kHotCandidateChunkRows) {
IndexPipeline.cpp-1663-                             const ffindexstore::ProjectionEntry* entry = nullptr;
IndexPipeline.cpp-1806-                 {
IndexPipeline.cpp-1807-                     const auto waitStart = Clock::now();
IndexPipeline.cpp-1808-                     std::unique_lock<std::mutex> lock(mutex_);
IndexPipeline.cpp-1809-                     lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
IndexPipeline.cpp-1810-                     const auto holdStart = Clock::now();
IndexPipeline.cpp:1811:                     if (!isContextCurrentLocked(context, true)) {
IndexPipeline.cpp-1812-                         stale = true;
IndexPipeline.cpp-1813-                     } else {
IndexPipeline.cpp-1814-                         while (chunk.size() < kHotCandidateChunkRows) {
IndexPipeline.cpp-1815-                             const ffindexstore::ProjectionEntry* entry = nullptr;
IndexPipeline.cpp-1816-                             if (context.filenameCandidates.has_value()) {
IndexPipeline.cpp-1920-         if (context.phaseHook) context.phaseHook(SearchPhase::GenerationValidation);
IndexPipeline.cpp-1921-         markStaleIfChanged();
IndexPipeline.cpp-1922-         if (stale) {
IndexPipeline.cpp-1923-             ++generationRestarts;
IndexPipeline.cpp-1924-             generationRestarts_.fetch_add(1, std::memory_order_relaxed);
IndexPipeline.cpp:1925:             if (restart == kMaxGenerationRestarts) {
IndexPipeline.cpp-1926-                 durableFallbackNeeded = true;
IndexPipeline.cpp-1927-                 break;
IndexPipeline.cpp-1928-             }
IndexPipeline.cpp-1929-             continue;
IndexPipeline.cpp-1930-         }
IndexPipeline.cpp-2061-             {
IndexPipeline.cpp-2062-                 const auto waitStart = Clock::now();
IndexPipeline.cpp-2063-                 std::unique_lock<std::mutex> lock(mutex_);
IndexPipeline.cpp-2064-                 lockWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - waitStart).count();
IndexPipeline.cpp-2065-                 const auto holdStart = Clock::now();
IndexPipeline.cpp:2066:                 configurationCurrent = isContextCurrentLocked(context, false);
IndexPipeline.cpp-2067-                 const double holdMs = std::chrono::duration<double, std::milli>(Clock::now() - holdStart).count();
IndexPipeline.cpp-2068-                 maxLockHoldMs = (std::max)(maxLockHoldMs, holdMs);
IndexPipeline.cpp-2069-                 lock.unlock();
IndexPipeline.cpp-2070-             }
IndexPipeline.cpp-2071-             if (!configurationCurrent) {
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore/i...
grep: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
FlatHashMap.h-27- 
FlatHashMap.h-28-     size_t size() const noexcept { return count_; }
FlatHashMap.h-29-     bool empty() const noexcept { return count_ == 0; }
FlatHashMap.h:30:     size_t bucket_count() const noexcept { return keys_.size(); }
FlatHashMap.h-31-     // performance-closure-and-multisurface-acceleration 8.1: the capacity
FlatHashMap.h-32-     // halves of MemoryUsageBytes, so a diagnostic can attribute the map's
FlatHashMap.h-33-     // allocated bytes per array instead of only as one opaque sum. These are
FlatHashMap.h-28-     size_t size() const noexcept { return count_; }
FlatHashMap.h-29-     bool empty() const noexcept { return count_ == 0; }
FlatHashMap.h-30-     size_t bucket_count() const noexcept { return keys_.size(); }
FlatHashMap.h:31:     // performance-closure-and-multisurface-acceleration 8.1: the capacity
FlatHashMap.h-32-     // halves of MemoryUsageBytes, so a diagnostic can attribute the map's
FlatHashMap.h-33-     // allocated bytes per array instead of only as one opaque sum. These are
FlatHashMap.h-34-     // live capacity() measurements, not object-size estimates.
FlatHashMap.h-29-     bool empty() const noexcept { return count_ == 0; }
FlatHashMap.h-30-     size_t bucket_count() const noexcept { return keys_.size(); }
FlatHashMap.h-31-     // performance-closure-and-multisurface-acceleration 8.1: the capacity
FlatHashMap.h:32:     // halves of MemoryUsageBytes, so a diagnostic can attribute the map's
FlatHashMap.h-33-     // allocated bytes per array instead of only as one opaque sum. These are
FlatHashMap.h-34-     // live capacity() measurements, not object-size estimates.
FlatHashMap.h-35-     size_t KeyCapacityBytes() const noexcept { return keys_.capacity() * sizeof(Key); }
FlatHashMap.h-31-     // performance-closure-and-multisurface-acceleration 8.1: the capacity
FlatHashMap.h-32-     // halves of MemoryUsageBytes, so a diagnostic can attribute the map's
FlatHashMap.h-33-     // allocated bytes per array instead of only as one opaque sum. These are
FlatHashMap.h:34:     // live capacity() measurements, not object-size estimates.
FlatHashMap.h-35-     size_t KeyCapacityBytes() const noexcept { return keys_.capacity() * sizeof(Key); }
FlatHashMap.h-36-     size_t ValueCapacityBytes() const noexcept { return values_.capacity() * sizeof(Value); }
FlatHashMap.h-37-     size_t StateCapacityBytes() const noexcept { return states_.capacity(); }
FlatHashMap.h-32-     // halves of MemoryUsageBytes, so a diagnostic can attribute the map's
FlatHashMap.h-33-     // allocated bytes per array instead of only as one opaque sum. These are
FlatHashMap.h-34-     // live capacity() measurements, not object-size estimates.
FlatHashMap.h:35:     size_t KeyCapacityBytes() const noexcept { return keys_.capacity() * sizeof(Key); }
FlatHashMap.h-36-     size_t ValueCapacityBytes() const noexcept { return values_.capacity() * sizeof(Value); }
FlatHashMap.h-37-     size_t StateCapacityBytes() const noexcept { return states_.capacity(); }
FlatHashMap.h-38-     size_t MemoryUsageBytes() const noexcept {
FlatHashMap.h-33-     // allocated bytes per array instead of only as one opaque sum. These are
FlatHashMap.h-34-     // live capacity() measurements, not object-size estimates.
FlatHashMap.h-35-     size_t KeyCapacityBytes() const noexcept { return keys_.capacity() * sizeof(Key); }
FlatHashMap.h:36:     size_t ValueCapacityBytes() const noexcept { return values_.capacity() * sizeof(Value); }
FlatHashMap.h-37-     size_t StateCapacityBytes() const noexcept { return states_.capacity(); }
FlatHashMap.h-38-     size_t MemoryUsageBytes() const noexcept {
FlatHashMap.h-39-         return KeyCapacityBytes() + ValueCapacityBytes() + StateCapacityBytes();
FlatHashMap.h-34-     // live capacity() measurements, not object-size estimates.
FlatHashMap.h-35-     size_t KeyCapacityBytes() const noexcept { return keys_.capacity() * sizeof(Key); }
FlatHashMap.h-36-     size_t ValueCapacityBytes() const noexcept { return values_.capacity() * sizeof(Value); }
FlatHashMap.h:37:     size_t StateCapacityBytes() const noexcept { return states_.capacity(); }
FlatHashMap.h-38-     size_t MemoryUsageBytes() const noexcept {
FlatHashMap.h-39-         return KeyCapacityBytes() + ValueCapacityBytes() + StateCapacityBytes();
FlatHashMap.h-40-     }
FlatHashMap.h-35-     size_t KeyCapacityBytes() const noexcept { return keys_.capacity() * sizeof(Key); }
FlatHashMap.h-36-     size_t ValueCapacityBytes() const noexcept { return values_.capacity() * sizeof(Value); }
FlatHashMap.h-37-     size_t StateCapacityBytes() const noexcept { return states_.capacity(); }
FlatHashMap.h:38:     size_t MemoryUsageBytes() const noexcept {
FlatHashMap.h-39-         return KeyCapacityBytes() + ValueCapacityBytes() + StateCapacityBytes();
FlatHashMap.h-40-     }
FlatHashMap.h-41- 
FlatHashMap.h-39-         return KeyCapacityBytes() + ValueCapacityBytes() + StateCapacityBytes();
FlatHashMap.h-40-     }
FlatHashMap.h-41- 
FlatHashMap.h:42:     void reserve(size_t n) {
FlatHashMap.h-43-         if (n > keys_.size()) {
FlatHashMap.h-44-             grow(pow2_ceil(n * 2));
FlatHashMap.h-45-         }
FlatHashMap.h-281-     size_t size() const noexcept { return keys_.size(); }
FlatHashMap.h-282-     bool empty() const noexcept { return keys_.empty(); }
FlatHashMap.h-283-     // performance-closure-and-multisurface-acceleration 8.1: element and
FlatHashMap.h:284:     // per-array capacity detail for the memory decomposition; the byte values
FlatHashMap.h-285-     // are capacity() measurements and sum to MemoryUsageBytes().
FlatHashMap.h-286-     size_t ElementCount() const noexcept { return count_; }
FlatHashMap.h-287-     // Length of the flat child-index array. This can exceed the number of live
FlatHashMap.h-282-     bool empty() const noexcept { return keys_.empty(); }
FlatHashMap.h-283-     // performance-closure-and-multisurface-acceleration 8.1: element and
FlatHashMap.h-284-     // per-array capacity detail for the memory decomposition; the byte values
FlatHashMap.h:285:     // are capacity() measurements and sum to MemoryUsageBytes().
FlatHashMap.h-286-     size_t ElementCount() const noexcept { return count_; }
FlatHashMap.h-287-     // Length of the flat child-index array. This can exceed the number of live
FlatHashMap.h-288-     // links after removals: a vacated element is swapped to its slice tail and
FlatHashMap.h-298-         }
FlatHashMap.h-299-         return result;
FlatHashMap.h-300-     }
FlatHashMap.h:301:     size_t KeyCapacityBytes() const noexcept { return keys_.capacity() * sizeof(EntryKey); }
FlatHashMap.h-302-     size_t ChildCapacityBytes() const noexcept { return values_.capacity() * sizeof(uint32_t); }
FlatHashMap.h-303-     size_t StateCapacityBytes() const noexcept { return states_.capacity(); }
FlatHashMap.h-304-     size_t OffsetCapacityBytes() const noexcept { return offsets_.capacity() * sizeof(size_t); }
FlatHashMap.h-299-         return result;
FlatHashMap.h-300-     }
FlatHashMap.h-301-     size_t KeyCapacityBytes() const noexcept { return keys_.capacity() * sizeof(EntryKey); }
FlatHashMap.h:302:     size_t ChildCapacityBytes() const noexcept { return values_.capacity() * sizeof(uint32_t); }
FlatHashMap.h-303-     size_t StateCapacityBytes() const noexcept { return states_.capacity(); }
FlatHashMap.h-304-     size_t OffsetCapacityBytes() const noexcept { return offsets_.capacity() * sizeof(size_t); }
FlatHashMap.h-305-     size_t CountCapacityBytes() const noexcept { return counts_.capacity() * sizeof(size_t); }
FlatHashMap.h-300-     }
FlatHashMap.h-301-     size_t KeyCapacityBytes() const noexcept { return keys_.capacity() * sizeof(EntryKey); }
FlatHashMap.h-302-     size_t ChildCapacityBytes() const noexcept { return values_.capacity() * sizeof(uint32_t); }
FlatHashMap.h:303:     size_t StateCapacityBytes() const noexcept { return states_.capacity(); }
FlatHashMap.h-304-     size_t OffsetCapacityBytes() const noexcept { return offsets_.capacity() * sizeof(size_t); }
FlatHashMap.h-305-     size_t CountCapacityBytes() const noexcept { return counts_.capacity() * sizeof(size_t); }
FlatHashMap.h-306-     size_t MemoryUsageBytes() const noexcept {
FlatHashMap.h-301-     size_t KeyCapacityBytes() const noexcept { return keys_.capacity() * sizeof(EntryKey); }
FlatHashMap.h-302-     size_t ChildCapacityBytes() const noexcept { return values_.capacity() * sizeof(uint32_t); }
FlatHashMap.h-303-     size_t StateCapacityBytes() const noexcept { return states_.capacity(); }
FlatHashMap.h:304:     size_t OffsetCapacityBytes() const noexcept { return offsets_.capacity() * sizeof(size_t); }
FlatHashMap.h-305-     size_t CountCapacityBytes() const noexcept { return counts_.capacity() * sizeof(size_t); }
FlatHashMap.h-306-     size_t MemoryUsageBytes() const noexcept {
FlatHashMap.h-307-         return KeyCapacityBytes() + ChildCapacityBytes() + StateCapacityBytes()
FlatHashMap.h-302-     size_t ChildCapacityBytes() const noexcept { return values_.capacity() * sizeof(uint32_t); }
FlatHashMap.h-303-     size_t StateCapacityBytes() const noexcept { return states_.capacity(); }
FlatHashMap.h-304-     size_t OffsetCapacityBytes() const noexcept { return offsets_.capacity() * sizeof(size_t); }
FlatHashMap.h:305:     size_t CountCapacityBytes() const noexcept { return counts_.capacity() * sizeof(size_t); }
FlatHashMap.h-306-     size_t MemoryUsageBytes() const noexcept {
FlatHashMap.h-307-         return KeyCapacityBytes() + ChildCapacityBytes() + StateCapacityBytes()
FlatHashMap.h-308-             + OffsetCapacityBytes() + CountCapacityBytes();
FlatHashMap.h-303-     size_t StateCapacityBytes() const noexcept { return states_.capacity(); }
FlatHashMap.h-304-     size_t OffsetCapacityBytes() const noexcept { return offsets_.capacity() * sizeof(size_t); }
FlatHashMap.h-305-     size_t CountCapacityBytes() const noexcept { return counts_.capacity() * sizeof(size_t); }
FlatHashMap.h:306:     size_t MemoryUsageBytes() const noexcept {
FlatHashMap.h-307-         return KeyCapacityBytes() + ChildCapacityBytes() + StateCapacityBytes()
FlatHashMap.h-308-             + OffsetCapacityBytes() + CountCapacityBytes();
FlatHashMap.h-309-     }
FlatHashMap.h-312-     // child-index array's append slack left by a bulk rebuild. The probe-table
FlatHashMap.h-313-     // arrays stay at their designed <=70% load factor, so only the payload
FlatHashMap.h-314-     // array is shrunk. Slices are addressed by the offsets stored per key, so
FlatHashMap.h:315:     // shrinking the array (indices, not pointers) preserves them.
FlatHashMap.h-316-     void ShrinkToFit() { values_.shrink_to_fit(); }
FlatHashMap.h-317- 
FlatHashMap.h-318-     std::span<const uint32_t> find(const EntryKey& key) const {
No matches found
grep: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
design.md-30- private ~461-477 MB, live requested 345.9/349.9 MB -- identical to before). The
design.md-31- memory-limit policy, LRU timestamps, eviction, and `EnforceMemoryLimit` watermark
design.md-32- semantics are untouched. Finding recorded for Phase 8: a 1M-row projection straddles
design.md:33: the default 500 MB budget (modeled ~414 MB vs the 400 MB low watermark in the
design.md-34- raw-projection harness), so rebuilds can deterministically end in eviction +
design.md-35- accelerator invalidation; `SetProjectionMemoryBudget` has no production caller. That
design.md-36- is a product memory-policy question, deliberately NOT bundled into this fix.
tasks.md-28- ## 5. Large-index search baseline
tasks.md-29- 
tasks.md-30- - [x] 5.1 Benchmark the 12 query classes (selective, moderate, broad, no_match, prefix, extension, worst_substring, exact_name, short_prefix, mixed_case, unicode, high_limit_broad) at 200k/1M/2M on the current tree with machine-readable output and idle gating. Evidence: `evidence/search/search-matrix-before.json` (in collection).
tasks.md:31: - [x] 5.2 Record per class: p50/p90/p95, rows examined, candidates matched/materialized, filename-index usage, wire bytes, result count, top-N admission; establish the scaling law (O(1)/O(log N)/O(K)/O(N)/worse) and identify the largest user-relevant cost at realistic scale. Recorded in `evidence/search/` + `evidence/explore-findings.md`: 200k->1M is O(N) scan; 1M->2M is a memory-budget regime change (all queries serve from the SQLite cold stream because the 839.7MB projection exceeds the 500MB ... [truncated]
tasks.md-32- 
tasks.md-33- ## 6. Planner / index candidate
tasks.md-34- 
evidence/adversarial-review-2026-10-21.md-13- | Lane | Finding | Severity | Remediation | Regression test |
evidence/adversarial-review-2026-10-21.md-14- |---|---|---|---|---|
evidence/adversarial-review-2026-10-21.md-15- | D P1-1 / C F1 | Memory-pressure enforcement evicts+compacts across volumes with no ProjectionResidency epoch bump; in-flight searches serve silently incomplete results as Completed and walk repacked layouts | P1 (results integrity) | `EnforceMemoryLimitNow` returns whether eviction ran; every enforcement/reload site bumps residency epochs via the existing restart machinery (scoped unrelated-mutation semantics preserved) | `TestEnforcementEvictionAdvancesQueryVisibleEpochs` |
evidence/adversarial-review-2026-10-21.md:16: | D P1-2 | Over-watermark reload streams the whole volume under the pipeline mutex (unbounded transient; at 2M ~839 MB) then re-evicts -> empty export, repeatedly | P1 (availability + memory bound) | fail-fast fit estimate vs the early-enforcement watermark before streaming; reload of an oversized volume returns false immediately and stays cold | `TestOversizedVolumeReloadFailsFastAndStaysCold` |
evidence/adversarial-review-2026-10-21.md-17- | D P2-2 | Allocation failure mid-reload-stream left a volume silently partial-but-"hot" | P2 | stream wrapped in catch-all -> existing failed-read cleanup (remove partials, re-mark cold) | covered by the reload test + existing failure-injection tests |
evidence/adversarial-review-2026-10-21.md-18- | C F2 | `ProjectionEntryCursor` holds raw map/array positions; growth/compaction scrambles them -> silent truncation | P2 | layout markers + `Stale()`; hot loops treat staleness as a stale generation (restart + re-capture), never "exhausted" | `TestCursorDetectsDisturbedLayout` |
evidence/adversarial-review-2026-10-21.md-19- | C F3 | Accelerator dead after every bulk scan (nothing rebuilt it) | P2 (silent perf loss) | `SetBulkScanActive(false)` rebuilds the accelerator once | `TestBulkScanCycleRevivesFilenameAccelerator` |
evidence/explore-findings.md-45-   unicode 122/584/11,849; high_limit_broad 127/560/28,669.
evidence/explore-findings.md-46-   - Scaling law: 200k->1M ≈ linear (x4-6 for x5 rows, all classes O(N) scan);
evidence/explore-findings.md-47-     1M->2M is a REGIME CHANGE, not algorithmic superlinearity: every 2M warm run
evidence/explore-findings.md:48:     shows `cold_rows_examined = 2,000,000` — the 2M projection (modeled 839.7MB,
evidence/explore-findings.md-49-     measured at forced eviction) exceeds the 500MB default budget, so all queries
evidence/explore-findings.md-50-     serve from the SQLite cold stream (2.6-37.6s).
evidence/explore-findings.md-51-   - `filename_index_used = false` for all 12 classes at every scale (structural —
evidence/memory-cold-decision.md-88- 
evidence/memory-cold-decision.md-89- ## 3. The 2M regime (task 8.2 finding, characterized)
evidence/memory-cold-decision.md-90- 
evidence/memory-cold-decision.md:91: The 2M-row projection models ~839.7 MB > the 500 MiB default, so every 2M
evidence/memory-cold-decision.md-92- query serves from the cold stream under the default budget (historical
evidence/memory-cold-decision.md-93- search-matrix p50 at 2M: selective ~2.5 s, broad ~27.1 s, extension ~37.6 s;
evidence/memory-cold-decision.md-94- the prefilter does not change non-literal classes and removes only the
evidence/memory-cold-decision.md-142-   latency is governed by the scan floor above. Lane E E-7: the 1M cold table
evidence/memory-cold-decision.md-143-   characterizes the production cold-stream cost model under a state that at
evidence/memory-cold-decision.md-144-   1M rows requires multi-volume memory pressure to reach (the 1M projection
evidence/memory-cold-decision.md:145:   models ~318 MiB, below the 400 MiB watermark); the common production cold
evidence/memory-cold-decision.md-146-   regime is >=2M rows.
evidence/memory-cold-decision.md-147- 
evidence/rebuild/memory-report-after-fix.json-18-   ,"machine":{"cpu":"AMD Ryzen 5 7535HS with Radeon Graphics        ","logical_cores":12,"ram_bytes":25028239360,"windows_version":"10.0.26200","compiler":"MSVC 1944.194435228","build_configuration":"Release","database_storage":"temporary directory (drive/model not collected)"}
evidence/rebuild/memory-report-after-fix.json-19-   ,"instrumentation":{"allocator_probe":"replaced global operator new/delete: live+cumulative requested and usable bytes, allocation counts, live peak; C++ allocations only (SQLite C allocations are outside the counter)","process_memory":"GetProcessMemoryInfo PROCESS_MEMORY_COUNTERS_EX (working set, private usage, peak working set, peak pagefile)","working_set_split":"QueryWorkingSet + QueryWorkingSetEx page classification (shareable vs private)","projection_categories":"Projection::GetMemoryBre... [truncated]
evidence/rebuild/memory-report-after-fix.json-20-   ,"limitations":["allocator counts cover C++ operator new/delete only; SQLite (compiled as C) allocates through malloc/free and appears in the private-bytes residual instead","standard-library node links, the probe's 32-byte allocation header, heap block headers and heap rounding are residual and are not attributed to any projection category","working-set shareable pages identify shareable/pagefile-backed pages; they are not proof that another process maps the same section","the export weighing... [truncated]
evidence/rebuild/memory-report-after-fix.json:21:   ,"scales":[{"scale":200000,"directory_count":3125,"file_count":196874,"bound_directories":4096,"rebuild_cycle_target":4,"database_bytes":15085568,"durable_volume_row_id":1,"generation_ms":2404.7960,"executed":true,"failure_reason":"","payload_identical":true,"rebuild_cycles":[{"name":"after_rebuild_cycle_1","elapsed_ms":823.5751,"working_set_bytes":108920832,"private_bytes":103403520,"peak_working_set_bytes":145297408,"peak_private_bytes":155893760,"allocator":{"live_requested_bytes":76024671,... [truncated]
evidence/rebuild/memory-report-after-fix.json-22- }
evidence/rebuild/memory-report-after-fix.json-23- 
evidence/search/search-matrix-after-rebuild-fix-control.json-69-     {"dataset_size":200000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":200000,"candidates_visited":16415,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dura... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-70-     {"dataset_size":200000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":200000,"candidates_visited":16415,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dura... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-71-     {"dataset_size":200000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":5,"result_count":4096,"candidate_count":200000,"candidates_visited":16415,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dura... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json:72:     {"dataset_size":200000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":6,"result_count":4096,"candidate_count":200000,"candidates_visited":16415,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dura... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-73-     {"dataset_size":200000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":7,"result_count":4096,"candidate_count":200000,"candidates_visited":16415,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dura... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-74-     {"dataset_size":200000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":8,"result_count":4096,"candidate_count":200000,"candidates_visited":16415,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dura... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-75-     {"dataset_size":200000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":9,"result_count":4096,"candidate_count":200000,"candidates_visited":16415,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dura... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-121-     {"dataset_size":200000,"query_class":"unicode","query":"список","temperature":"warm","iteration":5,"result_count":4096,"candidate_count":200000,"candidates_visited":13895,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-122-     {"dataset_size":200000,"query_class":"unicode","query":"список","temperature":"warm","iteration":6,"result_count":4096,"candidate_count":200000,"candidates_visited":13895,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-123-     {"dataset_size":200000,"query_class":"unicode","query":"список","temperature":"warm","iteration":7,"result_count":4096,"candidate_count":200000,"candidates_visited":13895,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json:124:     {"dataset_size":200000,"query_class":"unicode","query":"список","temperature":"warm","iteration":8,"result_count":4096,"candidate_count":200000,"candidates_visited":13895,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-125-     {"dataset_size":200000,"query_class":"unicode","query":"список","temperature":"warm","iteration":9,"result_count":4096,"candidate_count":200000,"candidates_visited":13895,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-126-     {"dataset_size":200000,"query_class":"high_limit_broad","query":"common","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-127-     {"dataset_size":200000,"query_class":"high_limit_broad","query":"common","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-174-     {"dataset_size":1000000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":1000000,"candidates_visited":46728,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-175-     {"dataset_size":1000000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":1000000,"candidates_visited":46728,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-176-     {"dataset_size":1000000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":1000000,"candidates_visited":46728,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json:177:     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-178-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-179-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-180-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-175-     {"dataset_size":1000000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":1000000,"candidates_visited":46728,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-176-     {"dataset_size":1000000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":1000000,"candidates_visited":46728,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-177-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json:178:     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-179-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-180-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-181-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-176-     {"dataset_size":1000000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":1000000,"candidates_visited":46728,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-177-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-178-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json:179:     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-180-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-181-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-182-     {"dataset_size":1000000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":20046,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3793,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-177-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-178-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-179-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json:180:     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-181-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-182-     {"dataset_size":1000000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":20046,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3793,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-183-     {"dataset_size":1000000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":1000000,"candidates_visited":20046,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3793,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-178-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-179-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-180-     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json:181:     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":1000000,"candidates_visited":22732,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-182-     {"dataset_size":1000000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":20046,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3793,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-183-     {"dataset_size":1000000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":1000000,"candidates_visited":20046,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3793,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-184-     {"dataset_size":1000000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":1000000,"candidates_visited":20046,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3793,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-214-     {"dataset_size":1000000,"query_class":"broad","query":"common","temperature":"cold","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":1000000,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durab... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-215-     {"dataset_size":1000000,"query_class":"no_match","query":"zzzz_never_present","temperature":"cold","iteration":0,"result_count":0,"candidate_count":1000000,"candidates_visited":1000000,"truncated":false,"cancelled":false,"unavailable":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-216-     {"dataset_size":1000000,"query_class":"prefix","query":"photo","temperature":"cold","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":1000000,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durab... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json:217:     {"dataset_size":1000000,"query_class":"extension","query":"ext:txt","temperature":"cold","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":1000000,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3937,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-218-     {"dataset_size":1000000,"query_class":"worst_substring","query":"component","temperature":"cold","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":1000000,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":3793,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attem... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-219-     {"dataset_size":1000000,"query_class":"exact_name","query":"unique_component_7391.txt","temperature":"cold","iteration":0,"result_count":1,"candidate_count":1000000,"candidates_visited":1000000,"truncated":false,"cancelled":false,"unavailable":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallbac... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json-220-     {"dataset_size":1000000,"query_class":"short_prefix","query":"co","temperature":"cold","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":1000000,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-before.json-58-     {"dataset_size":200000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":200000,"candidates_visited":13967,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable_f... [truncated]
evidence/search/search-matrix-before.json-59-     {"dataset_size":200000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":200000,"candidates_visited":13967,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable_f... [truncated]
evidence/search/search-matrix-before.json-60-     {"dataset_size":200000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":200000,"candidates_visited":13967,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable_f... [truncated]
evidence/search/search-matrix-before.json:61:     {"dataset_size":200000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":200000,"candidates_visited":13967,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable_f... [truncated]
evidence/search/search-matrix-before.json-62-     {"dataset_size":200000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":5,"result_count":4096,"candidate_count":200000,"candidates_visited":13967,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable_f... [truncated]
evidence/search/search-matrix-before.json-63-     {"dataset_size":200000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":6,"result_count":4096,"candidate_count":200000,"candidates_visited":13967,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable_f... [truncated]
evidence/search/search-matrix-before.json-64-     {"dataset_size":200000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":7,"result_count":4096,"candidate_count":200000,"candidates_visited":13967,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable_f... [truncated]
evidence/search/search-matrix-before.json-106-     {"dataset_size":200000,"query_class":"short_prefix","query":"co","temperature":"warm","iteration":9,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
evidence/search/search-matrix-before.json-107-     {"dataset_size":200000,"query_class":"mixed_case","query":"CoMmOn_CoMpOnEnT","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":200000,"candidates_visited":23948,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
evidence/search/search-matrix-before.json-108-     {"dataset_size":200000,"query_class":"mixed_case","query":"CoMmOn_CoMpOnEnT","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":200000,"candidates_visited":23948,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
evidence/search/search-matrix-before.json:109:     {"dataset_size":200000,"query_class":"mixed_case","query":"CoMmOn_CoMpOnEnT","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":200000,"candidates_visited":23948,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
evidence/search/search-matrix-before.json-110-     {"dataset_size":200000,"query_class":"mixed_case","query":"CoMmOn_CoMpOnEnT","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":200000,"candidates_visited":23948,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
evidence/search/search-matrix-before.json-111-     {"dataset_size":200000,"query_class":"mixed_case","query":"CoMmOn_CoMpOnEnT","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":200000,"candidates_visited":23948,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
evidence/search/search-matrix-before.json-112-     {"dataset_size":200000,"query_class":"mixed_case","query":"CoMmOn_CoMpOnEnT","temperature":"warm","iteration":5,"result_count":4096,"candidate_count":200000,"candidates_visited":23948,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
evidence/search/search-matrix-before.json-163-     {"dataset_size":1000000,"query_class":"broad","query":"common","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":52615,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-before.json-164-     {"dataset_size":1000000,"query_class":"broad","query":"common","temperature":"warm","iteration":1,"result_count":4096,"candidate_count":1000000,"candidates_visited":52615,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-before.json-165-     {"dataset_size":1000000,"query_class":"broad","query":"common","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":1000000,"candidates_visited":52615,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-before.json:166:     {"dataset_size":1000000,"query_class":"broad","query":"common","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":1000000,"candidates_visited":52615,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
evidence/search/search-matrix-before.json-167-     {"dataset_size":1000000,"query_class":"broad","query":"common","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":1000000,"candidates_visited":52615,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable... [truncated]
[20 matches limit reached. Use limit=40 for more, or refine pattern. 50.0KB limit reached. Some lines truncated to 500 chars. Use read tool to see full lines]
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
IndexPipeline.cpp-1361-         std::unordered_map<ffindexstore::VolumeRowId, ffprotocol::VolumeSetting> rules;
IndexPipeline.cpp-1362-         std::vector<SearchDependency> dependencies;
IndexPipeline.cpp-1363-         std::vector<ffindexstore::VolumeRowId> searchableVolumes;
IndexPipeline.cpp-1364-         std::unordered_set<ffindexstore::VolumeRowId> coldVolumes;
IndexPipeline.cpp:1365:         std::vector<ffindexstore::VolumeRowId> orderedCold;
IndexPipeline.cpp-1366-         std::unique_ptr<ffindexstore::StoreReadSession> reader;
IndexPipeline.cpp-1367-         ffindexstore::ProjectionEntryCursor cursor;
IndexPipeline.cpp-1368-         std::optional<std::vector<uint32_t>> filenameCandidates;
IndexPipeline.cpp-1369-         std::function<void(SearchPhase)> phaseHook;
IndexPipeline.cpp-1486-                     if (prepareProjection && projection_.NeedsReload(volumeId)) {
IndexPipeline.cpp-1487-                         context.coldVolumes.insert(volumeId);
IndexPipeline.cpp-1488-                     }
IndexPipeline.cpp-1489-                 }
IndexPipeline.cpp:1490:                 context.orderedCold.assign(context.coldVolumes.begin(), context.coldVolumes.end());
IndexPipeline.cpp-1491-                 std::sort(context.orderedCold.begin(), context.orderedCold.end());
IndexPipeline.cpp-1492-                 searchedColdVolumes = searchedColdVolumes || !context.orderedCold.empty();
IndexPipeline.cpp-1493-                 context.reader = store_.OpenReadSession({readSessionFaultInjector_});
IndexPipeline.cpp-1494-                 if (!context.reader) {
IndexPipeline.cpp-1487-                         context.coldVolumes.insert(volumeId);
IndexPipeline.cpp-1488-                     }
IndexPipeline.cpp-1489-                 }
IndexPipeline.cpp-1490-                 context.orderedCold.assign(context.coldVolumes.begin(), context.coldVolumes.end());
IndexPipeline.cpp:1491:                 std::sort(context.orderedCold.begin(), context.orderedCold.end());
IndexPipeline.cpp-1492-                 searchedColdVolumes = searchedColdVolumes || !context.orderedCold.empty();
IndexPipeline.cpp-1493-                 context.reader = store_.OpenReadSession({readSessionFaultInjector_});
IndexPipeline.cpp-1494-                 if (!context.reader) {
IndexPipeline.cpp-1495-                     readerFailures_.fetch_add(1, std::memory_order_relaxed);
IndexPipeline.cpp-1488-                     }
IndexPipeline.cpp-1489-                 }
IndexPipeline.cpp-1490-                 context.orderedCold.assign(context.coldVolumes.begin(), context.coldVolumes.end());
IndexPipeline.cpp-1491-                 std::sort(context.orderedCold.begin(), context.orderedCold.end());
IndexPipeline.cpp:1492:                 searchedColdVolumes = searchedColdVolumes || !context.orderedCold.empty();
IndexPipeline.cpp-1493-                 context.reader = store_.OpenReadSession({readSessionFaultInjector_});
IndexPipeline.cpp-1494-                 if (!context.reader) {
IndexPipeline.cpp-1495-                     readerFailures_.fetch_add(1, std::memory_order_relaxed);
IndexPipeline.cpp-1496-                     unavailable = true;
IndexPipeline.cpp-1584-         };
IndexPipeline.cpp-1585-         const auto materialize = [&](ffsearch::Candidate& candidate) {
IndexPipeline.cpp-1586-             if (isCancelled() || stale) return false;
IndexPipeline.cpp-1587-             const auto volumeId = static_cast<ffindexstore::VolumeRowId>(candidate.volumeId);
IndexPipeline.cpp:1588:             if (context.coldVolumes.contains(volumeId)) {
IndexPipeline.cpp-1589-                 const bool materialized = context.reader != nullptr && MaterializeCandidateFolderDurable(
IndexPipeline.cpp-1590-                     *context.reader, context.prefixes, candidate, isCancelled);
IndexPipeline.cpp-1591-                 if (!materialized && context.reader != nullptr && context.reader->Failed()) {
IndexPipeline.cpp-1592-                     streamFailed = true;
IndexPipeline.cpp-1679-                                 }
IndexPipeline.cpp-1680-                                 hotExhausted = true;
IndexPipeline.cpp-1681-                                 break;
IndexPipeline.cpp-1682-                             }
IndexPipeline.cpp:1683:                             if (entry == nullptr || context.coldVolumes.contains(entry->volumeRowId)) continue;
IndexPipeline.cpp-1684-                             const auto prefixIt = context.prefixes.find(entry->volumeRowId);
IndexPipeline.cpp-1685-                             if (prefixIt == context.prefixes.end()) continue;
IndexPipeline.cpp-1686-                             if (detailedDiagnostics) ++projectionEntriesVisited;
IndexPipeline.cpp-1687-                             ++projectionRowsExamined;
IndexPipeline.cpp-1831-                                 }
IndexPipeline.cpp-1832-                                 hotExhausted = true;
IndexPipeline.cpp-1833-                                 break;
IndexPipeline.cpp-1834-                             }
IndexPipeline.cpp:1835:                             if (entry == nullptr || context.coldVolumes.contains(entry->volumeRowId)) continue;
IndexPipeline.cpp-1836-                             const auto prefixIt = context.prefixes.find(entry->volumeRowId);
IndexPipeline.cpp-1837-                             if (prefixIt == context.prefixes.end()) continue;
IndexPipeline.cpp-1838-                             if (detailedDiagnostics) ++projectionEntriesVisited;
IndexPipeline.cpp-1839-                             ++projectionRowsExamined;
IndexPipeline.cpp-1873-             }
IndexPipeline.cpp-1874-         }
IndexPipeline.cpp-1875- 
IndexPipeline.cpp-1876-         if (!stale && !isCancelled() && !accumulator.Cancelled()) {
IndexPipeline.cpp:1877:             for (const auto coldVolumeId : context.orderedCold) {
IndexPipeline.cpp-1878-                 if (!coldHookCalled) {
IndexPipeline.cpp-1879-                     if (context.phaseHook) context.phaseHook(SearchPhase::ColdStreaming);
IndexPipeline.cpp-1880-                     coldHookCalled = true;
IndexPipeline.cpp-1881-                 }
## Review — Lane C: Hybrid hot/cold residency research (FastFiles)
Read-only investigation complete. Target: the UNDECIDED "Option C — hybrid hot/cold residency" of `openspec/changes/durable-cold-search-index-and-hybrid-residency` (`design.md` §2 row C). No files were touched.
---
### Correct — verified facts (with evidence)
**1. Memory model**
- `Projection::kDefaultMaximumMemoryBytes` = 500 MiB (`src/indexstore/include/ffindexstore/Projection.h:103`, default member at `:503`). `SetMaximumMemoryBytes(0)` restores the default and enforces the strict ceiling **synchronously**, even during a bulk scan (`src/indexstore/src/Projection.cpp:127-139`).
- Early-enforcement watermark = `max − max/5` = **400 MiB**, enforced at ingestion batch boundaries by `EnforceMemoryLimit` (`Projection.cpp:482-503`); `IsMemoryPressureHigh` uses the same 80% watermark (`Projection.cpp:~305`). `EnforceMemoryLimitNow()` (`Projection.cpp:142-144`) returns **whether an eviction pass ran**, with bounded multi-pass enforcement (`kMaxEnforcementPasses = 8`, no-progress guard, `Projection.cpp:488-502`).
- Eviction victims: `LeastRecentlyUsedEntry` prefers the oldest directory subtree, then oldest file, then root fallback (`Projection.cpp:~335-378`); LRU is one `uint64_t` per dense slot (`lastAccessTimes_`, `Projection.h:496-501`; layout revision 2, `Projection.h:~83`).
- **Whole-volume cold marking:** `EvictSubtree` removes only the LRU subtree but then `evictedVolumes_.insert(rootKey.volumeRowId)` — the **entire volume** is marked cold (`Projection.cpp:379-417`, insert at `:415`). `EvictVolume` removes all rows then cold-marks (`Projection.cpp:419-443`). `NeedsReload` = membership in `evictedVolumes_` (`Projection.cpp:316-318`). Residency is therefore a **per-volume boolean**, not per-row.
- `ReloadEvictedVolume` (`Projection.cpp:538-552`) → `RebuildVolumeFromStore` (`Projection.cpp:1142-1228`): remove+compact, clear cold marker, a **fail-fast fit estimate** against the low watermark using the projection's own measured average row bytes (oversized volume stays cold and returns false — the P1-2 remediation), stream `store.ForEachEntry` under the pipeline mutex, catch-all cleanup of partial streams (P2-2), accelerator rebuild unless bulk-scan-active, `ShrinkBulkRebuildCapacity`, trailing `EnforceMemoryLimit`, return `!NeedsReload`.
- Engine call sites: startup `RebuildAll` (`src/engine/src/IndexPipeline.cpp:572-578`) and the snapshot-export path (`IndexPipeline.cpp:1101-1111`). **Search never reloads**; cold volumes stream from SQLite (`context.coldVolumes` capture at `IndexPipeline.cpp:1486-1492`; hot scan skips cold-volume rows at `:1683`/`:1835`; cold stream at `:1877+`). Confirmed consistent with `evidence/memory-cold-decision.md` §1.
- Estimator/breakdown seam: `EstimatedMemoryBytes` (`Projection.cpp:~163-176`) and `GetMemoryBreakdown` (`Projection.h:110-215`, `Projection.cpp:~176-300`) — `modeledCategoryBytes == EstimatedMemoryBytes` is test-asserted. This is the correct instrumentation for any budget proof.
**2. Planner eligibility gates** (the map a compact layer must respect)
- Simple-literal classification: single `LiteralName` predicate, no folder requirement, non-empty term (`src/search/src/Query.cpp:748-759`); `simpleLiteralFolded` is populated **only for ASCII terms** (`Query.cpp:753-754`).
- Hot accelerator gate: `simpleLiteralName && simpleLiteralFolded.size() >= 3 && context.coldVolumes.empty()` (`IndexPipeline.cpp:1500-1501`) plus `FilenameCandidateIndices`'s own gate `filenameIndexValid_ && folded.size() >= 3 && IsAscii(folded)` (`Projection.cpp:949`) and candidate cap `kMaximumFilenameIndexCandidates = 16 * 1024` (`IndexPipeline.h:399`; cap checks at `Projection.cpp:971-1001`, nullopt = canonical scan).
- Cold prefilter gate: `ColdNamePrefilterNeedle` — single literal, folded ≥ 3, all-ASCII (`IndexPipeline.cpp:363-382`).
- Completeness contract: candidate sets are conservative supersets under `CompareStringOrdinal(..., TRUE)` host semantics incl. fold-exceptional + surrogate classes, merged via `filenameExceptionalEntries_` (`Projection.cpp:996-1010`; testing seam `SetTestingFoldExceptionalUnit`, `Projection.h:~262`). Any compact layer inherits this obligation verbatim (`specs/index-engine/spec.md`, "conservative superset under Windows ordinal folding").
**3. Residency epoch / staleness machinery**
- `QueryMutationReason::ProjectionResidency` (`IndexPipeline.h:383`) is the designated mechanism. `AdvanceResidencyEpochsForStructuralChangeLocked` (`IndexPipeline.cpp:423-440`) bumps **every** known volume's data epoch plus `dependencySetEpoch_` after enforcement eviction or reload; wired at every enforcement site (`IndexPipeline.cpp:788-789, 797-798, 928-929, 935-936`), `SetProjectionMemoryBudget` (`:1249-1274`), `UnloadVolumeFromProjection` (`:1286-1295`), reload (`:1102-1110`). Regression test `TestEnforcementEvictionAdvancesQueryVisibleEpochs` (`tests/engine/test_index_pipeline.cpp:2451`).
- In-flight search: context capture records per-volume data/configuration epochs + cold set + cursor + filename candidates (`IndexPipeline.cpp:1474-1520`); `isContextCurrentLocked` (`:1522-1538`) is checked per chunk (`:1576, 1608, 1658, 1811`) and at `GenerationValidation` (`:1920-1929`); stale ⇒ restart, bounded by `kMaxGenerationRestarts = 3` (`IndexPipeline.h:~398`), then durable fallback (`:1925-1927`).
- `ProjectionEntryCursor::Stale` layout-marker contract (`Projection.h:47-58`, `Projection.cpp:926-942`) — dense slot positions are invalidated by any `CompactStorage`/growth (adversarial review C F2, `evidence/adversarial-review-2026-10-21.md:18`).
**4. Fixture generator (bench harness)** — `tests/engine/bench_large_index_scalability.cpp`: `DirectoryName` (`:296-298`), `FileName` (`:300-315`), `BuildSyntheticDatabase` (`:557-629`, `directoryCount = scale/64` at `:603`, rows wired via `MakeMftRecord` `:281-294`). `tests/indexstore/bench_rebuild_isolation.cpp:26-27` mirrors this shape and adds a G0 no-trigram population (≤2-char names). The 12 query classes are pinned at `bench_large_index_scalability.cpp:2126-2138` and `tasks.md:30` of the predecessor change.
---
### Findings
**F1 (P1) — "Compact always-resident" is only affordable as fixed-size records; a folded-name-key layer alone blows a plausible separate budget at 5M.**
Bytes/entry arithmetic from the fixture name shapes (`bench_large_index_scalability.cpp:296-315`; digits `d` = decimal digits of the row index, avg ≈ 6.4 at 1M):
| shape | weight | units |
|---|---|---|
| `common_component_<d>.txt` | 20% | 21+d ≈ 27 |
| `report_<d>.txt` / `photo_<d>.jpg` / `archive_<d>.zip` / `project_<d>.cpp` / `notes_<d>.md` / `common_media_<d>.mp4` / `список_<d>.txt` / `backup_<d>.7z` | 10% each | 9+d … 17+d |
| `folder_<0..996>_branch_<d>` (dirs, 1.56%) | — | ≈ 23 |
| `unique_component_7391.txt` | 1 row | 25 |
Weighted mean ≈ **20 UTF-16 units/file**, ≈ 23/dir ⇒ corpus mean ≈ 20 units. Packed ASCII-folded keys are only possible for ASCII names; the 10% `список_` population (`pattern == 8`, `:314`) needs either sentinels or full UTF-16.
| candidate record | bytes/entry | bytes/1M | bytes/5M |
|---|---|---|---|
| (a) folded name key: 20 B key + 4 B rowid + 2 B len + 1 B flags (padded) | ~28 | ~28 MB | ~140 MB |
| (b) trigram signature: 4 B rowid + 8 B trigram Bloom + 1 B flags | 13 packed / 16 aligned | 13–16 MB | 65–80 MB |
| (c) minimal automaton (DAWG) + terminal rowid lists | ~8–12 *on this fixture* | 8–12 MB | 40–60 MB |
| exception side-set (fold-exceptional + surrogates only) | +4 B × rare | <1 MB | <1 MB |
Comparison: the current projection models **333–420 B/entry** total (measured: 1M ≈ 318 MiB, `evidence/memory-cold-decision.md:145`; raw harness ≈ 414 MB, predecessor `design.md:33`; 2M ≈ 839.7 MB ≈ 420 B/entry, `evidence/explore-findings.md:48`). Row storage alone is `sizeof(ProjectionEntry)` ≈ **104 B** (MSVC x64 layout of `Projection.h:22-44`: 8+16+16+4+4pad+8+8+1+7pad+8+8+8+4+1+1+pad), id map ≈ 58 B (FlatHashMap 2× sizing, `FlatHashMap.h:42-45`), LRU 8 B, plus child map/name pool/trigram postings. So a signature layer is ~7% of the projection's per-entry cost; a key layer ~25%+ and is corpus-length-proportional. **Smallest fix:** make the signature record (b) the default budgeted structure; treat (a) as the "verification without fetch" upgrade that requires its own measured budget.
**F2 (P1) — The hybrid layer cannot ride `Projection`'s residency model as-is.** `EvictSubtree` cold-marks a whole volume after removing one subtree (`Projection.cpp:415`), the search hot loop skips **all** rows of cold-marked volumes (`IndexPipeline.cpp:1683`), and one cold volume disables the filename accelerator for every query (`context.coldVolumes.empty()` gate, `:1500`). A per-row hot/cold hybrid must either (i) live outside `Projection` with its own per-segment residency and treat `evictedVolumes_` as "rows cold, layer rows still valid", or (ii) replace the boolean with per-row residency. Option (i) is the small corrective path and also fixes the "one cold volume kills acceleration" degradation.
**F3 (P1) — Row identity in the layer must not be dense slot indices.** `CompactStorage` repacks every slot (`Projection.cpp:445-480`), and the cursor-staleness class of bug is already proven (adversarial review C F2, `evidence/adversarial-review-2026-10-21.md:18`). A 4-byte slot index record (the cheapest key) becomes silently wrong after any enforcement pass. **Fix:** key on `(volumeRowId, FRN)` (adds 16 B/entry → signature layer ≈ 28–32 B/entry, still ≤ 160 MB at 5M) **or** store store-level rowids plus a validity generation checked against the residency epochs. The FRN variant keeps the strict bound independent of mutation history.
**F4 (P2) — Baseline measurement discrepancy must be closed before budgeting.** Three per-entry totals coexist in evidence (318 MiB @1M post-revision-2 vs 414 MB raw harness vs 439.85 MB @2M-derived). The campaign's own measurement plan (proposal task 1.1, acceptance dimension 6, lane E E-8 artifact-identity lesson) already requires re-baselining; the compact-layer budget arithmetic above should be re-derived from a fresh `GetMemoryBreakdown()` capture with `--source-sha` embedded.
**F5 (P2) — Fixture-derived sharing arithmetic is biased.** Fixture names are fixed stems + decimal digits + 3 fixed extensions (`bench_large_index_scalability.cpp:300-315`), so a DAWG's 8–12 B/entry is an optimistic floor; real NTFS corpora (GUIDs, hashes, photo names `IMG_20240101_123456.jpg`) have different entropy. Automaton arithmetic must be measured on a real name dump before selection.
**F6 (P2) — Fold-exceptional/surrogate superset obligation applies unchanged.** The signature's trigram set must be computed over ASCII-folded spans with non-ASCII sentinels **excluding** only trigrams that cannot align under any ASCII needle, and any name whose host fold can equate a non-ASCII unit with ASCII (or containing surrogates) must join the always-candidate exception set exactly as `filenameExceptionalEntries_` does (`Projection.cpp:996-1010`; P2-A red-first oracle `TestFilenameCandidatesAreConservativeSuperset` family). The fixture's 10% Cyrillic population is *not* fold-exceptional — it can take precise signatures — so the exception penalty is corpus-dependent and must be measured (proposal §2 item 2).
---
### Strict memory-bound design sketch (Option C, constrained)
1. **Separate budget constant** `kCompactCandidateLayerMaximumBytes` (suggested 64 MiB at GA; 128 MiB ceiling). Charged against nothing else; `kDefaultMaximumMemoryBytes` stays 500 MiB (`Projection.h:103`), satisfying the spec requirement "The durable index shall not raise the default memory budget" (`specs/index-engine/spec.md`). Documented envelope = 500 MiB + layer cap.
2. **Fixed-size records** (F3 key + 8 B signature + flags) in flat per-volume segments; hard bound = `entryCount × recordBytes` with a row-derived cap `N_max = cap / recordBytes` (64 MiB / 32 B ≈ 2M rows covered at FRN-keyed granularity; beyond that, keep whole **segments** and mark dropped segments `opaque`).
3. **Eviction semantics:** segment LRU, whole-segment drops only (no per-row compaction storms — mirrors `kPerEntryFilenameRemovalThreshold` batching philosophy, `Projection.cpp:120-125`). Dropping a segment is **never a correctness event**: queries treat opaque segments as "candidates unknown" and canonical-scan those volumes (the spec's fallback scenario). Layer eviction must **not** fire `ProjectionResidency` restarts unless candidate sets for captured queries would lose rows (they can't — fallback is a superset); layer **mutation** (insert/delete/rename) does bump epochs (below).
4. **Consistency with evicted rows:** the layer outlives projection residency by design — it is precisely what makes cold volumes searchable. Maintenance rides the commit-then-apply path (`ApplyMftBatchChanges`/`ApplyUsnBatchChanges`, `IndexPipeline.cpp:~700-940`), strictly post-commit like the projection (foundation `design.md` D2 ordering). Deletes/renames update or invalidate the affected segment in the same batch boundary; a stale positive is verified away, a stale negative is forbidden. Under bulk scans (`SetBulkScanActive`, `Projection.cpp:147-161`), buffer or invalidate-and-rebuild once at scan end (mirroring the accelerator revival fix, review-lane C Finding 3).
5. **Memory pressure:** under `IsMemoryPressureHigh`, shed layer segments first (cheapest to rebuild from Store) before projection rows; layer rebuild happens off the enforcement loop so `EnforceMemoryLimitNow`'s pass bound and epoch semantics (`Projection.h:218-224`) are untouched.
6. **Epochs:** every layer mutation visible to queries routes through `AdvanceResidencyEpochsForStructuralChangeLocked()` (`IndexPipeline.cpp:423-440`); search restarts remain bounded (`kMaxGenerationRestarts = 3`, `IndexPipeline.h:~398`) with durable fallback. Layer lookups return FRNs (stable), never slot indices (F3).
7. **Planner:** consult the layer only when its segment covers the candidate volume and the query passes the existing eligibility shape (single literal, folded ≥ 3 for the signature variant), with a candidate cap analogous to `kMaximumFilenameIndexCandidates` (`IndexPipeline.h:399`) and the same nullopt→scan contract as `FilenameCandidateIndices` (`Projection.h:~283`).
### Query-class coverage (12 classes vs eligibility gates)
Fixture queries at `bench_large_index_scalability.cpp:2126-2138`; gates per above.
| class | query | simple literal | folded ≥ 3 | ASCII | cap @1M+ | signature layer | folded-key layer |
|---|---|---|---|---|---|---|---|
| selective | `unique_component_7391` | ✔ | ✔ (21) | ✔ | passes | **YES** (best case) | **YES** |
| exact_name | `unique_component_7391.txt` | ✔ | ✔ (25) | ✔ | passes | **YES** | **YES** |
| no_match | `zzzz_never_present` | ✔ | ✔ (19) | ✔ | passes (0 hits) | **YES** (largest win) | **YES** |
| prefix | `photo` | ✔ | ✔ (5) | ✔ | ~10% rows → over cap | partial (cost-based) | **YES** (key scan) |
| moderate | `report` | ✔ | ✔ (6) | ✔ | 10% → over cap | partial | **YES** |
| mixed_case | `CoMmOn_CoMpOnEnT` | ✔ | ✔ (16) | ✔ | 20% → over cap | partial | **YES** |
| broad / high_limit_broad | `common` | ✔ | ✔ (6) | ✔ | 20-30% → over cap | NO (scan floor wins) | NO benefit |
| worst_substring | `component` | ✔ | ✔ (9) | ✔ | 30% → over cap | NO | NO benefit |
| short_prefix | `co` | ✔ | **✘ (2)** | ✔ | — | NO (1 trigram too weak) | YES via key scan (gate would need lifting to folded ≥ 1) |
| unicode | `список` | ✔ | **✘ (folded empty, non-ASCII)** | ✘ | — | NO | only with host-folded keys + P2-A exception proof |
| extension | `ext:txt` | **✘ (predicate)** | — | — | — | NO | only via a suffix-scan variant |
Conclusion: the signature layer answers exactly the **literal-eligible high-selectivity** classes (the proposal's "cold selective/exact/no-match … sub-200 ms" target) — 3 of 12 outright, ~3 more cost-based; it structurally cannot serve short_prefix/unicode/extension/broad-family. A host-folded packed-key layer widens coverage to all ASCII literals (incl. short_prefix) and, with the fold-exceptional superset machinery, potentially unicode; extension/glob/predicate classes stay canonical-scan regardless. This matches the spec's planner requirement ("Broad query keeps the canonical scan").
### Measurement plan (for the human campaign, tasks 1.1–1.3)
1. **Re-baseline** at 200k/1M/2M/5M with `ffengine_bench_large_index_scalability --scales ... --diagnostics` + memory report, embedding `--source-sha`/`--source-state` (lane E E-8); capture `GetMemoryBreakdown()` per scale → authoritative B/entry per category (closes F4). Existing hooks: `tests/indexstore/bench_projection_memory.cpp` (per-category decomposition), `bench_secondary_filename_index.cpp`, `bench_rebuild_isolation.cpp` (differential isolation pattern to copy for layer-maintenance attribution).
2. **Prototype** the signature and packed-key layers behind `FF_INDEXSTORE_TESTING`-style bench-only seams (never production, per tasks.md 1.2); record bytes/entry and bytes/1M on the bench fixture **and a real NTFS name dump** (F5).
3. **Candidate quality:** per class per scale — candidates returned, FP rate vs canonical, candidate-gen wall time vs the 1.05–1.10 µs/row scan floor (`evidence/memory-cold-decision.md` §2 corrections); verify the cap behavior vs `kMaximumFilenameIndexCandidates`.
4. **Completeness:** extend `TestFilenameCandidatesAreConservativeSuperset` + `TestForcedEvictionSearchEquivalence` to the layer; red-first cases via `SetTestingFoldExceptionalUnit` (`Projection.h:~262`) and surrogate names (F6).
5. **Strict-bound proof:** sustained query + ingestion load — layer bytes ≤ cap, projection `configuredMaximumBytes` still `kDefaultMaximumMemoryBytes`, private bytes non-monotonic across query cycles (spec scenario "Bounded caches under sustained query load").
6. **Concurrency/staleness:** force enforcement eviction + cold reload mid-search during layer use — assert `ProjectionResidency` epoch bumps (`TestEnforcementEvictionAdvancesQueryVisibleEpochs` extension), bounded `generation_restarts`, and result identity with a fresh capture; extend `TestRepeatedColdSearchesDoNotThrashMemory` to segment shedding.
7. **Ingestion overhead:** batch-apply wall-time delta with layer maintenance on/off (bench_rebuild_isolation-style arms); must not regress the certified 31–34x rebuild behavior.
---
- Correct: memory model, enforcement/eviction/reload semantics, epoch machinery, and planner gates verified against source (citations above); all consistent with the OpenSpec decision record.
- Fixed: nothing (read-only lane).
- Findings: F1–F6 above (3× P1 design-seam risks, 3× P2 measurement/obligation notes), each with source evidence and a smallest-fix.
- Merge verdict: **OK with notes** — research artifact only; the P1s (F1 budget shape, F2 residency seam, F3 row identity) must be resolved in the campaign's design delta before any implementation (tasks group 3) begins.
