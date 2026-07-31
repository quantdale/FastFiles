#include "ffindexstore/Projection.h"

#include <algorithm>

#include "ffindexstore/Store.h"

namespace ffindexstore {

void Projection::Reserve(size_t expectedEntryCount) {
    entries_.reserve(expectedEntryCount);
    idToIndex_.reserve(expectedEntryCount);
    parentToChildren_.reserve(expectedEntryCount);
    // Names recur constantly across a real tree (D2's rationale) -- a
    // quarter of the entry count is a reasonable rough estimate for the
    // unique-name-count hint; NamePool grows past this fine if wrong.
    namePool_.Reserve(expectedEntryCount / 4 + 16, expectedEntryCount * 12);
}

void Projection::RemoveFromChildrenList(const EntryKey& parentKey, uint32_t index) {
    auto it = parentToChildren_.find(parentKey);
    if (it == parentToChildren_.end()) {
        return;
    }
    auto& siblings = it->second;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), index), siblings.end());
    if (siblings.empty()) {
        parentToChildren_.erase(it);
    }
}

void Projection::Upsert(VolumeRowId volumeRowId, const EntryRecord& record) {
    const EntryKey key{volumeRowId, record.id};
    const NameId nameId = namePool_.Intern(record.name);

    auto it = idToIndex_.find(key);
    if (it != idToIndex_.end()) {
        const uint32_t index = it->second;
        ProjectionEntry& entry = entries_[index];

        if (entry.parentFrn != record.parentId) {
            RemoveFromChildrenList(EntryKey{volumeRowId, entry.parentFrn}, index);
            // A self-referential parent (record.parentId == record.id) is
            // the volume-root sentinel (NTFS record 5's parent is itself),
            // not a real containment relationship -- it must not make the
            // root list itself as its own child in a directory listing.
            if (record.parentId != record.id) {
                parentToChildren_[EntryKey{volumeRowId, record.parentId}].push_back(index);
            }
        }

        entry.parentFrn = record.parentId;
        entry.nameId = nameId;
        entry.sizeBytes = record.sizeBytes;
        entry.creationTime = record.creationTime;
        entry.lastModifiedTime = record.lastModifiedTime;
        entry.lastAccessTime = record.lastAccessTime;
        entry.attributes = record.attributes;
        return;
    }

    ProjectionEntry entry;
    entry.volumeRowId = volumeRowId;
    entry.frn = record.id;
    entry.parentFrn = record.parentId;
    entry.nameId = nameId;
    entry.sizeBytes = record.sizeBytes;
    entry.creationTime = record.creationTime;
    entry.lastModifiedTime = record.lastModifiedTime;
    entry.lastAccessTime = record.lastAccessTime;
    entry.attributes = record.attributes;

    uint32_t index;
    if (!freeList_.empty()) {
        index = freeList_.back();
        freeList_.pop_back();
        entries_[index] = entry;
    } else {
        index = static_cast<uint32_t>(entries_.size());
        entries_.push_back(entry);
    }

    idToIndex_.emplace(key, index);
    if (record.parentId != record.id) {
        parentToChildren_[EntryKey{volumeRowId, record.parentId}].push_back(index);
    }
}

void Projection::Remove(VolumeRowId volumeRowId, FileId frn) {
    const EntryKey key{volumeRowId, frn};
    auto it = idToIndex_.find(key);
    if (it == idToIndex_.end()) {
        return;
    }
    const uint32_t index = it->second;
    RemoveFromChildrenList(EntryKey{volumeRowId, entries_[index].parentFrn}, index);
    idToIndex_.erase(it);
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
    auto it = idToIndex_.find(EntryKey{volumeRowId, frn});
    return it == idToIndex_.end() ? nullptr : &entries_[it->second];
}

const std::vector<uint32_t>* Projection::ChildIndices(VolumeRowId volumeRowId, FileId parentFrn) const {
    auto it = parentToChildren_.find(EntryKey{volumeRowId, parentFrn});
    return it == parentToChildren_.end() ? nullptr : &it->second;
}

Projection::PathResult Projection::ReconstructPath(VolumeRowId volumeRowId, FileId frn) const {
    PathResult result;
    std::vector<NameId> partsReversed;
    std::unordered_set<EntryKey, EntryKeyHash> visited;

    FileId current = frn;
    for (;;) {
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
        if (!result.path.empty()) {
            result.path.push_back(u'\\');
        }
        auto name = namePool_.Get(*it);
        result.path.append(name.begin(), name.end());
    }
    return result;
}

void Projection::RebuildVolumeFromStore(Store& store, VolumeRowId volumeRowId, uint64_t expectedEntryCount) {
    Reserve(EntryCount() + expectedEntryCount);
    store.ForEachEntry(volumeRowId, [&](const EntryRecord& record) { Upsert(volumeRowId, record); });
}

} // namespace ffindexstore
