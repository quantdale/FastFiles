#pragma once
#include <cstdint>
#include <functional>

namespace ffindexstore {

// Wide enough for both NTFS's 64-bit FileReferenceNumber and ReFS's
// 128-bit file identifier (design.md D7, tasks.md 1.2/2.1). Today's wire
// protocol (ffprotocol::MftRecordFixedV1/UsnDeltaFixedV1) only carries a
// 64-bit FileReferenceNumber -- callers constructing a FileId128 from wire
// records set `high = 0`; a future ReFS-aware wire format would populate
// it. This is a storage/projection sizing decision only (design.md Open
// Questions), not an active ReFS code path.
struct FileId128 {
    uint64_t low = 0;
    uint64_t high = 0;

    static FileId128 FromNtfs(uint64_t fileReferenceNumber) noexcept { return FileId128{fileReferenceNumber, 0}; }
};

constexpr bool operator==(const FileId128& a, const FileId128& b) noexcept {
    return a.low == b.low && a.high == b.high;
}
constexpr bool operator!=(const FileId128& a, const FileId128& b) noexcept { return !(a == b); }

// Durable, stable identity for a volume (volume GUID + cached serial
// number), distinct from the wire protocol's ephemeral, connection-scoped
// VolumeId (design.md D6). Assigned locally as a small dense integer id by
// DurableStore once a volume's (guid, serial) pair is first seen, so the
// rest of this library can key everything off a cheap int32_t rather than
// a wide GUID everywhere.
using DurableVolumeId = int32_t;
constexpr DurableVolumeId kInvalidDurableVolumeId = -1;

struct EntryKey {
    DurableVolumeId volumeId = kInvalidDurableVolumeId;
    FileId128 fileReferenceNumber;
};

constexpr bool operator==(const EntryKey& a, const EntryKey& b) noexcept {
    return a.volumeId == b.volumeId && a.fileReferenceNumber == b.fileReferenceNumber;
}

struct EntryKeyHash {
    size_t operator()(const EntryKey& key) const noexcept {
        size_t h = std::hash<DurableVolumeId>{}(key.volumeId);
        h ^= std::hash<uint64_t>{}(key.fileReferenceNumber.low) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(key.fileReferenceNumber.high) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace ffindexstore
