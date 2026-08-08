#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ffprotocol {

// Layout of the shared memory-mapped snapshot section FastFilesEngine
// publishes and FastFiles (UI) reads directly, with zero IPC round trip
// (design.md D3; spec "Filesystem Snapshot Publication"). The mapping is
// sizeof(SnapshotSharedHeader) followed by two equally sized slots of
// kSnapshotSlotCapacityBytes each; the header says which slot is current.
//
// This is a private in-process-family format (engine writer, UI reader)
// rather than a wire protocol with external versioning guarantees -- both
// sides are built from the same source tree, so there is no
// cross-version compatibility concern the way there is for the named-pipe
// protocols in Commands.h/UiProtocol.h.
// A 64 MiB slot keeps ordinary large folders and a useful bounded set of
// browsed roots usable without changing the zero-IPC format. Publication still
// fails closed above this explicit bound and the UI surfaces that failure.
constexpr size_t kSnapshotSlotCapacityBytes = 64 * 1024 * 1024;

#pragma pack(push, 1)
struct SnapshotSharedHeader {
    uint64_t generation;
    uint32_t activeSlot; // 0 or 1
    // Each slot owns its descriptor. The writer fills the inactive slot and
    // publishes its size before flipping activeSlot, so a reader never pairs
    // an old slot with the new slot's length.
    uint32_t slotDataSize[2];
};
#pragma pack(pop)

enum class DirectoryEnumerationStatus : uint32_t {
    Success = 0,
    AccessDenied = 1,
    NotFound = 2,
    // The directory exists or was reachable, but enumeration failed with an
    // I/O/network/device error. This is distinct from a missing path so the
    // UI never presents a partial listing as a successful empty directory.
    IoError = 3,
};

struct SnapshotDirectoryEntry {
    std::wstring name;
    bool isDirectory = false;
    uint64_t sizeBytes = 0;
    uint32_t attributes = 0;
    uint64_t creationTime = 0;
    uint64_t lastModifiedTime = 0;
    int64_t volumeRowId = 0;
    uint64_t fileIdLow = 0;
    uint64_t fileIdHigh = 0;
    uint64_t parentIdLow = 0;
    uint64_t parentIdHigh = 0;
};

struct SnapshotDirectory {
    DirectoryEnumerationStatus status = DirectoryEnumerationStatus::NotFound;
    std::vector<SnapshotDirectoryEntry> entries;
    int64_t volumeRowId = 0;
    uint64_t directoryIdLow = 0;
    uint64_t directoryIdHigh = 0;
    uint64_t parentIdLow = 0;
    uint64_t parentIdHigh = 0;
};

// Serializes the given view of directories in path-sorted order (the
// caller passes a std::map, which is already ordered) into the flat
// format: [u32 directoryCount] { [len-prefixed path][u32 status][u32
// entryCount] { [u8 isDirectory][u64 size][u32 attributes]
// [len-prefixed name] } }.
std::vector<uint8_t> SerializeSnapshot(const std::map<std::wstring, SnapshotDirectory>& directories);

// Parses a buffer produced by SerializeSnapshot. Returns std::nullopt on
// any malformed input (truncated buffer, declared length exceeding what
// remains) rather than a partial result.
std::optional<std::map<std::wstring, SnapshotDirectory>> ParseSnapshot(const uint8_t* data, size_t size);

} // namespace ffprotocol
