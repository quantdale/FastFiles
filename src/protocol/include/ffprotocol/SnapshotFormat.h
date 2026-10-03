#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "WindowsPath.h"

namespace ffprotocol {

// Layout of an immutable, generation-named snapshot mapping that
// FastFilesEngine publishes and FastFiles (UI) reads directly, with zero IPC
// round trip (design.md D3; spec "Filesystem Snapshot Publication"). Each
// mapping is exactly sizeof(SnapshotSharedHeader) plus the serialized payload;
// a new mapping name is published only after the engine has fully written it.
//
// This is a private in-process-family format (engine writer, UI reader)
// rather than a wire protocol with external versioning guarantees -- both
// sides are built from the same source tree, so there is no
// cross-version compatibility concern the way there is for the named-pipe
// protocols in Commands.h/UiProtocol.h.
// A 256 MiB maximum payload keeps a complete large-volume projection usable
// without allowing an unbounded publish allocation. Publication fails closed
// above this explicit bound and the UI surfaces that failure. Unlike the old
// two-slot layout, this is a limit rather than a mapping reservation.
// inline constexpr: external linkage so this header constant is not reported
// unused by translation units that include SnapshotFormat.h without reading
// the cap (it is enforced by SnapshotPublisher's two size checks).
inline constexpr size_t kSnapshotMaximumPayloadBytes = 256 * 1024 * 1024;
// audit-remediation FF-AUDIT-04: the header is versioned so a UI never
// interprets a section written by an incompatible engine layout.
// allocated-size-storage-accuracy D6: version 3 adds per-entry allocated
// (on-disk) size + an explicit known flag (9 bytes/entry). Both peers ship
// in one install, so a mismatched build fails closed via
// IsSnapshotFormatCompatible exactly as v2 did -- never silently mis-parsed.
constexpr uint32_t kSnapshotFormatVersion = 3;

constexpr bool IsSnapshotFormatCompatible(uint32_t formatVersion) noexcept {
    return formatVersion == kSnapshotFormatVersion;
}

#pragma pack(push, 1)
struct SnapshotSharedHeader {
    uint32_t formatVersion;
    uint32_t reserved;
    uint64_t generation;
    uint64_t payloadSizeBytes;
};
#pragma pack(pop)

static_assert(offsetof(SnapshotSharedHeader, generation) % alignof(uint64_t) == 0,
              "snapshot generation must remain naturally aligned for InterlockedExchange64");

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
    // allocated-size-storage-accuracy D1/D6: on-disk size with an explicit
    // known flag -- zero is a valid size, unknown is never rendered as 0 B.
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
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

using CaseInsensitiveSnapshotMap =
    std::map<std::wstring, SnapshotDirectory, CaseInsensitivePathLess>;

// Serializes the given view of directories in path-sorted order (the
// caller passes a std::map, which is already ordered) into the flat
// format: [u32 directoryCount] { [len-prefixed path][u32 status][u32
// entryCount] { [u8 isDirectory][u64 size][u64 allocatedSize][u8
// allocatedSizeKnown][u32 attributes] [len-prefixed name] } }.
std::vector<uint8_t> SerializeSnapshot(const std::map<std::wstring, SnapshotDirectory>& directories);
std::vector<uint8_t> SerializeSnapshot(const CaseInsensitiveSnapshotMap& directories);

// Two-pass publication support. The first pass computes the exact payload
// size without allocating a second copy of the snapshot. The second pass
// writes directly into caller-owned storage and fails if the storage is too
// small or the trusted model cannot be represented by the length-prefixed
// format. A zero size indicates overflow/invalid input; a valid empty snapshot
// is four bytes (the directory count).
size_t SerializedSnapshotSize(const std::map<std::wstring, SnapshotDirectory>& directories) noexcept;
size_t SerializedSnapshotSize(const CaseInsensitiveSnapshotMap& directories) noexcept;
bool SerializeSnapshotTo(const std::map<std::wstring, SnapshotDirectory>& directories,
                         uint8_t* destination, size_t capacity) noexcept;
bool SerializeSnapshotTo(const CaseInsensitiveSnapshotMap& directories,
                         uint8_t* destination, size_t capacity) noexcept;

// Parses a buffer produced by SerializeSnapshot. Returns std::nullopt on
// any malformed input (truncated buffer, declared length exceeding what
// remains) rather than a partial result.
std::optional<std::map<std::wstring, SnapshotDirectory>> ParseSnapshot(const uint8_t* data, size_t size);

// performance-closure-and-multisurface-acceleration 6.1: pooled name
// storage for the engine-side snapshot model. The wire format is unchanged
// (version 3): these types only describe the ENGINE's in-memory model so that
// building the serialized bytes never constructs a per-entry owning
// std::wstring (measured: eliminating that per-entry allocation moves the
// 200k-entry build+serialize stages by -59% at identical payload bytes).
//
// Ownership and lifetime: a PooledSnapshotDirectory owns its name text in
// nameChars; entry.name views point INTO that vector. Views are created only
// by PooledSnapshotDirectoryBuilder once the name text is complete, and
// moving a directory transfers the vector buffer (pointer stable), so views
// survive map insertion and moves. Copying a directory rebuilds the views
// against the copy's own buffer. Nothing may append to nameChars while entry
// name views exist; the builder enforces this by construction.
struct PooledSnapshotEntry {
    std::wstring_view name;
    bool isDirectory = false;
    uint64_t sizeBytes = 0;
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
    uint32_t attributes = 0;
    uint64_t creationTime = 0;
    uint64_t lastModifiedTime = 0;
    int64_t volumeRowId = 0;
    uint64_t fileIdLow = 0;
    uint64_t fileIdHigh = 0;
    uint64_t parentIdLow = 0;
    uint64_t parentIdHigh = 0;
};

struct PooledSnapshotDirectory {
    // status is initialized in the constructor (explicit form, matching the
    // owning SnapshotDirectory's NotFound default without an in-class scoped
    // enum initializer).
    PooledSnapshotDirectory() : status(DirectoryEnumerationStatus::NotFound) {}
    PooledSnapshotDirectory(PooledSnapshotDirectory&&) = default;
    PooledSnapshotDirectory& operator=(PooledSnapshotDirectory&&) = default;
    // Copy rebuilds entry name views against this copy's own nameChars: the
    // default copy would leave views pointing into the source buffer.
    PooledSnapshotDirectory(const PooledSnapshotDirectory& other);
    PooledSnapshotDirectory& operator=(const PooledSnapshotDirectory& other);

    // Rebuilds copied entry name views against this directory's own buffer.
    void RemapEntryNames(const PooledSnapshotDirectory& other);

    DirectoryEnumerationStatus status;
    std::vector<wchar_t> nameChars;
    std::vector<PooledSnapshotEntry> entries;
    int64_t volumeRowId = 0;
    uint64_t directoryIdLow = 0;
    uint64_t directoryIdHigh = 0;
    uint64_t parentIdLow = 0;
    uint64_t parentIdHigh = 0;
};

// Builder holds name text as offset/length until Build() creates the views,
// so buffer growth during construction can never leave a dangling view.
// Name text may arrive as std::wstring_view (export paths) or
// std::u16string_view (the projection NamePool's native text); both carry
// UTF-16 code units and are copied with explicit per-unit conversion, which
// is byte-identical on Windows where wchar_t is 16-bit.
class PooledSnapshotDirectoryBuilder {
public:
    PooledSnapshotDirectoryBuilder() : status_(DirectoryEnumerationStatus::NotFound) {}
    void SetStatus(DirectoryEnumerationStatus status) { status_ = status; }
    void SetIdentity(int64_t volumeRowId, uint64_t directoryIdLow, uint64_t directoryIdHigh,
                     uint64_t parentIdLow, uint64_t parentIdHigh) {
        volumeRowId_ = volumeRowId;
        directoryIdLow_ = directoryIdLow;
        directoryIdHigh_ = directoryIdHigh;
        parentIdLow_ = parentIdLow;
        parentIdHigh_ = parentIdHigh;
    }
    // Accepts either name-view form the engine produces:
    // std::wstring_view / std::wstring (export and test paths) and
    // std::u16string_view (the projection NamePool's native text). Both carry
    // UTF-16 code units and are copied with explicit per-unit conversion,
    // which is byte-identical on Windows where wchar_t is 16-bit.
    void AddEntry(std::wstring_view name, bool isDirectory, uint64_t sizeBytes,
                  uint64_t allocatedSizeBytes, bool allocatedSizeKnown, uint32_t attributes,
                  uint64_t creationTime, uint64_t lastModifiedTime, int64_t entryVolumeRowId,
                  uint64_t fileIdLow, uint64_t fileIdHigh, uint64_t parentIdLow, uint64_t parentIdHigh) {
        AddEntryText(name, isDirectory, sizeBytes, allocatedSizeBytes, allocatedSizeKnown,
                     attributes, creationTime, lastModifiedTime, entryVolumeRowId,
                     fileIdLow, fileIdHigh, parentIdLow, parentIdHigh);
    }
    void AddEntry(std::u16string_view name, bool isDirectory, uint64_t sizeBytes,
                  uint64_t allocatedSizeBytes, bool allocatedSizeKnown, uint32_t attributes,
                  uint64_t creationTime, uint64_t lastModifiedTime, int64_t entryVolumeRowId,
                  uint64_t fileIdLow, uint64_t fileIdHigh, uint64_t parentIdLow, uint64_t parentIdHigh) {
        AddEntryText(name, isDirectory, sizeBytes, allocatedSizeBytes, allocatedSizeKnown,
                     attributes, creationTime, lastModifiedTime, entryVolumeRowId,
                     fileIdLow, fileIdHigh, parentIdLow, parentIdHigh);
    }
    PooledSnapshotDirectory Build() &&;

private:
    template <typename CharT>
    void AddEntryText(std::basic_string_view<CharT> name, bool isDirectory, uint64_t sizeBytes,
                      uint64_t allocatedSizeBytes, bool allocatedSizeKnown, uint32_t attributes,
                      uint64_t creationTime, uint64_t lastModifiedTime, int64_t entryVolumeRowId,
                      uint64_t fileIdLow, uint64_t fileIdHigh, uint64_t parentIdLow, uint64_t parentIdHigh) {
        static_assert(sizeof(wchar_t) == 2 && sizeof(char16_t) == 2,
                      "pooled name storage copies UTF-16 code units; Windows 2-byte code units are required");
        PendingEntry entry;
        const size_t offsetChars = nameChars_.size();
        entry.nameOffsetChars = static_cast<uint32_t>(offsetChars);
        entry.nameLengthChars = static_cast<uint32_t>(name.size());
        // Bulk append: one resize (vector growth is geometric) plus one copy.
        // A per-name reserve() here would allocate the exact requested size on
        // every call and re-copy the accumulated text each time -- measured as
        // an O(n^2) export regression (export 41.3 -> 64.6 ms at 200k entries)
        // before this was fixed.
        nameChars_.resize(offsetChars + name.size());
        if (!name.empty()) {
            std::memcpy(nameChars_.data() + offsetChars, name.data(), name.size() * sizeof(CharT));
        }
        entry.isDirectory = isDirectory;
        entry.sizeBytes = sizeBytes;
        entry.allocatedSizeBytes = allocatedSizeBytes;
        entry.allocatedSizeKnown = allocatedSizeKnown;
        entry.attributes = attributes;
        entry.creationTime = creationTime;
        entry.lastModifiedTime = lastModifiedTime;
        entry.volumeRowId = entryVolumeRowId;
        entry.fileIdLow = fileIdLow;
        entry.fileIdHigh = fileIdHigh;
        entry.parentIdLow = parentIdLow;
        entry.parentIdHigh = parentIdHigh;
        pending_.push_back(entry);
    }

    struct PendingEntry {
        uint32_t nameOffsetChars = 0;
        uint32_t nameLengthChars = 0;
        bool isDirectory = false;
        uint64_t sizeBytes = 0;
        uint64_t allocatedSizeBytes = 0;
        bool allocatedSizeKnown = false;
        uint32_t attributes = 0;
        uint64_t creationTime = 0;
        uint64_t lastModifiedTime = 0;
        int64_t volumeRowId = 0;
        uint64_t fileIdLow = 0;
        uint64_t fileIdHigh = 0;
        uint64_t parentIdLow = 0;
        uint64_t parentIdHigh = 0;
    };
    DirectoryEnumerationStatus status_;
    std::vector<wchar_t> nameChars_;
    std::vector<PendingEntry> pending_;
    int64_t volumeRowId_ = 0;
    uint64_t directoryIdLow_ = 0;
    uint64_t directoryIdHigh_ = 0;
    uint64_t parentIdLow_ = 0;
    uint64_t parentIdHigh_ = 0;
};

using PooledSnapshotMap = std::map<std::wstring, PooledSnapshotDirectory>;
using PooledCaseInsensitiveSnapshotMap =
    std::map<std::wstring, PooledSnapshotDirectory, CaseInsensitivePathLess>;

// Pooled overloads emit exactly the bytes the owning overloads emit: both
// share one layout implementation, and tests prove byte-equality over
// randomized fixtures. The pooled forms must not change the wire format.
std::vector<uint8_t> SerializeSnapshot(const PooledSnapshotMap& directories);
std::vector<uint8_t> SerializeSnapshot(const PooledCaseInsensitiveSnapshotMap& directories);
size_t SerializedSnapshotSize(const PooledSnapshotMap& directories) noexcept;
size_t SerializedSnapshotSize(const PooledCaseInsensitiveSnapshotMap& directories) noexcept;
bool SerializeSnapshotTo(const PooledSnapshotMap& directories,
                         uint8_t* destination, size_t capacity) noexcept;
bool SerializeSnapshotTo(const PooledCaseInsensitiveSnapshotMap& directories,
                         uint8_t* destination, size_t capacity) noexcept;

} // namespace ffprotocol
