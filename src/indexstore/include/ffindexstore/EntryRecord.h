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
    uint64_t creationTime = 0;
    uint64_t lastModifiedTime = 0;
    uint64_t lastAccessTime = 0;
    uint32_t attributes = 0;
};

enum class EntryChangeKind : uint8_t {
    Upsert,
    Remove,
};

struct EntryChange {
    EntryChangeKind kind = EntryChangeKind::Upsert;
    EntryRecord record; // for Remove, only `record.id` is required
};

} // namespace ffindexstore
