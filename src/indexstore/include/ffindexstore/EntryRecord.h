#pragma once
#include <cstdint>
#include <string>

#include "ffindexstore/Identity.h"

namespace ffindexstore {

// One filesystem entry's persisted fields -- $STANDARD_INFORMATION /
// $FILE_NAME allowlisted data only (design.md D2/D3, tasks.md 1.2). Used
// both as the durable store's row shape (name stored as a plain string,
// D3) and as the ingestion pipeline's in-flight unit before the projection
// interns the name (D2).
struct EntryRecord {
    FileId id;
    FileId parentId;
    std::u16string name;
    uint64_t sizeBytes = 0;
    // allocated-size-storage-accuracy D1/D4: on-disk size. `allocatedSizeKnown`
    // is the explicit unknown state -- zero is a valid size, never an absence
    // sentinel. The durable store persists unknown as NULL.
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
    uint64_t creationTime = 0;
    uint64_t lastModifiedTime = 0;
    uint64_t lastAccessTime = 0;
    uint32_t attributes = 0;
};

// A delta is not necessarily a complete EntryRecord. In particular, the USN
// journal carries an event timestamp but does not carry the authoritative
// creation/access timestamps from $STANDARD_INFORMATION. Keep absence
// explicit instead of overloading zero, which is a valid on-disk value.
using EntryFieldMask = uint32_t;
constexpr EntryFieldMask kEntryFieldParentId = 1u << 0;
constexpr EntryFieldMask kEntryFieldName = 1u << 1;
constexpr EntryFieldMask kEntryFieldSizeBytes = 1u << 2;
constexpr EntryFieldMask kEntryFieldCreationTime = 1u << 3;
constexpr EntryFieldMask kEntryFieldLastModifiedTime = 1u << 4;
constexpr EntryFieldMask kEntryFieldLastAccessTime = 1u << 5;
constexpr EntryFieldMask kEntryFieldAttributes = 1u << 6;
// allocated-size-storage-accuracy D4/D5: present only when the allocated
// size is authoritative (full MFT scan or successful re-enrichment); a
// partial update without it preserves the previous value, never erasing it.
constexpr EntryFieldMask kEntryFieldAllocatedSizeBytes = 1u << 7;
constexpr EntryFieldMask kEntryFieldMaskAll =
    kEntryFieldParentId | kEntryFieldName | kEntryFieldSizeBytes |
    kEntryFieldCreationTime | kEntryFieldLastModifiedTime |
    kEntryFieldLastAccessTime | kEntryFieldAttributes | kEntryFieldAllocatedSizeBytes;

constexpr bool HasEntryField(EntryFieldMask fields, EntryFieldMask field) noexcept {
    return (fields & field) != 0;
}

enum class EntryChangeKind : uint8_t {
    Upsert,
    Remove,
};

struct EntryChange {
    EntryChangeKind kind = EntryChangeKind::Upsert;
    EntryRecord record; // for Remove, only `record.id` is required
    EntryFieldMask fields = kEntryFieldMaskAll;
};

} // namespace ffindexstore
