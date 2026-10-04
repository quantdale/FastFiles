#include "ffindexstore/Projection.h"

#include <algorithm>
#include <iterator>
#include <limits>

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

void Projection::EnforceMemoryLimitNow() {
    EnforceMemoryLimit();
}

void Projection::SetBulkScanActive(const bool active) noexcept {
    if (active) {
        InvalidateFilenameIndex();
        enforcingMemoryLimit_ = false;
    } else {
        enforcingMemoryLimit_ = true;
    }
}

size_t Projection::EstimatedMemoryBytes() const noexcept {
    size_t result = entries_.capacity() * sizeof(ProjectionEntry)
        + freeList_.capacity() * sizeof(uint32_t)
        + idToIndex_.MemoryUsageBytes()
        + parentToChildren_.MemoryUsageBytes()
        + namePool_.MemoryUsageBytes()
        + filenameNonAsciiEntries_.capacity() * sizeof(uint32_t)
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
    result.childMapChildLinks = parentToChildren_.ChildLinkCount();
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
    // a sorted non-ASCII entry list. Posting vector capacity (not only the
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
    result.filenameIndexNonAsciiCount = filenameNonAsciiEntries_.size();
    result.filenameIndexNonAsciiSlots = filenameNonAsciiEntries_.capacity();
    result.filenameIndexNonAsciiBytes =
        result.filenameIndexNonAsciiSlots * sizeof(uint32_t);
    result.filenameIndexBytes = result.filenameIndexPostingBytes
        + result.filenameIndexApproxNodeBytes
        + result.filenameIndexBucketBytes
        + result.filenameIndexNonAsciiBytes;

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

void Projection::EnforceMemoryLimit() {
    if (!enforcingMemoryLimit_) return;
    // Leave headroom after a compaction so the next ingestion batches do not
    // immediately re-enter the full-index rebuild path.
    const size_t lowWatermark = maximumMemoryBytes_ - maximumMemoryBytes_ / 5;
    EnforceMemoryLimitOnceTo(lowWatermark);
}

void Projection::EnforceMemoryLimitTo(const size_t targetBytes) {
    if (!enforcingMemoryLimit_) return;
    while (EstimatedMemoryBytes() > targetBytes) {
        const auto victim = LeastRecentlyUsedEntry();
        if (!victim.has_value()) break;
        const size_t before = EstimatedMemoryBytes();
        EvictSubtree(*victim);
        CompactStorage();
        // memory-bounded-indexed-scale 3.1: the multi-pass shrink path records
        // each pass exactly like the single-shot watermark path.
        lastEvictionBytesBefore_ = before;
        lastEvictionBytesAfter_ = EstimatedMemoryBytes();
        if (EstimatedMemoryBytes() >= before && idToIndex_.empty()) break;
    }
}

void Projection::EnforceMemoryLimitOnceTo(const size_t targetBytes) {
    if (!enforcingMemoryLimit_ || EstimatedMemoryBytes() <= targetBytes) return;
    const auto victim = LeastRecentlyUsedEntry();
    if (!victim.has_value()) return;
    const size_t bytesBefore = EstimatedMemoryBytes();
    EvictSubtree(*victim);
    CompactStorage();
    // memory-bounded-indexed-scale 3.1: record the policy's actual effect so
    // tests can distinguish "eviction ran" from "eviction changed nothing".
    lastEvictionBytesBefore_ = bytesBefore;
    lastEvictionBytesAfter_ = EstimatedMemoryBytes();
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
    idToIndex_.ForEach([&](const EntryKey&, const uint32_t oldIndex) {
        remap[oldIndex] = static_cast<uint32_t>(reordered.size());
        reordered.push_back(entries_[oldIndex]);
    });
    if (reordered.size() != entries_.size()) return;

    idToIndex_.RemapValues([&](const uint32_t oldIndex) { return remap[oldIndex]; });
    parentToChildren_.RemapValues([&](const uint32_t oldIndex) { return remap[oldIndex]; });
    entries_ = std::move(reordered);
    denseIterationOrderValid_ = true;
}

void Projection::InvalidateFilenameIndex() noexcept {
    filenameIndexValid_ = false;
    filenameTrigramPostings_.clear();
    filenameNonAsciiEntries_.clear();
}

void Projection::AddFilenameIndexEntry(const uint32_t index) {
    if (!filenameIndexValid_) return;
    try {
        const std::u16string_view name = namePool_.Get(entries_[index].nameId);
        if (!IsAscii(name)) {
            if (filenameIndexBuilding_) filenameNonAsciiEntries_.push_back(index);
            else InsertSorted(filenameNonAsciiEntries_, index);
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
        if (!IsAscii(name)) EraseSorted(filenameNonAsciiEntries_, index);
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
    filenameNonAsciiEntries_.clear();
    filenameIndexValid_ = true;
    filenameIndexBuilding_ = true;
    try {
        idToIndex_.ForEachValue([&](const uint32_t index) { AddFilenameIndexEntry(index); });
        for (auto& [trigram, posting] : filenameTrigramPostings_) {
            (void)trigram;
            std::sort(posting.begin(), posting.end());
            posting.erase(std::unique(posting.begin(), posting.end()), posting.end());
        }
        std::sort(filenameNonAsciiEntries_.begin(), filenameNonAsciiEntries_.end());
        filenameNonAsciiEntries_.erase(
            std::unique(filenameNonAsciiEntries_.begin(), filenameNonAsciiEntries_.end()),
            filenameNonAsciiEntries_.end());
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
    denseIterationOrderValid_ = false;
    const uint32_t index = *pIndex;
    RemoveFilenameIndexEntry(index, namePool_.Get(entries_[index].nameId));
    RemoveFromChildrenList(EntryKey{volumeRowId, entries_[index].parentFrn}, index);
    idToIndex_.erase(key);
    if (index < lastAccessTimes_.size()) lastAccessTimes_[index] = 0;
    freeList_.push_back(index);
    // entries_[index] itself is left in place (tombstoned) -- nothing
    // reachable via idToIndex_/parentToChildren_ points at it anymore, and
    // it will be overwritten the next time freeList_ hands its index back
    // out in Upsert.
}

void Projection::RemoveVolume(VolumeRowId volumeRowId) {
    // Collect first, then remove: Remove() mutates idToIndex_, so it can't
    // run while iterating it.
    std::vector<FileId> frns;
    for (const auto& pair : idToIndex_) {
        if (pair.first.volumeRowId == volumeRowId) {
            frns.push_back(pair.first.frn);
        }
    }
    for (const FileId& frn : frns) {
        Remove(volumeRowId, frn);
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
      iterator_(dense_ ? EntryMap::Iterator{} : projection.idToIndex_.begin()) {}

bool ProjectionEntryCursor::Next(const ProjectionEntry*& entry) {
    entry = nullptr;
    if (projection_ == nullptr) return false;
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
        for (const uint64_t trigram : trigrams) {
            const auto it = filenameTrigramPostings_.find(trigram);
            if (it == filenameTrigramPostings_.end()) {
                result.clear();
                break;
            }
            postings.push_back(&it->second);
        }
        if (!postings.empty()) {
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
        std::vector<uint32_t> merged;
        if (maximumCandidates != 0 && result.size() > maximumCandidates) {
            return std::nullopt;
        }
        const size_t maximumSize = (std::numeric_limits<size_t>::max)();
        const size_t nonAsciiCount = filenameNonAsciiEntries_.size();
        const size_t combinedSize = nonAsciiCount > maximumSize - result.size()
            ? maximumSize : result.size() + nonAsciiCount;
        const size_t reserveSize = maximumCandidates == 0
            ? combinedSize
            : (std::min)(maximumCandidates == maximumSize ? maximumSize : maximumCandidates + 1,
                         combinedSize);
        merged.reserve(reserveSize);
        std::set_union(result.begin(), result.end(), filenameNonAsciiEntries_.begin(),
                       filenameNonAsciiEntries_.end(), std::back_inserter(merged));
        if (maximumCandidates != 0 && merged.size() > maximumCandidates) {
            return std::nullopt;
        }
        return merged;
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
        RemoveVolume(volumeRowId);
        CompactStorage();
    }
    evictedVolumes_.erase(volumeRowId);
    denseIterationOrderValid_ = false;
    filenameIndexValid_ = false;
    Reserve(EntryCount() + expectedEntryCount);
    const bool streamed = store.ForEachEntry(volumeRowId, [&](const EntryRecord& record) {
        Upsert(volumeRowId, record);
    });
    if (!streamed) {
        // completion-integrity-hardening-and-release-certification 4.1.2:
        // a failed durable read may have delivered a prefix of the rows. Do
        // not expose that prefix as a resident volume; retain the cold marker
        // so a later rebuild can retry from the durable source.
        RemoveVolume(volumeRowId);
        CompactStorage();
        evictedVolumes_.insert(volumeRowId);
        enforcingMemoryLimit_ = wasEnforcingMemoryLimit;
        EnforceMemoryLimit();
        return false;
    }
    CompactEntriesForIterationOrder();
    RebuildFilenameIndex();
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
    size_t slackBytes = namePool_.GrowthSlackBytes();
    slackBytes += parentToChildren_.ChildCapacityBytes()
        - parentToChildren_.ChildLinkCount() * sizeof(uint32_t);
    slackBytes += filenameNonAsciiEntries_.capacity() * sizeof(uint32_t)
        - filenameNonAsciiEntries_.size() * sizeof(uint32_t);
    for (const auto& [trigram, posting] : filenameTrigramPostings_) {
        (void)trigram;
        slackBytes += (posting.capacity() - posting.size()) * sizeof(uint32_t);
    }
    if (slackBytes < kMaterialSlackBytes) return;

    namePool_.ShrinkToFit();
    parentToChildren_.ShrinkToFit();
    filenameNonAsciiEntries_.shrink_to_fit();
    for (auto& [trigram, posting] : filenameTrigramPostings_) {
        (void)trigram;
        posting.shrink_to_fit();
    }
}

} // namespace ffindexstore
