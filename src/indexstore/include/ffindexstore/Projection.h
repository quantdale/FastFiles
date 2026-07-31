#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ffindexstore/FileId.h"
#include "ffindexstore/IngestEntry.h"
#include "ffindexstore/NamePool.h"

namespace ffindexstore {

// The well-known Win32 FILE_ATTRIBUTE_DIRECTORY bit value. Duplicated here
// (rather than including <windows.h>) so this library stays free of Win32
// dependencies -- it holds no handles and makes no syscalls, which keeps
// it buildable and unit-testable independent of the platform layer.
constexpr uint32_t kFileAttributeDirectory = 0x10;

struct ChildEntryView {
    EntryKey key;
    std::u16string_view name;
    uint64_t sizeBytes = 0;
    uint64_t lastWriteTime = 0;
    uint32_t attributes = 0;
};

// The compact, dense in-memory index (design.md D2, tasks.md section 2):
// every entry is a fixed-size record referencing its parent by
// FileReferenceNumber and its name by an interned NameId, never a stored
// full path. This is what search/browse/storage-analysis read at runtime;
// it is rebuilt from DurableStore at startup and kept incrementally in
// sync thereafter (never the other way around -- DurableStore is always
// the source of truth).
//
// Not internally synchronized: callers (IndexStore) serialize access
// exactly as the ingestion pipeline ordering already requires (commit to
// SQLite, then apply here, one batch at a time).
class Projection {
public:
    // Applies one already-durably-committed batch of changes (task 3.3,
    // D4). Upserts overwrite fields in place (including re-parenting) for
    // an already-known key; Removes on an unknown key are a harmless no-op
    // (idempotent, matching D7/D8's upsert semantics under resumed/
    // re-delivered records).
    void Apply(const std::vector<IngestEntry>& batch);
    void ApplyOne(const IngestEntry& entry);

    std::optional<ChildEntryView> TryGet(const EntryKey& key) const;
    bool Contains(const EntryKey& key) const noexcept { return keyToIndex_.count(key) != 0; }

    // Children of `parentKey`, for Column-View-style "list this directory"
    // lookups (task 2.3) -- backed by the parent->children index, not a
    // scan.
    std::vector<ChildEntryView> ChildrenOf(const EntryKey& parentKey) const;

    // On-demand full path reconstruction (task 2.4): walks parent
    // references up to a self-referential root (NTFS's root record is its
    // own parent) or an unknown ancestor, concatenating interned names,
    // with defensive cycle detection -- a walk that revisits an
    // already-seen FileReferenceNumber stops rather than looping, even
    // though a well-formed volume should never produce that (design.md
    // D7). `volumeRootPrefix` (e.g. L"C:") is prepended with a leading
    // backslash before the first reconstructed segment; pass an empty
    // string for a volume-relative path.
    //
    // Returns std::nullopt only if `key` itself is not present in the
    // projection.
    std::optional<std::wstring> ReconstructPath(const EntryKey& key, const std::wstring& volumeRootPrefix) const;

    size_t EntryCount() const noexcept { return entries_.size(); }
    const NamePool& Names() const noexcept { return namePool_; }

    // Pre-sizes internal storage ahead of a bulk startup rebuild (task
    // 2.5), using row counts already tracked in DurableStore's volumes
    // metadata.
    void Reserve(size_t expectedEntryCount);

    // Drops every entry belonging to `volumeId`, e.g. before a full
    // rebuild-from-store pass for that volume, or on an explicit "forget
    // this drive" action (task 7.4). O(entry count); intentionally not a
    // hot-path operation.
    void RemoveVolume(DurableVolumeId volumeId);

    void Clear();

private:
    struct ProjectionEntry {
        FileId128 fileReferenceNumber;
        FileId128 parentFileReferenceNumber;
        DurableVolumeId volumeId = kInvalidDurableVolumeId;
        NameId nameId = kInvalidNameId;
        uint64_t sizeBytes = 0;
        uint64_t lastWriteTime = 0;
        uint32_t attributes = 0;

        EntryKey Key() const noexcept { return EntryKey{volumeId, fileReferenceNumber}; }
        EntryKey ParentKey() const noexcept { return EntryKey{volumeId, parentFileReferenceNumber}; }
    };

    void InsertNew(const IngestEntry& entry);
    void UpdateExisting(uint32_t index, const IngestEntry& entry);
    void RemoveAt(uint32_t index);

    void AddToParentIndex(const EntryKey& parentKey, uint32_t childIndex);
    void RemoveFromParentIndex(const EntryKey& parentKey, uint32_t childIndex);
    void RenumberInParentIndex(const EntryKey& parentKey, uint32_t oldIndex, uint32_t newIndex);

    ChildEntryView ToView(const ProjectionEntry& entry) const;

    std::vector<ProjectionEntry> entries_;
    std::unordered_map<EntryKey, uint32_t, EntryKeyHash> keyToIndex_;
    std::unordered_multimap<EntryKey, uint32_t, EntryKeyHash> parentToChildren_;
    NamePool namePool_;
};

} // namespace ffindexstore
