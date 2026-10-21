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

private:
    friend class Projection;
    using EntryMap = FlatHashMap<EntryKey, uint32_t, EntryKeyHash>;
    explicit ProjectionEntryCursor(const Projection& projection);

    const Projection* projection_ = nullptr;
    bool dense_ = false;
    size_t denseIndex_ = 0;
    EntryMap::Iterator iterator_;
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
    void EnforceMemoryLimitNow();
    // Initial/reconciliation scans are bulk mutations. Disable the expensive
    // eviction/compaction path and the optional filename accelerator while a
    // scan is active; canonical matching remains correct and the durable
    // store is authoritative.
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
    // identical to per-entry Remove in both modes.
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
    // large-index-lifecycle-and-search-scalability review-remediation (P2-A):
    // testing-only seam that force-marks a UTF-16 unit as fold-exceptional
    // (simulates a host ordinal table that equates it with ASCII, e.g. U+017F
    // LONG S vs 's'), so the candidate-completeness machinery for the
    // exceptional-name fallback is deterministically testable on hosts whose
    // real table has no cross-ASCII fold classes. Never set in production.
    static void SetTestingFoldExceptionalUnit(char16_t unit, bool exceptional);
#endif

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
    void EnforceMemoryLimit();
    void EnforceMemoryLimitTo(size_t targetBytes);
    void EnforceMemoryLimitOnceTo(size_t targetBytes);
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

