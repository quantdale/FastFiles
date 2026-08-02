#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "ffindexstore/EntryRecord.h"
#include "ffindexstore/FlatHashMap.h"
#include "ffindexstore/Identity.h"
#include "ffindexstore/NamePool.h"

namespace ffindexstore {

// FlatChildrenMap provides EntryKey -> vector<uint32_t> mapping using
// contiguous storage for values to eliminate per-key heap allocation.
class FlatChildrenMap {
public:
    using Value = std::vector<uint32_t>;

    size_t size() const noexcept { return keys_.size(); }
    bool empty() const noexcept { return keys_.empty(); }

    const Value* find(const EntryKey& key) const {
        size_t idx = find_slot(key);
        if (idx == keys_.size() || states_[idx] != 1) return nullptr;
        return &values_[offsets_[idx]];
    }

    Value* find(const EntryKey& key) {
        return const_cast<Value*>(static_cast<const FlatChildrenMap*>(this)->find(key));
    }

    bool contains(const EntryKey& key) const { return find(key) != nullptr; }

    // Insert a single child index for `key`. Appends to the existing
    // value's contiguous slice, or creates a new one.
    void insert(const EntryKey& key, uint32_t childIndex) {
        size_t idx = find_slot(key);
        if (idx < keys_.size() && states_[idx] == 1) {
            values_.push_back(childIndex);
            ++counts_[idx];
            return;
        }
        if (idx == keys_.size()) {
            idx = keys_.size();
            keys_.push_back(key);
            states_.push_back(1);
            offsets_.push_back(values_.size());
            counts_.push_back(1);
            values_.push_back(childIndex);
        } else {
            keys_[idx] = key;
            states_[idx] = 1;
            offsets_[idx] = values_.size();
            counts_[idx] = 1;
            values_.push_back(childIndex);
        }
    }

    // Remove `childIndex` from the vector stored at `key`.
    void remove(const EntryKey& key, uint32_t childIndex) {
        size_t idx = find_slot(key);
        if (idx == keys_.size() || states_[idx] != 1) return;
        size_t off = offsets_[idx];
        size_t cnt = counts_[idx];
        for (size_t i = 0; i < cnt; ++i) {
            if (values_[off + i] == childIndex) {
                values_[off + i] = values_[off + cnt - 1];
                --counts_[idx];
                if (counts_[idx] == 0) {
                    states_[idx] = 2; // tombstone
                }
                return;
            }
        }
    }

    void clear() noexcept {
        keys_.clear();
        values_.clear();
        states_.clear();
        offsets_.clear();
        counts_.clear();
    }

    struct Iterator {
        const FlatChildrenMap* map = nullptr;
        size_t index = 0;

        std::pair<const EntryKey&, const std::vector<uint32_t>&> operator*() const {
            return {map->keys_[map->offsets_[index]], map->values_};
        }
        Iterator& operator++() {
            do { ++index; } while (index < map->keys_.size() && map->states_[index] != 1);
            return *this;
        }
        bool operator!=(const Iterator& other) const { return index != other.index; }
    };

    Iterator begin() const {
        size_t i = 0;
        while (i < keys_.size() && states_[i] != 1) ++i;
        return Iterator{this, i};
    }
    Iterator end() const { return Iterator{this, keys_.size()}; }

private:
    std::vector<EntryKey> keys_;
    std::vector<uint32_t> values_; // contiguous storage for all child-index vectors
    std::vector<uint8_t> states_;
    std::vector<size_t> offsets_; // per-key offset into values_
    std::vector<size_t> counts_;  // per-key count of values

    static size_t pow2_ceil(size_t n) {
        if (n <= 1) return 1;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    void grow(size_t new_buckets) {
        std::vector<EntryKey> old_keys = std::move(keys_);
        std::vector<uint32_t> old_values = std::move(values_);
        std::vector<uint8_t> old_states = std::move(states_);
        std::vector<size_t> old_offsets = std::move(offsets_);
        std::vector<size_t> old_counts = std::move(counts_);
        keys_.assign(new_buckets, EntryKey{});
        values_.clear();
        states_.assign(new_buckets, 0);
        offsets_.assign(new_buckets, 0);
        counts_.assign(new_buckets, 0);
        for (size_t i = 0; i < old_keys.size(); ++i) {
            if (old_states[i] == 1) {
                EntryKey key = old_keys[i];
                size_t cnt = old_counts[i];
                size_t off = old_offsets[i];
                size_t idx = find_slot(key);
                if (idx == keys_.size()) {
                    idx = keys_.size();
                    keys_.push_back(key);
                    states_.push_back(1);
                    offsets_.push_back(values_.size());
                    counts_.push_back(cnt);
                    values_.insert(values_.end(), old_values.begin() + off, old_values.begin() + off + cnt);
                } else {
                    keys_[idx] = key;
                    states_[idx] = 1;
                    offsets_[idx] = values_.size();
                    counts_[idx] = cnt;
                    values_.insert(values_.end(), old_values.begin() + off, old_values.begin() + off + cnt);
                }
            }
        }
    }

    size_t find_slot(const EntryKey& key) const {
        if (keys_.empty()) return 0;
        size_t mask = keys_.size() - 1;
        size_t idx = std::hash<EntryKey>{}(key) & mask;
        for (;;) {
            if (states_[idx] == 0) return idx;
            if (states_[idx] == 1 && keys_[idx] == key) return idx;
            idx = (idx + 1) & mask;
        }
    }
};


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
    uint64_t creationTime = 0;
    uint64_t lastModifiedTime = 0;
    uint64_t lastAccessTime = 0;
    uint32_t attributes = 0;
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
    // task 2.5: pre-size before a bulk rebuild.
    void Reserve(size_t expectedEntryCount);

    // Inserts a brand-new entry, or updates an existing one in place
    // (including re-parenting, which updates the parent->children index --
    // task 2.3).
    void Upsert(VolumeRowId volumeRowId, const EntryRecord& record);
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

    // task 2.3: parent->children lookup for directory-listing/Column-View
    // style access. Returns empty span if the parent has no known children
    // (which is not the same as the parent itself being unknown).
    std::span<const uint32_t> ChildIndices(VolumeRowId volumeRowId, FileId parentFrn) const;
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
        for (const auto& [key, index] : idToIndex_) {
            fn(key, entries_[index]);
        }
    }

private:
    void RemoveFromChildrenList(const EntryKey& parentKey, uint32_t index);

    NamePool namePool_;
    std::vector<ProjectionEntry> entries_; // dense array; freed slots reused via freeList_
    std::vector<uint32_t> freeList_;
    FlatHashMap<EntryKey, uint32_t> idToIndex_;
    FlatChildrenMap parentToChildren_;
};

} // namespace ffindexstore

