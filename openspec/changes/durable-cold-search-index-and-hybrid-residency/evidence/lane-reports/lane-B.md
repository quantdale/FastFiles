Task: READ-ONLY research lane B (FastFiles repo; work ONLY read-only, no shell, no edits) — Durable trigram postings. Repo: C:/Users/Michael Roy/Documents/FastFiles. Investigate the in-memory filename accelerator (src/indexstore/src/Projection.cpp, include/ffindexstore/Projection.h — FilenameCandidateIndices, folded trigram postings, fold-exceptional + surrogate sets, kMaximumFilenameIndexCandidates) and the durable store (src/indexstore/src/Store.cpp). Report: (1) exact structure of the in-memory postings (folding function FoldAscii, trigram encoding, sorted vectors? maps?), sizes implied by the 200k/1M/2M fixtures (10% non-ASCII names); (2) a durable schema design for persisting postings (table shapes, row identity — FRN? volume+rowid?, consistency markers) that does NOT reintroduce the predecessor's quadratic per-entry posting-erasure problem during bulk removal (see RemoveBatch/kPerEntryFilenameRemovalThreshold and the evidence docs openspec/changes/large-index-lifecycle-and-search-scalability/evidence/rebuild/); (3) incremental update cost per USN batch; deletion cost; posting-intersection read path at query time; disk footprint estimate bytes/1M rows (derive from fixture name lengths — the fixture generator is in the bench harness, find it); (4) Unicode-exception handling: how the fold-exceptional + surrogate sets would be persisted and kept host-consistent. Deliver: findings with file:line evidence, an estimated bytes/1M-rows footprint with arithmetic, and the measurement plan a human should run.
read: src/indexstore/include/ffindexstore/Projection.h
find: **/bench*
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
docs/benchmarks/
docs/benchmarks/indexed-search-allocated-size.md
docs/benchmarks/large-index-scalability-secondary-index.json
docs/benchmarks/large-index-scalability-sql-no-count.json
docs/benchmarks/large-index-scalability.json
docs/benchmarks/large-index-scalability.md
docs/benchmarks/phase8-bench-run-2m.log
docs/benchmarks/phase8-bench-run.log
docs/benchmarks/phase8-large-index-scalability-2m.json
docs/benchmarks/phase8-large-index-scalability.json
docs/benchmarks/phase8-large-index-scalability.md
docs/benchmarks/secondary-filename-index.json
docs/benchmarks/secondary-filename-index.md
openspec/changes/indexed-search-zero-copy-hot-path-and-lock-pressure-convergence/evidence/benchmark-and-validation-plan.md
tests/engine/bench_allocation_isolation.cpp
tests/engine/bench_allocator_probe.h
tests/engine/bench_first_generation_publish.cpp
tests/engine/bench_large_index_scalability.cpp
tests/engine/bench_secondary_filename_index.cpp
tests/indexstore/bench_projection_memory.cpp
tests/indexstore/bench_rebuild_isolation.cpp
tests/search/bench_literal_matcher.cpp
grep: src/indexstore
grep: src
src/Projection.cpp:28: std::u16string FoldAscii(std::u16string_view name) {
src/Projection.cpp:94: uint64_t PackTrigram(std::u16string_view value, size_t offset) {
src/Projection.cpp:100: std::vector<uint64_t> UniqueTrigrams(std::u16string_view value) {
src/Projection.cpp:105:         result.push_back(PackTrigram(value, index));
src/Projection.cpp:127: constexpr size_t kPerEntryFilenameRemovalThreshold = 256;
src/Projection.cpp:182:     for (const auto& [trigram, posting] : filenameTrigramPostings_) {
src/Projection.cpp:187:     result += filenameTrigramPostings_.bucket_count() * sizeof(void*);
src/Projection.cpp:257:     result.filenameIndexDistinctTrigrams = filenameTrigramPostings_.size();
src/Projection.cpp:258:     for (const auto& [trigram, posting] : filenameTrigramPostings_) {
src/Projection.cpp:266:         result.filenameIndexDistinctTrigrams * sizeof(std::vector<uint32_t>);
src/Projection.cpp:267:     result.filenameIndexBucketCount = filenameTrigramPostings_.bucket_count();
src/Projection.cpp:268:     result.filenameIndexBucketBytes = filenameTrigramPostings_.bucket_count() * sizeof(void*);
src/Projection.cpp:270:         result.filenameIndexDistinctTrigrams * sizeof(uint64_t)
src/Projection.cpp:409:     if (removalOrder.size() > kPerEntryFilenameRemovalThreshold) {
src/Projection.cpp:434:     if (keys.size() > kPerEntryFilenameRemovalThreshold) {
src/Projection.cpp:605:     filenameTrigramPostings_.clear();
src/Projection.cpp:617:         const std::u16string folded = FoldAscii(name);
src/Projection.cpp:618:         for (const uint64_t trigram : UniqueTrigrams(folded)) {
src/Projection.cpp:619:             auto& posting = filenameTrigramPostings_[trigram];
src/Projection.cpp:634:         for (const uint64_t trigram : UniqueTrigrams(FoldAscii(name))) {
src/Projection.cpp:635:             const auto it = filenameTrigramPostings_.find(trigram);
src/Projection.cpp:636:             if (it == filenameTrigramPostings_.end()) continue;
src/Projection.cpp:638:             if (it->second.empty()) filenameTrigramPostings_.erase(it);
src/Projection.cpp:646:     filenameTrigramPostings_.clear();
src/Projection.cpp:652:         for (auto& [trigram, posting] : filenameTrigramPostings_) {
src/Projection.cpp:884:     const bool bulk = frns.size() > kPerEntryFilenameRemovalThreshold;
src/Projection.cpp:954:         const std::vector<uint64_t> trigrams = UniqueTrigrams(foldedNeedle);
src/Projection.cpp:957:         bool anyTrigramMissing = false;
src/Projection.cpp:959:             const auto it = filenameTrigramPostings_.find(trigram);
src/Projection.cpp:960:             if (it == filenameTrigramPostings_.end()) {
src/Projection.cpp:967:                 anyTrigramMissing = true;
src/Projection.cpp:972:         if (!anyTrigramMissing && !postings.empty()) {
src/Projection.cpp:1244:     for (const auto& [trigram, posting] : filenameTrigramPostings_) {
src/Projection.cpp:1253:     for (auto& [trigram, posting] : filenameTrigramPostings_) {
include/ffindexstore/Projection.h:181:         size_t filenameIndexDistinctTrigrams = 0;
include/ffindexstore/Projection.h:488:     std::unordered_map<uint64_t, std::vector<uint32_t>> filenameTrigramPostings_;
search/src/Query.cpp:48: std::u16string FoldAscii16(std::wstring_view value) {
search/src/Query.cpp:117: constexpr char16_t FoldAscii16Char(char16_t value) {
search/src/Query.cpp:123:         if (FoldAscii16Char(haystack[start + index]) != folded[index]) return false;
search/src/Query.cpp:136:         if (FoldAscii16Char(haystack[position]) != anchor) continue;
search/src/Query.cpp:152:         if (FoldAscii16Char(rawTail) == needle.back() &&
search/src/Query.cpp:164: constexpr wchar_t FoldAsciiWideChar(wchar_t value) {
search/src/Query.cpp:170:         if (static_cast<char16_t>(FoldAsciiWideChar(haystack[start + index])) != folded[index]) return false;
search/src/Query.cpp:178:         if (static_cast<char16_t>(FoldAsciiWideChar(haystack[position])) != needle.anchor) continue;
search/src/Query.cpp:192:         if (static_cast<char16_t>(FoldAsciiWideChar(rawTail)) == folded.back() &&
search/src/Query.cpp:451:         const auto foldedText = IsAscii(text) ? FoldAscii16(text) : std::u16string{};
search/src/Query.cpp:647:     needle.folded = FoldAscii16(value);
search/src/Query.cpp:667:         if (static_cast<char16_t>(FoldAsciiWideChar(left[index])) !=
search/src/Query.cpp:668:             FoldAscii16Char(static_cast<char16_t>(right[index]))) {
search/src/Query.cpp:735:             const auto foldedToken = IsAscii(token) ? FoldAscii16(token) : std::u16string{};
search/src/Query.cpp:754:             ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
search/src/Query.cpp:775:             if (FoldAscii16Char(name[index]) != static_cast<char16_t>(FoldAsciiWideChar(term[index]))) {
search/src/Query.cpp:788:             if (FoldAscii16Char(name[index]) != static_cast<char16_t>(FoldAsciiWideChar(term[index]))) {
search/src/Query.cpp:804:                 if (FoldAscii16Char(name[start + index])
search/src/Query.cpp:805:                     != static_cast<char16_t>(FoldAsciiWideChar(term[index]))) {
indexstore/src/Projection.cpp:28: std::u16string FoldAscii(std::u16string_view name) {
indexstore/src/Projection.cpp:127: constexpr size_t kPerEntryFilenameRemovalThreshold = 256;
indexstore/src/Projection.cpp:409:     if (removalOrder.size() > kPerEntryFilenameRemovalThreshold) {
indexstore/src/Projection.cpp:434:     if (keys.size() > kPerEntryFilenameRemovalThreshold) {
indexstore/src/Projection.cpp:617:         const std::u16string folded = FoldAscii(name);
indexstore/src/Projection.cpp:634:         for (const uint64_t trigram : UniqueTrigrams(FoldAscii(name))) {
indexstore/src/Projection.cpp:884:     const bool bulk = frns.size() > kPerEntryFilenameRemovalThreshold;
engine/src/IndexPipeline.h:399:     static constexpr size_t kMaximumFilenameIndexCandidates = 16 * 1024;
engine/src/IndexPipeline.cpp:1504:                                 searchRequest.query.simpleLiteralFolded, kMaximumFilenameIndexCandidates);
read: src/indexstore/src/Projection.cpp
#include "ffindexstore/Projection.h"
#include <algorithm>
#include <atomic>
#include <iterator>
#include <limits>
#include <windows.h>
#include "ffindexstore/Store.h"
namespace ffindexstore {
namespace {
constexpr uint32_t kFileAttributeDirectory = 0x00000010;
constexpr size_t kMaxProjectionPathDepth = 4096;
bool IsAsciiLower(std::u16string_view name) {
    for (const char16_t ch : name) {
        if (ch > 0x7f || (ch >= u'A' && ch <= u'Z')) return false;
    }
    return true;
}
bool IsAscii(std::u16string_view name) {
    return std::all_of(name.begin(), name.end(), [](const char16_t ch) { return ch <= 0x7f; });
}
std::u16string FoldAscii(std::u16string_view name) {
    std::u16string folded;
    folded.reserve(name.size());
    for (const char16_t ch : name) {
        folded.push_back(ch >= u'A' && ch <= u'Z' ? static_cast<char16_t>(ch + (u'a' - u'A')) : ch);
    }
    return folded;
}
// large-index-lifecycle-and-search-scalability review-remediation (P2-A):
// the canonical matcher equates two UTF-16 units case-insensitively through
// the HOST's ordinal table (CompareStringOrdinal(..., TRUE)), which the
// ASCII-folded trigram accelerator must never contradict. The accelerator is
// a conservative superset of canonical matches exactly when every name that
// could match an ASCII needle THROUGH a non-ASCII code unit is retained
// separately from the posting intersection. That exceptional set is derived
// from the actual host comparison function -- never from Unicode casefold
// tables or assumed equivalences: a unit is fold-exceptional iff the host
// equates it with any ASCII unit. Surrogate units (astral pairs and unpaired
// surrogates) are always treated as exceptional, because pair semantics
// cannot be probed unit-wise and false negatives are forbidden.
// Measured on the campaign host: NO BMP unit is fold-exceptional (U+017F
// LONG S is NOT equal to 's'/'S', U+212A KELVIN is not 'k'/'K', U+0131 is
// not 'i'/'I', and the exhaustive 65,280 x 128 scan found zero cross-ASCII
// fold classes), so ordinary corpora pay nothing for this guarantee and a
// host whose table ever changed is handled automatically at run time.
enum class FoldProbe : uint8_t { Unknown = 0, Ordinary = 1, Exceptional = 2 };
std::atomic<uint8_t> gFoldProbe[65536]; // FoldProbe per UTF-16 unit; idempotent lazy fill
#ifdef FF_INDEXSTORE_TESTING
// Review-remediation P2-A testing seam: force-mark units fold-exceptional to
// simulate a host ordinal table that equates them with ASCII. Never set in
// production code (see header seam).
std::atomic<uint8_t> gTestingForceFoldExceptional[65536];
#endif
bool UnitMayFoldToAscii(const char16_t unit) {
    const uint16_t raw = static_cast<uint16_t>(unit);
    if (raw <= 0x7f) return false;
    if (raw >= 0xd800 && raw <= 0xdfff) return true; // conservative: surrogates
#ifdef FF_INDEXSTORE_TESTING
    if (gTestingForceFoldExceptional[raw].load(std::memory_order_relaxed) != 0) return true;
#endif
    uint8_t state = gFoldProbe[raw].load(std::memory_order_acquire);
    if (state == static_cast<uint8_t>(FoldProbe::Unknown)) {
        bool exceptional = false;
        const wchar_t wide = static_cast<wchar_t>(unit);
        for (int ascii = 0; ascii < 0x80 && !exceptional; ++ascii) {
            const wchar_t other = static_cast<wchar_t>(ascii);
            const int verdict = CompareStringOrdinal(&wide, 1, &other, 1, TRUE);
            // A failed comparison is classified exceptional: fail closed, so
            // a transient API failure can only cost candidates, never a miss.
            exceptional = verdict == CSTR_EQUAL || verdict == 0;
        }
        state = static_cast<uint8_t>(exceptional ? FoldProbe::Exceptional : FoldProbe::Ordinary);
        gFoldProbe[raw].store(state, std::memory_order_release);
    }
    return state == static_cast<uint8_t>(FoldProbe::Exceptional);
}
bool NameNeedsExceptionalFallback(const std::u16string_view name) {
    for (const char16_t ch : name) {
        if (static_cast<uint16_t>(ch) > 0x7f && UnitMayFoldToAscii(ch)) return true;
    }
    return false;
}
uint64_t PackTrigram(std::u16string_view value, size_t offset) {
    return static_cast<uint64_t>(value[offset]) |
           (static_cast<uint64_t>(value[offset + 1]) << 16) |
           (static_cast<uint64_t>(value[offset + 2]) << 32);
}
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
[960 more lines in file. Use offset=301 to continue.]
read: src/indexstore/src/Projection.cpp
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
[710 more lines in file. Use offset=551 to continue.]
read: src/indexstore/src/Projection.cpp
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
    denseIterationOrderValid_ = true;
}
void Projection::InvalidateFilenameIndex() noexcept {
    filenameIndexValid_ = false;
    filenameTrigramPostings_.clear();
    filenameExceptionalEntries_.clear();
}
void Projection::AddFilenameIndexEntry(const uint32_t index) {
    if (!filenameIndexValid_) return;
    try {
        const std::u16string_view name = namePool_.Get(entries_[index].nameId);
        if (NameNeedsExceptionalFallback(name)) {
            if (filenameIndexBuilding_) filenameExceptionalEntries_.push_back(index);
            else InsertSorted(filenameExceptionalEntries_, index);
        }
        const std::u16string folded = FoldAscii(name);
        for (const uint64_t trigram : UniqueTrigrams(folded)) {
            auto& posting = filenameTrigramPostings_[trigram];
            if (filenameIndexBuilding_) posting.push_back(index);
            else InsertSorted(posting, index);
        }
    } catch (...) {
        InvalidateFilenameIndex();
    }
}
void Projection::RemoveFilenameIndexEntry(const uint32_t index, const std::u16string_view name) {
    if (!filenameIndexValid_) return;
    try {
        // Must use the exact predicate AddFilenameIndexEntry used, or stale
        // exceptional entries would linger (or real ones get dropped).
        if (NameNeedsExceptionalFallback(name)) EraseSorted(filenameExceptionalEntries_, index);
        for (const uint64_t trigram : UniqueTrigrams(FoldAscii(name))) {
            const auto it = filenameTrigramPostings_.find(trigram);
            if (it == filenameTrigramPostings_.end()) continue;
            EraseSorted(it->second, index);
            if (it->second.empty()) filenameTrigramPostings_.erase(it);
        }
    } catch (...) {
        InvalidateFilenameIndex();
    }
}
void Projection::RebuildFilenameIndex() {
    filenameTrigramPostings_.clear();
    filenameExceptionalEntries_.clear();
    filenameIndexValid_ = true;
    filenameIndexBuilding_ = true;
    try {
        idToIndex_.ForEachValue([&](const uint32_t index) { AddFilenameIndexEntry(index); });
        for (auto& [trigram, posting] : filenameTrigramPostings_) {
            (void)trigram;
            std::sort(posting.begin(), posting.end());
            posting.erase(std::unique(posting.begin(), posting.end()), posting.end());
        }
        std::sort(filenameExceptionalEntries_.begin(), filenameExceptionalEntries_.end());
        filenameExceptionalEntries_.erase(
            std::unique(filenameExceptionalEntries_.begin(), filenameExceptionalEntries_.end()),
            filenameExceptionalEntries_.end());
    } catch (...) {
        InvalidateFilenameIndex();
    }
    filenameIndexBuilding_ = false;
}
void Projection::RemoveFromChildrenList(const EntryKey& parentKey, uint32_t index) {
    parentToChildren_.remove(parentKey, index);
}
void Projection::Upsert(VolumeRowId volumeRowId, const EntryRecord& record) {
    const EntryKey key{volumeRowId, record.id};
    const uint32_t* pIndex = idToIndex_.find(key);
    if (pIndex != nullptr) {
        const uint32_t index = *pIndex;
        ProjectionEntry& entry = entries_[index];
        std::u16string oldName;
        if (filenameIndexValid_) {
            try {
                oldName = std::u16string(namePool_.Get(entry.nameId));
            } catch (...) {
                InvalidateFilenameIndex();
            }
        }
        const NameId nameId = namePool_.Intern(record.name);
        if (filenameIndexValid_) RemoveFilenameIndexEntry(index, oldName);
        if (entry.parentFrn != record.parentId) {
            RemoveFromChildrenList(EntryKey{volumeRowId, entry.parentFrn}, index);
            // A self-referential parent (record.parentId == record.id) is
            // the volume-root sentinel (NTFS record 5's parent is itself),
            // not a real containment relationship -- it must not make the
            // root list itself as its own child in a directory listing.
            if (record.parentId != record.id) {
                parentToChildren_.insert(EntryKey{volumeRowId, record.parentId}, index);
            }
        }
        entry.parentFrn = record.parentId;
        entry.nameId = nameId;
        entry.sizeBytes = record.sizeBytes;
        entry.allocatedSizeBytes = record.allocatedSizeBytes;
        entry.allocatedSizeKnown = record.allocatedSizeKnown;
        entry.creationTime = record.creationTime;
        entry.lastModifiedTime = record.lastModifiedTime;
        entry.lastAccessTime = record.lastAccessTime;
        entry.attributes = record.attributes;
        entry.nameAsciiLower = IsAsciiLower(record.name);
        entry.nameIsAscii = IsAscii(record.name);
        TouchIndex(index);
        AddFilenameIndexEntry(index);
        return;
    }
    const NameId nameId = namePool_.Intern(record.name);
    denseIterationOrderValid_ = false;
    ProjectionEntry entry;
    entry.volumeRowId = volumeRowId;
    entry.frn = record.id;
    entry.parentFrn = record.parentId;
    entry.nameId = nameId;
    entry.sizeBytes = record.sizeBytes;
    entry.allocatedSizeBytes = record.allocatedSizeBytes;
    entry.allocatedSizeKnown = record.allocatedSizeKnown;
    entry.creationTime = record.creationTime;
    entry.lastModifiedTime = record.lastModifiedTime;
    entry.lastAccessTime = record.lastAccessTime;
    entry.attributes = record.attributes;
    entry.nameAsciiLower = IsAsciiLower(record.name);
    entry.nameIsAscii = IsAscii(record.name);
    uint32_t index;
    if (!freeList_.empty()) {
        index = freeList_.back();
        freeList_.pop_back();
        entries_[index] = entry;
    } else {
        index = static_cast<uint32_t>(entries_.size());
        entries_.push_back(entry);
    }
    if (index >= lastAccessTimes_.size()) {
        lastAccessTimes_.resize(static_cast<size_t>(index) + 1, 0);
    }
    lastAccessTimes_[index] = 0;
    idToIndex_.insert(key, index);
    if (record.parentId != record.id) {
        parentToChildren_.insert(EntryKey{volumeRowId, record.parentId}, index);
    }
    TouchIndex(index);
    AddFilenameIndexEntry(index);
}
void Projection::UpsertPartial(VolumeRowId volumeRowId, const EntryRecord& record,
                               const EntryFieldMask fields) {
    if (fields == kEntryFieldMaskAll) {
        Upsert(volumeRowId, record);
        return;
    }
    EntryRecord merged = record;
    const ProjectionEntry* existing = Find(volumeRowId, record.id);
    if (existing != nullptr) {
        merged.parentId = existing->parentFrn;
        merged.name = std::u16string(namePool_.Get(existing->nameId));
        merged.sizeBytes = existing->sizeBytes;
        merged.allocatedSizeBytes = existing->allocatedSizeBytes;
        merged.allocatedSizeKnown = existing->allocatedSizeKnown;
        merged.creationTime = existing->creationTime;
        merged.lastModifiedTime = existing->lastModifiedTime;
        merged.lastAccessTime = existing->lastAccessTime;
        merged.attributes = existing->attributes;
    }
    if (HasEntryField(fields, kEntryFieldParentId)) merged.parentId = record.parentId;
    if (HasEntryField(fields, kEntryFieldName)) merged.name = record.name;
    if (HasEntryField(fields, kEntryFieldSizeBytes)) merged.sizeBytes = record.sizeBytes;
    if (HasEntryField(fields, kEntryFieldAllocatedSizeBytes)) {
        // allocated-size-storage-accuracy D5: authoritative only when the
        // bit is present; absence preserves the previous value (and known
        // state) above -- a partial update never erases a known allocated
        // size with a fabricated zero.
        merged.allocatedSizeBytes = record.allocatedSizeBytes;
        merged.allocatedSizeKnown = record.allocatedSizeKnown;
    }
    if (HasEntryField(fields, kEntryFieldCreationTime)) merged.creationTime = record.creationTime;
    if (HasEntryField(fields, kEntryFieldLastModifiedTime)) merged.lastModifiedTime = record.lastModifiedTime;
    if (HasEntryField(fields, kEntryFieldLastAccessTime)) merged.lastAccessTime = record.lastAccessTime;
    if (HasEntryField(fields, kEntryFieldAttributes)) merged.attributes = record.attributes;
    if (!HasEntryField(fields, kEntryFieldParentId) && existing == nullptr) {
        merged.parentId = FileId{};
    }
    if (!HasEntryField(fields, kEntryFieldName) && existing == nullptr) {
        merged.name.clear();
    }
    if (!HasEntryField(fields, kEntryFieldSizeBytes) && existing == nullptr) {
        merged.sizeBytes = 0;
    }
    if (!HasEntryField(fields, kEntryFieldAllocatedSizeBytes) && existing == nullptr) {
        merged.allocatedSizeBytes = 0;
        merged.allocatedSizeKnown = false; // unknown, not zero -- zero is a valid size
    }
    if (!HasEntryField(fields, kEntryFieldCreationTime) && existing == nullptr) {
        merged.creationTime = 0;
    }
    if (!HasEntryField(fields, kEntryFieldLastModifiedTime) && existing == nullptr) {
        merged.lastModifiedTime = 0;
    }
    if (!HasEntryField(fields, kEntryFieldLastAccessTime) && existing == nullptr) {
        merged.lastAccessTime = 0;
    }
    if (!HasEntryField(fields, kEntryFieldAttributes) && existing == nullptr) {
        merged.attributes = 0;
    }
    Upsert(volumeRowId, merged);
}
void Projection::Remove(VolumeRowId volumeRowId, FileId frn) {
    const EntryKey key{volumeRowId, frn};
    const uint32_t* pIndex = idToIndex_.find(key);
    if (pIndex == nullptr) {
        return;
    }
#ifdef FF_INDEXSTORE_TESTING
    // large-index-lifecycle-and-search-scalability 4.2: isolation-matrix
    // modes skip exactly one kind of per-entry maintenance; production
    // builds compile both checks to constant false (see header seam).
    const bool skipFilenameMaintenance =
        removalMaintenanceMode_ == RemovalMaintenanceMode::SkipFilenameMaintenance ||
        removalMaintenanceMode_ == RemovalMaintenanceMode::SkipBoth;
    const bool skipChildUnlink =
        removalMaintenanceMode_ == RemovalMaintenanceMode::SkipChildUnlink ||
        removalMaintenanceMode_ == RemovalMaintenanceMode::SkipBoth;
#else
    constexpr bool skipFilenameMaintenance = false;
    constexpr bool skipChildUnlink = false;
#endif
    denseIterationOrderValid_ = false;
    const uint32_t index = *pIndex;
    if (!skipFilenameMaintenance) {
        RemoveFilenameIndexEntry(index, namePool_.Get(entries_[index].nameId));
    }
    if (!skipChildUnlink) {
        RemoveFromChildrenList(EntryKey{volumeRowId, entries_[index].parentFrn}, index);
    }
    idToIndex_.erase(key);
    if (index < lastAccessTimes_.size()) lastAccessTimes_[index] = 0;
[410 more lines in file. Use offset=851 to continue.]
read: src/indexstore/src/Projection.cpp
    freeList_.push_back(index);
    // entries_[index] itself is left in place (tombstoned) -- nothing
    // reachable via idToIndex_/parentToChildren_ points at it anymore, and
    // it will be overwritten the next time freeList_ hands its index back
    // out in Upsert.
}
void Projection::RemoveVolume(VolumeRowId volumeRowId, bool keepFilenameIndexCurrent) {
    // Collect first, then remove: Remove() mutates idToIndex_, so it can't
    // run while iterating it.
    std::vector<FileId> frns;
    for (const auto& pair : idToIndex_) {
        if (pair.first.volumeRowId == volumeRowId) {
            frns.push_back(pair.first.frn);
        }
    }
    RemoveBatch(volumeRowId, frns, keepFilenameIndexCurrent);
}
void Projection::RemoveBatch(VolumeRowId volumeRowId, const std::vector<FileId>& frns,
                             bool keepFilenameIndexCurrent) {
    if (frns.empty()) return;
    // large-index-lifecycle-and-search-scalability 4.4: per-entry trigram
    // removal is an O(posting-length) sorted-vector erase, so removing a
    // large batch entry-by-entry is quadratic in the accelerator's posting
    // sizes (measured: 12,983 ms to remove 200k shared-name rows vs 44.8 ms
    // with the per-entry accelerator work skipped -- 99.7% of removal cost).
    // For large batches drop the accelerator wholesale (O(1)) and let the
    // caller either rebuild it once here (keepFilenameIndexCurrent, O(N)) or
    // rebuild it as part of an enclosing bulk operation. Child unlink, id-map
    // erase and free-list maintenance stay per entry: they are O(1) each
    // (measured residual below 0.7 us/row) and preserve exact removal semantics.
    const bool wasValid = filenameIndexValid_;
    const bool bulk = frns.size() > kPerEntryFilenameRemovalThreshold;
    if (bulk && wasValid) {
        InvalidateFilenameIndex();
    }
    for (const FileId& frn : frns) {
        Remove(volumeRowId, frn);
    }
    if (bulk && wasValid && keepFilenameIndexCurrent) {
        RebuildFilenameIndex();
    }
    // large-index-lifecycle-and-search-scalability review-lane C Finding 5:
    // keep=false must leave the accelerator invalidated regardless of batch
    // size (the small per-entry path kept it valid, contradicting the
    // contract).
    if (!keepFilenameIndexCurrent) {
        InvalidateFilenameIndex();
    }
}
const ProjectionEntry* Projection::Find(VolumeRowId volumeRowId, FileId frn) const {
    const EntryKey key{volumeRowId, frn};
    const uint32_t* pIndex = idToIndex_.find(key);
    if (pIndex != nullptr) TouchIndex(*pIndex);
    return pIndex ? &entries_[*pIndex] : nullptr;
}
ProjectionEntryCursor::ProjectionEntryCursor(const Projection& projection)
    : projection_(&projection), dense_(projection.denseIterationOrderValid_),
      iterator_(dense_ ? EntryMap::Iterator{} : projection.idToIndex_.begin()),
      entriesData_(projection.entries_.data()),
      mapBuckets_(projection.idToIndex_.bucket_count()) {}
bool ProjectionEntryCursor::Stale() const noexcept {
    if (projection_ == nullptr) return false;
    // large-index-lifecycle-and-search-scalability review-lane C Finding 2:
    // entries_ reallocation/reorder or flat-table growth re-places row slots
    // under the iterator, which would silently skip rows. In-place updates
    // and appends within reserved capacity leave these markers untouched.
    return projection_->entries_.data() != entriesData_ ||
           projection_->idToIndex_.bucket_count() != mapBuckets_;
}
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
            std::set_union(result.begin(), result.end(), filenameExceptionalEntries_.begin(),
                           filenameExceptionalEntries_.end(), std::back_inserter(merged));
            if (maximumCandidates != 0 && merged.size() > maximumCandidates) {
                return std::nullopt;
            }
            return merged;
        }
        return result;
    } catch (...) {
        // The index is an accelerator only. A transient allocation failure in
        // candidate materialization must leave the canonical scan available.
        return std::nullopt;
    }
}
std::span<const uint32_t> Projection::ChildIndices(VolumeRowId volumeRowId, FileId parentFrn) const {
    const EntryKey key{volumeRowId, parentFrn};
    const uint32_t* index = idToIndex_.find(key);
    if (index != nullptr) TouchIndex(*index);
    return parentToChildren_.find(key);
}
std::optional<Projection::FolderAggregate> Projection::GetFolderAggregate(VolumeRowId volumeRowId, FileId parentFrn) const {
    FolderAggregate result = {0, 0};
    const ProjectionEntry* root = Find(volumeRowId, parentFrn);
    if (root == nullptr) {
        return std::nullopt;
    }
    std::vector<FileId> stack;
    stack.push_back(parentFrn);
    // allocated-size-storage-accuracy D7: checked, saturating addition --
    // a representationally impossible total must not wrap into a
    // plausible-looking number; on saturation the aggregate is marked
    // incomplete instead.
    const auto saturatingAdd = [](uint64_t& destination, uint64_t value, bool& overflowed) {
        if (std::numeric_limits<uint64_t>::max() - destination < value) {
            destination = std::numeric_limits<uint64_t>::max();
            overflowed = true;
            return;
        }
        destination += value;
    };
    bool overflowed = false;
    while (!stack.empty()) {
        FileId current = stack.back();
        stack.pop_back();
        auto children = ChildIndices(volumeRowId, current);
        for (uint32_t childIndex : children) {
            const auto& child = EntryAt(childIndex);
            ++result.itemCount;
            saturatingAdd(result.totalSizeBytes, child.sizeBytes, overflowed);
            ++result.allocatedBytesTotalCount;
            if (child.allocatedSizeKnown) {
                saturatingAdd(result.totalAllocatedSizeBytes, child.allocatedSizeBytes, overflowed);
                ++result.allocatedBytesKnownCount;
            }
            if ((child.attributes & kFileAttributeDirectory) != 0) {
                stack.push_back(child.frn);
            }
        }
    }
    // Complete only when every descendant's allocated size is known AND no
    // sum saturated -- a partial or wrapped subtotal is never exact.
    result.complete = !overflowed && result.allocatedBytesKnownCount == result.allocatedBytesTotalCount;
    return result;
}
Projection::PathResult Projection::ReconstructPath(VolumeRowId volumeRowId, FileId frn) const {
    return ReconstructPathUntil(volumeRowId, frn, {});
}
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
[130 more lines in file. Use offset=1131 to continue.]
grep: tests
ls: openspec/changes/large-index-lifecycle-and-search-scalabi...
drift/test_binary_identity_binding.ps1:77: $packageTrustText = [Text.Encoding]::Unicode.GetString($packageTrustBytes)
engine/AuthenticodeFixture.ps1:151:     [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
engine/AuthenticodeFixture.ps1:340:     if ($algorithmOid -ne '2.16.840.1.101.3.4.2.1' -or $hashedMessage.Length -ne 32) {
engine/AuthenticodeFixture.ps1:363:     $tstWriter.WriteObjectIdentifier('2.16.840.1.101.3.4.2.1')
engine/AuthenticodeFixture.ps1:375:         [Security.Cryptography.Oid]::new('1.2.840.113549.1.9.16.1.4'), $tstInfo)
engine/AuthenticodeFixture.ps1:391:             '1.2.840.113549.1.9.16.2.47', $essWriter.Encode()))
engine/AuthenticodeFixture.ps1:393:             [Security.Cryptography.Oid]::new('1.2.840.113549.1.9.16.2.47'), $essValues))
engine/AuthenticodeFixture.ps1:399:             '1.2.840.113549.1.9.5', $signingTimeEncoded))
engine/AuthenticodeFixture.ps1:401:             [Security.Cryptography.Oid]::new('1.2.840.113549.1.9.5'), $signingTimeValues))
engine/AuthenticodeFixture.ps1:460:         foreach ($argument in @('timestamp', '/tr', "http://127.0.0.1:$port/", '/td', 'SHA256', $OutputPath)) {
protocol/test_snapshot_pooled_equivalence.cpp:4: // unchanged), names must survive Unicode / mixed case / pathological
protocol/test_snapshot_pooled_equivalence.cpp:26: const std::wstring kUnicodeName = L"\u0391\u03bb\u03ac\u03c6\u03b1_123";  // Greek "Alpha_123"
protocol/test_snapshot_pooled_equivalence.cpp:27: const std::wstring kMixedCaseName = L"MixedCASE_NoNAscii_\u00c4\u00d6\u00dc";
protocol/test_snapshot_pooled_equivalence.cpp:60:         kUnicodeName,
protocol/test_snapshot_pooled_equivalence.cpp:123:     // Byte-equivalence across randomized fixtures, including Unicode,
protocol/test_protocol.cpp:637:           "escaped Windows and Unicode paths round-trip without accumulating backslashes");
fileoperations/test_file_operation_policy.cpp:90:     int unicodeCasePrompts = 0;
fileoperations/test_file_operation_policy.cpp:91:     const auto unicodeCaseNames = BuildTransferPlan(
fileoperations/test_file_operation_policy.cpp:95:             ++unicodeCasePrompts;
fileoperations/test_file_operation_policy.cpp:98:     Check(unicodeCaseNames && unicodeCaseNames->size() == 2 &&
fileoperations/test_file_operation_policy.cpp:99:               (*unicodeCaseNames)[0].destinationName.empty() &&
fileoperations/test_file_operation_policy.cpp:100:               (*unicodeCaseNames)[1].destinationName == L"É (2).txt" &&
fileoperations/test_file_operation_policy.cpp:101:               unicodeCasePrompts == 1,
fileoperations/test_file_operation_policy.cpp:102:           "Unicode case-variant source names were assigned distinct destinations");
indexstore/test_store_read_session.cpp:265:     const std::vector<uint64_t> nonAsciiClass{102, 103, 104};
indexstore/test_store_read_session.cpp:321:         Check(ids == nonAsciiClass,
indexstore/test_projection.cpp:376:     Check(!proj.FilenameCandidateIndices(u"\u6771\u4EAC").has_value(), "Unicode terms use the canonical fallback");
indexstore/test_projection.cpp:1034: // so at any scale whose Unicode population exceeds the caller's cap every
indexstore/test_projection.cpp:1058: void PopulateUnicodeHeavyFixture(Projection& proj) {
indexstore/test_projection.cpp:1078:     PopulateUnicodeHeavyFixture(proj);
indexstore/test_projection.cpp:1116: void TestFilenameCandidatesNarrowForSelectiveNeedlesDespiteUnicodeRows() {
indexstore/test_projection.cpp:1118:     PopulateUnicodeHeavyFixture(proj);
indexstore/test_projection.cpp:1123:           "a selective pure-ASCII needle yields candidates regardless of the Unicode row count");
indexstore/test_projection.cpp:1130:           "a no-match pure-ASCII needle yields an empty candidate set, not every Unicode row");
indexstore/test_projection.cpp:1136: // stated against this function -- never against an assumed Unicode casefold.
indexstore/test_projection.cpp:1157: // ordinal case table. The PR review's example assumed Unicode casefold
indexstore/test_projection.cpp:1158: // semantics (U+017F LONG S uppercases to 'S' in Unicode's table); the REAL
indexstore/test_projection.cpp:1434:     TestFilenameCandidatesNarrowForSelectiveNeedlesDespiteUnicodeRows();
engine/bench_secondary_filename_index.cpp:179:     {"unicode", L"\u6771\u4EAC"},
engine/bench_secondary_filename_index.cpp:180:     {"mixed_unicode", L"R\u00C9SUM\u00C9"},
engine/bench_secondary_filename_index.cpp:269:         nonAsciiIds_.reserve(entries.size() / 100 + 16);
engine/bench_secondary_filename_index.cpp:281:         if (!IsAscii(name)) InsertSorted(nonAsciiIds_, id);
engine/bench_secondary_filename_index.cpp:288:         if (!IsAscii(name)) EraseSorted(nonAsciiIds_, id);
engine/bench_secondary_filename_index.cpp:331:         std::vector<EntryId> activeNonAscii;
engine/bench_secondary_filename_index.cpp:332:         activeNonAscii.reserve(nonAsciiIds_.size());
engine/bench_secondary_filename_index.cpp:333:         for (const EntryId id : nonAsciiIds_) {
engine/bench_secondary_filename_index.cpp:334:             if (entries[id].active) activeNonAscii.push_back(id);
engine/bench_secondary_filename_index.cpp:337:         merged.reserve(result.size() + activeNonAscii.size());
engine/bench_secondary_filename_index.cpp:338:         std::set_union(result.begin(), result.end(), activeNonAscii.begin(), activeNonAscii.end(),
engine/bench_secondary_filename_index.cpp:354:                                        nonAsciiIds_.capacity() * sizeof(EntryId);
engine/bench_secondary_filename_index.cpp:355:         result.estimatedPersistentBytes += nonAsciiIds_.size() * sizeof(EntryId);
engine/bench_secondary_filename_index.cpp:371:         if (!IsAscii(name)) nonAsciiIds_.push_back(id);
engine/bench_secondary_filename_index.cpp:376:     std::vector<EntryId> nonAsciiIds_;
engine/bench_secondary_filename_index.cpp:854:               "\"unicode_policy\":\"benchmark_uses_ascii_literal_eligibility_and_verifies_non_ascii_names\","
engine/bench_large_index_scalability.cpp:311:     // Unicode-name subset (10% of the fixture) so the Unicode query class is
engine/bench_large_index_scalability.cpp:2138:         {"unicode", L"\u0441\u043f\u0438\u0441\u043e\u043a", 0},
search/bench_literal_matcher.cpp:244: std::vector<Name> MakeUnicodeNames(size_t count) {
search/bench_literal_matcher.cpp:319:     const std::vector<Name> unicode = MakeUnicodeNames(kCount);
search/bench_literal_matcher.cpp:336:     RunCase("unicode", unicode, L"Résumé");
search/bench_literal_matcher.cpp:337:     RunCase("unicode_mixed", unicode, L"REPORT_ФАЙЛ");
engine/test_active_path_resilience.cpp:91:         if (candidate.filename() == L"seed.txt" || candidate.filename() == L"unicode-U0001F4C1.txt") {
engine/test_active_path_resilience.cpp:217:         || !CreateFixtureFile(root / L"unicode-U0001F4C1.txt", seed + 1)) {
ui/ClipboardRoundTripClassifier.h:68:     bool independentFormatAdvertised = false; // diagnostic: a non-CF_HDROP probe format (e.g. CF_UNICODETEXT) advertised
engine/test_directory_watcher.cpp:532: void TestWatcherDeleteRecreateUnicodeAndLongName() {
engine/test_directory_watcher.cpp:537:           "unicode watcher directory fixture is created");
engine/test_directory_watcher.cpp:572:                   "recreated unicode directory observes a long Unicode filename");
engine/test_directory_watcher.cpp:621:     TestWatcherDeleteRecreateUnicodeAndLongName();
setup/PackageTrustFixture.ps1:171:     [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
setup/PackageTrustFixture.ps1:290:     if ($algorithmOid -ne '2.16.840.1.101.3.4.2.1' -or $hashedMessage.Length -ne 32) {
setup/PackageTrustFixture.ps1:312:     $tstWriter.WriteObjectIdentifier('2.16.840.1.101.3.4.2.1')
setup/PackageTrustFixture.ps1:324:         [Security.Cryptography.Oid]::new('1.2.840.113549.1.9.16.1.4'), $tstInfo)
setup/PackageTrustFixture.ps1:339:             '1.2.840.113549.1.9.16.2.47', $essWriter.Encode()))
setup/PackageTrustFixture.ps1:341:             [Security.Cryptography.Oid]::new('1.2.840.113549.1.9.16.2.47'), $essValues))
setup/PackageTrustFixture.ps1:347:             '1.2.840.113549.1.9.5', $signingTimeEncoded))
setup/PackageTrustFixture.ps1:349:             [Security.Cryptography.Oid]::new('1.2.840.113549.1.9.5'), $signingTimeValues))
setup/PackageTrustFixture.ps1:408:         foreach ($argument in @('timestamp', '/p7', '/tr', "http://127.0.0.1:$port/", '/td', 'SHA256',
setup/test_package_trust.cpp:64:                 "productVersion=0.1.0\nbuildIdentity=0000000000000000000000000000000000000000\n"
setup/test_package_trust.cpp:65:                 "architecture=x64\nfile=FastFiles.exe|ui|FastFiles.exe|FastFiles.exe|FastFiles|0.1.0|"
ui/test_display_path.cpp:43:         "Unicode-containing path uses the resolved long display form");
navigation/test_navigation_workspace.cpp:139:     const std::wstring unicodePath = L"C:\\日本語\\é\\Ω";
navigation/test_navigation_workspace.cpp:140:     const std::wstring unicodeName = L"旅行 📁";
navigation/test_navigation_workspace.cpp:141:     workspace.Navigate(unicodePath);
navigation/test_navigation_workspace.cpp:142:     workspace.State().bookmarks = {{unicodePath, unicodeName}};
navigation/test_navigation_workspace.cpp:143:     workspace.State().lastLocation = unicodePath;
navigation/test_navigation_workspace.cpp:144:     workspace.State().recentlyClosedTabs = {NavigationLocation::FromPath(unicodePath)};
navigation/test_navigation_workspace.cpp:146:     Check(workspace.FlushState(), "workspace state accepts Unicode paths and labels");
navigation/test_navigation_workspace.cpp:147:     WorkspaceState unicodeLoaded{};
navigation/test_navigation_workspace.cpp:148:     Check(NavigationWorkspace::LoadState(unicodeLoaded) == WorkspaceLoadStatus::Ok && unicodeLoaded.lastLocation == unicodePath &&
navigation/test_navigation_workspace.cpp:149:           unicodeLoaded.bookmarks.size() == 1 && unicodeLoaded.bookmarks.front().path == unicodePath &&
navigation/test_navigation_workspace.cpp:150:           unicodeLoaded.bookmarks.front().displayName == unicodeName &&
navigation/test_navigation_workspace.cpp:151:           unicodeLoaded.recentlyClosedTabs.size() == 1 && unicodeLoaded.recentlyClosedTabs.front().path == unicodePath,
navigation/test_navigation_workspace.cpp:152:           "workspace state round-trips Unicode paths and labels");
ui/test_quick_actions.cpp:162:                     const UINT probeFormats[] = {CF_HDROP, dropEffectFormat, CF_UNICODETEXT, CF_TEXT, CF_DIB};
ui/test_quick_actions.cpp:168:                         if (format == CF_UNICODETEXT) observations.independentFormatAdvertised = true;
ui/test_quick_actions.cpp:187:                         if (format == CF_UNICODETEXT) observations.independentFormatReadable = true;
search/test_ascii_fold_matcher.cpp:138:         {L"r\xE9sum\xE9", L"C:\\unicode", 40, 50, false},
engine/test_index_pipeline.cpp:1371:         {"unicode filename", makeQuery(L"\u0444\u0430\u0439\u043b"), false},
engine/test_pooled_snapshot_lifetime.cpp:11: // produces for the same fixture, including Unicode, mixed-case, and long
engine/test_pooled_snapshot_lifetime.cpp:81: // seeded pipeline whose names include Unicode, mixed case, and long text.
engine/test_pooled_snapshot_lifetime.cpp:107:         L"\u0391\u03bb\u03ac\u03c6\u03b1.txt",                 // Unicode
[100 matches limit reached. Use limit=200 for more, or refine pattern]
adversarial-review-2026-10-21.md
certification-ladder.md
equivalence-gate-adversarial-matrix.md
equivalence-product-tree-diff-43fc3c1-2f365a5.txt
explore-findings.md
hosted-ci-truth-2026-10-21.md
lifecycle-reload-resources.md
memory-cold-decision.md
rebuild/
review-remediation/
review-remediation-2026-10-21.md
search/
uia-explorer-drag-in-investigation.md
grep: tests/engine/bench_large_index_scalability.cpp
ls: openspec/changes/large-index-lifecycle-and-search-scalabi...
bench_large_index_scalability.cpp-158-     bool detailedDiagnostics = false;
bench_large_index_scalability.cpp-159-     // performance-closure-and-multisurface-acceleration 8.1: projection-memory
bench_large_index_scalability.cpp-160-     // decomposition mode. When set, the benchmark runs only the memory report
bench_large_index_scalability.cpp:161:     // (fixture generation -> rebuild cycles -> export weighing -> teardown) and
bench_large_index_scalability.cpp-162-     // writes its machine-readable result to memoryOutputPath.
bench_large_index_scalability.cpp-163-     std::filesystem::path memoryOutputPath;
bench_large_index_scalability.cpp-164-     bool memoryReport = false;
bench_large_index_scalability.cpp-288-     record.fixed.lastAccessTime = timestamp + 200;
bench_large_index_scalability.cpp-289-     record.fixed.sizeBytes = sizeBytes;
bench_large_index_scalability.cpp-290-     record.fixed.fileAttributes = attributes;
bench_large_index_scalability.cpp:291:     record.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
bench_large_index_scalability.cpp-292-     record.fileName = ToUtf16(name);
bench_large_index_scalability.cpp-293-     return record;
bench_large_index_scalability.cpp-294- }
bench_large_index_scalability.cpp-308-     if (pattern == 6) return L"notes_" + std::to_wstring(index) + L".md";
bench_large_index_scalability.cpp-309-     if (pattern == 7) return L"common_media_" + std::to_wstring(index) + L".mp4";
bench_large_index_scalability.cpp-310-     // performance-closure-and-multisurface-acceleration 7.1: a deterministic
bench_large_index_scalability.cpp:311:     // Unicode-name subset (10% of the fixture) so the Unicode query class is
bench_large_index_scalability.cpp-312-     // measured against real matches, not only the no-result path. Escape
bench_large_index_scalability.cpp-313-     // sequences keep the source encoding-independent.
bench_large_index_scalability.cpp-314-     if (pattern == 8) return L"\u0441\u043f\u0438\u0441\u043e\u043a_" + std::to_wstring(index) + L".txt";
bench_large_index_scalability.cpp-329-     size_t actualSnapshotEntryCount = 0;
bench_large_index_scalability.cpp-330-     size_t actualSnapshotBytes = 0;
bench_large_index_scalability.cpp-331-     bool actualSnapshotPublishSucceeded = false;
bench_large_index_scalability.cpp:332:     MemorySample beforeFixture;
bench_large_index_scalability.cpp-333-     MemorySample afterFixture;
bench_large_index_scalability.cpp-334-     MemorySample afterDatabaseOpen;
bench_large_index_scalability.cpp-335-     MemorySample afterRebuild;
bench_large_index_scalability.cpp-330-     size_t actualSnapshotBytes = 0;
bench_large_index_scalability.cpp-331-     bool actualSnapshotPublishSucceeded = false;
bench_large_index_scalability.cpp-332-     MemorySample beforeFixture;
bench_large_index_scalability.cpp:333:     MemorySample afterFixture;
bench_large_index_scalability.cpp-334-     MemorySample afterDatabaseOpen;
bench_large_index_scalability.cpp-335-     MemorySample afterRebuild;
bench_large_index_scalability.cpp-336-     MemorySample afterBroadQuery;
bench_large_index_scalability.cpp-562-     ffengine::IndexPipeline pipeline;
bench_large_index_scalability.cpp-563-     if (!pipeline.Open(PathToUtf8(database))) return false;
bench_large_index_scalability.cpp-564-     if (detailedDiagnostics) {
bench_large_index_scalability.cpp:565:         std::printf("  [%zu] fixture open %.2f ms\n", scale, ElapsedMs(openStart));
bench_large_index_scalability.cpp-566-         std::printf("  [%zu] durable insert start (target %zu records)\n", scale, scale);
bench_large_index_scalability.cpp-567-         std::fflush(stdout);
bench_large_index_scalability.cpp-568-     }
bench_large_index_scalability.cpp-870- 
bench_large_index_scalability.cpp-871-     ffprotocol::IndexedSearchRequest request;
bench_large_index_scalability.cpp-872-     request.requestId = 950'000 + static_cast<uint64_t>(scale % 100'000);
bench_large_index_scalability.cpp:873:     request.scope = 2; // Keep the cancellation probe tied to the fixture volume.
bench_large_index_scalability.cpp-874-     request.scopePath = L"C:";
bench_large_index_scalability.cpp-875-     request.maxResults = 10'000;
bench_large_index_scalability.cpp-876-     request.query = L"common";
bench_large_index_scalability.cpp-1284-     WriteJsonDouble(output, dataset.actualSnapshotPublishCopyAndCommitMs);
bench_large_index_scalability.cpp-1285-     output << ",\"actual_snapshot_ui_mapping_and_parsing_ms\":";
bench_large_index_scalability.cpp-1286-     WriteJsonDouble(output, dataset.actualSnapshotUiMappingAndParsingMs);
bench_large_index_scalability.cpp:1287:     output << ",\"memory_before_fixture\":"; WriteMemory(output, dataset.beforeFixture);
bench_large_index_scalability.cpp-1288-     output << ",\"memory_after_fixture\":"; WriteMemory(output, dataset.afterFixture);
bench_large_index_scalability.cpp-1289-     output << ",\"memory_after_database_open\":"; WriteMemory(output, dataset.afterDatabaseOpen);
bench_large_index_scalability.cpp-1290-     output << ",\"memory_after_rebuild\":"; WriteMemory(output, dataset.afterRebuild);
bench_large_index_scalability.cpp-1285-     output << ",\"actual_snapshot_ui_mapping_and_parsing_ms\":";
bench_large_index_scalability.cpp-1286-     WriteJsonDouble(output, dataset.actualSnapshotUiMappingAndParsingMs);
bench_large_index_scalability.cpp-1287-     output << ",\"memory_before_fixture\":"; WriteMemory(output, dataset.beforeFixture);
bench_large_index_scalability.cpp:1288:     output << ",\"memory_after_fixture\":"; WriteMemory(output, dataset.afterFixture);
bench_large_index_scalability.cpp-1289-     output << ",\"memory_after_database_open\":"; WriteMemory(output, dataset.afterDatabaseOpen);
bench_large_index_scalability.cpp-1290-     output << ",\"memory_after_rebuild\":"; WriteMemory(output, dataset.afterRebuild);
bench_large_index_scalability.cpp-1291-     output << ",\"projection_estimated_bytes\":" << dataset.projectionEstimatedBytes
bench_large_index_scalability.cpp-1718-     bool succeeded = false;
bench_large_index_scalability.cpp-1719-     do {
bench_large_index_scalability.cpp-1720-         report.captures.push_back(CaptureMemoryPoint("process_start", 0.0));
bench_large_index_scalability.cpp:1721:         MemorySample beforeFixture{};
bench_large_index_scalability.cpp-1722-         MemorySample afterFixture{};
bench_large_index_scalability.cpp-1723-         ffindexstore::VolumeRowId volumeId = 0;
bench_large_index_scalability.cpp-1724-         if (!BuildSyntheticDatabase(database, scale, report.generationMs, volumeId,
bench_large_index_scalability.cpp-1719-     do {
bench_large_index_scalability.cpp-1720-         report.captures.push_back(CaptureMemoryPoint("process_start", 0.0));
bench_large_index_scalability.cpp-1721-         MemorySample beforeFixture{};
bench_large_index_scalability.cpp:1722:         MemorySample afterFixture{};
bench_large_index_scalability.cpp-1723-         ffindexstore::VolumeRowId volumeId = 0;
bench_large_index_scalability.cpp-1724-         if (!BuildSyntheticDatabase(database, scale, report.generationMs, volumeId,
bench_large_index_scalability.cpp-1725-                                     beforeFixture, afterFixture, options.detailedDiagnostics)) {
bench_large_index_scalability.cpp-1722-         MemorySample afterFixture{};
bench_large_index_scalability.cpp-1723-         ffindexstore::VolumeRowId volumeId = 0;
bench_large_index_scalability.cpp-1724-         if (!BuildSyntheticDatabase(database, scale, report.generationMs, volumeId,
bench_large_index_scalability.cpp:1725:                                     beforeFixture, afterFixture, options.detailedDiagnostics)) {
bench_large_index_scalability.cpp-1726-             report.failureReason = "synthetic database generation failed";
bench_large_index_scalability.cpp-1727-             break;
bench_large_index_scalability.cpp-1728-         }
bench_large_index_scalability.cpp-1728-         }
bench_large_index_scalability.cpp-1729-         report.databaseBytes = DatabaseBytes(database);
bench_large_index_scalability.cpp-1730-         report.volumeRowId = static_cast<uint64_t>(volumeId);
bench_large_index_scalability.cpp:1731:         report.captures.push_back(CaptureMemoryPoint("after_fixture_generation", 0.0));
bench_large_index_scalability.cpp-1732- 
bench_large_index_scalability.cpp-1733-         ffprotocol::VolumeSetting setting;
bench_large_index_scalability.cpp-1734-         setting.key = L"C:";
bench_large_index_scalability.cpp-2108-     if (!ParseOptions(argc, argv, options)) return 2;
bench_large_index_scalability.cpp-2109- 
bench_large_index_scalability.cpp-2110-     // performance-closure-and-multisurface-acceleration 8.1: the memory mode is
bench_large_index_scalability.cpp:2111:     // a separate, focused path (fixture -> rebuild cycles -> export weighing ->
bench_large_index_scalability.cpp-2112-     // teardown) that writes its own machine-readable report.
bench_large_index_scalability.cpp-2113-     if (options.memoryReport) {
bench_large_index_scalability.cpp-2114-         if (options.memoryOutputPath.empty()) {
bench_large_index_scalability.cpp-2160-         dataset.databaseLocation = database.wstring();
bench_large_index_scalability.cpp-2161-         ffindexstore::VolumeRowId volumeId = 0;
bench_large_index_scalability.cpp-2162-         if (!BuildSyntheticDatabase(database, scale, dataset.generationMs, volumeId,
bench_large_index_scalability.cpp:2163:                                      dataset.beforeFixture, dataset.afterFixture,
bench_large_index_scalability.cpp-2164-                                      options.detailedDiagnostics)) {
bench_large_index_scalability.cpp-2165-             std::fprintf(stderr, "synthetic database generation failed at %zu\n", scale);
bench_large_index_scalability.cpp-2166-             std::error_code ignored;
memory-report-after-fix.json
memory-report-before.json
rebuild-isolation-after-fix.json
rebuild-isolation-before.json
read: tests/engine/bench_large_index_scalability.cpp
        }
        if (argument == L"--rebuild-cycles" && index + 1 < argc) {
            const long parsed = _wtol(argv[++index]);
            if (parsed < 1 || parsed > 64) {
                std::fwprintf(stderr, L"Invalid --rebuild-cycles value\n");
                return false;
            }
            options.rebuildCycles = static_cast<size_t>(parsed);
            continue;
        }
        if (argument == L"--export-bound-directories" && index + 1 < argc) {
            const long parsed = _wtol(argv[++index]);
            if (parsed < 1) {
                std::fwprintf(stderr, L"Invalid --export-bound-directories value\n");
                return false;
            }
            options.exportBoundDirectories = static_cast<size_t>(parsed);
            continue;
        }
        std::fwprintf(stderr, L"Unknown or incomplete argument: %s\n", argument.c_str());
        return false;
    }
    return true;
}
std::string PathToUtf8(const std::filesystem::path& path) {
    return ToUtf8(path.wstring());
}
std::filesystem::path MakeTemporaryDirectory(size_t scale) {
    const std::filesystem::path base = std::filesystem::temp_directory_path();
    const std::wstring stem = L"FastFiles-large-index-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(scale);
    for (unsigned int suffix = 0; suffix < 1000; ++suffix) {
        const std::filesystem::path candidate = base / (stem + L"-" + std::to_wstring(suffix));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) return candidate;
    }
    return {};
}
ffprotocol::MftRecordV1 MakeMftRecord(uint64_t id, uint64_t parent, const std::wstring& name,
                                      uint32_t attributes, uint64_t sizeBytes, uint64_t timestamp) {
    ffprotocol::MftRecordV1 record{};
    record.fixed.fileReferenceNumber = id;
    record.fixed.parentFileReferenceNumber = parent;
    record.fixed.creationTime = timestamp;
    record.fixed.lastModifiedTime = timestamp + 100;
    record.fixed.lastAccessTime = timestamp + 200;
    record.fixed.sizeBytes = sizeBytes;
    record.fixed.fileAttributes = attributes;
    record.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
    record.fileName = ToUtf16(name);
    return record;
}
std::wstring DirectoryName(size_t index) {
    return L"folder_" + std::to_wstring(index % 997) + L"_branch_" + std::to_wstring(index);
}
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
struct DatasetResult {
    size_t scale = 0;
    std::wstring databaseLocation;
    uint64_t databaseBytes = 0;
    double generationMs = 0.0;
    double rebuildMs = 0.0;
    double snapshotExportMs = 0.0;
    double actualSnapshotSerializationMs = 0.0;
    double actualSnapshotPublishCopyAndCommitMs = 0.0;
    double actualSnapshotUiMappingAndParsingMs = 0.0;
    size_t actualSnapshotDirectoryCount = 0;
    size_t actualSnapshotEntryCount = 0;
    size_t actualSnapshotBytes = 0;
    bool actualSnapshotPublishSucceeded = false;
    MemorySample beforeFixture;
    MemorySample afterFixture;
    MemorySample afterDatabaseOpen;
    MemorySample afterRebuild;
    MemorySample afterBroadQuery;
    MemorySample postQuery;
    size_t projectionEstimatedBytes = 0;
    size_t projectionEntryBytes = sizeof(ffindexstore::ProjectionEntry);
    ResourceSample resourcesAfterRebuild;
    ResourceSample resourcesPostQuery;
    struct ContentionResult {
        bool executed = false;
        bool searchReachedColdStream = false;
        bool ingestionCompletedWhileHeld = false;
        bool aggregateCompletedWhileHeld = false;
        bool snapshotCompletedWhileHeld = false;
        bool secondSearchCompletedWhileHeld = false;
        bool cancelledSearch = false;
        size_t activeReadersAfterCleanup = 0;
        size_t peakReaders = 0;
        double heldSearchMs = 0.0;
        double ingestionMs = 0.0;
        double aggregateMs = 0.0;
        double snapshotMs = 0.0;
        double secondSearchMs = 0.0;
        double cancellationMs = 0.0;
    } contention;
    ffengine::IndexPipeline::ReadPlaneDiagnostics readPlane;
};
struct SearchRun {
    size_t scale = 0;
    std::string queryClass;
    std::string query;
    std::string temperature;
    size_t iteration = 0;
    size_t resultCount = 0;
    size_t candidateCount = 0;
[2050 more lines in file. Use offset=370 to continue.]
grep: tests/engine/bench_large_index_scalability.cpp
grep: src/indexstore/src/Store.cpp
bench_large_index_scalability.cpp-268- 
bench_large_index_scalability.cpp-269- std::filesystem::path MakeTemporaryDirectory(size_t scale) {
bench_large_index_scalability.cpp:270:     const std::filesystem::path base = std::filesystem::temp_directory_path();
bench_large_index_scalability.cpp-271-     const std::wstring stem = L"FastFiles-large-index-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
bench_large_index_scalability.cpp-272-                               std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(scale);
bench_large_index_scalability.cpp-274-         const std::filesystem::path candidate = base / (stem + L"-" + std::to_wstring(suffix));
bench_large_index_scalability.cpp-275-         std::error_code error;
bench_large_index_scalability.cpp:276:         if (std::filesystem::create_directory(candidate, error)) return candidate;
bench_large_index_scalability.cpp-277-     }
bench_large_index_scalability.cpp-278-     return {};
bench_large_index_scalability.cpp-294- }
bench_large_index_scalability.cpp-295- 
bench_large_index_scalability.cpp:296: std::wstring DirectoryName(size_t index) {
bench_large_index_scalability.cpp-297-     return L"folder_" + std::to_wstring(index % 997) + L"_branch_" + std::to_wstring(index);
bench_large_index_scalability.cpp-298- }
bench_large_index_scalability.cpp-298- }
bench_large_index_scalability.cpp-299- 
bench_large_index_scalability.cpp:300: std::wstring FileName(size_t index) {
bench_large_index_scalability.cpp-301-     const size_t pattern = index % 10;
bench_large_index_scalability.cpp-302-     if (index == 7391) return L"unique_component_7391.txt";
bench_large_index_scalability.cpp-541-             "non-Release",
bench_large_index_scalability.cpp-542- #endif
bench_large_index_scalability.cpp:543:             "temporary directory (drive/model not collected)"};
bench_large_index_scalability.cpp-544- }
bench_large_index_scalability.cpp-545- 
bench_large_index_scalability.cpp-555- }
bench_large_index_scalability.cpp-556- 
bench_large_index_scalability.cpp:557: bool BuildSyntheticDatabase(const std::filesystem::path& database, size_t scale, double& generationMs,
bench_large_index_scalability.cpp-558-                             ffindexstore::VolumeRowId& volumeId, MemorySample& before,
bench_large_index_scalability.cpp-559-                             MemorySample& after, bool detailedDiagnostics) {
bench_large_index_scalability.cpp-577-     pipeline.SetVolumeRules(volumeId, setting);
bench_large_index_scalability.cpp-578- 
bench_large_index_scalability.cpp:579:     const size_t directoryCount = (std::max)(size_t{1}, scale / 64);
bench_large_index_scalability.cpp-580-     const auto generationStart = Clock::now();
bench_large_index_scalability.cpp-581-     std::vector<ffprotocol::MftRecordV1> batch;
bench_large_index_scalability.cpp-601- 
bench_large_index_scalability.cpp-602-     batch.push_back(MakeMftRecord(5, 5, L"", 0x10, 0, 1));
bench_large_index_scalability.cpp:603:     for (size_t index = 0; index < directoryCount && 1 + index < scale; ++index) {
bench_large_index_scalability.cpp-604-         const uint64_t id = 100 + static_cast<uint64_t>(index);
bench_large_index_scalability.cpp-605-         const uint64_t parent = index < 16 ? 5 : 100 + static_cast<uint64_t>((index - 1) / 16);
bench_large_index_scalability.cpp-604-         const uint64_t id = 100 + static_cast<uint64_t>(index);
bench_large_index_scalability.cpp-605-         const uint64_t parent = index < 16 ? 5 : 100 + static_cast<uint64_t>((index - 1) / 16);
bench_large_index_scalability.cpp:606:         batch.push_back(MakeMftRecord(id, parent, DirectoryName(index), 0x10, 0, 10 + index));
bench_large_index_scalability.cpp-607-         if (batch.size() == kBatchSize && !flush()) return false;
bench_large_index_scalability.cpp-608-     }
bench_large_index_scalability.cpp-608-     }
bench_large_index_scalability.cpp-609- 
bench_large_index_scalability.cpp:610:     const size_t filesToCreate = scale > 1 + directoryCount ? scale - 1 - directoryCount : 0;
bench_large_index_scalability.cpp-611-     const uint64_t fileIdBase = 100 + static_cast<uint64_t>(directoryCount);
bench_large_index_scalability.cpp-612-     for (size_t index = 0; index < filesToCreate; ++index) {
bench_large_index_scalability.cpp-609- 
bench_large_index_scalability.cpp-610-     const size_t filesToCreate = scale > 1 + directoryCount ? scale - 1 - directoryCount : 0;
bench_large_index_scalability.cpp:611:     const uint64_t fileIdBase = 100 + static_cast<uint64_t>(directoryCount);
bench_large_index_scalability.cpp-612-     for (size_t index = 0; index < filesToCreate; ++index) {
bench_large_index_scalability.cpp-613-         const uint64_t parent = 100 + static_cast<uint64_t>(index % directoryCount);
bench_large_index_scalability.cpp-611-     const uint64_t fileIdBase = 100 + static_cast<uint64_t>(directoryCount);
bench_large_index_scalability.cpp-612-     for (size_t index = 0; index < filesToCreate; ++index) {
bench_large_index_scalability.cpp:613:         const uint64_t parent = 100 + static_cast<uint64_t>(index % directoryCount);
bench_large_index_scalability.cpp-614-         const uint64_t id = fileIdBase + static_cast<uint64_t>(index);
bench_large_index_scalability.cpp-615-         const uint64_t sizeBytes = 1024 + static_cast<uint64_t>((index * 7919) % (64 * 1024 * 1024));
bench_large_index_scalability.cpp-614-         const uint64_t id = fileIdBase + static_cast<uint64_t>(index);
bench_large_index_scalability.cpp-615-         const uint64_t sizeBytes = 1024 + static_cast<uint64_t>((index * 7919) % (64 * 1024 * 1024));
bench_large_index_scalability.cpp:616:         batch.push_back(MakeMftRecord(id, parent, FileName(index), 0, sizeBytes, 1000 + index));
bench_large_index_scalability.cpp-617-         if (batch.size() == kBatchSize && !flush()) return false;
bench_large_index_scalability.cpp-618-     }
bench_large_index_scalability.cpp-1058- 
bench_large_index_scalability.cpp-1059- std::map<std::wstring, ffprotocol::SnapshotDirectory> MakeSnapshot(size_t rows) {
bench_large_index_scalability.cpp:1060:     ffprotocol::SnapshotDirectory directory;
bench_large_index_scalability.cpp-1061-     directory.status = ffprotocol::DirectoryEnumerationStatus::Success;
bench_large_index_scalability.cpp-1062-     directory.volumeRowId = 1;
bench_large_index_scalability.cpp-1059- std::map<std::wstring, ffprotocol::SnapshotDirectory> MakeSnapshot(size_t rows) {
bench_large_index_scalability.cpp-1060-     ffprotocol::SnapshotDirectory directory;
bench_large_index_scalability.cpp:1061:     directory.status = ffprotocol::DirectoryEnumerationStatus::Success;
bench_large_index_scalability.cpp-1062-     directory.volumeRowId = 1;
bench_large_index_scalability.cpp-1063-     directory.directoryIdLow = 5;
bench_large_index_scalability.cpp-1060-     ffprotocol::SnapshotDirectory directory;
bench_large_index_scalability.cpp-1061-     directory.status = ffprotocol::DirectoryEnumerationStatus::Success;
bench_large_index_scalability.cpp:1062:     directory.volumeRowId = 1;
bench_large_index_scalability.cpp-1063-     directory.directoryIdLow = 5;
bench_large_index_scalability.cpp-1064-     directory.parentIdLow = 5;
bench_large_index_scalability.cpp-1061-     directory.status = ffprotocol::DirectoryEnumerationStatus::Success;
bench_large_index_scalability.cpp-1062-     directory.volumeRowId = 1;
bench_large_index_scalability.cpp:1063:     directory.directoryIdLow = 5;
bench_large_index_scalability.cpp-1064-     directory.parentIdLow = 5;
bench_large_index_scalability.cpp-1065-     directory.entries.reserve(rows);
bench_large_index_scalability.cpp-1062-     directory.volumeRowId = 1;
bench_large_index_scalability.cpp-1063-     directory.directoryIdLow = 5;
bench_large_index_scalability.cpp:1064:     directory.parentIdLow = 5;
bench_large_index_scalability.cpp-1065-     directory.entries.reserve(rows);
bench_large_index_scalability.cpp-1066-     for (size_t index = 0; index < rows; ++index) {
bench_large_index_scalability.cpp-1063-     directory.directoryIdLow = 5;
bench_large_index_scalability.cpp-1064-     directory.parentIdLow = 5;
bench_large_index_scalability.cpp:1065:     directory.entries.reserve(rows);
bench_large_index_scalability.cpp-1066-     for (size_t index = 0; index < rows; ++index) {
bench_large_index_scalability.cpp-1067-         ffprotocol::SnapshotDirectoryEntry entry;
bench_large_index_scalability.cpp-1074-         entry.fileIdLow = 100 + index;
bench_large_index_scalability.cpp-1075-         entry.parentIdLow = 5;
bench_large_index_scalability.cpp:1076:         directory.entries.push_back(std::move(entry));
bench_large_index_scalability.cpp-1077-     }
bench_large_index_scalability.cpp-1078-     std::map<std::wstring, ffprotocol::SnapshotDirectory> result;
bench_large_index_scalability.cpp-1077-     }
bench_large_index_scalability.cpp-1078-     std::map<std::wstring, ffprotocol::SnapshotDirectory> result;
bench_large_index_scalability.cpp:1079:     result.emplace(L"C:\\Benchmark", std::move(directory));
bench_large_index_scalability.cpp-1080-     return result;
bench_large_index_scalability.cpp-1081- }
bench_large_index_scalability.cpp-1082- 
bench_large_index_scalability.cpp-1083- size_t SerializedSizeForRows(size_t rows) {
bench_large_index_scalability.cpp:1084:     constexpr size_t kDirectoryCountBytes = sizeof(uint32_t);
bench_large_index_scalability.cpp-1085-     constexpr size_t kStatusBytes = sizeof(uint32_t);
bench_large_index_scalability.cpp-1086-     constexpr size_t kDirectoryIdentifierBytes = 5 * sizeof(uint64_t);
bench_large_index_scalability.cpp-1087-     constexpr size_t kEntryFixedBytes = sizeof(uint8_t) + 8 * sizeof(uint64_t) + sizeof(uint32_t);
bench_large_index_scalability.cpp-1088-     const std::wstring path = L"C:\\Benchmark";
bench_large_index_scalability.cpp:1089:     size_t total = kDirectoryCountBytes + sizeof(uint32_t) + path.size() * sizeof(wchar_t) +
bench_large_index_scalability.cpp-1090-         kStatusBytes + kDirectoryIdentifierBytes + sizeof(uint32_t);
bench_large_index_scalability.cpp-1091-     for (size_t index = 0; index < rows; ++index) {
bench_large_index_scalability.cpp-1137-             if (parsed) {
bench_large_index_scalability.cpp-1138-                 result.parsed = true;
bench_large_index_scalability.cpp:1139:                 for (const auto& [path, directory] : *parsed) {
bench_large_index_scalability.cpp-1140-                     UNREFERENCED_PARAMETER(path);
bench_large_index_scalability.cpp-1141-                     result.rows += directory.entries.size();
bench_large_index_scalability.cpp-1139-                 for (const auto& [path, directory] : *parsed) {
bench_large_index_scalability.cpp-1140-                     UNREFERENCED_PARAMETER(path);
bench_large_index_scalability.cpp:1141:                     result.rows += directory.entries.size();
bench_large_index_scalability.cpp-1142-                 }
bench_large_index_scalability.cpp-1143-             }
bench_large_index_scalability.cpp-1274-     output << ",\"rebuild_ms\":"; WriteJsonDouble(output, dataset.rebuildMs);
bench_large_index_scalability.cpp-1275-     output << ",\"snapshot_export_ms\":"; WriteJsonDouble(output, dataset.snapshotExportMs);
bench_large_index_scalability.cpp:1276:     output << ",\"actual_snapshot_directory_count\":" << dataset.actualSnapshotDirectoryCount
bench_large_index_scalability.cpp-1277-            << ",\"actual_snapshot_entry_count\":" << dataset.actualSnapshotEntryCount
bench_large_index_scalability.cpp-1278-            << ",\"actual_snapshot_bytes\":" << dataset.actualSnapshotBytes
bench_large_index_scalability.cpp-1442- //     for plateau and return-to-baseline evidence;
bench_large_index_scalability.cpp-1443- //   * a counterbalanced pooled-vs-owning export/publication weighing on the
bench_large_index_scalability.cpp:1444: //     same projection generation and the same bounded directory set.
bench_large_index_scalability.cpp-1445- // ---------------------------------------------------------------------------
bench_large_index_scalability.cpp-1446- 
bench_large_index_scalability.cpp-1608-     pass.exportAllocationCount = ffbench::AllocationsDelta(pass.allocatorBefore, pass.allocatorAfterExport);
bench_large_index_scalability.cpp-1609-     pass.directories = snapshot.size();
bench_large_index_scalability.cpp:1610:     for (const auto& [path, directory] : snapshot) {
bench_large_index_scalability.cpp-1611-         UNREFERENCED_PARAMETER(path);
bench_large_index_scalability.cpp-1612-         pass.entries += directory.entries.size();
bench_large_index_scalability.cpp-1610-     for (const auto& [path, directory] : snapshot) {
bench_large_index_scalability.cpp-1611-         UNREFERENCED_PARAMETER(path);
bench_large_index_scalability.cpp:1612:         pass.entries += directory.entries.size();
bench_large_index_scalability.cpp-1613-     }
bench_large_index_scalability.cpp-1614- 
bench_large_index_scalability.cpp-1654-     pass.exportAllocationCount = ffbench::AllocationsDelta(pass.allocatorBefore, pass.allocatorAfterExport);
bench_large_index_scalability.cpp-1655-     pass.directories = snapshot.size();
bench_large_index_scalability.cpp:1656:     for (const auto& [path, directory] : snapshot) {
bench_large_index_scalability.cpp-1657-         UNREFERENCED_PARAMETER(path);
bench_large_index_scalability.cpp-1658-         pass.entries += directory.entries.size();
bench_large_index_scalability.cpp-1656-     for (const auto& [path, directory] : snapshot) {
bench_large_index_scalability.cpp-1657-         UNREFERENCED_PARAMETER(path);
bench_large_index_scalability.cpp:1658:         pass.entries += directory.entries.size();
bench_large_index_scalability.cpp-1659-     }
bench_large_index_scalability.cpp-1660- 
bench_large_index_scalability.cpp-1683- struct ScaleMemoryReport {
bench_large_index_scalability.cpp-1684-     size_t scale = 0;
bench_large_index_scalability.cpp:1685:     size_t directoryCount = 0;
bench_large_index_scalability.cpp-1686-     size_t fileCount = 0;
bench_large_index_scalability.cpp-1687-     size_t boundDirectories = 0;
bench_large_index_scalability.cpp-1703-                           ffengine::SnapshotPublisher& publisher, ScaleMemoryReport& report) {
bench_large_index_scalability.cpp-1704-     report.scale = scale;
bench_large_index_scalability.cpp:1705:     report.directoryCount = (std::max)(size_t{1}, scale / 64);
bench_large_index_scalability.cpp-1706-     report.fileCount = scale > 1 + report.directoryCount ? scale - 1 - report.directoryCount : 0;
bench_large_index_scalability.cpp-1707-     report.boundDirectories = options.exportBoundDirectories;
bench_large_index_scalability.cpp-1704-     report.scale = scale;
bench_large_index_scalability.cpp-1705-     report.directoryCount = (std::max)(size_t{1}, scale / 64);
bench_large_index_scalability.cpp:1706:     report.fileCount = scale > 1 + report.directoryCount ? scale - 1 - report.directoryCount : 0;
bench_large_index_scalability.cpp-1707-     report.boundDirectories = options.exportBoundDirectories;
bench_large_index_scalability.cpp-1708-     report.rebuildCycleTarget = options.rebuildCycles;
bench_large_index_scalability.cpp-1710-     const std::filesystem::path temporaryDirectory = MakeTemporaryDirectory(scale);
bench_large_index_scalability.cpp-1711-     if (temporaryDirectory.empty()) {
bench_large_index_scalability.cpp:1712:         report.failureReason = "temporary directory creation failed";
bench_large_index_scalability.cpp-1713-         return false;
bench_large_index_scalability.cpp-1714-     }
bench_large_index_scalability.cpp-1722-         MemorySample afterFixture{};
bench_large_index_scalability.cpp-1723-         ffindexstore::VolumeRowId volumeId = 0;
bench_large_index_scalability.cpp:1724:         if (!BuildSyntheticDatabase(database, scale, report.generationMs, volumeId,
bench_large_index_scalability.cpp-1725-                                     beforeFixture, afterFixture, options.detailedDiagnostics)) {
bench_large_index_scalability.cpp-1726-             report.failureReason = "synthetic database generation failed";
bench_large_index_scalability.cpp-1966- void WriteScaleMemoryReport(std::ostream& output, const ScaleMemoryReport& report) {
bench_large_index_scalability.cpp-1967-     output << "{\"scale\":" << report.scale
bench_large_index_scalability.cpp:1968:            << ",\"directory_count\":" << report.directoryCount
bench_large_index_scalability.cpp-1969-            << ",\"file_count\":" << report.fileCount
bench_large_index_scalability.cpp-1970-            << ",\"bound_directories\":" << report.boundDirectories
bench_large_index_scalability.cpp-2021-         std::filesystem::create_directories(parent, error);
bench_large_index_scalability.cpp-2022-         if (error) {
bench_large_index_scalability.cpp:2023:             std::fprintf(stderr, "unable to create memory report directory\n");
bench_large_index_scalability.cpp-2024-             return 1;
bench_large_index_scalability.cpp-2025-         }
bench_large_index_scalability.cpp-2152-         const std::filesystem::path temporaryDirectory = MakeTemporaryDirectory(scale);
bench_large_index_scalability.cpp-2153-         if (temporaryDirectory.empty()) {
bench_large_index_scalability.cpp:2154:             std::fprintf(stderr, "unable to create temporary directory for %zu\n", scale);
bench_large_index_scalability.cpp-2155-             return 1;
bench_large_index_scalability.cpp-2156-         }
bench_large_index_scalability.cpp-2160-         dataset.databaseLocation = database.wstring();
bench_large_index_scalability.cpp-2161-         ffindexstore::VolumeRowId volumeId = 0;
bench_large_index_scalability.cpp:2162:         if (!BuildSyntheticDatabase(database, scale, dataset.generationMs, volumeId,
bench_large_index_scalability.cpp-2163-                                      dataset.beforeFixture, dataset.afterFixture,
bench_large_index_scalability.cpp-2164-                                      options.detailedDiagnostics)) {
bench_large_index_scalability.cpp-2191-             dataset.snapshotExportMs = ElapsedMs(exportStart);
bench_large_index_scalability.cpp-2192-             dataset.actualSnapshotDirectoryCount = actualSnapshot.size();
bench_large_index_scalability.cpp:2193:             for (const auto& [path, directory] : actualSnapshot) {
bench_large_index_scalability.cpp-2194-                 UNREFERENCED_PARAMETER(path);
bench_large_index_scalability.cpp-2195-                 dataset.actualSnapshotEntryCount += directory.entries.size();
bench_large_index_scalability.cpp-2193-             for (const auto& [path, directory] : actualSnapshot) {
bench_large_index_scalability.cpp-2194-                 UNREFERENCED_PARAMETER(path);
bench_large_index_scalability.cpp:2195:                 dataset.actualSnapshotEntryCount += directory.entries.size();
bench_large_index_scalability.cpp-2196-             }
bench_large_index_scalability.cpp-2197-             ffengine::SnapshotPublisher::PublishTimings publishTimings;
bench_large_index_scalability.cpp-2362-         std::filesystem::create_directories(parent, error);
bench_large_index_scalability.cpp-2363-         if (error) {
bench_large_index_scalability.cpp:2364:             std::fprintf(stderr, "unable to create benchmark output directory\n");
bench_large_index_scalability.cpp-2365-             return 1;
bench_large_index_scalability.cpp-2366-         }
Store.cpp:16: // task 1.4: PRAGMA user_version is the stored schema-version marker.
Store.cpp:25:     "CREATE TABLE IF NOT EXISTS volumes ("
Store.cpp:38:     "CREATE TABLE IF NOT EXISTS entries ("
Store.cpp:55:     "CREATE INDEX IF NOT EXISTS idx_entries_parent ON entries(volume_id, parent_frn_low, parent_frn_high);";
Store.cpp:314:     Statement stmt(db_, "PRAGMA integrity_check;");
Store.cpp:369:     if (!ExecSql(db_, "PRAGMA journal_mode=WAL;") || !ExecSql(db_, "PRAGMA synchronous=NORMAL;")
Store.cpp:370:         || !ExecSql(db_, "PRAGMA foreign_keys=ON;")) {
Store.cpp:380:     Statement versionStmt(db_, "PRAGMA user_version;");
Store.cpp:387:         std::snprintf(pragma, sizeof(pragma), "PRAGMA user_version=%d;", kCurrentSchemaVersion);
Store.cpp:403:         std::snprintf(pragma, sizeof(pragma), "PRAGMA user_version=%d;", kCurrentSchemaVersion);
Store.cpp:457:     // (PRAGMA page_size) make the old approximation wrong by up to 8x. Query
Store.cpp:465:         if (sqlite3_prepare_v2(db_, "PRAGMA page_size;", -1, &stmt, nullptr) == SQLITE_OK) {
Store.cpp:1123:         !ExecSql(db_, "PRAGMA query_only=ON;")) {
read: src/indexstore/src/Store.cpp
#include "ffindexstore/Store.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>
#include "sqlite3.h"
namespace ffindexstore {
namespace {
// task 1.4: PRAGMA user_version is the stored schema-version marker.
// Version 2 (allocated-size-storage-accuracy D4) adds the nullable
// allocated_size_bytes column (NULL = unknown, never zero) via an in-place
// ALTER TABLE for version-1 databases; a fresh database is created at
// version 2 directly. A mismatched newer version is refused rather than
// guessed at.
constexpr int kCurrentSchemaVersion = 2;
constexpr char kSchemaSql[] =
    "CREATE TABLE IF NOT EXISTS volumes ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  volume_guid BLOB NOT NULL,"
    "  serial_number INTEGER NOT NULL,"
    "  available INTEGER NOT NULL DEFAULT 1,"
    "  last_seen_time INTEGER NOT NULL DEFAULT 0,"
    "  journal_id INTEGER,"
    "  resume_usn INTEGER NOT NULL DEFAULT 0,"
    "  scan_cursor BLOB,"
    "  scan_complete INTEGER NOT NULL DEFAULT 0,"
    "  last_reconciliation_time INTEGER NOT NULL DEFAULT 0,"
    "  UNIQUE(volume_guid, serial_number)"
    ");"
    "CREATE TABLE IF NOT EXISTS entries ("
    "  volume_id INTEGER NOT NULL REFERENCES volumes(id),"
    "  frn_low INTEGER NOT NULL,"
    "  frn_high INTEGER NOT NULL DEFAULT 0,"
    "  parent_frn_low INTEGER NOT NULL,"
    "  parent_frn_high INTEGER NOT NULL DEFAULT 0,"
    "  name TEXT NOT NULL,"
    "  size_bytes INTEGER NOT NULL DEFAULT 0,"
    // allocated-size-storage-accuracy D4: NULL == unknown. Zero is a valid
    // size and is stored as 0, never as NULL.
    "  allocated_size_bytes INTEGER,"
    "  creation_time INTEGER NOT NULL DEFAULT 0,"
    "  last_modified_time INTEGER NOT NULL DEFAULT 0,"
    "  last_access_time INTEGER NOT NULL DEFAULT 0,"
    "  attributes INTEGER NOT NULL DEFAULT 0,"
    "  PRIMARY KEY (volume_id, frn_low, frn_high)"
    ") WITHOUT ROWID;"
    "CREATE INDEX IF NOT EXISTS idx_entries_parent ON entries(volume_id, parent_frn_low, parent_frn_high);";
// allocated-size-storage-accuracy D4: version 1 -> 2 in-place migration.
// ALTER TABLE ADD COLUMN is metadata-only in SQLite (no table rewrite, no
// row churn); pre-existing rows read back as NULL == unknown until
// re-observed by a scan or reconciliation sweep. Runs inside the same
// WAL/integrity-checked open path.
constexpr char kMigrateV1ToV2Sql[] =
    "ALTER TABLE entries ADD COLUMN allocated_size_bytes INTEGER;";
uint64_t NowEpochSeconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
uint64_t DatabaseFileSize(const std::string& path) {
    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > std::numeric_limits<uint64_t>::max()) return 0;
    return static_cast<uint64_t>(size);
}
// Manual, dependency-free UTF-16 <-> UTF-8 conversion (surrogate-pair
// aware) so this library has no platform-specific text-conversion
// dependency (e.g. Win32's WideCharToMultiByte) and stays independently
// testable off-Windows, consistent with ffprotocol's own portable parsing
// layer (Records.h/SnapshotFormat.h use std::u16string/std::wstring
// directly rather than calling into Win32).
std::string Utf16ToUtf8(const std::u16string& in) {
    // Fast path: pure ASCII (common for file names) avoids per-codepoint
    // branching and surrogate checks; ~80% of typical NTFS names are ASCII.
    bool isAscii = true;
    for (char16_t ch : in) {
        if (ch > 0x7F) { isAscii = false; break; }
    }
    if (isAscii) {
        std::string out;
        out.resize(in.size());
        for (size_t idx = 0; idx < in.size(); ++idx) out[idx] = static_cast<char>(in[idx]);
        return out;
    }
    std::string out;
    out.reserve(in.size() * 3 / 2 + 4);
    size_t i = 0;
    while (i < in.size()) {
        char32_t codepoint = in[i];
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && i + 1 < in.size()) {
            const char16_t low = in[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        ++i;
        if (codepoint <= 0x7F) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
[1177 more lines in file. Use offset=121 to continue.]
grep: src/indexstore/src/Store.cpp
grep: src/indexstore/include/ffindexstore
Store.cpp:334: bool Store::RunIntegrityCheck() {
Store.cpp:338: bool Store::Open(const std::string& dbPathUtf8, bool* outIntegrityFailed) {
Store.cpp:422: void Store::Close() {
Store.cpp:433: std::unique_ptr<StoreReadSession> Store::OpenReadSession(
Store.cpp:441: bool Store::CheckpointPassive() {
Store.cpp:448: bool Store::CheckpointIfWalExceeds(uint64_t thresholdBytes) {
Store.cpp:481: bool Store::Analyze() {
Store.cpp:485: bool Store::VacuumIfDue(bool userIdle, uint64_t nowEpochSeconds, uint64_t intervalHours,
Store.cpp:511: std::optional<VolumeRowId> Store::GetOrCreateVolume(const VolumeKey& key, bool* outCreated) {
Store.cpp:580: std::optional<VolumeMetadata> Store::GetVolumeMetadata(VolumeRowId volumeRowId) {
Store.cpp:598: std::vector<VolumeMetadata> Store::GetAllVolumes() {
Store.cpp:602: std::vector<VolumeMetadata> Store::GetAllVolumesWithoutEntryCounts() {
Store.cpp:606: std::vector<VolumeMetadata> Store::GetAllVolumesImpl(const bool includeEntryCounts) {
Store.cpp:626: bool Store::SetVolumeAvailable(VolumeRowId volumeRowId, bool available, uint64_t nowTimestamp,
Store.cpp:647:     Statement stmt(db_, "UPDATE volumes SET available=?, last_seen_time=? WHERE id=?;");
Store.cpp:657: bool Store::SetJournalPosition(VolumeRowId volumeRowId, uint64_t journalId, uint64_t resumeUsn) {
Store.cpp:661:     Statement stmt(db_, "UPDATE volumes SET journal_id=?, resume_usn=? WHERE id=?;");
Store.cpp:671: bool Store::SetScanCursor(VolumeRowId volumeRowId, const std::vector<uint8_t>& cursor) {
Store.cpp:675:     Statement stmt(db_, "UPDATE volumes SET scan_cursor=? WHERE id=?;");
Store.cpp:688: bool Store::MarkScanComplete(VolumeRowId volumeRowId) {
Store.cpp:692:     Statement stmt(db_, "UPDATE volumes SET scan_complete=1, scan_cursor=NULL WHERE id=?;");
Store.cpp:700: bool Store::SetLastReconciliationTime(VolumeRowId volumeRowId, uint64_t nowTimestamp) {
Store.cpp:704:     Statement stmt(db_, "UPDATE volumes SET last_reconciliation_time=? WHERE id=?;");
Store.cpp:713: bool Store::ForgetVolume(VolumeRowId volumeRowId) {
Store.cpp:737:         Statement deleteEntries(db_, "DELETE FROM entries WHERE volume_id=?;");
Store.cpp:745:         Statement deleteVolume(db_, "DELETE FROM volumes WHERE id=?;");
Store.cpp:763: bool Store::ApplyBatch(VolumeRowId volumeRowId, const std::vector<EntryChange>& changes) {
Store.cpp:779:         "ON CONFLICT(volume_id, frn_low, frn_high) DO UPDATE SET "
Store.cpp:790:     Statement remove(db_, "DELETE FROM entries WHERE volume_id=? AND frn_low=? AND frn_high=?;");
Store.cpp:805:             // that were not present"). The UPDATE arm already guards via the
Store.cpp:876: bool Store::ForEachEntry(VolumeRowId volumeRowId, const EntryVisitor& visitor) {
Store.cpp:911: bool Store::ForEachEntryUntil(VolumeRowId volumeRowId, const EntryVisitorUntil& visitor) {
Store.cpp:949: std::optional<EntryRecord> Store::FindEntry(VolumeRowId volumeRowId, FileId frn) {
Store.cpp:983: std::vector<FileId> Store::ListEntryIds(VolumeRowId volumeRowId) {
Store.cpp:999: uint64_t Store::CountEntries(VolumeRowId volumeRowId) {
Store.h:41: struct VolumeMetadata {
EntryRecord.h:14: struct EntryRecord {
Identity.h:12: struct FileId {
Identity.h:45: using VolumeRowId = int64_t;
Identity.h:49: struct EntryKey {
Identity.h:59: struct EntryKeyHash {
NamePool.h:9: using NameId = uint32_t;
read: src/indexstore/include/ffindexstore/Identity.h
grep: openspec/changes/large-index-lifecycle-and-search-scalabi...
#pragma once
#include <array>
#include <cstdint>
#include <functional>
namespace ffindexstore {
// A file's identity within its volume. Wide enough for both NTFS's 64-bit
// FileReferenceNumber (high == 0) and ReFS's 128-bit file identifier
// (design.md D2/D7, tasks.md 1.2). Never compared or hashed across volumes
// without pairing with a VolumeRowId -- see EntryKey below.
struct FileId {
    uint64_t low = 0;
    uint64_t high = 0;
};
constexpr bool operator==(const FileId& a, const FileId& b) noexcept {
    return a.low == b.low && a.high == b.high;
}
constexpr bool operator!=(const FileId& a, const FileId& b) noexcept { return !(a == b); }
constexpr bool operator<(const FileId& a, const FileId& b) noexcept {
    return a.high != b.high ? a.high < b.high : a.low < b.low;
}
// The durable, cross-restart volume identity (design.md D6): volume GUID
// plus cached serial number. Distinct from the wire protocol's ephemeral,
// connection-scoped VolumeId (ffprotocol::VolumeId), which the engine maps
// to this identity on every EnumerateVolumes call.
struct VolumeKey {
    std::array<uint8_t, 16> volumeGuid{};
    uint32_t serialNumber = 0;
};
constexpr bool operator==(const VolumeKey& a, const VolumeKey& b) noexcept {
    return a.volumeGuid == b.volumeGuid && a.serialNumber == b.serialNumber;
}
constexpr bool operator!=(const VolumeKey& a, const VolumeKey& b) noexcept { return !(a == b); }
// The durable store's small, dense, per-volume row id (SQLite `volumes.id`).
// Stable for the lifetime of the database (never reused across a
// forget-volume deletion), used everywhere in the projection and ingestion
// pipeline in place of the bulkier VolumeKey (design.md D7's "(volume
// identity, FileReferenceNumber)" pairing uses this as the volume identity
// half).
using VolumeRowId = int64_t;
// Identifies one entry uniquely across the whole store: (volume, FRN) --
// design.md D7. Never a path string.
struct EntryKey {
    VolumeRowId volumeRowId = 0;
    FileId frn;
};
constexpr bool operator==(const EntryKey& a, const EntryKey& b) noexcept {
    return a.volumeRowId == b.volumeRowId && a.frn == b.frn;
}
constexpr bool operator!=(const EntryKey& a, const EntryKey& b) noexcept { return !(a == b); }
struct EntryKeyHash {
    size_t operator()(const EntryKey& key) const noexcept {
        // address-engineering-audit-findings 3.2: FlatHashMap bounds linear
        // probing at 64 steps, so the in-memory identity hash must retain a
        // well-distributed low-bit pattern for power-of-two bucket counts.
        uint64_t h = static_cast<uint64_t>(key.volumeRowId);
        h ^= key.frn.low + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= key.frn.high + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= h >> 30;
        h *= 0xbf58476d1ce4e5b9ULL;
        h ^= h >> 27;
        h *= 0x94d049bb133111ebULL;
        h ^= h >> 31;
        return static_cast<size_t>(h);
    }
};
} // namespace ffindexstore
proposal.md:24:   158,877.3 / 158,252.3 / 164,348.8 ms (~39x); 200k 2,444.8 ms vs 3,856-5,085 ms.
proposal.md:25: - **Root cause (differential isolation matrix, arms A-G):** per-entry
proposal.md:26:   filename-accelerator removal (`EraseSorted` posting erase, O(posting length)
proposal.md:31:   once at the end -- so the per-entry work is pure quadratic waste.
proposal.md:33:   path, not the pathology). The same quadratic primitives are production-reachable
proposal.md:40:   per-entry path. Result: 1M replacement cycles 158,877/158,252/164,349 ms ->
design.md:6: therefore removes the wasted per-entry work instead of restructuring ownership:
design.md:11:   (`RebuildFilenameIndex`, O(N)); below the threshold the per-entry path keeps the
design.md:18: - `EvictSubtree`/`EvictVolume` skip per-entry erases above the threshold; their
design.md:24: see D2); per-entry removal retained (the measured 40x pathology); unconditional
design.md:61:   name's folded trigram multiset, so the posting intersection can never drop a true
design.md:70: (`kMaximumFilenameIndexCandidates`): queries whose smallest posting already exceeds
specs/index-engine/spec.md:5: ### Requirement: Bulk removal shall replace per-entry accelerator maintenance for large removals
specs/index-engine/spec.md:9: invalidation and at most one bulk rebuild, instead of per-entry posting removal.
specs/index-engine/spec.md:11: reconstruction, name ownership, LRU slots) SHALL be identical to per-entry removal.
specs/index-engine/spec.md:12: Small batches SHALL keep the per-entry path so the accelerator stays current
specs/index-engine/spec.md:20: - **AND** removal no longer performs per-entry trigram posting erases
specs/index-engine/spec.md:32: For a pure-ASCII folded needle of at least three units whose trigram posting
specs/index-engine/spec.md:37: and non-ASCII names' folded trigrams are indexed, so the posting intersection is
specs/index-engine/spec.md:41: and queries whose smallest posting already exceeds the cap SHALL remain on the
tasks.md:13: - [x] 2.2 Classify the repeated-rebuild path: the benchmark's repeated `RebuildAll` is BENCHMARK_ONLY as exercised; its dominant primitive (per-entry `Remove` with trigram erase inside `RemoveVolume`/mass-removal) is `PRODUCTION_REACHABLE` through `ForgetVolume`, `FinishReconciliationPass`, rule-prune loops, `EnforceMemoryLimitNow` eviction, and `ReloadEvictedVolume`. Startup `RebuildAll` is the empty-projection fast path.
tasks.md:19: - [x] 3.3 Record rows/sec and per-arm state (filename-index validity, eviction state, rebuild verdict) so every number is self-explaining. Classify: trigram per-entry erase = 99.7% of removal cost; child unlink and id-map residual are small; silent `RebuildFilenameIndex` invalidation under allocation pressure is a recorded robustness finding.
tasks.md:23: - [x] 4.1 Implement the narrowest safe bulk-removal fix: `Projection::RemoveBatch` (wholesale accelerator invalidation + one bulk rebuild above `kPerEntryFilenameRemovalThreshold = 256`; per-entry path below), `RemoveVolume(vol, keepFilenameIndexCurrent)` routed through it, `RebuildVolumeFromStore` skipping the double rebuild, `EvictSubtree`/`EvictVolume` skipping per-entry erases (their trailing `CompactStorage` invalidates anyway). No double-buffer; memory-limit/LRU/eviction/generation semanti... [truncated]
tasks.md:37: - [x] 6.3 Implemented: candidate set = posting intersection alone (union removed; `243199e`), red-first via `TestFilenameCandidatesNarrowForSelectiveNeedlesDespiteUnicodeRows`. Cost-based selection = the existing cap gate (smallest posting / candidate set above `kMaximumFilenameIndexCandidates` falls back to the canonical scan; broad classes measured on the scan path).
evidence/explore-findings.md:16:   164,348.8 ms. 200k: 2,444.8 vs 3,856-5,085 ms.
evidence/explore-findings.md:20:   (99.7% of removal cost is per-entry trigram posting erase); G1 no-trigram
evidence/explore-findings.md:60: the posting intersection alone — `FoldAscii` is length-preserving, non-ASCII names'
evidence/review-remediation-2026-10-21.md:48:   posting intersection, cap-gated as before.
evidence/rebuild/memory-report-after-fix.json:21:   ,"scales":[{"scale":200000,"directory_count":3125,"file_count":196874,"bound_directories":4096,"rebuild_cycle_target":4,"database_bytes":15085568,"durable_volume_row_id":1,"generation_ms":2404.7960,"executed":true,"failure_reason":"","payload_identical":true,"rebuild_cycles":[{"name":"after_rebuild_cycle_1","elapsed_ms":823.5751,"working_set_bytes":108920832,"private_bytes":103403520,"peak_working_set_bytes":145297408,"peak_private_bytes":155893760,"allocator":{"live_requested_bytes":76024671,... [truncated]
evidence/rebuild/memory-report-before.json:21:   ,"scales":[{"scale":200000,"directory_count":3125,"file_count":196874,"bound_directories":4096,"rebuild_cycle_target":4,"database_bytes":15085568,"durable_volume_row_id":1,"generation_ms":2218.2362,"executed":true,"failure_reason":"","payload_identical":true,"rebuild_cycles":[{"name":"after_rebuild_cycle_1","elapsed_ms":2444.8243,"working_set_bytes":108871680,"private_bytes":103206912,"peak_working_set_bytes":145260544,"peak_private_bytes":154992640,"allocator":{"live_requested_bytes":76024719... [truncated]
evidence/rebuild/rebuild-isolation-after-fix.json:38:           "notes": "RemoveVolume on populated projection (per-entry Remove, production semantics)",
evidence/rebuild/rebuild-isolation-after-fix.json:102:           "notes": "RemoveVolume with per-entry trigram/non-ASCII accelerator removal skipped",
evidence/rebuild/rebuild-isolation-after-fix.json:177:           "notes": "RemoveVolume on populated projection (per-entry Remove, production semantics)",
evidence/rebuild/rebuild-isolation-after-fix.json:241:           "notes": "RemoveVolume with per-entry trigram/non-ASCII accelerator removal skipped",
evidence/rebuild/rebuild-isolation-before.json:38:           "notes": "RemoveVolume on populated projection (per-entry Remove, production semantics)",
evidence/rebuild/rebuild-isolation-before.json:102:           "notes": "RemoveVolume with per-entry trigram/non-ASCII accelerator removal skipped",
evidence/rebuild/rebuild-isolation-before.json:134:           "notes": "RemoveVolume with per-entry parent/child unlink skipped (trigram removal retained)",
evidence/rebuild/rebuild-isolation-before.json:150:           "notes": "Upsert population with <=2-char names (no trigram postings, single NameId per kind)",
evidence/rebuild/rebuild-isolation-before.json:225:           "notes": "RemoveVolume on populated projection (per-entry Remove, production semantics)",
evidence/rebuild/rebuild-isolation-before.json:289:           "notes": "RemoveVolume with per-entry trigram/non-ASCII accelerator removal skipped",
evidence/rebuild/rebuild-isolation-before.json:321:           "notes": "RemoveVolume with per-entry parent/child unlink skipped (trigram removal retained)",
evidence/rebuild/rebuild-isolation-before.json:337:           "notes": "Upsert population with <=2-char names (no trigram postings, single NameId per kind)",
evidence/search/search-matrix-after-rebuild-fix-control.json:48:     {"dataset_size":200000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":2,"result_count":0,"candidate_count":200000,"candidates_visited":0,"truncated":false,"cancelled":false,"unavailable":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
evidence/search/search-matrix-after-rebuild-fix-control.json:89:     {"dataset_size":200000,"query_class":"exact_name","query":"unique_component_7391.txt","temperature":"warm","iteration":3,"result_count":1,"candidate_count":200000,"candidates_visited":1,"truncated":false,"cancelled":false,"unavailable":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attemp... [truncated]
evidence/search/search-matrix-planner-fix.json:30:     {"dataset_size":200000,"query_class":"moderate","query":"report","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":200000,"candidates_visited":13915,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durabl... [truncated]
evidence/search/search-matrix-planner-fix.json:56:     {"dataset_size":200000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":0,"result_count":4096,"candidate_count":200000,"candidates_visited":13967,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable_f... [truncated]
[Some lines truncated to 500 chars. Use read tool to see full lines]
read: openspec/changes/large-index-lifecycle-and-search-scalabi...
read: openspec/changes/large-index-lifecycle-and-search-scalabi...
# Design: large-index-lifecycle-and-search-scalability
## D1. Bulk removal, not double-buffer replacement
The measured defect is incremental destruction cost, not construction cost. The fix
therefore removes the wasted per-entry work instead of restructuring ownership:
- `Projection::RemoveBatch(volumeRowId, frns, keepFilenameIndexCurrent)` above
  `kPerEntryFilenameRemovalThreshold = 256` drops the filename accelerator wholesale
  (`InvalidateFilenameIndex`, O(1)) and rebuilds it exactly once at the end
  (`RebuildFilenameIndex`, O(N)); below the threshold the per-entry path keeps the
  accelerator hot without a full rebuild. Child unlink, id-map erase, and free-list
  maintenance stay per entry: they are O(1) each (measured residual < 0.7 us/row).
- `RemoveVolume(volumeRowId, keepFilenameIndexCurrent = true)` routes through
  `RemoveBatch`; the default preserves the external contract (accelerator current
  afterwards). `RebuildVolumeFromStore` passes `false` because its own end-of-rebuild
  `RebuildFilenameIndex` covers the accelerator (the measured double-rebuild waste).
- `EvictSubtree`/`EvictVolume` skip per-entry erases above the threshold; their
  trailing `CompactStorage` invalidates the accelerator anyway, and the
  memory-pressure path must not allocate a rebuild.
Rejected alternatives: fresh-`Projection`-and-swap (D-arm measurement shows a
double-buffer peak of ~1 GB private at 1M rows -- unacceptable at the default budget,
see D2); per-entry removal retained (the measured 40x pathology); unconditional
rebuild-on-every-small-batch (O(N) regression for single USN deletes).
## D2. Memory semantics preserved by construction
The fix is in-place: no second projection, no peak change (after-fix memory report:
private ~461-477 MB, live requested 345.9/349.9 MB -- identical to before). The
memory-limit policy, LRU timestamps, eviction, and `EnforceMemoryLimit` watermark
semantics are untouched. Finding recorded for Phase 8: a 1M-row projection straddles
the default 500 MB budget (modeled ~414 MB vs the 400 MB low watermark in the
raw-projection harness), so rebuilds can deterministically end in eviction +
accelerator invalidation; `SetProjectionMemoryBudget` has no production caller. That
is a product memory-policy question, deliberately NOT bundled into this fix.
## D3. Benchmark-only seams stay out of production
`FF_INDEXSTORE_TESTING` compiles the `RemovalMaintenanceMode` attribution seam into
the isolation benchmark target's `Projection.cpp` only; production builds contain no
seam (the mode checks compile to constant false). The isolation matrix is
informational and never a CTest gate.
## D4. Search planner: conservative candidates without the false-negative risk
`FilenameCandidateIndices` currently unions every non-ASCII-named row into every
candidate set. Since the fixture's 10% Unicode population alone exceeds
`kMaximumFilenameIndexCandidates` (16,384) at 200k+ rows, every lookup returns
nullopt and the accelerator is dead code at scale.
Correctness argument for removing the unconditional union (to be proven by the
mandatory differential suite before it lands):
- `FoldAscii` is length-preserving (one output unit per input unit; only ASCII
  A-Z is lowered), so trigram windows align between a name and its fold.
- Non-ASCII names' folded trigrams ARE indexed (`AddFilenameIndexEntry` indexes the
  fold of every name; the non-ASCII list is a separate structure).
- A pure-ASCII needle of length >= 3 that occurs in a name (fold-matched for ASCII
  names, ordinally for non-ASCII names) has all its folded trigrams present in that
  name's folded trigram multiset, so the posting intersection can never drop a true
  match. The non-ASCII union can only add false positives.
The candidate set stays conservative; the canonical matcher still verifies every
candidate; hot/cold candidate identity and ordering equivalence (the pinned spec
contract) is unaffected because both paths verify through the same matcher.
Eligibility/cost-based selection: keep the existing gates (simple single literal,
folded length >= 3, every searched volume resident) and the existing cap
(`kMaximumFilenameIndexCandidates`): queries whose smallest posting already exceeds
the cap correctly fall back to the canonical scan (broad classes) -- the planner
gains come from enabling the clearly selective classes (selective, exact, no-match),
not from forcing the index where candidate sets are broad.
## D5. Differential testing is mandatory (spec contract)
Every accelerated path must reproduce the reference result set exactly across the
full matrix: sort fields, directions, capped/unbounded, volume roots, Unicode,
mixed case, empty term, dotted filenames, extension, ties, path sort, cancellation,
stale query replacement. The superset property is tested directly against a
brute-force canonical scan over Unicode-heavy fixtures; end-to-end equivalence
reuses the hot/cold differential harness (`TestForcedEvictionSearchEquivalence`
family) plus a focused ASCII-needle-on-Unicode-fixture case.
## D6. Evidence and identity discipline
Before/after use the same harness, fixture, and command line for exact
comparability. Report rows/second and operation counts alongside wall time. Keep
`implementation_sha`, `certified_binary_sha`, `evidence_sha`, and
`repository_tip_sha` separate; regenerate final artifacts at a clean embedded
identity; never claim reproducible-binary equivalence. Benchmark claims are
single-machine wall-clock; comparisons are strongest within one report.
# Evidence: large-index-lifecycle-and-search-scalability
All measurements: release-test (RelWithDebInfo) on one idle Windows host; wall-clock
single-run arms where noted; machine metadata embedded in each JSON. Comparisons are
strongest within one report/harness. Identity nuance (recorded, not hidden): the
before artifacts embed `4415926`/`54feddd` (clean/dirty build labels) and the
after-fix artifacts embed `54feddd` with `--source-sha 2e0d819` — the binaries were
built from the working tree that became commit `2e0d819` (product tree identical);
final certification regenerates artifacts at a clean embedded identity.
## rebuild/
- `memory-report-before.json` — reproduction on current `main` product tree
  (`ffengine_bench_large_index_scalability --memory-report`, 200k+1M, 4 cycles).
  1M: first build 4,095.8 ms vs replacement cycles 158,877.3 / 158,252.3 /
  164,348.8 ms. 200k: 2,444.8 vs 3,856-5,085 ms.
- `rebuild-isolation-before.json` — differential isolation matrix (arms A-G,
  `FF_INDEXSTORE_TESTING` seam). 200k clean attribution: B1 production removal
  10,225.2 ms vs E1 removal without filename-accelerator maintenance 28.7 ms
  (99.7% of removal cost is per-entry trigram posting erase); G1 no-trigram
  residual 55.1 ms. Self-explaining fields (index validity, eviction state, rebuild
  verdict) — the 1M arms record the default-budget straddle (modeled 413.8MB >
  400MB watermark: rebuilds end evicted with the accelerator invalidated).
- `memory-report-after-fix.json` — same harness/command after the bulk-removal fix.
  1M cycles 4,715.7 / 5,100.8 / 4,772.3 ms (**31-34x**, at first-build cost
  4,686.9 ms); 200k 823-872 ms (4.4-5.8x). Memory unchanged (private ~461-477MB;
  live requested 345.9/349.9MB).
- `rebuild-isolation-after-fix.json` — arm-level: B1 22.0 ms at 200k (**465x**);
  all build phases at the fresh-build floor.
- `../search/search-matrix-after.json` — **post-rebuild-fix control** (same harness
  as the search baseline). Result sets are EXACTLY equal to the baseline for all
  24 class x scale pairs (the rebuild fix changes no search semantics). Honest
  attribution note: this run used a release-test binary built before the planner
  fix (only Debug carried it at that point), so its `filename_index_used=false`
  rows are NOT evidence about the planner fix — they are the stale-binary control.
  The planner-fix matrix is a separate run (`search-matrix-planner.json`).
## search/
- `search-matrix-before.json` — 12 query classes x 200k/1M/2M, hot + cold,
  `--diagnostics`. Hot p50 (200k/1M/2M ms): selective 51/303/2,533; no_match
  52/307/2,538; exact_name 53/314/2,520; moderate 108/525/10,958; broad
  126/603/27,093; extension 107/410/37,578; worst_substring 124/523/19,591;
  prefix 110/532/10,762; short_prefix 139/573/29,272; mixed_case 133/579/21,120;
  unicode 122/584/11,849; high_limit_broad 127/560/28,669.
  - Scaling law: 200k->1M ≈ linear (x4-6 for x5 rows, all classes O(N) scan);
    1M->2M is a REGIME CHANGE, not algorithmic superlinearity: every 2M warm run
    shows `cold_rows_examined = 2,000,000` — the 2M projection (modeled 839.7MB,
    measured at forced eviction) exceeds the 500MB default budget, so all queries
    serve from the SQLite cold stream (2.6-37.6s).
  - `filename_index_used = false` for all 12 classes at every scale (structural —
    see the planner root cause below).
## Planner root cause (search scaling)
`FilenameCandidateIndices` unconditionally unioned `filenameNonAsciiEntries_` into
every candidate set. The fixture's ~10% Unicode rows alone (19.7k at 200k, 100k at
1M) exceed `kMaximumFilenameIndexCandidates = 16,384`, so every lookup returned
nullopt regardless of query shape. Fix (commit in this change): the candidate set is
the posting intersection alone — `FoldAscii` is length-preserving, non-ASCII names'
folded trigrams are indexed, so the intersection is already a conservative superset
(verified by `TestFilenameCandidatesAreConservativeSuperset` across ASCII/Unicode/
mixed-case/dotted shapes and the structural eligibility rules);
`TestFilenameCandidatesNarrowForSelectiveNeedlesDespiteUnicodeRows` reproduces the
cap pathology red-first and passes after the fix. Broad classes keep the canonical
scan by the existing cap gate (no forced index use).
## Test repairs discovered (red-first, `c261da4`)
- `Test-HookGuard.ps1`: `Start-Process -ArgumentList` path quoting — any checkout
  path with a space split `-File`; children exited 64; 9 checks failed; fixed at
  script scope (both call sites).
- `ffquick_actions_tests`: session-level clipboard read interception proven by raw
  Win32 probe (SetClipboardData sticks; GetClipboardData null for every format incl.
  raw CF_UNICODETEXT; OLE path fine) — round-trip block now discriminates genuine
  data loss (fails loud) from read interception (loud skip).
## Validation at the fix tree
Debug CTest 127/127 and release-test CTest 127/127 were green at `2e0d819`+`c261da4`
(two suites then repaired; identity-binding test compares the Debug preset's
binaries against current HEAD — Debug rebuilt at `c261da4`). Focused
indexstore/engine/search suites 18/18 green after the planner fix; full-suite
re-run at the planner-fix tree in progress at handoff.
## Planner-fix matrix (`search/search-matrix-planner-fix.json`, release-test at `243199e`)
Selective classes now use the accelerator (`filename_index_used=true`, rows
examined collapse from N to the candidate set); broad classes keep the canonical
scan by the cap gate (correct per the cost model). Result sets EXACTLY equal to
the baseline for all 24 class x scale pairs (zero regressions).
| class | 200k before -> after | 1M before -> after | rows examined (1M) |
| --- | --- | --- | --- |
| selective | 50.9 -> 1.2 ms (44x) | 302.5 -> 1.6 ms (189x) | 1 (was 1,000,000) |
| exact_name | 53.1 -> 1.0 ms (52x) | 313.8 -> 2.8 ms (114x) | 1 |
| no_match | 51.6 -> 1.5 ms (35x) | 307.4 -> 5.5 ms (56x) | 15,625 (was 1,000,000) |
Scan-class medians moved 0.6-1.0x across runs (uniform session variance: the
control run measured them at 1.0-1.3x of baseline with a code-identical scan
path; no scan regression is claimed or hidden). `no_match` candidates (15,625)
are trigram false positives (directory-count-shaped intersection), verified
empty of matches by the canonical matcher.
Acceptance (§17): linear scanning avoided for the clearly selective classes
(O(K) candidate verification replaces O(N) scans; 56-189x at 1M); no result-set,
Unicode, or cancellation regressions (24/24 exact result equality; the existing
cancellation/Unicode suites green).
grep: openspec/changes/large-index-lifecycle-and-search-scalabi...
read: engine/src/IndexPipeline.cpp
memory-report-after-fix.json:7:   ,"requested_scales":[200000,1000000]
memory-report-after-fix.json:8:   ,"command_line":"C:\\Users\\Michael Roy\\Documents\\FastFiles\\build\\release-test\\tests\\engine\\ffengine_bench_large_index_scalability.exe --memory-report build/campaign/phase-a/memory-report-after-fix.json --scales 200000,1000000 --rebuild-cycles 4 --source-state clean --source-sha 2e0d819 --preset release-test --diagnostics"
memory-report-after-fix.json:21:   ,"scales":[{"scale":200000,"directory_count":3125,"file_count":196874,"bound_directories":4096,"rebuild_cycle_target":4,"database_bytes":15085568,"durable_volume_row_id":1,"generation_ms":2404.7960,"executed":true,"failure_reason":"","payload_identical":true,"rebuild_cycles":[{"name":"after_rebuild_cycle_1","elapsed_ms":823.5751,"working_set_bytes":108920832,"private_bytes":103403520,"peak_working_set_bytes":145297408,"peak_private_bytes":155893760,"allocator":{"live_requested_bytes":76024671,... [truncated]
[Some lines truncated to 500 chars. Use read tool to see full lines]
ENOENT: no such file or directory, access 'C:\Users\Michael Roy\Documents\FastFiles\engine\src\IndexPipeline.cpp'
grep: src/engine/src/IndexPipeline.cpp
grep: docs/benchmarks
IndexPipeline.cpp-367- // (StoreReadSession::ForEachEntryWithAsciiNameUntil): LIKE folds exactly the
IndexPipeline.cpp-368- // ASCII case the superset proof needs and the query's own fold is already
IndexPipeline.cpp-369- // ASCII-lowercase. Non-ASCII needles return nullopt and keep the full stream
IndexPipeline.cpp-370- // because SQLite LIKE cannot fold non-ASCII case variants.
IndexPipeline.cpp-371- std::optional<std::string> ColdNamePrefilterNeedle(const ffsearch::Query& query) {
IndexPipeline.cpp-372-     if (!query.simpleLiteralName) return std::nullopt;
IndexPipeline.cpp:373:     const std::u16string& folded = query.simpleLiteralFolded;
IndexPipeline.cpp-374-     if (folded.size() < 3) return std::nullopt;
IndexPipeline.cpp-375-     std::string needle;
IndexPipeline.cpp-376-     needle.reserve(folded.size());
IndexPipeline.cpp-377-     for (const char16_t ch : folded) {
IndexPipeline.cpp-378-         if (ch > 0x7f) return std::nullopt;
IndexPipeline.cpp-379-         needle.push_back(static_cast<char>(ch));
IndexPipeline.cpp-1495-                     readerFailures_.fetch_add(1, std::memory_order_relaxed);
IndexPipeline.cpp-1496-                     unavailable = true;
IndexPipeline.cpp-1497-                 } else {
IndexPipeline.cpp-1498-                     if (prepareProjection) {
IndexPipeline.cpp-1499-                         context.cursor = projection_.BeginEntryCursor();
IndexPipeline.cpp-1500-                         const bool filenameEligible = searchRequest.query.simpleLiteralName &&
IndexPipeline.cpp:1501:                             searchRequest.query.simpleLiteralFolded.size() >= 3 && context.coldVolumes.empty();
IndexPipeline.cpp-1502-                         if (filenameEligible) {
IndexPipeline.cpp-1503-                             context.filenameCandidates = projection_.FilenameCandidateIndices(
IndexPipeline.cpp-1504-                                 searchRequest.query.simpleLiteralFolded, kMaximumFilenameIndexCandidates);
IndexPipeline.cpp-1505-                         }
IndexPipeline.cpp-1506-                         if (timings != nullptr) {
IndexPipeline.cpp-1507-                             timings->filenameIndexUsed = context.filenameCandidates.has_value();
IndexPipeline.cpp-1497-                 } else {
IndexPipeline.cpp-1498-                     if (prepareProjection) {
IndexPipeline.cpp-1499-                         context.cursor = projection_.BeginEntryCursor();
IndexPipeline.cpp-1500-                         const bool filenameEligible = searchRequest.query.simpleLiteralName &&
IndexPipeline.cpp-1501-                             searchRequest.query.simpleLiteralFolded.size() >= 3 && context.coldVolumes.empty();
IndexPipeline.cpp-1502-                         if (filenameEligible) {
IndexPipeline.cpp:1503:                             context.filenameCandidates = projection_.FilenameCandidateIndices(
IndexPipeline.cpp-1504-                                 searchRequest.query.simpleLiteralFolded, kMaximumFilenameIndexCandidates);
IndexPipeline.cpp-1505-                         }
IndexPipeline.cpp-1506-                         if (timings != nullptr) {
IndexPipeline.cpp-1507-                             timings->filenameIndexUsed = context.filenameCandidates.has_value();
IndexPipeline.cpp-1508-                             timings->filenameIndexCandidateCount = context.filenameCandidates.has_value()
IndexPipeline.cpp-1509-                                 ? context.filenameCandidates->size() : 0;
IndexPipeline.cpp-1498-                     if (prepareProjection) {
IndexPipeline.cpp-1499-                         context.cursor = projection_.BeginEntryCursor();
IndexPipeline.cpp-1500-                         const bool filenameEligible = searchRequest.query.simpleLiteralName &&
IndexPipeline.cpp-1501-                             searchRequest.query.simpleLiteralFolded.size() >= 3 && context.coldVolumes.empty();
IndexPipeline.cpp-1502-                         if (filenameEligible) {
IndexPipeline.cpp-1503-                             context.filenameCandidates = projection_.FilenameCandidateIndices(
IndexPipeline.cpp:1504:                                 searchRequest.query.simpleLiteralFolded, kMaximumFilenameIndexCandidates);
IndexPipeline.cpp-1505-                         }
IndexPipeline.cpp-1506-                         if (timings != nullptr) {
IndexPipeline.cpp-1507-                             timings->filenameIndexUsed = context.filenameCandidates.has_value();
IndexPipeline.cpp-1508-                             timings->filenameIndexCandidateCount = context.filenameCandidates.has_value()
IndexPipeline.cpp-1509-                                 ? context.filenameCandidates->size() : 0;
IndexPipeline.cpp-1510-                         }
large-index-scalability.md:436: The measured UTF-16 trigram posting design is now integrated into the production indexed-search path. The new historical artifact is [large-index-scalability-secondary-index.json](large-index-scalability-secondary-index.json); the original `large-index-scalability.json` remains unchanged so the optimization history is preserved.
large-index-scalability.md:444: → eligible simple-literal UTF-16 trigram candidate lookup
large-index-scalability.md:466: `Projection` owns the postings beside its generation-owned dense slots. Rebuilds bulk-load and sort/deduplicate postings after compaction; insert, delete, and rename update postings incrementally. Allocation or update failure invalidates only the accelerator, and search falls back to the canonical linear path. Focused tests cover duplicate/uppercase/non-ASCII recall, empty no-match, short and Unicode fallback, delete, and rename behavior; engine tests cover eligible, no-match, short-term, and fo... [truncated]
large-index-scalability.md:468: The final artifact reports private bytes after rebuild of approximately `59.3/280.7/551.9/1120.6 MiB` at `100K/500K/1M/2M`, with post-query values of `61.3/292.7/565.4/1294.2 MiB`. The isolated architecture benchmark estimated 168.6 MiB of trigram postings at 2M; the integrated process also retains the existing SQLite/projection/snapshot working set. The 2M rebuild took approximately 5.0 seconds and the temporary database occupied 150,564,864 bytes. No schema migration or persistent secondary-in... [truncated]
large-index-scalability.md:487: At 2M, the eligible selective query still used one trigram candidate and the no-match query used zero; SQLite metadata timing was approximately `0.10 ms`, down from approximately `65–68 ms`. Broad search retained the existing 4,096-result cap and used 590,624 candidates. The result is now below the low-tens-of-milliseconds engineering target for eligible selective/no-match queries through 2M; no further SQL redesign is justified by this measurement.
large-index-scalability.md:566: | hot selective | 1.49 ms | 0 | bounded projection/trigram path |
large-index-scalability.json:13:     {"dataset_size":10000,"query_class":"selective","query":"unique_component_7391","temperature":"cold-ish-first-after-rebuild","iteration":0,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.1238,"projection_traversal_ms":0.4309,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":... [truncated]
large-index-scalability.json:14:     {"dataset_size":10000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":0,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0707,"projection_traversal_ms":0.1627,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0438,"lightweight_cand... [truncated]
large-index-scalability.json:15:     {"dataset_size":10000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":1,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0615,"projection_traversal_ms":0.2101,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0375,"lightweight_cand... [truncated]
large-index-scalability.json:16:     {"dataset_size":10000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":2,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0528,"projection_traversal_ms":0.1489,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0383,"lightweight_cand... [truncated]
large-index-scalability.json:17:     {"dataset_size":10000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":3,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0507,"projection_traversal_ms":0.1322,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0227,"lightweight_cand... [truncated]
large-index-scalability.json:18:     {"dataset_size":10000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":4,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0494,"projection_traversal_ms":0.1323,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0221,"lightweight_cand... [truncated]
large-index-scalability.json:19:     {"dataset_size":10000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":5,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0635,"projection_traversal_ms":0.2093,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0352,"lightweight_cand... [truncated]
large-index-scalability.json:20:     {"dataset_size":10000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":6,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0824,"projection_traversal_ms":0.1360,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0231,"lightweight_cand... [truncated]
large-index-scalability.json:21:     {"dataset_size":10000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":7,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0488,"projection_traversal_ms":0.1298,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0216,"lightweight_cand... [truncated]
large-index-scalability.json:22:     {"dataset_size":10000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":8,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0468,"projection_traversal_ms":0.1463,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0334,"lightweight_cand... [truncated]
large-index-scalability.json:23:     {"dataset_size":10000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":9,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0655,"projection_traversal_ms":0.1327,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0224,"lightweight_cand... [truncated]
large-index-scalability.json:24:     {"dataset_size":10000,"query_class":"moderate","query":"report","temperature":"warm","iteration":0,"result_count":985,"candidate_count":985,"candidates_visited":985,"truncated":false,"wire_result_count":985,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":985,"sqlite_ms":0.1861,"projection_traversal_ms":53.4706,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":27.1690,"lightweight_candidat... [truncated]
large-index-scalability.json:25:     {"dataset_size":10000,"query_class":"moderate","query":"report","temperature":"warm","iteration":1,"result_count":985,"candidate_count":985,"candidates_visited":985,"truncated":false,"wire_result_count":985,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":985,"sqlite_ms":0.1846,"projection_traversal_ms":55.7347,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":29.3443,"lightweight_candidat... [truncated]
large-index-scalability.json:26:     {"dataset_size":10000,"query_class":"moderate","query":"report","temperature":"warm","iteration":2,"result_count":985,"candidate_count":985,"candidates_visited":985,"truncated":false,"wire_result_count":985,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":985,"sqlite_ms":0.1603,"projection_traversal_ms":46.3007,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":21.6238,"lightweight_candidat... [truncated]
large-index-scalability.json:27:     {"dataset_size":10000,"query_class":"moderate","query":"report","temperature":"warm","iteration":3,"result_count":985,"candidate_count":985,"candidates_visited":985,"truncated":false,"wire_result_count":985,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":985,"sqlite_ms":0.1279,"projection_traversal_ms":37.7589,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":18.7763,"lightweight_candidat... [truncated]
large-index-scalability.json:28:     {"dataset_size":10000,"query_class":"moderate","query":"report","temperature":"warm","iteration":4,"result_count":985,"candidate_count":985,"candidates_visited":985,"truncated":false,"wire_result_count":985,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":985,"sqlite_ms":0.1151,"projection_traversal_ms":54.4666,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":26.8427,"lightweight_candidat... [truncated]
large-index-scalability.json:29:     {"dataset_size":10000,"query_class":"moderate","query":"report","temperature":"warm","iteration":5,"result_count":985,"candidate_count":985,"candidates_visited":985,"truncated":false,"wire_result_count":985,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":985,"sqlite_ms":0.1564,"projection_traversal_ms":50.6588,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":25.5940,"lightweight_candidat... [truncated]
large-index-scalability.json:30:     {"dataset_size":10000,"query_class":"moderate","query":"report","temperature":"warm","iteration":6,"result_count":985,"candidate_count":985,"candidates_visited":985,"truncated":false,"wire_result_count":985,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":985,"sqlite_ms":0.1370,"projection_traversal_ms":43.8309,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":21.4605,"lightweight_candidat... [truncated]
large-index-scalability.json:31:     {"dataset_size":10000,"query_class":"moderate","query":"report","temperature":"warm","iteration":7,"result_count":985,"candidate_count":985,"candidates_visited":985,"truncated":false,"wire_result_count":985,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":985,"sqlite_ms":0.1182,"projection_traversal_ms":47.2546,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":25.2303,"lightweight_candidat... [truncated]
large-index-scalability.json:32:     {"dataset_size":10000,"query_class":"moderate","query":"report","temperature":"warm","iteration":8,"result_count":985,"candidate_count":985,"candidates_visited":985,"truncated":false,"wire_result_count":985,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":985,"sqlite_ms":0.1293,"projection_traversal_ms":43.2971,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":21.0342,"lightweight_candidat... [truncated]
large-index-scalability.json:33:     {"dataset_size":10000,"query_class":"moderate","query":"report","temperature":"warm","iteration":9,"result_count":985,"candidate_count":985,"candidates_visited":985,"truncated":false,"wire_result_count":985,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":985,"sqlite_ms":0.1586,"projection_traversal_ms":40.7669,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":20.3013,"lightweight_candidat... [truncated]
large-index-scalability.json:34:     {"dataset_size":10000,"query_class":"broad","query":"common","temperature":"warm","iteration":0,"result_count":2953,"candidate_count":2953,"candidates_visited":2953,"truncated":false,"wire_result_count":2953,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":2953,"sqlite_ms":0.1519,"projection_traversal_ms":212.9574,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":102.7749,"lightweight_cand... [truncated]
large-index-scalability.json:35:     {"dataset_size":10000,"query_class":"broad","query":"common","temperature":"warm","iteration":1,"result_count":2953,"candidate_count":2953,"candidates_visited":2953,"truncated":false,"wire_result_count":2953,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":2953,"sqlite_ms":0.1638,"projection_traversal_ms":204.5811,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":94.8593,"lightweight_candi... [truncated]
large-index-scalability.json:36:     {"dataset_size":10000,"query_class":"broad","query":"common","temperature":"warm","iteration":2,"result_count":2953,"candidate_count":2953,"candidates_visited":2953,"truncated":false,"wire_result_count":2953,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":2953,"sqlite_ms":0.1933,"projection_traversal_ms":197.7056,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":93.1384,"lightweight_candi... [truncated]
large-index-scalability.json:37:     {"dataset_size":10000,"query_class":"broad","query":"common","temperature":"warm","iteration":3,"result_count":2953,"candidate_count":2953,"candidates_visited":2953,"truncated":false,"wire_result_count":2953,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":2953,"sqlite_ms":0.1661,"projection_traversal_ms":172.2137,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":81.0424,"lightweight_candi... [truncated]
large-index-scalability.json:38:     {"dataset_size":10000,"query_class":"broad","query":"common","temperature":"warm","iteration":4,"result_count":2953,"candidate_count":2953,"candidates_visited":2953,"truncated":false,"wire_result_count":2953,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":2953,"sqlite_ms":0.1479,"projection_traversal_ms":192.9965,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":89.5823,"lightweight_candi... [truncated]
large-index-scalability.json:39:     {"dataset_size":10000,"query_class":"broad","query":"common","temperature":"warm","iteration":5,"result_count":2953,"candidate_count":2953,"candidates_visited":2953,"truncated":false,"wire_result_count":2953,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":2953,"sqlite_ms":0.1728,"projection_traversal_ms":183.2064,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":86.3933,"lightweight_candi... [truncated]
large-index-scalability.json:40:     {"dataset_size":10000,"query_class":"broad","query":"common","temperature":"warm","iteration":6,"result_count":2953,"candidate_count":2953,"candidates_visited":2953,"truncated":false,"wire_result_count":2953,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":2953,"sqlite_ms":0.7699,"projection_traversal_ms":160.8531,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":70.6335,"lightweight_candi... [truncated]
large-index-scalability.json:41:     {"dataset_size":10000,"query_class":"broad","query":"common","temperature":"warm","iteration":7,"result_count":2953,"candidate_count":2953,"candidates_visited":2953,"truncated":false,"wire_result_count":2953,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":2953,"sqlite_ms":0.1527,"projection_traversal_ms":189.9343,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":87.2600,"lightweight_candi... [truncated]
large-index-scalability.json:42:     {"dataset_size":10000,"query_class":"broad","query":"common","temperature":"warm","iteration":8,"result_count":2953,"candidate_count":2953,"candidates_visited":2953,"truncated":false,"wire_result_count":2953,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":2953,"sqlite_ms":0.1614,"projection_traversal_ms":193.6767,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":91.8053,"lightweight_candi... [truncated]
large-index-scalability.json:43:     {"dataset_size":10000,"query_class":"broad","query":"common","temperature":"warm","iteration":9,"result_count":2953,"candidate_count":2953,"candidates_visited":2953,"truncated":false,"wire_result_count":2953,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":2953,"sqlite_ms":0.1822,"projection_traversal_ms":193.2136,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":89.8812,"lightweight_candi... [truncated]
large-index-scalability.json:44:     {"dataset_size":10000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":0,"result_count":0,"candidate_count":156,"candidates_visited":156,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":156,"sqlite_ms":0.0678,"projection_traversal_ms":0.3864,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweight_ca... [truncated]
large-index-scalability.json:45:     {"dataset_size":10000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":1,"result_count":0,"candidate_count":156,"candidates_visited":156,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":156,"sqlite_ms":0.1216,"projection_traversal_ms":0.2710,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweight_ca... [truncated]
large-index-scalability.json:46:     {"dataset_size":10000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":2,"result_count":0,"candidate_count":156,"candidates_visited":156,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":156,"sqlite_ms":0.0955,"projection_traversal_ms":0.2064,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweight_ca... [truncated]
large-index-scalability.json:47:     {"dataset_size":10000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":3,"result_count":0,"candidate_count":156,"candidates_visited":156,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":156,"sqlite_ms":0.0587,"projection_traversal_ms":0.1364,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweight_ca... [truncated]
large-index-scalability.json:48:     {"dataset_size":10000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":4,"result_count":0,"candidate_count":156,"candidates_visited":156,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":156,"sqlite_ms":0.0516,"projection_traversal_ms":0.1923,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweight_ca... [truncated]
large-index-scalability.json:49:     {"dataset_size":10000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":5,"result_count":0,"candidate_count":156,"candidates_visited":156,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":156,"sqlite_ms":0.0697,"projection_traversal_ms":0.2426,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweight_ca... [truncated]
large-index-scalability.json:50:     {"dataset_size":10000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":6,"result_count":0,"candidate_count":156,"candidates_visited":156,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":156,"sqlite_ms":0.0889,"projection_traversal_ms":0.2318,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweight_ca... [truncated]
large-index-scalability.json:51:     {"dataset_size":10000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":7,"result_count":0,"candidate_count":156,"candidates_visited":156,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":156,"sqlite_ms":0.0739,"projection_traversal_ms":0.2784,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweight_ca... [truncated]
large-index-scalability.json:52:     {"dataset_size":10000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":8,"result_count":0,"candidate_count":156,"candidates_visited":156,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":156,"sqlite_ms":0.0744,"projection_traversal_ms":0.2357,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweight_ca... [truncated]
large-index-scalability.json:53:     {"dataset_size":10000,"query_class":"no_match","query":"zzzz_never_present","temperature":"warm","iteration":9,"result_count":0,"candidate_count":156,"candidates_visited":156,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":156,"sqlite_ms":0.0705,"projection_traversal_ms":0.2544,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweight_ca... [truncated]
large-index-scalability.json:54:     {"dataset_size":10000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":0,"result_count":984,"candidate_count":984,"candidates_visited":984,"truncated":false,"wire_result_count":984,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":984,"sqlite_ms":0.1513,"projection_traversal_ms":58.2095,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":29.3071,"lightweight_candidate_m... [truncated]
large-index-scalability.json:55:     {"dataset_size":10000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":1,"result_count":984,"candidate_count":984,"candidates_visited":984,"truncated":false,"wire_result_count":984,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":984,"sqlite_ms":0.1496,"projection_traversal_ms":66.4167,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":29.4737,"lightweight_candidate_m... [truncated]
large-index-scalability.json:56:     {"dataset_size":10000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":2,"result_count":984,"candidate_count":984,"candidates_visited":984,"truncated":false,"wire_result_count":984,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":984,"sqlite_ms":0.1465,"projection_traversal_ms":64.9959,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":29.0877,"lightweight_candidate_m... [truncated]
large-index-scalability.json:57:     {"dataset_size":10000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":3,"result_count":984,"candidate_count":984,"candidates_visited":984,"truncated":false,"wire_result_count":984,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":984,"sqlite_ms":0.1544,"projection_traversal_ms":63.4991,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":32.5535,"lightweight_candidate_m... [truncated]
large-index-scalability.json:58:     {"dataset_size":10000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":4,"result_count":984,"candidate_count":984,"candidates_visited":984,"truncated":false,"wire_result_count":984,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":984,"sqlite_ms":0.1646,"projection_traversal_ms":56.7579,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":28.7405,"lightweight_candidate_m... [truncated]
large-index-scalability.json:59:     {"dataset_size":10000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":5,"result_count":984,"candidate_count":984,"candidates_visited":984,"truncated":false,"wire_result_count":984,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":984,"sqlite_ms":0.1575,"projection_traversal_ms":61.4557,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":32.5274,"lightweight_candidate_m... [truncated]
large-index-scalability.json:60:     {"dataset_size":10000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":6,"result_count":984,"candidate_count":984,"candidates_visited":984,"truncated":false,"wire_result_count":984,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":984,"sqlite_ms":0.1643,"projection_traversal_ms":63.1991,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":33.2609,"lightweight_candidate_m... [truncated]
large-index-scalability.json:61:     {"dataset_size":10000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":7,"result_count":984,"candidate_count":984,"candidates_visited":984,"truncated":false,"wire_result_count":984,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":984,"sqlite_ms":0.1361,"projection_traversal_ms":60.8823,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":30.7026,"lightweight_candidate_m... [truncated]
large-index-scalability.json:62:     {"dataset_size":10000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":8,"result_count":984,"candidate_count":984,"candidates_visited":984,"truncated":false,"wire_result_count":984,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":984,"sqlite_ms":0.1517,"projection_traversal_ms":58.6744,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":27.6937,"lightweight_candidate_m... [truncated]
large-index-scalability.json:63:     {"dataset_size":10000,"query_class":"prefix","query":"photo","temperature":"warm","iteration":9,"result_count":984,"candidate_count":984,"candidates_visited":984,"truncated":false,"wire_result_count":984,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":984,"sqlite_ms":0.1875,"projection_traversal_ms":60.8642,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":31.0681,"lightweight_candidate_m... [truncated]
large-index-scalability.json:64:     {"dataset_size":10000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":0,"result_count":2955,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":2955,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1874,"projection_traversal_ms":214.1481,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":95.2890,"lightweight_... [truncated]
large-index-scalability.json:65:     {"dataset_size":10000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":1,"result_count":2955,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":2955,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1732,"projection_traversal_ms":197.5688,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":92.6524,"lightweight_... [truncated]
large-index-scalability.json:66:     {"dataset_size":10000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":2,"result_count":2955,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":2955,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.2559,"projection_traversal_ms":207.6430,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":101.8798,"lightweight... [truncated]
large-index-scalability.json:67:     {"dataset_size":10000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":3,"result_count":2955,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":2955,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1663,"projection_traversal_ms":186.4225,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":85.2319,"lightweight_... [truncated]
large-index-scalability.json:68:     {"dataset_size":10000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":4,"result_count":2955,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":2955,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1827,"projection_traversal_ms":200.3336,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":94.6279,"lightweight_... [truncated]
large-index-scalability.json:69:     {"dataset_size":10000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":5,"result_count":2955,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":2955,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1623,"projection_traversal_ms":180.0728,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":86.4850,"lightweight_... [truncated]
large-index-scalability.json:70:     {"dataset_size":10000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":6,"result_count":2955,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":2955,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1715,"projection_traversal_ms":212.4128,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":100.7044,"lightweight... [truncated]
large-index-scalability.json:71:     {"dataset_size":10000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":7,"result_count":2955,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":2955,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1577,"projection_traversal_ms":184.9734,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":85.9270,"lightweight_... [truncated]
large-index-scalability.json:72:     {"dataset_size":10000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":8,"result_count":2955,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":2955,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1454,"projection_traversal_ms":168.0388,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":77.5264,"lightweight_... [truncated]
large-index-scalability.json:73:     {"dataset_size":10000,"query_class":"extension","query":"ext:txt","temperature":"warm","iteration":9,"result_count":2955,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":2955,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1460,"projection_traversal_ms":163.1733,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":77.0181,"lightweight_... [truncated]
large-index-scalability.json:74:     {"dataset_size":10000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":0,"result_count":1970,"candidate_count":1970,"candidates_visited":1970,"truncated":false,"wire_result_count":1970,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1970,"sqlite_ms":0.1639,"projection_traversal_ms":130.2434,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":67.3104,"ligh... [truncated]
large-index-scalability.json:75:     {"dataset_size":10000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":1,"result_count":1970,"candidate_count":1970,"candidates_visited":1970,"truncated":false,"wire_result_count":1970,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1970,"sqlite_ms":0.1518,"projection_traversal_ms":141.4704,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":69.3006,"ligh... [truncated]
large-index-scalability.json:76:     {"dataset_size":10000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":2,"result_count":1970,"candidate_count":1970,"candidates_visited":1970,"truncated":false,"wire_result_count":1970,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1970,"sqlite_ms":0.1840,"projection_traversal_ms":129.9622,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":60.7576,"ligh... [truncated]
large-index-scalability.json:77:     {"dataset_size":10000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":3,"result_count":1970,"candidate_count":1970,"candidates_visited":1970,"truncated":false,"wire_result_count":1970,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1970,"sqlite_ms":0.1528,"projection_traversal_ms":120.9134,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":61.7156,"ligh... [truncated]
large-index-scalability.json:78:     {"dataset_size":10000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":4,"result_count":1970,"candidate_count":1970,"candidates_visited":1970,"truncated":false,"wire_result_count":1970,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1970,"sqlite_ms":0.3595,"projection_traversal_ms":139.5647,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":68.8381,"ligh... [truncated]
large-index-scalability.json:79:     {"dataset_size":10000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":5,"result_count":1970,"candidate_count":1970,"candidates_visited":1970,"truncated":false,"wire_result_count":1970,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1970,"sqlite_ms":0.1605,"projection_traversal_ms":134.5846,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":70.0976,"ligh... [truncated]
large-index-scalability.json:80:     {"dataset_size":10000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":6,"result_count":1970,"candidate_count":1970,"candidates_visited":1970,"truncated":false,"wire_result_count":1970,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1970,"sqlite_ms":0.1461,"projection_traversal_ms":109.8685,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":55.4380,"ligh... [truncated]
large-index-scalability.json:81:     {"dataset_size":10000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":7,"result_count":1970,"candidate_count":1970,"candidates_visited":1970,"truncated":false,"wire_result_count":1970,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1970,"sqlite_ms":0.1299,"projection_traversal_ms":111.7352,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":54.3822,"ligh... [truncated]
large-index-scalability.json:82:     {"dataset_size":10000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":8,"result_count":1970,"candidate_count":1970,"candidates_visited":1970,"truncated":false,"wire_result_count":1970,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1970,"sqlite_ms":0.1693,"projection_traversal_ms":121.3769,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":61.0808,"ligh... [truncated]
large-index-scalability.json:83:     {"dataset_size":10000,"query_class":"worst_substring","query":"component","temperature":"warm","iteration":9,"result_count":1970,"candidate_count":1970,"candidates_visited":1970,"truncated":false,"wire_result_count":1970,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1970,"sqlite_ms":0.1497,"projection_traversal_ms":124.5086,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":62.5085,"ligh... [truncated]
large-index-scalability.json:84:     {"dataset_size":10000,"query_class":"selective","query":"unique_component_7391","temperature":"cold","iteration":0,"result_count":1,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1386,"projection_traversal_ms":110.2859,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.4483,"light... [truncated]
large-index-scalability.json:85:     {"dataset_size":10000,"query_class":"moderate","query":"report","temperature":"cold","iteration":0,"result_count":985,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":985,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1503,"projection_traversal_ms":313.6463,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":194.0156,"lightweight_can... [truncated]
large-index-scalability.json:86:     {"dataset_size":10000,"query_class":"broad","query":"common","temperature":"cold","iteration":0,"result_count":2953,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":2953,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1862,"projection_traversal_ms":880.4086,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":651.5425,"lightweight_cand... [truncated]
large-index-scalability.json:87:     {"dataset_size":10000,"query_class":"no_match","query":"zzzz_never_present","temperature":"cold","iteration":0,"result_count":0,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.5232,"projection_traversal_ms":84.5898,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0000,"lightweigh... [truncated]
large-index-scalability.json:88:     {"dataset_size":10000,"query_class":"prefix","query":"photo","temperature":"cold","iteration":0,"result_count":984,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":984,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1610,"projection_traversal_ms":354.9163,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":214.9528,"lightweight_candid... [truncated]
large-index-scalability.json:89:     {"dataset_size":10000,"query_class":"extension","query":"ext:txt","temperature":"cold","iteration":0,"result_count":2955,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":2955,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1587,"projection_traversal_ms":786.0289,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":594.3327,"lightweight... [truncated]
large-index-scalability.json:90:     {"dataset_size":10000,"query_class":"worst_substring","query":"component","temperature":"cold","iteration":0,"result_count":1970,"candidate_count":10000,"candidates_visited":10000,"truncated":false,"wire_result_count":1970,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"sqlite_ms":0.1753,"projection_traversal_ms":479.9794,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":339.8119,"lig... [truncated]
phase8-large-index-scalability.md:20: filename-trigram accelerator is used only when every searched volume is
phase8-large-index-scalability.md:80: ## 2M rows (clean rerun; canonical-path dominated — the trigram accelerator was
large-index-scalability-sql-no-count.json:14:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"cold-ish-first-after-rebuild","iteration":0,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0994,"projection_traversal_ms":0.0360,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms"... [truncated]
large-index-scalability-sql-no-count.json:15:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":0,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0259,"projection_traversal_ms":0.0103,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0019,"lightweight_can... [truncated]
large-index-scalability-sql-no-count.json:16:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":1,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0145,"projection_traversal_ms":0.0113,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0015,"lightweight_can... [truncated]
large-index-scalability-sql-no-count.json:17:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":2,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0127,"projection_traversal_ms":0.0139,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0015,"lightweight_can... [truncated]
large-index-scalability-sql-no-count.json:18:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":3,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0118,"projection_traversal_ms":0.0136,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0015,"lightweight_can... [truncated]
large-index-scalability-sql-no-count.json:19:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":4,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0110,"projection_traversal_ms":0.0138,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0014,"lightweight_can... [truncated]
large-index-scalability-sql-no-count.json:20:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":5,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0108,"projection_traversal_ms":0.0108,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0014,"lightweight_can... [truncated]
large-index-scalability-sql-no-count.json:21:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":6,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0168,"projection_traversal_ms":0.0140,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0016,"lightweight_can... [truncated]
large-index-scalability-sql-no-count.json:22:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":7,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0133,"projection_traversal_ms":0.0136,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0013,"lightweight_can... [truncated]
large-index-scalability-sql-no-count.json:23:     {"dataset_size":100000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":8,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"sqlite_ms":0.0111,"projection_traversal_ms":0.0133,"projection_enumeration_ms":0.0000,"name_pool_lookup_ms":0.0000,"name_view_construction_ms":0.0000,"materialization_ms":0.0012,"lightweight_can... [truncated]
[100 matches limit reached. Use limit=200 for more, or refine pattern. 50.0KB limit reached. Some lines truncated to 500 chars. Use read tool to see full lines]
read: docs/benchmarks/large-index-scalability.md
read: docs/benchmarks/secondary-filename-index.md
| 1M | Broad | 48.83 ms | 48.82 ms | 1.00x |
| 1M | Worst substring | 50.92 ms | 49.95 ms | 1.02x |
| 1M | Extension | 50.27 ms | 49.30 ms | 1.02x |
Final 1M p90/max were selective `129.34/130.71 ms`, no-match `149.38/152.35 ms`, broad `49.99/50.73 ms`, and worst-substring `50.13/51.20 ms`. Incremental `p/ph/pho/phot/photo` was `46.97/56.26/56.38/55.62/55.70 ms` median; cancellation was intentionally a separate future concern for this historical slice and is now addressed by the audit-remediation work.
### Attribution and decision
Using the established projection-only floor (`54.28 ms`) and SQLite metadata floor (`33.98 ms`), the 1M no-match failed-literal residual is approximately `59.64 ms` after this slice, versus approximately `83.73 ms` before it. The combined lower bound is already about `88 ms` before meaningful result work. The full scan remains linear in projection size and still costs `147.90 ms` for a guaranteed no-match query; a secondary filename-search design must therefore be evaluated before further matcher micro-optimization.
`ARCHITECTURAL DECISION: SECONDARY FILENAME INDEX EVALUATION REQUIRED`
`NEW PRIMARY BOTTLENECK: full O(N) projection traversal plus residual literal rejection`
## Secondary filename index integration — 2026-08-11
The measured UTF-16 trigram posting design is now integrated into the production indexed-search path. The new historical artifact is [large-index-scalability-secondary-index.json](large-index-scalability-secondary-index.json); the original `large-index-scalability.json` remains unchanged so the optimization history is preserved.
The current pipeline is:
```text
ParseQuery
→ GetAllVolumes metadata query
→ projection lock
→ eligible simple-literal UTF-16 trigram candidate lookup
→ canonical filename/path/predicate verification
→ existing result cap, ordering, and response
```
Only simple literal filename terms with at least three ASCII-foldable UTF-16 units use the accelerator. Short terms, Unicode terms, globs, multi-term queries, path predicates, extension predicates, metadata predicates, and any unavailable/invalid index use the existing canonical scan. The index is a candidate generator, never the semantic authority; exact result sets and ordering remain owned by the canonical matcher.
### Integrated Release medians
| Scale | Selective | No match | Broad |
| ----: | --------: | -------: | ----: |
| 100K | 1.93 ms | 2.02 ms | 10.99 ms |
| 500K | 17.71 ms | 18.06 ms | 29.15 ms |
| 1M | 34.34 ms | 36.25 ms | 47.83 ms |
| 2M | 64.91 ms | 67.84 ms | 81.36 ms |
At 2M, the selective query verified one candidate and the no-match query verified zero candidates; both still measured approximately 65–68 ms because the `GetAllVolumes` SQLite metadata query is now the dominant stage. Broad search used 590,624 candidates and retained the existing 4,096-result cap. The query summaries include p90/max values and all seven benchmark classes.
The integrated result improves the prior same-harness 1M medians of approximately `126.99/147.90/48.82 ms` (selective/no-match/broad) to `34.34/36.25/47.83 ms`. Selective and no-match no longer scale with the full projection row count; broad remains governed by candidate verification and result materialization.
### Lifecycle and resource behavior
`Projection` owns the postings beside its generation-owned dense slots. Rebuilds bulk-load and sort/deduplicate postings after compaction; insert, delete, and rename update postings incrementally. Allocation or update failure invalidates only the accelerator, and search falls back to the canonical linear path. Focused tests cover duplicate/uppercase/non-ASCII recall, empty no-match, short and Unicode fallback, delete, and rename behavior; engine tests cover eligible, no-match, short-term, and folder-query routing.
The final artifact reports private bytes after rebuild of approximately `59.3/280.7/551.9/1120.6 MiB` at `100K/500K/1M/2M`, with post-query values of `61.3/292.7/565.4/1294.2 MiB`. The isolated architecture benchmark estimated 168.6 MiB of trigram postings at 2M; the integrated process also retains the existing SQLite/projection/snapshot working set. The 2M rebuild took approximately 5.0 seconds and the temporary database occupied 150,564,864 bytes. No schema migration or persistent secondary-index file was introduced.
This historical slice did not optimize SQLite or add search cancellation. Those were separate decisions requiring fresh measurements rather than attribution from the pre-index path; the current audit-remediation work adds cancellation with dedicated regression and latency coverage.
No 2M run was performed because the 1M selective result remained above the optional scale-up gate. An isolated 1M memory check measured approximately `4.9 / 453.8 / 468.8 / 470.1 MiB` for open / rebuild / after broad / post-query, with no material regression against the prior isolated `4.4 / 454.0 / 470.4 / 471.2 MiB` check. Focused and full semantic tests passed for selective/no-match, early/late matching, short and overlong needles, ASCII case-insensitivity, uppercase fallback, Unicode/mixed Unicode, prefix, glob, extension, parent path, multi-term, duplicate names, capped 4,096 results, and ordering.
## SQL metadata no-count path — 2026-08-11
The secondary-index artifact showed that `GetAllVolumes()` had become the dominant stage because it counted every entry for every volume even though indexed search only needs volume identity, availability, and the resolved root key. The focused change adds `GetAllVolumesWithoutEntryCounts()` and routes both indexed-search branches through it. Rebuild, public metadata, and unavailable-volume reporting retain the counted API and its existing semantics; no schema migration or entry-count cache was added.
The final historical artifact is [large-index-scalability-sql-no-count.json](large-index-scalability-sql-no-count.json). It preserves the previous secondary-index artifact and records the same Release harness at 100K/500K/1M/2M:
| Scale | Selective | No match | Broad |
| ----: | --------: | -------: | ----: |
| 100K | 0.03 ms | 0.06 ms | 8.82 ms |
| 500K | 0.50 ms | 0.62 ms | 11.89 ms |
| 1M | 0.64 ms | 0.63 ms | 14.29 ms |
| 2M | 0.55 ms | 2.24 ms | 17.55 ms |
At 2M, the eligible selective query still used one trigram candidate and the no-match query used zero; SQLite metadata timing was approximately `0.10 ms`, down from approximately `65–68 ms`. Broad search retained the existing 4,096-result cap and used 590,624 candidates. The result is now below the low-tens-of-milliseconds engineering target for eligible selective/no-match queries through 2M; no further SQL redesign is justified by this measurement.
The benchmark process exited after writing the complete artifact when the external 120-second command deadline expired; post-run checks found no live benchmark process, no leftover disposable benchmark directories, and valid JSON with 4 datasets, 179 runs, and 28 summaries. Focused Store/engine tests passed before the benchmark. Full Debug/Analyze/CTest validation and the bounded product commit remain the next gate for this slice.
## Phase 9 non-blocking read-plane evidence — 2026-08-24
The benchmark now records `search_generation`, generation restarts, state-lock
wait/maximum hold time, peak searches/readers, cumulative read-plane counters,
and a deterministic contention probe. The probe uses a real cold query parked
at the durable-stream boundary. While it is parked, the existing pipeline
executes an MFT ingestion batch, a durable folder aggregate, a directory
snapshot export, and a second indexed search; the first query is then
cancelled and its reader is released. These are evidence fields, not brittle
latency gates.
The first Phase 9 run used the established 10K fixture on the same host and
compared the current refactor with the immediately preceding Phase 8 binary:
| 10K case | Phase 8 | Phase 9 | Interpretation |
| --- | ---: | ---: | --- |
| warm selective | 0.64 ms | 0.48 ms | same-harness reference; no fast-path regression observed |
| cold broad | 806.99 ms | 742.34 ms | cold streaming remains expensive but is isolated from writers |
The Phase 9 contention probe recorded: ingestion `2.07 ms`, aggregate
`81.55 ms`, snapshot export `311.74 ms`, second query `365.62 ms`, all
completed while the first search was held for `365.98 ms`; cancellation took
`0.24 ms`. The probe reached the cold stream, reported two peak durable
readers, ended with zero active readers, and published no partial result.
The cumulative read-plane counters ended at two peak searches, 99 completed
searches, one cancellation, zero unavailable searches, and zero reader
failures. The 10K post-query private bytes were approximately 13.6 MiB, with
99 handles and four process threads; the WAL measured 20,632 bytes at the
post-query sample. The forced-eviction projection estimate remained
[78 more lines in file. Use offset=520 to continue.]
# Secondary filename-index evaluation
This is the decision artifact for the measured secondary filename-index
architectures. The benchmark is informational and uses the same canonical
`ffsearch::Query::MatchesView` matcher for verification after candidate lookup;
it does not change the production projection or query path.
Command used for the final matrix:
```powershell
build\release\tests\engine\ffengine_bench_secondary_filename_index.exe --scales 100000,500000,1000000,2000000 --output docs\benchmarks\secondary-filename-index.json
```
## SQLite capability
The vendored SQLite is 3.53.4, but the target is compiled without
`SQLITE_ENABLE_FTS5`. `CREATE VIRTUAL TABLE ... USING fts5` fails with
`no such module: fts5`, and the FTS5 trigram tokenizer is unavailable as well.
Ordinary tokenized FTS would not preserve arbitrary infix semantics anyway:
the benchmark's tokenized-infix probe is a required recall check, not an
assumption that token search is a substring index.
## Architectures
### Linear canonical scan
This is the existing correctness baseline. It has no secondary build or index
memory cost, but selective and no-match queries remain O(N).
### UTF-16 trigram postings
Each filename contributes unique three-UTF-16-code-unit grams to sorted posting
lists. Query grams are intersected, then every candidate is verified by the
canonical matcher. ASCII literal terms of at least three units are eligible;
short terms, Unicode terms, globs, multi-term queries, folder predicates, and
metadata predicates remain on the linear canonical path. Non-ASCII names are
kept in a conservative verification bucket for ASCII queries, preventing
false negatives from the accelerator.
At 2M entries:
| Query | Canonical matches | Candidates | Reduction | Median |
| --- | ---: | ---: | ---: | ---: |
| selective | 1 | 4 | 99.9998% | 0.01 ms |
| no match | 0 | 3 | 99.9999% | 0.00 ms |
| moderate `report_` | 196,875 | 196,877 | 90.16% | 19.51 ms |
| broad `common_` | 588,654 | 588,657 | 70.57% | 40.24 ms |
| late substring `7391` | 1 | 597 | 99.97% | 0.04 ms |
The exact machine-readable values, including all query classes, are in
[`secondary-filename-index.json`](secondary-filename-index.json). The table
above highlights the scaling behavior without replacing the full artifact.
Build and memory scale approximately linearly: 54.0 ms / 8.8 MiB at 100K,
285.1 ms / 45.3 MiB at 500K, 574.2 ms / 91.2 MiB at 1M, and 1,198.9 ms /
168.6 MiB at 2M. The memory figure is an explicit container-allocation
estimate; the benchmark also records process private-byte deltas. The
persistent-size figure is a compact posting-stream proxy, not a committed
database schema.
All 12 eligible query classes had zero false negatives and exact canonical
verification at every measured scale. The 10 ineligible classes are explicitly
marked as fallback-only and were never treated as accelerated semantics.
Mutation probes at 2M remained semantically exact after each batch. For 100,
1,000, and 10,000 operations respectively, trigram insert/delete/rename times
were approximately 33/4/45 ms, 35/27/453 ms, and 45/268/4,574 ms. The rename
cost is the important tradeoff: sorted posting-list erase/insert is measurable
for large batches, but remains bounded and correctness-preserving.
### Sorted normalized names / prefix ranges
The sorted-name structure costs 246.0 MiB at 2M and does not accelerate the
current literal query because Fast Files literal matching is arbitrary infix
matching. The benchmark also ran the tempting prefix-only range as a separate
probe: at 2M, `photo_` produced 196,874 prefix candidates but missed 1,969
canonical infix matches created by names containing the term later in the
filename. It is therefore not a safe production accelerator without changing
search semantics.
## Decision
`utf16_trigram_postings` is the single measured winner. It is the only tested
design that materially removes the O(N) selective/no-match cost while
preserving the canonical match set, and it has a straightforward fallback for
unsupported query classes. FTS5 is unavailable in the shipped SQLite build;
prefix ranges are semantically unsafe for the current infix language.
Production integration is the next bounded slice. It must retain the same
generation ownership as the projection, update on insert/delete/rename, and
fall back to the canonical scan if construction or maintenance fails. No
production code or SQLite schema has been changed by this evaluation.
read: src/indexstore/src/Store.cpp
grep: tests/engine/bench_secondary_filename_index.cpp
            ok = false;
        }
    } else {
        ExecSql(db_, "ROLLBACK;");
    }
    return ok;
}
bool Store::ApplyBatch(VolumeRowId volumeRowId, const std::vector<EntryChange>& changes) {
    if (db_ == nullptr) {
        return false;
    }
    if (changes.empty()) {
        return true;
    }
    if (!ExecSql(db_, "BEGIN IMMEDIATE;")) {
        return false;
    }
    Statement upsert(db_,
        "INSERT INTO entries(volume_id, frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, "
        "allocated_size_bytes, creation_time, last_modified_time, last_access_time, attributes) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(volume_id, frn_low, frn_high) DO UPDATE SET "
        "parent_frn_low=CASE WHEN (? & 1) != 0 THEN excluded.parent_frn_low ELSE entries.parent_frn_low END, "
        "parent_frn_high=CASE WHEN (? & 1) != 0 THEN excluded.parent_frn_high ELSE entries.parent_frn_high END, "
        "name=CASE WHEN (? & 2) != 0 THEN excluded.name ELSE entries.name END, "
        "size_bytes=CASE WHEN (? & 4) != 0 THEN excluded.size_bytes ELSE entries.size_bytes END, "
        "allocated_size_bytes=CASE WHEN (? & 128) != 0 THEN excluded.allocated_size_bytes "
        "  ELSE entries.allocated_size_bytes END, "
        "creation_time=CASE WHEN (? & 8) != 0 THEN excluded.creation_time ELSE entries.creation_time END, "
        "last_modified_time=CASE WHEN (? & 16) != 0 THEN excluded.last_modified_time ELSE entries.last_modified_time END, "
        "last_access_time=CASE WHEN (? & 32) != 0 THEN excluded.last_access_time ELSE entries.last_access_time END, "
        "attributes=CASE WHEN (? & 64) != 0 THEN excluded.attributes ELSE entries.attributes END;");
    Statement remove(db_, "DELETE FROM entries WHERE volume_id=? AND frn_low=? AND frn_high=?;");
    if (!upsert.Valid() || !remove.Valid()) {
        ExecSql(db_, "ROLLBACK;");
        return false;
    }
    bool ok = true;
    for (const auto& change : changes) {
        if (change.kind == EntryChangeKind::Upsert) {
            const auto& record = change.record;
            const std::string nameUtf8 = Utf16ToUtf8(record.name);
            // large-index-lifecycle-and-search-scalability review-lane C
            // Finding 4: on the INSERT arm the field mask must zero absent
            // fields, matching Projection::UpsertPartial's missing-entry
            // contract ("created with zero/default values only for fields
            // that were not present"). The UPDATE arm already guards via the
            // CASE expressions; before this, a partial upsert of a row that
            // did not yet exist persisted the wire values for absent fields
            // and hot vs cold planes reported different metadata.
            const EntryFieldMask fields = change.fields;
            const bool hasParent = HasEntryField(fields, kEntryFieldParentId);
            const bool hasName = HasEntryField(fields, kEntryFieldName);
            const bool hasSize = HasEntryField(fields, kEntryFieldSizeBytes);
            const bool hasAllocated = HasEntryField(fields, kEntryFieldAllocatedSizeBytes);
            const bool hasCreation = HasEntryField(fields, kEntryFieldCreationTime);
            const bool hasModified = HasEntryField(fields, kEntryFieldLastModifiedTime);
            const bool hasAccess = HasEntryField(fields, kEntryFieldLastAccessTime);
            const bool hasAttributes = HasEntryField(fields, kEntryFieldAttributes);
            sqlite3_reset(upsert);
            sqlite3_bind_int64(upsert, 1, static_cast<sqlite3_int64>(volumeRowId));
            sqlite3_bind_int64(upsert, 2, static_cast<sqlite3_int64>(record.id.low));
            sqlite3_bind_int64(upsert, 3, static_cast<sqlite3_int64>(record.id.high));
            sqlite3_bind_int64(upsert, 4,
                static_cast<sqlite3_int64>(hasParent ? record.parentId.low : 0));
            sqlite3_bind_int64(upsert, 5,
                static_cast<sqlite3_int64>(hasParent ? record.parentId.high : 0));
            sqlite3_bind_text(upsert, 6,
                hasName ? nameUtf8.data() : "", hasName ? static_cast<int>(nameUtf8.size()) : 0,
                SQLITE_TRANSIENT);
            sqlite3_bind_int64(upsert, 7,
                static_cast<sqlite3_int64>(hasSize ? record.sizeBytes : 0));
            if (hasAllocated && record.allocatedSizeKnown) {
                sqlite3_bind_int64(upsert, 8, static_cast<sqlite3_int64>(record.allocatedSizeBytes));
            } else {
                // Unknown is NULL on disk -- a genuine zero is stored as 0.
                sqlite3_bind_null(upsert, 8);
            }
            sqlite3_bind_int64(upsert, 9,
                static_cast<sqlite3_int64>(hasCreation ? record.creationTime : 0));
            sqlite3_bind_int64(upsert, 10,
                static_cast<sqlite3_int64>(hasModified ? record.lastModifiedTime : 0));
            sqlite3_bind_int64(upsert, 11,
                static_cast<sqlite3_int64>(hasAccess ? record.lastAccessTime : 0));
            sqlite3_bind_int64(upsert, 12,
                static_cast<sqlite3_int64>(hasAttributes ? record.attributes : 0));
            const sqlite3_int64 mask = static_cast<sqlite3_int64>(change.fields);
            for (int parameter = 13; parameter <= 21; ++parameter) {
                sqlite3_bind_int64(upsert, parameter, mask);
            }
            ok = sqlite3_step(upsert) == SQLITE_DONE;
        } else {
            sqlite3_reset(remove);
            sqlite3_bind_int64(remove, 1, static_cast<sqlite3_int64>(volumeRowId));
            sqlite3_bind_int64(remove, 2, static_cast<sqlite3_int64>(change.record.id.low));
            sqlite3_bind_int64(remove, 3, static_cast<sqlite3_int64>(change.record.id.high));
            ok = sqlite3_step(remove) == SQLITE_DONE;
        }
        if (!ok) {
            break;
        }
    }
    if (!ok) {
        ExecSql(db_, "ROLLBACK;");
        return false;
    }
    // The commit result is part of the operation's contract. Returning true
    // after a failed COMMIT would let IndexPipeline apply changes to the
    // in-memory projection that never became durable.
    if (!ExecSql(db_, "COMMIT;")) {
        ExecSql(db_, "ROLLBACK;");
        return false;
    }
    return true;
}
bool Store::ForEachEntry(VolumeRowId volumeRowId, const EntryVisitor& visitor) {
    if (db_ == nullptr) {
        return false;
    }
    Statement stmt(db_,
        "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
        "creation_time, last_modified_time, last_access_time, attributes FROM entries WHERE volume_id=?;");
    if (!stmt.Valid()) {
        return false;
[413 more lines in file. Use offset=885 to continue.]
bench_secondary_filename_index.cpp-244- }
bench_secondary_filename_index.cpp-245- 
bench_secondary_filename_index.cpp-246- struct IndexStats {
bench_secondary_filename_index.cpp-247-     uint64_t estimatedMemoryBytes = 0;
bench_secondary_filename_index.cpp:248:     uint64_t estimatedPersistentBytes = 0;
bench_secondary_filename_index.cpp-249-     size_t postingCount = 0;
bench_secondary_filename_index.cpp-250-     size_t postingReferences = 0;
bench_secondary_filename_index.cpp-251- };
bench_secondary_filename_index.cpp-252- 
bench_secondary_filename_index.cpp-345-         result.postingCount = postings_.size();
bench_secondary_filename_index.cpp-346-         for (const auto& [key, posting] : postings_) {
bench_secondary_filename_index.cpp-347-             (void)key;
bench_secondary_filename_index.cpp-348-             result.postingReferences += posting.size();
bench_secondary_filename_index.cpp:349:             result.estimatedPersistentBytes += sizeof(uint64_t) + posting.size() * sizeof(EntryId);
bench_secondary_filename_index.cpp-350-             result.estimatedMemoryBytes += sizeof(uint64_t) + sizeof(std::vector<EntryId>) +
bench_secondary_filename_index.cpp-351-                                            posting.capacity() * sizeof(EntryId);
bench_secondary_filename_index.cpp-352-         }
bench_secondary_filename_index.cpp-353-         result.estimatedMemoryBytes += sizeof(*this) + postings_.bucket_count() * sizeof(void*) * 2 +
bench_secondary_filename_index.cpp-351-                                            posting.capacity() * sizeof(EntryId);
bench_secondary_filename_index.cpp-352-         }
bench_secondary_filename_index.cpp-353-         result.estimatedMemoryBytes += sizeof(*this) + postings_.bucket_count() * sizeof(void*) * 2 +
bench_secondary_filename_index.cpp-354-                                        nonAsciiIds_.capacity() * sizeof(EntryId);
bench_secondary_filename_index.cpp:355:         result.estimatedPersistentBytes += nonAsciiIds_.size() * sizeof(EntryId);
bench_secondary_filename_index.cpp-356-         return result;
bench_secondary_filename_index.cpp-357-     }
bench_secondary_filename_index.cpp-358- 
bench_secondary_filename_index.cpp-359- private:
bench_secondary_filename_index.cpp-433-         result.postingCount = postings_.size();
bench_secondary_filename_index.cpp-434-         for (const Posting& posting : postings_) {
bench_secondary_filename_index.cpp-435-             result.postingReferences += 1;
bench_secondary_filename_index.cpp-436-             result.estimatedMemoryBytes += sizeof(Posting) + posting.folded.capacity() * sizeof(char16_t);
bench_secondary_filename_index.cpp:437:             result.estimatedPersistentBytes += sizeof(EntryId) + posting.folded.size() * sizeof(char16_t);
bench_secondary_filename_index.cpp-438-         }
bench_secondary_filename_index.cpp-439-         result.estimatedMemoryBytes += sizeof(*this) + postings_.capacity() * sizeof(Posting);
bench_secondary_filename_index.cpp-440-         return result;
bench_secondary_filename_index.cpp-441-     }
bench_secondary_filename_index.cpp-759-            << ",\"observed_working_set_delta_bytes\":"
bench_secondary_filename_index.cpp-760-            << static_cast<int64_t>(measurement.afterBuild.workingSetBytes) -
bench_secondary_filename_index.cpp-761-                   static_cast<int64_t>(measurement.beforeBuild.workingSetBytes);
bench_secondary_filename_index.cpp-762-     output << ",\"index_stats\":{\"estimated_memory_bytes\":" << measurement.stats.estimatedMemoryBytes
bench_secondary_filename_index.cpp:763:            << ",\"estimated_persistent_bytes\":" << measurement.stats.estimatedPersistentBytes
bench_secondary_filename_index.cpp-764-            << ",\"posting_count\":" << measurement.stats.postingCount
bench_secondary_filename_index.cpp-765-            << ",\"posting_references\":" << measurement.stats.postingReferences << "},\"queries\":[";
bench_secondary_filename_index.cpp-766-     for (size_t index = 0; index < measurement.queries.size(); ++index) {
bench_secondary_filename_index.cpp-767-         if (index != 0) output << ',';
grep: docs/benchmarks/secondary-filename-index.json
secondary-filename-index.json:1: {"schema":"fastfiles-secondary-filename-index-v1","sqlite_fts_capability":{"compile_option_enable_fts5":false,"fts5_create":false,"tokenized_infix_recall":false,"trigram_create":false,"trigram_infix_recall":false,"create_error":"no such module: fts5"},"machine":{"logical_cores":12,"ram_bytes":25028239360,"compiler":"MSVC 194435228","build_configuration":"Release"},"scales":[{"dataset_size":100000,"architectures":[{"name":"linear_canonical_scan","build_ms":0.0001,"memory_before_build":{"private_b... [truncated]
[Some lines truncated to 500 chars. Use read tool to see full lines]
grep: src/engine/src
grep: src/indexstore/src/Store.cpp
IndexPipeline.h-214-     // close-independent-validation-gaps 4.6: initial and reconciliation
IndexPipeline.h-215-     // scans are bulk projection mutations. Their durable writes continue
IndexPipeline.h-216-     // normally, while the projection defers compaction and its optional
IndexPipeline.h-217-     // filename accelerator until the bulk scan is no longer active.
IndexPipeline.h:218:     void SetBulkScanActive(bool active);
IndexPipeline.h-219- 
IndexPipeline.h-220-     ffindexstore::VolumeRowId ResolveVolume(const ffindexstore::VolumeKey& key);
IndexPipeline.h-221-     std::optional<ffindexstore::VolumeMetadata> GetVolumeMetadata(ffindexstore::VolumeRowId id);
IndexPipeline.h-222-     std::vector<ffindexstore::VolumeMetadata> GetAllVolumes();
IndexPipeline.cpp-322-     if ((fields & ffprotocol::kUsnFieldLastAccessTime) != 0) result |= ffindexstore::kEntryFieldLastAccessTime;
IndexPipeline.cpp-323-     if ((fields & ffprotocol::kUsnFieldAttributes) != 0) result |= ffindexstore::kEntryFieldAttributes;
IndexPipeline.cpp-324-     // allocated-size-storage-accuracy 5.3: one-to-one wire-bit to
IndexPipeline.cpp-325-     // store-bit mapping; absence leaves the durable/projection value
IndexPipeline.cpp:326:     // untouched (UpsertPartial's preserve semantics).
IndexPipeline.cpp-327-     if ((fields & ffprotocol::kUsnFieldAllocatedSizeBytes) != 0) result |= ffindexstore::kEntryFieldAllocatedSizeBytes;
IndexPipeline.cpp-328-     return result;
IndexPipeline.cpp-329- }
IndexPipeline.cpp-330- 
IndexPipeline.cpp-510-     result.durableFallbackRowsStreamed = durableFallbackRowsStreamed_.load(std::memory_order_relaxed);
IndexPipeline.cpp-511-     return result;
IndexPipeline.cpp-512- }
IndexPipeline.cpp-513- 
IndexPipeline.cpp:514: void IndexPipeline::SetBulkScanActive(const bool active) {
IndexPipeline.cpp-515-     std::lock_guard<std::mutex> lock(mutex_);
IndexPipeline.cpp-516-     projection_.SetBulkScanActive(active);
IndexPipeline.cpp-517-     RecordMetadataOnlyUpdateLocked();
IndexPipeline.cpp-518- }
IndexPipeline.cpp-512- }
IndexPipeline.cpp-513- 
IndexPipeline.cpp-514- void IndexPipeline::SetBulkScanActive(const bool active) {
IndexPipeline.cpp-515-     std::lock_guard<std::mutex> lock(mutex_);
IndexPipeline.cpp:516:     projection_.SetBulkScanActive(active);
IndexPipeline.cpp-517-     RecordMetadataOnlyUpdateLocked();
IndexPipeline.cpp-518- }
IndexPipeline.cpp-519- 
IndexPipeline.cpp-520- void IndexPipeline::RunStoreMaintenance(bool userIdle, uint64_t vacuumIntervalHours,
IndexPipeline.cpp-603-                     const bool injectedFailure = false;
IndexPipeline.cpp-604- #endif
IndexPipeline.cpp-605-                     bool secondaryApplied = false;
IndexPipeline.cpp-606-                     if (!injectedFailure) {
IndexPipeline.cpp:607:                         secondaryApplied = store_.ApplyBatch(meta.rowId, ruleExcluded);
IndexPipeline.cpp-608-                     }
IndexPipeline.cpp-609-                     if (secondaryApplied) {
IndexPipeline.cpp-610-                         std::vector<ffindexstore::FileId> excludedIds;
IndexPipeline.cpp-611-                         excludedIds.reserve(ruleExcluded.size());
IndexPipeline.cpp-614-                         }
IndexPipeline.cpp-615-                         // large-index-lifecycle-and-search-scalability 4.4:
IndexPipeline.cpp-616-                         // one bulk accelerator update instead of per-entry
IndexPipeline.cpp-617-                         // sorted-vector posting erases on mass rule removal.
IndexPipeline.cpp:618:                         projection_.RemoveBatch(meta.rowId, excludedIds);
IndexPipeline.cpp-619-                     } else {
IndexPipeline.cpp-620-                         // The projection was rebuilt from the unchanged durable
IndexPipeline.cpp-621-                         // rows, so retaining them is consistent. Do not publish
IndexPipeline.cpp-622-                         // this volume as a successful rebuild; the next rebuild
IndexPipeline.cpp-728-                                          std::vector<ffindexstore::EntryChange>&& changes) {
IndexPipeline.cpp-729-     std::lock_guard<std::mutex> lock(mutex_);
IndexPipeline.cpp-730-     // design.md D4: commit to the durable store first; the projection is
IndexPipeline.cpp-731-     // only touched once that commit has actually succeeded.
IndexPipeline.cpp:732:     if (!store_.ApplyBatch(volumeId, changes)) {
IndexPipeline.cpp-733-         return false;
IndexPipeline.cpp-734-     }
IndexPipeline.cpp-735-     MaybeAnalyzeAfterBulkInsertLocked(changes.size());
IndexPipeline.cpp-736-     for (const auto& change : changes) {
IndexPipeline.cpp-769-         const bool injectedFailure = false;
IndexPipeline.cpp-770- #endif
IndexPipeline.cpp-771-         bool secondaryApplied = false;
IndexPipeline.cpp-772-         if (!injectedFailure) {
IndexPipeline.cpp:773:             secondaryApplied = store_.ApplyBatch(volumeId, ruleExcluded);
IndexPipeline.cpp-774-         }
IndexPipeline.cpp-775-         if (secondaryApplied) {
IndexPipeline.cpp-776-             std::vector<ffindexstore::FileId> excludedIds;
IndexPipeline.cpp-777-             excludedIds.reserve(ruleExcluded.size());
IndexPipeline.cpp-780-             }
IndexPipeline.cpp-781-             // large-index-lifecycle-and-search-scalability 4.4: one bulk
IndexPipeline.cpp-782-             // accelerator update instead of per-entry sorted-vector posting
IndexPipeline.cpp-783-             // erases on mass rule removal.
IndexPipeline.cpp:784:             projection_.RemoveBatch(volumeId, excludedIds);
IndexPipeline.cpp-785-         } else {
IndexPipeline.cpp-786-             // Secondary pruning failed: durable and projection remain consistent (both retain the excluded rows),
IndexPipeline.cpp-787-             // but do not report success. Caller can retry the batch; the next attempt will re-evaluate pruning.
IndexPipeline.cpp-788-             if (projection_.EnforceMemoryLimitNow()) {
IndexPipeline.cpp-852- 
IndexPipeline.cpp-853- bool IndexPipeline::ApplyUsnBatchChanges(ffindexstore::VolumeRowId volumeId,
IndexPipeline.cpp-854-                                          std::vector<ffindexstore::EntryChange>&& changes) {
IndexPipeline.cpp-855-     std::lock_guard<std::mutex> lock(mutex_);
IndexPipeline.cpp:856:     if (!store_.ApplyBatch(volumeId, changes)) {
IndexPipeline.cpp-857-         return false;
IndexPipeline.cpp-858-     }
IndexPipeline.cpp-859-     size_t upsertCount = 0;
IndexPipeline.cpp-860-     for (const auto& change : changes) {
IndexPipeline.cpp-864-     }
IndexPipeline.cpp-865-     MaybeAnalyzeAfterBulkInsertLocked(upsertCount);
IndexPipeline.cpp-866-     for (const auto& change : changes) {
IndexPipeline.cpp-867-         if (change.kind == ffindexstore::EntryChangeKind::Upsert) {
IndexPipeline.cpp:868:             projection_.UpsertPartial(volumeId, change.record, change.fields);
IndexPipeline.cpp-869-         } else {
IndexPipeline.cpp-870-             projection_.Remove(volumeId, change.record.id);
IndexPipeline.cpp-871-         }
IndexPipeline.cpp-872-     }
IndexPipeline.cpp-911-         const bool injectedFailure = false;
IndexPipeline.cpp-912- #endif
IndexPipeline.cpp-913-         bool secondaryApplied = false;
IndexPipeline.cpp-914-         if (!injectedFailure) {
IndexPipeline.cpp:915:             secondaryApplied = store_.ApplyBatch(volumeId, ruleExcluded);
IndexPipeline.cpp-916-         }
IndexPipeline.cpp-917-         if (secondaryApplied) {
IndexPipeline.cpp-918-             std::vector<ffindexstore::FileId> excludedIds;
IndexPipeline.cpp-919-             excludedIds.reserve(ruleExcluded.size());
IndexPipeline.cpp-922-             }
IndexPipeline.cpp-923-             // large-index-lifecycle-and-search-scalability 4.4: one bulk
IndexPipeline.cpp-924-             // accelerator update instead of per-entry sorted-vector posting
IndexPipeline.cpp-925-             // erases on mass rule removal.
IndexPipeline.cpp:926:             projection_.RemoveBatch(volumeId, excludedIds);
IndexPipeline.cpp-927-         } else {
IndexPipeline.cpp-928-             if (projection_.EnforceMemoryLimitNow()) {
IndexPipeline.cpp-929-                 AdvanceResidencyEpochsForStructuralChangeLocked();
IndexPipeline.cpp-930-             }
IndexPipeline.cpp-1046- #else
IndexPipeline.cpp-1047-         const bool injectedFailure = false;
IndexPipeline.cpp-1048- #endif
IndexPipeline.cpp-1049-         if (!injectedFailure) {
IndexPipeline.cpp:1050:             secondaryApplied = store_.ApplyBatch(id, removals);
IndexPipeline.cpp-1051-         } else {
IndexPipeline.cpp-1052-             secondaryApplied = false;
IndexPipeline.cpp-1053-         }
IndexPipeline.cpp-1054-         if (secondaryApplied) {
IndexPipeline.cpp-1058-                 removedIds.push_back(removal.record.id);
IndexPipeline.cpp-1059-             }
IndexPipeline.cpp-1060-             // large-index-lifecycle-and-search-scalability 4.4: reconciliation
IndexPipeline.cpp-1061-             // mass removal is the same bulk case as rule pruning.
IndexPipeline.cpp:1062:             projection_.RemoveBatch(id, removedIds);
IndexPipeline.cpp-1063-             searchableMutation = true;
IndexPipeline.cpp-1064-         }
IndexPipeline.cpp-1065-     }
IndexPipeline.cpp-1066- 
VolumeSessionManager.cpp-338-                                         {}, std::wstring(1, driveLetter),
VolumeSessionManager.cpp-339-                                         L"reconciliation-pass-aborted-connection-lost", 0, 0);
VolumeSessionManager.cpp-340-             }
VolumeSessionManager.cpp-341-         }
VolumeSessionManager.cpp:342:         pipeline_.SetBulkScanActive(false);
VolumeSessionManager.cpp-343-         return;
VolumeSessionManager.cpp-344-     }
VolumeSessionManager.cpp-345-     const bool wasActive = active_.exchange(true);
VolumeSessionManager.cpp-346-     if (!wasActive) {
VolumeSessionManager.cpp-1006-                                 {}, std::wstring(1, driveLetter),
VolumeSessionManager.cpp-1007-                                 L"scan-complete-before-scan-attempt-started", 0, 0);
VolumeSessionManager.cpp-1008-         return;
VolumeSessionManager.cpp-1009-     }
VolumeSessionManager.cpp:1010:     pipeline_.SetBulkScanActive(anyScanInProgress);
VolumeSessionManager.cpp-1011- 
VolumeSessionManager.cpp-1012-     if (paused || batchFailed) {
VolumeSessionManager.cpp-1013-         // index-durability-false-success-closure D2/D5 (PA-001/PA-005):
VolumeSessionManager.cpp-1014-         // ScanComplete is only authority when the scan actually ran to the
VolumeSessionManager.cpp-1134-                                 {}, std::wstring(1, driveLetter),
VolumeSessionManager.cpp-1135-                                 L"scan-failed-before-scan-attempt-started", 0, 0);
VolumeSessionManager.cpp-1136-         return;
VolumeSessionManager.cpp-1137-     }
VolumeSessionManager.cpp:1138:     pipeline_.SetBulkScanActive(anyScanInProgress);
VolumeSessionManager.cpp-1139-     if (pipeline_.IsReconciliationPassActive(durableId)) {
VolumeSessionManager.cpp-1140-         pipeline_.AbortReconciliationPass(durableId);
VolumeSessionManager.cpp-1141-     }
VolumeSessionManager.cpp-1142-     AppendManagerDiagnostic(ffprotocol::DiagnosticCategory::IndexingError,
VolumeSessionManager.cpp-1326-         anyScanInProgress = std::any_of(
VolumeSessionManager.cpp-1327-             sessionsByEphemeralId_.begin(), sessionsByEphemeralId_.end(),
VolumeSessionManager.cpp-1328-             [](const auto& entry) { return entry.second.scanInProgress; });
VolumeSessionManager.cpp-1329-     }
VolumeSessionManager.cpp:1330:     pipeline_.SetBulkScanActive(anyScanInProgress);
VolumeSessionManager.cpp-1331- }
VolumeSessionManager.cpp-1332- 
VolumeSessionManager.cpp-1333- std::vector<ffprotocol::VolumeStatusRecord> VolumeSessionManager::CollectVolumeStatus() {
VolumeSessionManager.cpp-1334-     std::lock_guard<std::mutex> lock(mutex_);
Store.cpp-1200-     pattern.push_back('%');
Store.cpp-1201-     for (const char ch : needle) {
Store.cpp-1202-         if (ch == '\\' || ch == '%' || ch == '_') pattern.push_back('\\');
Store.cpp-1203-         pattern.push_back(ch);
Store.cpp-1204-     }
Store.cpp-1205-     pattern.push_back('%');
Store.cpp-1206-     return pattern;
Store.cpp-1207- }
Store.cpp-1208- } // namespace
Store.cpp-1209- 
Store.cpp:1210: bool StoreReadSession::ForEachEntryWithAsciiNameUntil(
Store.cpp-1211-     const VolumeRowId volumeRowId, const std::string_view asciiFoldedNeedle,
Store.cpp-1212-     const Store::EntryVisitorUntil& visitor) {
Store.cpp-1213-     // Conservative-superset prefilter; see the header contract. The needle
Store.cpp-1214-     // must be ASCII (the caller gates on it): SQLite LIKE folds ASCII case
Store.cpp-1215-     // only, which is exactly the fold the superset proof needs, and the GLOB
Store.cpp-1216-     // disjunct admits every name carrying non-ASCII bytes (whose canonical
Store.cpp-1217-     // match could pass through a non-ASCII unit under any host ordinal table).
Store.cpp-1218-     if (!IsOpen() || !visitor) {
Store.cpp-1219-         if (!IsOpen()) failed_ = true;
Store.cpp-1220-         return false;
read: docs/benchmarks/phase8-large-index-scalability.md
read: src/indexstore/src/Store.cpp
# Phase 8 — memory-bounded indexed scale certification run
Reference run: 2026-08-24, Release, MSVC 19.44 (VS 2022 BuildTools), Windows,
AMD Ryzen 5 7535HS-class host (12 logical cores), ~23 GiB RAM, NVMe SSD, NTFS
`C:`. Machine-readable runs: [phase8-large-index-scalability.json](phase8-large-index-scalability.json)
(100K/500K/1M) and, when present, [phase8-large-index-scalability-2m.json](phase8-large-index-scalability-2m.json)
(2M). Reproduce with:
```powershell
cmake --build --preset release --target ffengine_bench_large_index_scalability
build\release\tests\engine\ffengine_bench_large_index_scalability.exe ^
  --scales 100000,500000,1000000 --output docs/benchmarks/phase8-large-index-scalability.json
```
This run supersedes the 2026-08-10 baseline in `large-index-scalability.md`
for *architecture* claims: indexed search is no longer purely
projection-resident. Since `memory-bounded-indexed-scale`, a searchable volume
that has been subtree-evicted streams its rows **durably** from SQLite
(`Store::ForEachEntryUntil`) under the same matcher/sorter/cap contract; the
filename-trigram accelerator is used only when every searched volume is
resident. The historical document is retained for provenance; its hot-path
numbers remain representative for fully-resident indexes.
## Index/store stage
| Scale | DB size | Fixture generation | Projection rebuild |
| ---: | ---: | ---: | ---: |
| 100,000 | 7.0 MB | 1.3 s | 0.7 s |
| 500,000 | 36.0 MB | 17.9 s | 3.7 s |
| 1,000,000 | 72.2 MB | 49.6 s | 6.3 s |
## Search medians (ms), warm = all rows projection-resident
| Query class | 100K | 500K | 1M | Results |
| --- | ---: | ---: | ---: | ---: |
| selective | 0.31 | 0.89 | 1.35 | 1 |
| moderate | 57.7 | 344.1 | 588.1 | 4,096 (capped) |
| broad | 150.6 | 934.9 | 1,705.4 | 4,096 (capped) |
| no_match | 0.14 | 1.57 | 3.95 | 0 |
| prefix | 58.2 | 306.1 | 597.7 | 4,096 (capped) |
| extension | 154.4 | 936.6 | 1,736.3 | 4,096 (capped) |
| worst_substring | 89.2 | 553.7 | 1,058.7 | 4,096 (capped) |
## Cold search after forced eviction (`UnloadVolumeFromProjection`)
Projection estimated bytes drop from the full-volume residency to a small
bounded residue, and every query then streams the durable store. Result counts
are **identical** to the warm run for every query class at every scale — this
is the hot/cold completeness contract exercised at scale.
| Query class | 100K cold ms (coldRows) | 500K cold ms | 1M cold ms |
| --- | ---: | ---: | ---: |
| selective | 174.4 (100K) | 1,501.7 | 1,675.0 |
| moderate | 775.5 | 7,087.7 | 7,965.1 |
| broad | 1,954.2 | 10,960.3 | 19,527.7 |
| no_match | 143.2 | 892.9 | 1,511.7 |
| prefix | 785.4 | 4,033.3 | 7,620.7 |
| extension | 2,000.1 | 9,816.4 | 18,970.6 |
| worst_substring | 1,214.4 | 6,831.8 | 13,199.0 |
Cold cost is a bounded, cancellable SQLite stream (linear in indexed rows);
repeated cold searches do not grow or thrash the projection (enforced by
`TestRepeatedColdSearchesDoNotThrashMemory`). Estimated projection bytes at
forced eviction: 41.7 MB → 1.6 MB (100K), 201.6 MB → 7.0 MB (500K),
403.9 MB → 12.6 MB (1M).
## Snapshot publication (256 MiB fail-closed format, v3)
The benchmark measures model/export time, serialized bytes, publish
copy+commit, and UI-side map/parse per scale (see the JSON `snapshot` arrays).
Capacity probe: the largest published snapshot observed was **1,980,421 rows**
against a theoretical single-generation capacity of ≈2,200,468 rows at
268,435,456 bytes. With the bounded working-set publication introduced in this
change, ordinary browsing publishes at most
`kInitialIndexedExportDirectories` (4,096) top-of-tree directories plus the
actively browsed/observed set — payload scales with the working set, and the
256 MiB ceiling now bounds an extreme multi-volume worst case rather than a
routine large-volume scan.
## 2M rows (clean rerun; canonical-path dominated — the trigram accelerator was
not available in this run's residency history, so "warm" here measures the
canonical scan; cold adds the durable stream on top)
| Query class | warm median ms | COLD ms | results |
| --- | ---: | ---: | ---: |
| selective | 2,305.3 | 2,374.2 | 1 |
| moderate | 3,157.4 | 12,737.8 | 4,096 |
| broad | 4,672.7 | 29,827.6 | 4,096 |
| no_match | 2,322.4 | 2,374.5 | 0 |
| prefix | 3,185.0 | 11,551.6 | 4,096 |
| extension | 4,389.1 | 30,912.3 | 4,096 |
| worst_substring | 3,802.0 | 27,026.6 | 4,096 |
DB 145.9 MB; generation 115.8 s; rebuild 20.9 s; forced eviction 903.3 MB →
26.2 MB estimated projection bytes.
## Interpretation
- Fully-resident search remains sub-second to ~1.7 s for capped broad queries
  at 1M rows; selective lookups are low-single-digit milliseconds.
- Cold (evicted) searches are complete but linearly slower; they exist so that
  correctness never depends on RAM. Interactive use keeps its working set
  resident via normal browse/rebuild behavior.
- No wall-clock number here is a CI gate; deterministic equivalence and
  policy tests cover correctness (`ffengine_index_pipeline_tests`,
  `ffindexstore_projection_tests`, `ffengine_seeded_indexed_mode_tests`).
bool StoreReadSession::ForEachEntryWithAsciiNameUntil(
    const VolumeRowId volumeRowId, const std::string_view asciiFoldedNeedle,
    const Store::EntryVisitorUntil& visitor) {
    // Conservative-superset prefilter; see the header contract. The needle
    // must be ASCII (the caller gates on it): SQLite LIKE folds ASCII case
    // only, which is exactly the fold the superset proof needs, and the GLOB
    // disjunct admits every name carrying non-ASCII bytes (whose canonical
    // match could pass through a non-ASCII unit under any host ordinal table).
    if (!IsOpen() || !visitor) {
        if (!IsOpen()) failed_ = true;
        return false;
    }
    if (ShouldFail(ReadSessionFaultPoint::DatabaseClosed)) {
        failed_ = true;
        Close();
        return false;
    }
    Statement stmt(db_,
        "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
        "creation_time, last_modified_time, last_access_time, attributes FROM entries WHERE volume_id=? "
        "AND (name LIKE ? ESCAPE '\\' OR name GLOB '*[^ -~]*');");
    if (!stmt.Valid()) {
        failed_ = true;
        return false;
    }
    const std::string pattern = LikePatternForLiteralNeedle(asciiFoldedNeedle);
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    sqlite3_bind_text(stmt, 2, pattern.data(), static_cast<int>(pattern.size()), SQLITE_TRANSIENT);
    int stepResult;
    while ((stepResult = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (ShouldFail(ReadSessionFaultPoint::Stream)) {
            failed_ = true;
            return false;
        }
        if (!visitor(ReadEntryRecord(stmt))) return true;
    }
    if (stepResult != SQLITE_DONE) failed_ = true;
    return stepResult == SQLITE_DONE;
}
std::optional<EntryRecord> StoreReadSession::FindEntry(VolumeRowId volumeRowId, FileId frn) {
    if (!IsOpen()) {
        failed_ = true;
        return std::nullopt;
    }
    if (ShouldFail(ReadSessionFaultPoint::FindEntry)) {
        failed_ = true;
        return std::nullopt;
    }
    Statement stmt(db_,
        "SELECT frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes, allocated_size_bytes, "
        "creation_time, last_modified_time, last_access_time, attributes "
        "FROM entries WHERE volume_id=? AND frn_low=? AND frn_high=?;");
    if (!stmt.Valid()) {
        failed_ = true;
        return std::nullopt;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(volumeRowId));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(frn.low));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(frn.high));
    const int stepResult = sqlite3_step(stmt);
    if (stepResult == SQLITE_DONE) return std::nullopt;
    if (stepResult != SQLITE_ROW) {
        failed_ = true;
        return std::nullopt;
    }
    return ReadEntryRecord(stmt);
}
Store::FolderAggregate StoreReadSession::GetFolderAggregate(VolumeRowId volumeRowId,
                                                             FileId parentFrn) {
    if (!IsOpen()) {
        failed_ = true;
        return {};
    }
    if (ShouldFail(ReadSessionFaultPoint::Aggregate)) {
        failed_ = true;
        return {};
    }
[8 more lines in file. Use offset=1290 to continue.]
read: src/engine/src/IndexPipeline.h
read: openspec/changes/large-index-lifecycle-and-search-scalabi...
[pi-lens: read widened to the enclosing function_definition "function_definition" (symbol boundary): you asked for lines 380-409, this shows lines 196-473. Re-request with limit > 100 for the exact range.]
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
}
Let me pin exact line numbers for the key symbols I'll cite.
grep: src/indexstore/src/Projection.cpp
grep: src/indexstore/include/ffindexstore/Store.h
Projection.cpp:28: std::u16string FoldAscii(std::u16string_view name) {
Projection.cpp:63: bool UnitMayFoldToAscii(const char16_t unit) {
Projection.cpp:87: bool NameNeedsExceptionalFallback(const std::u16string_view name) {
Projection.cpp:94: uint64_t PackTrigram(std::u16string_view value, size_t offset) {
Projection.cpp:100: std::vector<uint64_t> UniqueTrigrams(std::u16string_view value) {
Projection.cpp:112: void InsertSorted(std::vector<uint32_t>& values, uint32_t value) {
Projection.cpp:117: void EraseSorted(std::vector<uint32_t>& values, uint32_t value) {
Projection.cpp:130: void Projection::SetMaximumMemoryBytes(const size_t maximumBytes) {
Projection.cpp:142: bool Projection::EnforceMemoryLimitNow() {
Projection.cpp:146: void Projection::SetBulkScanActive(const bool active) noexcept {
Projection.cpp:166: void Projection::SetTestingFoldExceptionalUnit(const char16_t unit, const bool exceptional) {
Projection.cpp:174: size_t Projection::EstimatedMemoryBytes() const noexcept {
Projection.cpp:199: Projection::MemoryBreakdown Projection::GetMemoryBreakdown() const noexcept {
Projection.cpp:311: bool Projection::IsMemoryPressureHigh() const noexcept {
Projection.cpp:316: bool Projection::NeedsReload(const VolumeRowId volumeRowId) const {
Projection.cpp:320: Projection::MemoryDiagnostics Projection::GetMemoryDiagnostics() const {
Projection.cpp:332: void Projection::TouchIndex(const uint32_t index) const noexcept {
Projection.cpp:346: std::optional<EntryKey> Projection::LeastRecentlyUsedEntry() const {
Projection.cpp:379: void Projection::EvictSubtree(const EntryKey& rootKey) {
Projection.cpp:419: void Projection::EvictVolume(const VolumeRowId volumeRowId) {
Projection.cpp:445: void Projection::CompactStorage() {
Projection.cpp:482: bool Projection::EnforceMemoryLimit() {
Projection.cpp:505: bool Projection::EnforceMemoryLimitTo(const size_t targetBytes) {
Projection.cpp:524: bool Projection::EnforceMemoryLimitOnceTo(const size_t targetBytes) {
Projection.cpp:538: bool Projection::ReloadEvictedVolume(Store& store, const VolumeRowId volumeRowId) {
Projection.cpp:552: bool Projection::ReloadAllEvictedVolumes(Store& store) {
Projection.cpp:567: void Projection::Reserve(size_t expectedEntryCount) {
Projection.cpp:576: void Projection::CompactEntriesForIterationOrder() {
Projection.cpp:603: void Projection::InvalidateFilenameIndex() noexcept {
Projection.cpp:609: void Projection::AddFilenameIndexEntry(const uint32_t index) {
Projection.cpp:628: void Projection::RemoveFilenameIndexEntry(const uint32_t index, const std::u16string_view name) {
Projection.cpp:645: void Projection::RebuildFilenameIndex() {
Projection.cpp:667: void Projection::RemoveFromChildrenList(const EntryKey& parentKey, uint32_t index) {
Projection.cpp:671: void Projection::Upsert(VolumeRowId volumeRowId, const EntryRecord& record) {
Projection.cpp:755: void Projection::UpsertPartial(VolumeRowId volumeRowId, const EntryRecord& record,
Projection.cpp:821: void Projection::Remove(VolumeRowId volumeRowId, FileId frn) {
Projection.cpp:858: void Projection::RemoveVolume(VolumeRowId volumeRowId, bool keepFilenameIndexCurrent) {
Projection.cpp:870: void Projection::RemoveBatch(VolumeRowId volumeRowId, const std::vector<FileId>& frns,
Projection.cpp:947: std::optional<std::vector<uint32_t>> Projection::FilenameCandidateIndices(
Projection.cpp:1035: std::span<const uint32_t> Projection::ChildIndices(VolumeRowId volumeRowId, FileId parentFrn) const {
Projection.cpp:1042: std::optional<Projection::FolderAggregate> Projection::GetFolderAggregate(VolumeRowId volumeRowId, FileId parentFrn) const {
Projection.cpp:1092: Projection::PathResult Projection::ReconstructPath(VolumeRowId volumeRowId, FileId frn) const {
Projection.cpp:1096: Projection::PathResult Projection::ReconstructPathUntil(
Projection.cpp:1142: bool Projection::RebuildVolumeFromStore(Store& store, VolumeRowId volumeRowId, uint64_t expectedEntryCount) {
Projection.cpp:1230: void Projection::ShrinkBulkRebuildCapacity() {
No matches found
grep: src/indexstore
include/ffindexstore/EntryRecord.h:34: using EntryFieldMask = uint32_t;
include/ffindexstore/EntryRecord.h:35: constexpr EntryFieldMask kEntryFieldParentId = 1u << 0;
include/ffindexstore/EntryRecord.h:36: constexpr EntryFieldMask kEntryFieldName = 1u << 1;
include/ffindexstore/EntryRecord.h:37: constexpr EntryFieldMask kEntryFieldSizeBytes = 1u << 2;
include/ffindexstore/EntryRecord.h:38: constexpr EntryFieldMask kEntryFieldCreationTime = 1u << 3;
include/ffindexstore/EntryRecord.h:39: constexpr EntryFieldMask kEntryFieldLastModifiedTime = 1u << 4;
include/ffindexstore/EntryRecord.h:40: constexpr EntryFieldMask kEntryFieldLastAccessTime = 1u << 5;
include/ffindexstore/EntryRecord.h:41: constexpr EntryFieldMask kEntryFieldAttributes = 1u << 6;
include/ffindexstore/EntryRecord.h:45: constexpr EntryFieldMask kEntryFieldAllocatedSizeBytes = 1u << 7;
include/ffindexstore/EntryRecord.h:46: constexpr EntryFieldMask kEntryFieldMaskAll =
include/ffindexstore/EntryRecord.h:51: constexpr bool HasEntryField(EntryFieldMask fields, EntryFieldMask field) noexcept {
include/ffindexstore/EntryRecord.h:55: enum class EntryChangeKind : uint8_t {
include/ffindexstore/EntryRecord.h:60: struct EntryChange {
include/ffindexstore/EntryRecord.h:61:     EntryChangeKind kind = EntryChangeKind::Upsert;
include/ffindexstore/EntryRecord.h:63:     EntryFieldMask fields = kEntryFieldMaskAll;
include/ffindexstore/Store.h:176:     bool ApplyBatch(VolumeRowId volumeRowId, const std::vector<EntryChange>& changes);
include/ffindexstore/Projection.h:265:                        EntryFieldMask fields);
## Review — Lane B: Durable trigram postings research (read-only; no files modified)
### Correct — verified current structure (with evidence)
**(1a) In-memory postings shape.** `Projection::filenameTrigramPostings_` is `std::unordered_map<uint64_t, std::vector<uint32_t>>` (`src/indexstore/include/ffindexstore/Projection.h:488`). Key = a packed 48-bit trigram: `u0 | u1<<16 | u2<<32` over folded UTF-16 units (`src/indexstore/src/Projection.cpp:94-98`, `PackTrigram`). Values = **sorted-ascending dense slot indices** (`uint32_t`), maintained by `InsertSorted`/`EraseSorted` (`Projection.cpp:112-121`) on incremental updates; the bulk path push_backs unsorted then `std::sort` + `unique` per posting (`RebuildFilenameIndex`, `Projection.cpp:645-665`) and for the exceptional list (`:659-663`). Empty postings are erased (`RemoveFilenameIndexEntry`, `:637`).
**(1b) Folding.** `FoldAscii` (`Projection.cpp:28-34`) is length-preserving and lowers **only ASCII A–Z** (ch+0x20); non-ASCII units pass through unchanged. `UniqueTrigrams` (`:100-109`) takes len−2 sliding windows, sorts and dedups → per-name unique trigram set; names < 3 units get no postings. The trigram map is `std::unordered_map` (not a sorted structure); per-posting vectors are sorted.
**(1c) Exceptional set.** One combined sorted `std::vector<uint32_t> filenameExceptionalEntries_` (`Projection.h:489-494`) = fold-exceptional ∪ surrogate-bearing names. `UnitMayFoldToAscii` (`Projection.cpp:63-85`): surrogates 0xD800–0xDFFF are unconditionally exceptional (`:65`); other non-ASCII units are probed lazily against all 128 ASCII units with `CompareStringOrdinal(..., TRUE)`, cached in `std::atomic<uint8_t> gFoldProbe[65536]` (`:56`); probe failure fails **closed** (→ exceptional). The header comment (`:38-45`) records the measured campaign host: zero BMP units are fold-exceptional (U+017F/U+212A/U+0131 all fail to match ASCII), so on that host the set ≈ surrogate names only.
**(1d) Cap and read path.** `kMaximumFilenameIndexCandidates = 16 * 1024` (`src/engine/src/IndexPipeline.h:399`). `FilenameCandidateIndices` (`Projection.cpp:947-1032`): eligibility (ASCII folded needle ≥ 3, index valid) → unique needle trigrams → hash lookups (one missing trigram ⇒ empty intersection, `:960-969`) → postings sorted by size, early bail if smallest > cap (`:973-976`) → `set_intersection` into a scratch buffer, cap-checked each pass (`:978-987`) → `set_union` with `filenameExceptionalEntries_` (`:1013-1028`) → final cap check; any oversize/exception → `nullopt` → canonical scan. Engine side: `IndexPipeline.cpp:1500-1509` (eligible only when simple literal, folded ≥ 3, and **no cold volumes**), verification in 256-row chunks (`kHotCandidateChunkRows`, `IndexPipeline.h:385`).
**(1e) Sizes implied by the 200k/1M/2M fixtures (10% non-ASCII).** Fixture generator: `tests/engine/bench_large_index_scalability.cpp` — `DirectoryName` (`:296-298`), `FileName` (`:300-316`, pattern 8 = `список_<n>.txt`, the 10% Cyrillic subset per comment `:310-314`), `BuildSyntheticDatabase` (`:557-618`: `directoryCount = scale/64` at `:579`; files = scale − 1 − dirs at `:610`). Confirmed against the memory report (`directory_count:3125, file_count:196874` at 200k, `evidence/rebuild/memory-report-after-fix.json:21`). Name-length arithmetic (all lengths UTF-16 units):
- Per-pattern base length (excluding digits): `common_component_.txt` 21 (2/10 of rows), `report_.txt` 11, `photo_.jpg` 10, `archive_.zip` 12, `project_.cpp` 12, `notes_.md` 9, `common_media_.mp4` 17, `список_.txt` 11 (7 Cyrillic+`_`+4), `backup_.7z` 10 → mean base **13.4**.
- Mean digit suffix over file indices: ≈ 5.2 (200k) / 5.89 (1M) / 6.44 (2M) → mean file name ≈ 18.6 / 19.3 / 19.8 units; directory names ≈ 21.5–22.2 (recomputed from `DirectoryName` digit runs). Weighted mean ≈ **18.6 / 19.3 / 19.9 units** per row.
- Posting references ≈ (L−2)/row (`UniqueTrigrams`) ≈ 16.6 / 17.3 / 17.9 → **≈ 3.3M / 17.3M / 35.8M refs at 200k/1M/2M**. Non-ASCII rows (19.7k / 98.4k / 196.9k) are fold-ordinary on the campaign host (Cyrillic BMP) and contribute ordinary folded trigrams — they are **not** in the exceptional set (the historical `filenameNonAsciiEntries_` union that killed the accelerator above the 16,384 cap is gone; see `evidence/explore-findings.md` "Planner root cause" and `design.md` D4).
- In-memory cross-check: model benchmark container-estimate 8.8 / 45.3 / 91.2 / 168.6 MiB at 100K/500K/1M/2M (`docs/benchmarks/secondary-filename-index.md`, "Build and memory scale approximately linearly") = ≈ 88–96 B/row, consistent with 17.3M refs × 4 B (66 MiB payload at 1M) × ~1.2 push_back capacity slack + per-posting headers — exactly what `GetMemoryBreakdown` models (`Projection.cpp:255-276`, which reports posting *capacity* not just payload).
### Findings (design-relevant; no diff under review)
- **P1 — Row identity for durable postings must NOT be the projection's slot index.** `uint32_t` values in the postings are dense slot indices that are remapped by `CompactStorage` (`Projection.cpp:445-480`) and recycled via `freeList_` (`:821-856`); `FilenameCandidateIndices` explicitly scopes them to "this projection generation" (`Projection.h:300-303`). The durable store has **no rowid**: `entries` is `WITHOUT ROWID, PRIMARY KEY (volume_id, frn_low, frn_high)` (`src/indexstore/src/Store.cpp:38-55`), and `FileId` is 128-bit for ReFS (`src/indexstore/include/ffindexstore/Identity.h:12-20`). Durable postings must key on `(volume_id, frn_low, frn_high)` or on a **new durable dense seqid** assigned at `ApplyBatch` time and invalidated by `ForgetVolume` (`Store.cpp:713-750`).
- **P1 — Fold-exceptional classification is host-table-dependent and must not be persisted raw.** It is derived from `CompareStringOrdinal` on the running host (`Projection.cpp:38-52`), not from Unicode data. See section (4) below for the safe persistence contract.
- **P2 — The insert side still has the removal pathology's primitive.** `InsertSorted` (`Projection.cpp:112-115`) is an O(posting-length) memmove per trigram per upserted row — the exact cost class that made `EraseSorted` removal quadratic (measured 12,983 ms vs 44.8 ms for 200k shared-name rows, comment at `Projection.cpp:876-882`; isolation matrix B1 10,225.2 ms vs E1 28.7 ms, `evidence/explore-findings.md:18-20`). `ApplyUsnBatchChanges` (`src/engine/src/IndexPipeline.cpp:853-872`) applies every USN upsert per-entry while the accelerator is live (bulk scans are the exception — `SetBulkScanActive(true)` invalidates it, `Projection.cpp:146-164`); a post-scan burst of shared-name rows (fixture `common_component_` class: 20% of rows share ~15 trigrams whose postings reach ~200k slots at 1M → ~800 KB memmove per trigram) is production-reachable. A durable schema must use O(log n) btree inserts or bounded chunk appends, never sorted-vector insert/erase.
- **P2 — Cold durable prefilter carries pre-6.3 union semantics.** `StoreReadSession::ForEachEntryWithAsciiNameUntil` (`Store.cpp:1210-1246`) admits every non-ASCII name via `name GLOB '*[^ -~]*'` — a superset (safe) but the same cap-hostile shape the planner fix removed. A durable postings index replacing it must mirror the 6.3 semantics (intersection ∪ **exceptional-only**), or the "10% Unicode rows blow the 16,384 cap" pathology returns on the cold path.
### (2) Durable schema design (non-quadratic bulk removal)
Recommended first cut — **row-per-posting btree**, sharing the `ApplyBatch` transaction:
```sql
CREATE TABLE filename_trigrams (
    trigram   INTEGER NOT NULL,          -- packed 48-bit, exactly PackTrigram's key
    volume_id INTEGER NOT NULL,
    frn_low   INTEGER NOT NULL,
    frn_high  INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (trigram, volume_id, frn_low, frn_high)
) WITHOUT ROWID;                         -- leaf order == posting order (sorted!)
CREATE TABLE filename_index_meta (
    volume_id INTEGER PRIMARY KEY, state TEXT,        -- 'building'|'complete'|'invalid'
    generation INTEGER, entries_epoch INTEGER, fold_signature BLOB );
```
- **Why this cannot reproduce the quadratic erase:** a btree delete is O(log n) *regardless of posting length*; the predecessor's pathology was O(posting) memmove inside a sorted vector. Batch removal = one `DELETE ... WHERE (volume_id, frn_low, frn_high) IN (...)` or per-row deletes **inside the same `BEGIN IMMEDIATE ... COMMIT` as the `entries` deletes** (`Store::ApplyBatch` is already one transaction with commit-result contract, `Store.cpp:763-866`) — total O(B·T·log n), linear in batch size. Keep `kPerEntryFilenameRemovalThreshold = 256` (`Projection.cpp:127`, `RemoveBatch` `:870-899`) as the policy for the *in-memory* accelerator only; for the durable btree it is optional (a threshold-triggered one-shot rebuild remains useful after `ForgetVolume`/reconciliation mass removals — `IndexPipeline.cpp:1050-1062`).
- **Bulk rebuild without double-buffering:** the rejected alternative (fresh structure + swap) measured ~1 GB peak at 1M rows (`design.md` D1/D2). Use a **generation column** on the postings (fold it into `trigram` ordering or add `generation` to the PK): rebuild writes generation g+1 with `state='building'`, one final transaction flips `active_generation`/`state='complete'`, old generation rows are dropped lazily (bounded deletes per idle tick). Queries against a non-`complete` state return "index unavailable" → canonical path — exactly the existing `nullopt` contract (`Projection.h:296-303`).
- **Consistency markers:** postings writes join the `entries` transaction, so torn divergence is impossible within SQLite's WAL atomicity; `entries_epoch`/`generation` detects external corruption or schema drift at open (mirror the fail-closed `user_version` discipline, `Store.cpp:16-22, 380-403`). Startup rebuild hook mirrors `RebuildFilenameIndex` at `SetBulkScanActive(false)` (`Projection.cpp:146-164`) — which exists specifically because the accelerator otherwise stays dead after bulk scans (review-lane C Finding 3).
- **Footprint-optimized variant** (if the row-per-posting size proves too fat): per-trigram chunked BLOB streams `(trigram, chunk_seq, refs BLOB)` with delta+varint packed sorted seqids (4096-ref chunks; insert = bounded ≤16 KB chunk rewrite; delete = chunk rewrite/tombstone + lazy compaction; bulk removal = generation swap). This is the only design that reaches 2–4 B/ref.
### (3) Cost model + footprint estimate per 1M rows
- **Incremental USN batch (in-memory, current):** per record `UpsertPartial`→`Upsert`→`AddFilenameIndexEntry` (`Projection.cpp:609-626`) = fold + trigram extraction O(L) + per-trigram hash lookup + `InsertSorted` O(posting). Hot-trigram inserts cost ms/row at 1M scale (P2 above). Durable btree inserts: O(T·log n) ≈ 17 × log₂(17M) ≈ 17×24 ≈ 400 btree ops per record, each a small cell — expect ~50–200 µs/record, batched in one transaction.
- **Deletion:** in-memory per-entry O(posting) erase; >256 rows → O(1) invalidate + one O(N) rebuild (`RemoveBatch`, `Projection.cpp:870-899`; measured 200k rows 12,983 ms → 44.8 ms skip-mode, 22 ms post-fix arm B1, `evidence/rebuild/rebuild-isolation-after-fix.json:38`). Durable: O(B·T·log n), no threshold pathology to avoid.
- **Query-time intersection:** see (1d); cost = Σ needle-trigram lookups + merge of sorted postings by size order + O(K) canonical verifications (K ≤ 16,384 or `nullopt`). Measured outcome classes: selective 1 candidate / no_match 15,625 false-positive candidates at 1M / broad falls back by cap (`evidence/explore-findings.md` planner-fix matrix; `search-matrix-planner-fix.json`).
- **Disk footprint arithmetic per 1M rows** (fixture-derived, refs = 17.3M from section 1e):
  - *Row-per-posting WITHOUT ROWID*: cell = trigram varint ≈ 6 B (packed key ≥ 2³² → 5–6 B) + volume_id 2 B + frn_low 4–6 B + frn_high 0–1 B + cell header/pointer ≈ 8 B → **22–30 B/ref** → 17.3M × 22–30 ≈ **380–520 MB per 1M rows** (~6× the `entries` table itself: measured DB = 72.2 MB @1M, 145.9 MB @2M, `docs/benchmarks/phase8-large-index-scalability.md:24-30, 80-92`). Simple and transactional, but fat.
  - *Chunked delta-varint seqid streams*: 2–4 B/ref + ~12 B per distinct trigram (low tens of thousands — fixed prefixes + digit-window vocabulary) → 17.3M × 2–4 ≈ **35–70 MB per 1M rows** (≈ +50–100% of the entries DB). Sanity cross-check: model bench's persistent proxy (8 B key + 4 B/ref, `tests/engine/bench_secondary_filename_index.cpp:349, 355`) = ~70 MB at 1M, inside this range.
  - *Exceptional postings*: ≈ 0 rows on the campaign-host fixture (Cyrillic is fold-ordinary); worst case (persist-all-non-ASCII like the GLOB prefilter) = 100k rows/1M × ~20 B ≈ 2 MB.
  - **Estimate: 35–70 MB per 1M rows for the recommended packed design (380–520 MB naive); state the variant with every number.**
### (4) Unicode-exception persistence & host consistency
- Persist **host-independent facts** only: surrogate-bearing (unit range test, `Projection.cpp:65`) and non-ASCII presence per name — derivable from `entries.name` (already durable as UTF-8 TEXT, `Store.cpp:47`), so the minimal design persists *nothing* and recomputes the exceptional set at rebuild from names exactly as `RebuildFilenameIndex` does (`Projection.cpp:645-665`), via the memoized `gFoldProbe` (O(Σ name chars)).
- If the classified set is persisted to skip recompute, bind it to a **`fold_signature`** in `filename_index_meta`: a hash over the host's full 65,280-unit probe verdicts (or the filled `gFoldProbe` vector). On open: recompute signature; mismatch (different host/table/OS build) ⇒ mark exceptional postings invalid, recompute from names, or fall back to "all non-ASCII names are exceptional" (the safe pre-6.3 superset — costs candidates, never misses). Any probe/API failure keeps the fail-closed classification (`Projection.cpp:76-79`).
- The durable surrogate set (host-independent) can be persisted keyed `(volume_id, frn)`; at query time the merge must be exactly the 6.3 shape: posting intersection `set_union` exceptional set (`Projection.cpp:1013-1028`), preserving the conservative-superset contract (false negatives forbidden — `specs/index-engine/spec.md:32-41`, `Projection.h:291-303`). Never persist narrowed semantics across hosts.
### Measurement plan (for a human; all commands run from a VS developer PowerShell)
1. **Pin the ref-count derivation:** `cmake --build --preset release-test --target ffengine_bench_large_index_scalability`, then `build\release-test\tests\engine\ffengine_bench_large_index_scalability.exe --memory-report <out.json> --scales 200000,1000000,2000000 --rebuild-cycles 2 --preset release-test --diagnostics`; read `MemoryBreakdown.filenameIndexDistinctTrigrams / PostingSlots / PostingPayload / ExceptionalCount` (`Projection.h:181-193`) and compare against 3.3M/17.3M/35.8M refs and the 88–96 B/row model cross-check.
2. **Prototype both durable schemas** on a scratch copy of a fixture DB (fixture DB sizes already known: 15.1/72.2/145.9 MB at 200k/1M/2M); bulk-load postings from `entries.name`; `VACUUM` + `PRAGMA page_count` → actual bytes/row per variant; run the benchmark's 12 query classes through the postings vs. `ForEachEntryWithAsciiNameUntil` (`Store.cpp:1210`) to quantify the cold-path win.
3. **Mutation probes** mirroring `secondary-filename-index.md`'s protocol (100/1,000/10,000 insert/delete/rename ops at 2M — prior run: 33/4/45, 35/27/453, 45/268/4,574 ms) on both prototype and in-memory accelerator; extend `tests/engine/bench_rebuild_isolation.cpp` (`FF_INDEXSTORE_TESTING` seam) with **insert-path** arms over shared-name batches (`common_component_` class) to expose the `InsertSorted` cost curve (P2).
4. **Crash/consistency:** kill between `ApplyBatch` COMMIT and projection apply; reopen; assert `filename_index_meta` state forces fallback and lazy rebuild; exercise `SetTestingFoldExceptionalUnit` (`Projection.cpp:166-172`) as the simulated host-table change and prove `fold_signature` invalidation + conservative reclassification.
5. **Gates:** `ctest --test-dir build/debug -R indexstore --output-on-failure`, `ctest --test-dir build/debug -R ffengine_index_pipeline_tests --output-on-failure`, then `ctest --preset debug` (existing anchors: `test_projection.cpp:1058-1136` Unicode-heavy fixtures, `tests/indexstore/test_store_read_session.cpp:265-321` ASCII-class prefilter).
### Merge verdict
OK — research lane only; no diff to merge and no repository state changed. Two design-critical P1s (row identity; host-bound fold classification) and two P2s (sorted-vector insert cost; cold-prefilter union semantics) must be carried into any proposal that follows.
