#pragma once
#include <array>
#include <cstdint>
#include <functional>

namespace ffindexstore {

// A file's identity within its volume. Wide enough for both NTFS's 64-bit
// FileReferenceNumber (high == 0) and ReFS's 128-bit file identifier
// (design.md D2/D7, tasks.md 1.2). Never compared or hashed across volumes
// without pairing with a VolumeRowId -- see EntryKey below.
struct FileId {
    uint64_t low = 0;
    uint64_t high = 0;
};

constexpr bool operator==(const FileId& a, const FileId& b) noexcept {
    return a.low == b.low && a.high == b.high;
}
constexpr bool operator!=(const FileId& a, const FileId& b) noexcept { return !(a == b); }
constexpr bool operator<(const FileId& a, const FileId& b) noexcept {
    return a.high != b.high ? a.high < b.high : a.low < b.low;
}

// The durable, cross-restart volume identity (design.md D6): volume GUID
// plus cached serial number. Distinct from the wire protocol's ephemeral,
// connection-scoped VolumeId (ffprotocol::VolumeId), which the engine maps
// to this identity on every EnumerateVolumes call.
struct VolumeKey {
    std::array<uint8_t, 16> volumeGuid{};
    uint32_t serialNumber = 0;
};

constexpr bool operator==(const VolumeKey& a, const VolumeKey& b) noexcept {
    return a.volumeGuid == b.volumeGuid && a.serialNumber == b.serialNumber;
}
constexpr bool operator!=(const VolumeKey& a, const VolumeKey& b) noexcept { return !(a == b); }

// The durable store's small, dense, per-volume row id (SQLite `volumes.id`).
// Stable for the lifetime of the database (never reused across a
// forget-volume deletion), used everywhere in the projection and ingestion
// pipeline in place of the bulkier VolumeKey (design.md D7's "(volume
// identity, FileReferenceNumber)" pairing uses this as the volume identity
// half).
using VolumeRowId = int64_t;

// Identifies one entry uniquely across the whole store: (volume, FRN) --
// design.md D7. Never a path string.
struct EntryKey {
    VolumeRowId volumeRowId = 0;
    FileId frn;
};

constexpr bool operator==(const EntryKey& a, const EntryKey& b) noexcept {
    return a.volumeRowId == b.volumeRowId && a.frn == b.frn;
}
constexpr bool operator!=(const EntryKey& a, const EntryKey& b) noexcept { return !(a == b); }

struct EntryKeyHash {
    size_t operator()(const EntryKey& key) const noexcept {
        size_t h = std::hash<int64_t>{}(key.volumeRowId);
        h ^= std::hash<uint64_t>{}(key.frn.low) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(key.frn.high) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace ffindexstore
