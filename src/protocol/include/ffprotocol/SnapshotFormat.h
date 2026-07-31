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
constexpr size_t kSnapshotSlotCapacityBytes = 8 * 1024 * 1024;

#pragma pack(push, 1)
struct SnapshotSharedHeader {
    uint64_t generation;
    uint32_t activeSlot; // 0 or 1
    uint32_t activeSlotDataSize;
};
#pragma pack(pop)

enum class DirectoryEnumerationStatus : uint32_t {
    Success = 0,
    AccessDenied = 1,
    NotFound = 2,
};

struct SnapshotDirectoryEntry {
    std::wstring name;
    bool isDirectory = false;
};

struct SnapshotDirectory {
    DirectoryEnumerationStatus status = DirectoryEnumerationStatus::NotFound;
    std::vector<SnapshotDirectoryEntry> entries;
};

// Serializes the given view of directories in path-sorted order (the
// caller passes a std::map, which is already ordered) into the flat
// format: [u32 directoryCount] { [len-prefixed path][u32 status][u32
// entryCount] { [u8 isDirectory][len-prefixed name] } }.
std::vector<uint8_t> SerializeSnapshot(const std::map<std::wstring, SnapshotDirectory>& directories);

// Parses a buffer produced by SerializeSnapshot. Returns std::nullopt on
// any malformed input (truncated buffer, declared length exceeding what
// remains) rather than a partial result.
std::optional<std::map<std::wstring, SnapshotDirectory>> ParseSnapshot(const uint8_t* data, size_t size);

} // namespace ffprotocol
