#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ffindexstore/EntryRecord.h"
#include "ffindexstore/Identity.h"
#include "ffindexstore/NamePool.h"

namespace ffindexstore {

class Store;

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

    const ProjectionEntry* Find(VolumeRowId volumeRowId, FileId frn) const;

    // task 2.3: parent->children lookup for directory-listing/Column-View
    // style access. Returns nullptr if the parent has no known children
    // (which is not the same as the parent itself being unknown).
    const std::vector<uint32_t>* ChildIndices(VolumeRowId volumeRowId, FileId parentFrn) const;
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

private:
    void RemoveFromChildrenList(const EntryKey& parentKey, uint32_t index);

    NamePool namePool_;
    std::vector<ProjectionEntry> entries_; // dense array; freed slots reused via freeList_
    std::vector<uint32_t> freeList_;
    std::unordered_map<EntryKey, uint32_t, EntryKeyHash> idToIndex_;
    std::unordered_map<EntryKey, std::vector<uint32_t>, EntryKeyHash> parentToChildren_;
};

} // namespace ffindexstore
