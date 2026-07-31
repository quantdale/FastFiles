#include "ffindexstore/Projection.h"

#include <unordered_set>

namespace ffindexstore {

void Projection::Apply(const std::vector<IngestEntry>& batch) {
    for (const auto& entry : batch) {
        ApplyOne(entry);
    }
}

void Projection::ApplyOne(const IngestEntry& entry) {
    auto it = keyToIndex_.find(entry.key);

    if (entry.op == IngestOp::Remove) {
        if (it != keyToIndex_.end()) {
            RemoveAt(it->second);
        }
        return; // unknown key: harmless no-op (D7/D8 idempotent upsert semantics)
    }

    if (it == keyToIndex_.end()) {
        InsertNew(entry);
    } else {
        UpdateExisting(it->second, entry);
    }
}

void Projection::InsertNew(const IngestEntry& entry) {
    ProjectionEntry projected;
    projected.fileReferenceNumber = entry.key.fileReferenceNumber;
    projected.parentFileReferenceNumber = entry.parentFileReferenceNumber;
    projected.volumeId = entry.key.volumeId;
    projected.nameId = namePool_.Intern(entry.name);
    projected.sizeBytes = entry.sizeBytes;
    projected.lastWriteTime = entry.lastWriteTime;
    projected.attributes = entry.attributes;

    const auto newIndex = static_cast<uint32_t>(entries_.size());
    entries_.push_back(projected);
    keyToIndex_.emplace(entry.key, newIndex);
    // A self-referential root (NTFS record 5, whose ParentFileReferenceNumber
    // is itself) is not its own child -- registering it would make
    // ChildrenOf(root) incorrectly include the root itself.
    if (!(projected.ParentKey() == entry.key)) {
        AddToParentIndex(projected.ParentKey(), newIndex);
    }
}

void Projection::UpdateExisting(uint32_t index, const IngestEntry& entry) {
    ProjectionEntry& projected = entries_[index];
    const EntryKey oldParentKey = projected.ParentKey();
    const EntryKey newParentKey = EntryKey{entry.key.volumeId, entry.parentFileReferenceNumber};
    const bool wasSelfReferential = oldParentKey == entry.key;
    const bool isSelfReferential = newParentKey == entry.key;

    if (!(oldParentKey == newParentKey)) {
        if (!wasSelfReferential) {
            RemoveFromParentIndex(oldParentKey, index);
        }
        if (!isSelfReferential) {
            AddToParentIndex(newParentKey, index);
        }
    }

    projected.parentFileReferenceNumber = entry.parentFileReferenceNumber;
    projected.nameId = namePool_.Intern(entry.name);
    projected.sizeBytes = entry.sizeBytes;
    projected.lastWriteTime = entry.lastWriteTime;
    projected.attributes = entry.attributes;
}

void Projection::RemoveAt(uint32_t index) {
    const ProjectionEntry removed = entries_[index];
    if (!(removed.ParentKey() == removed.Key())) {
        RemoveFromParentIndex(removed.ParentKey(), index);
    }
    keyToIndex_.erase(removed.Key());

    const auto lastIndex = static_cast<uint32_t>(entries_.size() - 1);
    if (index != lastIndex) {
        const ProjectionEntry moved = entries_[lastIndex];
        entries_[index] = moved;
        keyToIndex_[moved.Key()] = index;
        if (!(moved.ParentKey() == moved.Key())) {
            RenumberInParentIndex(moved.ParentKey(), lastIndex, index);
        }
    }
    entries_.pop_back();
}

void Projection::AddToParentIndex(const EntryKey& parentKey, uint32_t childIndex) {
    parentToChildren_.emplace(parentKey, childIndex);
}

void Projection::RemoveFromParentIndex(const EntryKey& parentKey, uint32_t childIndex) {
    auto range = parentToChildren_.equal_range(parentKey);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second == childIndex) {
            parentToChildren_.erase(it);
            return;
        }
    }
}

void Projection::RenumberInParentIndex(const EntryKey& parentKey, uint32_t oldIndex, uint32_t newIndex) {
    auto range = parentToChildren_.equal_range(parentKey);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second == oldIndex) {
            it->second = newIndex;
            return;
        }
    }
}

ChildEntryView Projection::ToView(const ProjectionEntry& entry) const {
    ChildEntryView view;
    view.key = entry.Key();
    view.name = namePool_.Lookup(entry.nameId);
    view.sizeBytes = entry.sizeBytes;
    view.lastWriteTime = entry.lastWriteTime;
    view.attributes = entry.attributes;
    return view;
}

std::optional<ChildEntryView> Projection::TryGet(const EntryKey& key) const {
    auto it = keyToIndex_.find(key);
    if (it == keyToIndex_.end()) {
        return std::nullopt;
    }
    return ToView(entries_[it->second]);
}

std::vector<ChildEntryView> Projection::ChildrenOf(const EntryKey& parentKey) const {
    std::vector<ChildEntryView> result;
    auto range = parentToChildren_.equal_range(parentKey);
    for (auto it = range.first; it != range.second; ++it) {
        result.push_back(ToView(entries_[it->second]));
    }
    return result;
}

std::optional<std::wstring> Projection::ReconstructPath(const EntryKey& key, const std::wstring& volumeRootPrefix) const {
    auto startIt = keyToIndex_.find(key);
    if (startIt == keyToIndex_.end()) {
        return std::nullopt;
    }

    std::vector<NameId> segmentsRootToLeafReversed; // collected leaf-to-root, reversed before join
    std::unordered_set<uint64_t> visited; // keyed on a combined hash of (low, high) per step

    uint32_t index = startIt->second;
    for (;;) {
        const ProjectionEntry& entry = entries_[index];
        const uint64_t visitToken = entry.fileReferenceNumber.low ^ (entry.fileReferenceNumber.high * 0x9e3779b97f4a7c15ULL);
        if (!visited.insert(visitToken).second) {
            break; // defensive cycle guard (task 2.4) -- should never trigger on a well-formed volume
        }
        segmentsRootToLeafReversed.push_back(entry.nameId);

        if (entry.fileReferenceNumber == entry.parentFileReferenceNumber) {
            break; // self-referential root record (e.g. NTFS record 5)
        }
        const EntryKey parentKey = entry.ParentKey();
        auto parentIt = keyToIndex_.find(parentKey);
        if (parentIt == keyToIndex_.end()) {
            break; // ancestor outside the projection (volume root boundary, or not yet ingested)
        }
        index = parentIt->second;
    }

    std::wstring path = volumeRootPrefix;
    for (auto it = segmentsRootToLeafReversed.rbegin(); it != segmentsRootToLeafReversed.rend(); ++it) {
        std::u16string_view name = namePool_.Lookup(*it);
        if (name.empty()) {
            continue; // the synthetic root record's own name is empty; nothing to append
        }
        path.push_back(L'\\');
        // Element-by-element conversion, not a reinterpret_cast of the
        // buffer: char16_t and wchar_t are the same width on Windows (the
        // only platform this ships on) but that's not guaranteed by the
        // language, and a raw pointer-width cast would be undefined
        // behavior anywhere it isn't.
        path.reserve(path.size() + name.size());
        for (char16_t ch : name) {
            path.push_back(static_cast<wchar_t>(ch));
        }
    }
    return path;
}

void Projection::Reserve(size_t expectedEntryCount) {
    entries_.reserve(expectedEntryCount);
    keyToIndex_.reserve(expectedEntryCount);
    parentToChildren_.reserve(expectedEntryCount);
    // Names are far fewer than entries in practice (D2); a conservative
    // fraction avoids over-reserving the arena while still cutting rehash
    // churn during a bulk load.
    namePool_.Reserve(expectedEntryCount / 4 + 16, expectedEntryCount * 4);
}

void Projection::RemoveVolume(DurableVolumeId volumeId) {
    std::vector<EntryKey> keysToRemove;
    keysToRemove.reserve(entries_.size());
    for (const auto& entry : entries_) {
        if (entry.volumeId == volumeId) {
            keysToRemove.push_back(entry.Key());
        }
    }
    for (const auto& key : keysToRemove) {
        auto it = keyToIndex_.find(key);
        if (it != keyToIndex_.end()) {
            RemoveAt(it->second);
        }
    }
}

void Projection::Clear() {
    entries_.clear();
    keyToIndex_.clear();
    parentToChildren_.clear();
    namePool_.Clear();
}

} // namespace ffindexstore
