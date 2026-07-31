#pragma once
#include <cstdint>
#include <string>

#include "ffindexstore/FileId.h"

namespace ffindexstore {

enum class IngestOp {
    Upsert, // create or update (D7: identity is (volumeId, fileReferenceNumber), never path)
    Remove,
};

// Source-agnostic ingestion record: built by the engine from either a
// StartVolumeScan MftRecordV1 batch, an OpenUsnJournal UsnDeltaV1 batch, or
// a reconciliation sweep's ground-truth read -- DurableStore/Projection
// don't need to know which. Filename canonicalization (task 6.2) happens
// before construction, keyed off fileReferenceNumber, never a re-derived
// path.
struct IngestEntry {
    IngestOp op = IngestOp::Upsert;
    EntryKey key;
    FileId128 parentFileReferenceNumber;
    std::u16string name; // ignored for Remove
    uint64_t sizeBytes = 0;
    uint64_t lastWriteTime = 0; // FILETIME (100ns ticks since 1601), ignored for Remove
    uint32_t attributes = 0;    // ignored for Remove
};

} // namespace ffindexstore
