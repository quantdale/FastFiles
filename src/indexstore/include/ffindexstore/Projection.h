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
    // tasks.md 7.4's projection half: drops every entry belonging to a
    // forgotten volume (dense-slot tombstoning, id->index and
    // parent->children rows) -- equivalent to calling Remove on each of
    // the volume's entries. Interned names are not reclaimed, exactly as
    // with individual Remove (the pool is reclaimed on the next full
    // rebuild -- design.md "Risks" name-pool eviction note).
    void RemoveVolume(VolumeRowId volumeRowId);

    const ProjectionEntry* Find(VolumeRowId volumeRowId, FileId frn) const;

    // Returns a conservative candidate set for an already ASCII-folded
    // simple literal. nullopt means the secondary accelerator is ineligible
    // or unavailable, so callers must use the canonical live-entry scan.
    // Returned slot indices belong to this projection generation and are
    // valid only while the caller holds the projection's synchronization.
    std::optional<std::vector<uint32_t>> FilenameCandidateIndices(std::u16string_view foldedNeedle) const;

    // task 2.3: parent->children lookup for directory-listing/Column-View
    // style access. Returns empty span if the parent has no known children
    // (which is not the same as the parent itself being unknown).
    std::span<const uint32_t> ChildIndices(VolumeRowId volumeRowId, FileId parentFrn) const;

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

    // task 3.1: rebuilds this volume's portion of the projection from the
    // durable store in a single streaming pass, interning names and
    // building the parent->children index as rows arrive.
    // expectedEntryCount pre-sizes allocations (task 2.5/D4); the caller
    // gets this from Store::GetVolumeMetadata(...)->entryCount.
    void RebuildVolumeFromStore(Store& store, VolumeRowId volumeRowId, uint64_t expectedEntryCount);

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
    void EnforceMemoryLimit();
    void EnforceMemoryLimitTo(size_t targetBytes);
    void EnforceMemoryLimitOnceTo(size_t targetBytes);
    std::optional<EntryKey> LeastRecentlyUsedEntry() const;
    void EvictSubtree(const EntryKey& rootKey);
    void CompactStorage();
    void Touch(const EntryKey& key) const noexcept;
    void CompactEntriesForIterationOrder();
    void RemoveFromChildrenList(const EntryKey& parentKey, uint32_t index);
    void RebuildFilenameIndex();
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
    std::vector<uint32_t> filenameNonAsciiEntries_;
    bool filenameIndexValid_ = true;
    bool filenameIndexBuilding_ = false;
    size_t maximumMemoryBytes_ = kDefaultMaximumMemoryBytes;
    mutable uint64_t accessClock_ = 0;
    mutable std::unordered_map<EntryKey, uint64_t, EntryKeyHash> lastAccess_;
    std::unordered_set<VolumeRowId> evictedVolumes_;
    bool enforcingMemoryLimit_ = true;
};

} // namespace ffindexstore

