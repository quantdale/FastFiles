// performance-closure-and-multisurface-acceleration 5.1: benchmark-only
// allocation-isolation arm. Deliberately NOT registered with CTest (wall-clock
// bench; design.md D5). No production source or wire-format change is made to
// obtain the measurement.
//
// Question (design D5): how much first-generation latency is caused by
// per-entry owning-string allocation when logical names, name lengths,
// serialized bytes, directory count, file count, and generation identity are
// held constant? The historical short-name probe is a CONFLATED ALLOCATION +
// PAYLOAD-SIZE EXPERIMENT (evidence/allocation-probe-interpretation-v2.json)
// and cannot answer this.
//
// Arms (each measured separately under the D5 protocol via --arm):
//   control            owning std::wstring names -> production
//                      SerializedSnapshotSize/SerializeSnapshotTo -> production
//                      ParseSnapshot (the production publication + parse path).
//   export-eliminated  names in one pre-sized contiguous arena referenced by
//                      offset/length; a bench-local writer emits the SAME
//                      length-prefixed bytes; production ParseSnapshot reads
//                      them. No per-entry heap allocation while building the
//                      bytes.
//   parse-eliminated   production bytes parsed into offset/length views (no
//                      per-entry owning strings on the parse side).
//   full-eliminated    arena build + bench writer + view parse.
//
// Every arm self-validates against the production serializer's bytes in the
// same process: byte count and full memcmp must match or the run FAILS (exit
// 3). Parsed names/fields/counts are compared against the fixture entry by
// entry. A byte mismatch invalidates the allocation-only conclusion (spec:
// "Payload identity is a precondition").
//
// Allocation counting is a separate pass (--count-allocations) using a
// bench-scoped global new/delete counter behind an atomic gate. Latency runs
// keep the gate off (one relaxed load per allocation); counted runs are
// labelled instrumented and are never used as latency evidence.

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ffprotocol/SnapshotFormat.h"

namespace {

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Bench-scoped allocation counter (count pass only; gate-off for latency).
// ---------------------------------------------------------------------------
std::atomic<bool> gCountingEnabled{false};
std::atomic<unsigned long long> gAllocationCount{0};
std::atomic<unsigned long long> gAllocationBytes{0};

double ElapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// ---------------------------------------------------------------------------
// Fixture: the exact logical shape of the first-generation benchmark fixture
// (bench_first_generation_publish.cpp): 200,000 nodes = 1 root + 3,125
// directories + 196,874 files, 199,999 child entries, names from the same
// generators. Built once, outside every measured window.
// ---------------------------------------------------------------------------
struct LogicalEntry {
    std::wstring name;
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

struct LogicalDirectory {
    std::wstring path;
    ffprotocol::DirectoryEnumerationStatus status = ffprotocol::DirectoryEnumerationStatus::Success;
    int64_t volumeRowId = 0;
    uint64_t directoryIdLow = 0;
    uint64_t directoryIdHigh = 0;
    uint64_t parentIdLow = 0;
    uint64_t parentIdHigh = 0;
    std::vector<LogicalEntry> entries;
};

// The fixture's logical generation identity: constant across arms and recorded
// in the output so a reader can prove both arms measured the same generation.
constexpr uint64_t kFixtureGenerationToken = 0x4649585455524500ull;  // "FIXTURE\0"

std::wstring DirectoryName(size_t index) {
    return L"folder_" + std::to_wstring(index % 997) + L"_branch_" + std::to_wstring(index);
}

std::wstring FileName(size_t index) {
    const size_t pattern = index % 10;
    const wchar_t* extension = pattern == 2 ? L".txt" : (pattern == 3 ? L".jpg" : L".dat");
    return L"entry_" + std::to_wstring(index) + extension;
}

struct Fixture {
    // path-sorted like the production std::map export.
    std::map<std::wstring, LogicalDirectory> directories;
    size_t totalEntries = 0;
    size_t totalNameChars = 0;
};

Fixture BuildFixture(size_t scale) {
    Fixture fixture;
    const size_t directoryCount = (std::max)(size_t{1}, scale / 64);
    const size_t fileCount = scale > 1 + directoryCount ? scale - 1 - directoryCount : 0;

    // Node ids mirror BuildSyntheticDatabase: root 5, directories 100..,
    // files 100+directoryCount...
    std::vector<std::wstring> dirPaths(directoryCount);
    std::vector<uint64_t> dirParent(directoryCount);
    for (size_t index = 0; index < directoryCount; ++index) {
        const size_t parentIndex = index < 16 ? (std::numeric_limits<size_t>::max)() : (index - 1) / 16;
        dirParent[index] = parentIndex;
        const std::wstring component = DirectoryName(index);
        dirPaths[index] = (parentIndex == (std::numeric_limits<size_t>::max)())
            ? (std::wstring(L"C:\\") + component)
            : (dirPaths[parentIndex] + L"\\" + component);
    }

    for (size_t index = 0; index < directoryCount; ++index) {
        LogicalDirectory directory;
        directory.path = dirPaths[index];
        directory.volumeRowId = static_cast<int64_t>(100 + index);
        directory.directoryIdLow = 100 + index;
        directory.directoryIdHigh = 0;
        const size_t parentIndex = dirParent[index];
        directory.parentIdLow = parentIndex == (std::numeric_limits<size_t>::max)() ? 5 : 100 + parentIndex;
        directory.parentIdHigh = 0;
        fixture.totalNameChars += directory.path.size();
        fixture.directories.emplace(directory.path, std::move(directory));
    }
    // The root directory key mirrors the production export ("C:").
    {
        LogicalDirectory root;
        root.path = L"C:";
        root.volumeRowId = 5;
        root.directoryIdLow = 5;
        root.parentIdLow = 5;
        fixture.totalNameChars += root.path.size();
        fixture.directories.emplace(root.path, std::move(root));
    }

    const uint64_t fileIdBase = 100 + static_cast<uint64_t>(directoryCount);
    // Every non-root node appears as a child entry in its parent: 199,999
    // child entries for scale 200,000, exactly like the production bench.
    for (size_t index = 0; index < directoryCount; ++index) {
        LogicalEntry entry;
        entry.name = DirectoryName(index);
        entry.isDirectory = true;
        entry.attributes = 0x10;
        entry.creationTime = 10 + index;
        entry.lastModifiedTime = entry.creationTime + 100;
        entry.volumeRowId = static_cast<int64_t>(100 + index);
        entry.fileIdLow = 100 + index;
        const size_t parentIndex = dirParent[index];
        entry.parentIdLow = parentIndex == (std::numeric_limits<size_t>::max)() ? 5 : 100 + parentIndex;
        const std::wstring& parentPath = parentIndex == (std::numeric_limits<size_t>::max)()
            ? fixture.directories.find(L"C:")->first
            : fixture.directories.find(dirPaths[parentIndex])->first;
        fixture.totalNameChars += entry.name.size();
        fixture.directories.find(parentPath)->second.entries.push_back(std::move(entry));
        ++fixture.totalEntries;
    }
    for (size_t index = 0; index < fileCount; ++index) {
        LogicalEntry entry;
        entry.name = FileName(index);
        entry.isDirectory = false;
        entry.sizeBytes = 1024 + index;
        entry.allocatedSizeBytes = 4096 + index;
        entry.allocatedSizeKnown = true;
        entry.attributes = 0x80;
        entry.creationTime = 1000 + index;
        entry.lastModifiedTime = entry.creationTime + 100;
        entry.volumeRowId = static_cast<int64_t>(fileIdBase + index);
        entry.fileIdLow = fileIdBase + index;
        entry.parentIdLow = 100 + (index % directoryCount);
        const std::wstring& parentPath = fixture.directories.find(dirPaths[index % directoryCount])->first;
        fixture.totalNameChars += entry.name.size();
        fixture.directories.find(parentPath)->second.entries.push_back(std::move(entry));
        ++fixture.totalEntries;
    }
    return fixture;
}

// ---------------------------------------------------------------------------
// Production-path build: owning names copied into the production map type.
// This is the export-side per-entry allocation the experiment isolates.
// ---------------------------------------------------------------------------
std::map<std::wstring, ffprotocol::SnapshotDirectory> BuildOwningMap(const Fixture& fixture) {
    std::map<std::wstring, ffprotocol::SnapshotDirectory> map;
    for (const auto& [path, logical] : fixture.directories) {
        ffprotocol::SnapshotDirectory directory;
        directory.status = logical.status;
        directory.volumeRowId = logical.volumeRowId;
        directory.directoryIdLow = logical.directoryIdLow;
        directory.directoryIdHigh = logical.directoryIdHigh;
        directory.parentIdLow = logical.parentIdLow;
        directory.parentIdHigh = logical.parentIdHigh;
        directory.entries.reserve(logical.entries.size());
        for (const auto& source : logical.entries) {
            ffprotocol::SnapshotDirectoryEntry entry;
            entry.name = source.name;
            entry.isDirectory = source.isDirectory;
            entry.sizeBytes = source.sizeBytes;
            entry.allocatedSizeBytes = source.allocatedSizeBytes;
            entry.allocatedSizeKnown = source.allocatedSizeKnown;
            entry.attributes = source.attributes;
            entry.creationTime = source.creationTime;
            entry.lastModifiedTime = source.lastModifiedTime;
            entry.volumeRowId = source.volumeRowId;
            entry.fileIdLow = source.fileIdLow;
            entry.fileIdHigh = source.fileIdHigh;
            entry.parentIdLow = source.parentIdLow;
            entry.parentIdHigh = source.parentIdHigh;
            directory.entries.push_back(std::move(entry));
        }
        map.emplace(path, std::move(directory));
    }
    return map;
}

// ---------------------------------------------------------------------------
// Arena build: one contiguous character pool; entries reference offset/length.
// No per-entry heap allocation; all vectors pre-sized exactly.
// ---------------------------------------------------------------------------
struct ArenaEntryRef {
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

struct ArenaDirectoryRef {
    uint32_t pathOffsetChars = 0;
    uint32_t pathLengthChars = 0;
    ffprotocol::DirectoryEnumerationStatus status = ffprotocol::DirectoryEnumerationStatus::Success;
    int64_t volumeRowId = 0;
    uint64_t directoryIdLow = 0;
    uint64_t directoryIdHigh = 0;
    uint64_t parentIdLow = 0;
    uint64_t parentIdHigh = 0;
    uint32_t entryOffset = 0;
    uint32_t entryCount = 0;
};

struct ArenaSnapshot {
    std::vector<wchar_t> chars;
    std::vector<ArenaDirectoryRef> directories;
    std::vector<ArenaEntryRef> entries;
    size_t directoryCount = 0;
    size_t fileCount = 0;
};

ArenaSnapshot BuildArena(const Fixture& fixture) {
    ArenaSnapshot arena;
    arena.chars.reserve(fixture.totalNameChars + fixture.directories.size());
    arena.directories.reserve(fixture.directories.size());
    arena.entries.reserve(fixture.totalEntries);
    for (const auto& [path, logical] : fixture.directories) {
        ArenaDirectoryRef directory;
        directory.pathOffsetChars = static_cast<uint32_t>(arena.chars.size());
        directory.pathLengthChars = static_cast<uint32_t>(path.size());
        arena.chars.insert(arena.chars.end(), path.begin(), path.end());
        directory.status = logical.status;
        directory.volumeRowId = logical.volumeRowId;
        directory.directoryIdLow = logical.directoryIdLow;
        directory.directoryIdHigh = logical.directoryIdHigh;
        directory.parentIdLow = logical.parentIdLow;
        directory.parentIdHigh = logical.parentIdHigh;
        directory.entryOffset = static_cast<uint32_t>(arena.entries.size());
        directory.entryCount = static_cast<uint32_t>(logical.entries.size());
        for (const auto& source : logical.entries) {
            ArenaEntryRef entry;
            entry.nameOffsetChars = static_cast<uint32_t>(arena.chars.size());
            entry.nameLengthChars = static_cast<uint32_t>(source.name.size());
            arena.chars.insert(arena.chars.end(), source.name.begin(), source.name.end());
            entry.isDirectory = source.isDirectory;
            entry.sizeBytes = source.sizeBytes;
            entry.allocatedSizeBytes = source.allocatedSizeBytes;
            entry.allocatedSizeKnown = source.allocatedSizeKnown;
            entry.attributes = source.attributes;
            entry.creationTime = source.creationTime;
            entry.lastModifiedTime = source.lastModifiedTime;
            entry.volumeRowId = source.volumeRowId;
            entry.fileIdLow = source.fileIdLow;
            entry.fileIdHigh = source.fileIdHigh;
            entry.parentIdLow = source.parentIdLow;
            entry.parentIdHigh = source.parentIdHigh;
            if (entry.isDirectory) { ++arena.directoryCount; } else { ++arena.fileCount; }
            arena.entries.push_back(entry);
        }
        arena.directories.push_back(directory);
    }
    return arena;
}

// ---------------------------------------------------------------------------
// Bench-local writer: emits exactly the bytes ffprotocol::SerializeSnapshotTo
// emits (validated byte-for-byte at runtime against the production serializer;
// a mismatch fails the run). Exists only so the treatment arm can build the
// bytes without per-entry owning strings. It writes into a pre-sized buffer
// with a cursor, exactly like the production Writer, so the treatment's
// serialize stage differs from production only in the name source.
// ---------------------------------------------------------------------------
class ArenaWriter {
public:
    ArenaWriter(uint8_t* destination, size_t capacity)
        : destination_(destination), capacity_(capacity) {}

    void U32(uint32_t value) { Raw(&value, sizeof(value)); }
    void U64(uint64_t value) { Raw(&value, sizeof(value)); }
    void U8(uint8_t value) { Raw(&value, sizeof(value)); }
    void Wide(const wchar_t* text, uint32_t lengthChars) {
        U32(lengthChars);
        Raw(text, static_cast<size_t>(lengthChars) * sizeof(wchar_t));
    }
    bool Valid() const { return valid_; }
private:
    void Raw(const void* source, size_t bytes) {
        if (!valid_ || bytes > capacity_ - offset_) {
            valid_ = false;
            return;
        }
        if (bytes != 0) std::memcpy(destination_ + offset_, source, bytes);
        offset_ += bytes;
    }
    uint8_t* destination_;
    size_t capacity_;
    size_t offset_ = 0;
    bool valid_ = true;
};

// Exact wire size, mirroring SerializedSnapshotSizeImpl's layout arithmetic.
size_t ArenaSerializedSize(const ArenaSnapshot& arena) {
    size_t total = sizeof(uint32_t);
    for (const auto& directory : arena.directories) {
        total += sizeof(uint32_t) + static_cast<size_t>(directory.pathLengthChars) * sizeof(wchar_t)
            + sizeof(uint32_t) + 5 * sizeof(uint64_t) + sizeof(uint32_t);
        for (uint32_t index = 0; index < directory.entryCount; ++index) {
            const auto& entry = arena.entries[directory.entryOffset + index];
            total += 1 + sizeof(uint64_t) + sizeof(uint64_t) + 1 + sizeof(uint32_t)
                + 7 * sizeof(uint64_t)
                + sizeof(uint32_t) + static_cast<size_t>(entry.nameLengthChars) * sizeof(wchar_t);
        }
    }
    return total;
}

void SerializeFromArena(const ArenaSnapshot& arena, std::vector<uint8_t>& out) {
    out.clear();
    out.resize(ArenaSerializedSize(arena));
    ArenaWriter writer(out.data(), out.size());
    writer.U32(static_cast<uint32_t>(arena.directories.size()));
    for (const auto& directory : arena.directories) {
        writer.Wide(arena.chars.data() + directory.pathOffsetChars, directory.pathLengthChars);
        writer.U32(static_cast<uint32_t>(directory.status));
        writer.U64(static_cast<uint64_t>(directory.volumeRowId));
        writer.U64(directory.directoryIdLow);
        writer.U64(directory.directoryIdHigh);
        writer.U64(directory.parentIdLow);
        writer.U64(directory.parentIdHigh);
        writer.U32(directory.entryCount);
        for (uint32_t index = 0; index < directory.entryCount; ++index) {
            const auto& entry = arena.entries[directory.entryOffset + index];
            writer.U8(entry.isDirectory ? 1 : 0);
            writer.U64(entry.sizeBytes);
            writer.U64(entry.allocatedSizeBytes);
            writer.U8(entry.allocatedSizeKnown ? 1 : 0);
            writer.U32(entry.attributes);
            writer.U64(entry.creationTime);
            writer.U64(entry.lastModifiedTime);
            writer.U64(static_cast<uint64_t>(entry.volumeRowId));
            writer.U64(entry.fileIdLow);
            writer.U64(entry.fileIdHigh);
            writer.U64(entry.parentIdLow);
            writer.U64(entry.parentIdHigh);
            writer.Wide(arena.chars.data() + entry.nameOffsetChars, entry.nameLengthChars);
        }
    }
    if (!writer.Valid()) out.clear();
}

// ---------------------------------------------------------------------------
// Zero-copy parse: entries reference (offset, length) into the byte buffer.
// No per-entry owning strings; one pre-sized entry vector and one directory
// vector. Names are compared byte-wise to avoid unaligned wchar loads.
// ---------------------------------------------------------------------------
struct EntryView {
    std::string_view nameBytes;  // UTF-16LE bytes inside the input buffer
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

struct DirectoryView {
    std::string_view pathBytes;
    uint32_t status = 0;
    int64_t volumeRowId = 0;
    uint64_t directoryIdLow = 0;
    uint64_t directoryIdHigh = 0;
    uint64_t parentIdLow = 0;
    uint64_t parentIdHigh = 0;
    uint32_t entryOffset = 0;
    uint32_t entryCount = 0;
};

struct ZeroCopySnapshot {
    std::vector<DirectoryView> directories;
    std::vector<EntryView> entries;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    bool U32(uint32_t& out) { return Raw(&out, sizeof(out)); }
    bool U64(uint64_t& out) { return Raw(&out, sizeof(out)); }
    bool U8(uint8_t& out) { return Raw(&out, sizeof(out)); }
    bool Wide(std::string_view& out) {
        uint32_t lengthChars = 0;
        if (!U32(lengthChars)) return false;
        const size_t byteLength = static_cast<size_t>(lengthChars) * sizeof(wchar_t);
        if (byteLength > remaining()) return false;
        out = std::string_view(reinterpret_cast<const char*>(data_ + offset_), byteLength);
        offset_ += byteLength;
        return true;
    }
private:
    bool Raw(void* out, size_t bytes) {
        if (bytes > remaining()) return false;
        std::memcpy(out, data_ + offset_, bytes);
        offset_ += bytes;
        return true;
    }
    size_t remaining() const { return size_ - offset_; }
    const uint8_t* data_;
    size_t size_;
    size_t offset_ = 0;
};

bool ParseZeroCopy(const uint8_t* data, size_t size, ZeroCopySnapshot& out,
                   std::vector<DirectoryView>& directories, std::vector<EntryView>& entries) {
    ByteReader reader(data, size);
    uint32_t directoryCount = 0;
    if (!reader.U32(directoryCount)) return false;
    directories.clear();
    entries.clear();
    directories.reserve(directoryCount);
    for (uint32_t d = 0; d < directoryCount; ++d) {
        DirectoryView directory;
        uint64_t volumeRowId = 0;
        if (!reader.Wide(directory.pathBytes)
            || !reader.U32(directory.status)
            || !reader.U64(volumeRowId)
            || !reader.U64(directory.directoryIdLow)
            || !reader.U64(directory.directoryIdHigh)
            || !reader.U64(directory.parentIdLow)
            || !reader.U64(directory.parentIdHigh)
            || !reader.U32(directory.entryCount)) {
            return false;
        }
        directory.volumeRowId = static_cast<int64_t>(volumeRowId);
        directory.entryOffset = static_cast<uint32_t>(entries.size());
        for (uint32_t e = 0; e < directory.entryCount; ++e) {
            EntryView entry;
            uint64_t volumeRowIdEntry = 0;
            uint8_t isDirectoryEntry = 0;
            uint8_t allocatedSizeKnown = 0;
            if (!reader.U8(isDirectoryEntry)
                || !reader.U64(entry.sizeBytes)
                || !reader.U64(entry.allocatedSizeBytes)
                || !reader.U8(allocatedSizeKnown)
                || !reader.U32(entry.attributes)
                || !reader.U64(entry.creationTime)
                || !reader.U64(entry.lastModifiedTime)
                || !reader.U64(volumeRowIdEntry)
                || !reader.U64(entry.fileIdLow)
                || !reader.U64(entry.fileIdHigh)
                || !reader.U64(entry.parentIdLow)
                || !reader.U64(entry.parentIdHigh)
                || !reader.Wide(entry.nameBytes)) {
                return false;
            }
            entry.isDirectory = isDirectoryEntry != 0;
            entry.allocatedSizeKnown = allocatedSizeKnown != 0;
            entry.volumeRowId = static_cast<int64_t>(volumeRowIdEntry);
            entries.push_back(entry);
        }
        directories.push_back(directory);
    }
    out.directories = std::move(directories);
    out.entries = std::move(entries);
    return true;
}

// ---------------------------------------------------------------------------
// Arms.
// ---------------------------------------------------------------------------
struct StageTimes {
    double buildMs = 0.0;
    double serializeMs = 0.0;
    double parseMs = 0.0;
    double totalMs = 0.0;
};

enum class Arm { Control, ExportEliminated, ParseEliminated, FullEliminated };

const char* ArmName(Arm arm) {
    switch (arm) {
        case Arm::Control: return "control";
        case Arm::ExportEliminated: return "export-eliminated";
        case Arm::ParseEliminated: return "parse-eliminated";
        case Arm::FullEliminated: return "full-eliminated";
    }
    return "unknown";
}

std::optional<Arm> ParseArm(const std::wstring& value) {
    if (value == L"control") return Arm::Control;
    if (value == L"export-eliminated") return Arm::ExportEliminated;
    if (value == L"parse-eliminated") return Arm::ParseEliminated;
    if (value == L"full-eliminated") return Arm::FullEliminated;
    return std::nullopt;
}

struct ArmResult {
    StageTimes times;
    size_t payloadBytes = 0;
    size_t directoriesSeen = 0;
    size_t entriesSeen = 0;
    bool valid = false;
};

std::vector<uint8_t> SerializeOwning(const std::map<std::wstring, ffprotocol::SnapshotDirectory>& map) {
    const size_t size = ffprotocol::SerializedSnapshotSize(map);
    std::vector<uint8_t> bytes(size);
    if (size == 0 || !ffprotocol::SerializeSnapshotTo(map, bytes.data(), bytes.size())) {
        bytes.clear();
    }
    return bytes;
}

ArmResult RunArm(Arm arm, const Fixture& fixture, std::vector<uint8_t>& scratch) {
    ArmResult result;
    switch (arm) {
        case Arm::Control:
        case Arm::ParseEliminated: {
            const auto buildStart = Clock::now();
            auto map = BuildOwningMap(fixture);
            result.times.buildMs = ElapsedMs(buildStart);

            const auto serializeStart = Clock::now();
            scratch = SerializeOwning(map);
            result.times.serializeMs = ElapsedMs(serializeStart);
            if (scratch.empty()) return result;

            const auto parseStart = Clock::now();
            if (arm == Arm::Control) {
                auto parsed = ffprotocol::ParseSnapshot(scratch.data(), scratch.size());
                result.times.parseMs = ElapsedMs(parseStart);
                if (!parsed) return result;
                result.directoriesSeen = parsed->size();
                for (const auto& [path, directory] : *parsed) {
                    (void)path;
                    result.entriesSeen += directory.entries.size();
                }
                result.valid = true;
            } else {
                ZeroCopySnapshot parsed;
                std::vector<DirectoryView> directories;
                std::vector<EntryView> entries;
                const bool ok = ParseZeroCopy(scratch.data(), scratch.size(), parsed, directories, entries);
                result.times.parseMs = ElapsedMs(parseStart);
                if (!ok) return result;
                result.directoriesSeen = parsed.directories.size();
                result.entriesSeen = parsed.entries.size();
                result.valid = true;
            }
            result.payloadBytes = scratch.size();
            break;
        }
        case Arm::ExportEliminated:
        case Arm::FullEliminated: {
            const auto buildStart = Clock::now();
            ArenaSnapshot arena = BuildArena(fixture);
            result.times.buildMs = ElapsedMs(buildStart);

            const auto serializeStart = Clock::now();
            SerializeFromArena(arena, scratch);
            result.times.serializeMs = ElapsedMs(serializeStart);
            if (scratch.empty()) return result;

            const auto parseStart = Clock::now();
            if (arm == Arm::ExportEliminated) {
                auto parsed = ffprotocol::ParseSnapshot(scratch.data(), scratch.size());
                result.times.parseMs = ElapsedMs(parseStart);
                if (!parsed) return result;
                result.directoriesSeen = parsed->size();
                for (const auto& [path, directory] : *parsed) {
                    (void)path;
                    result.entriesSeen += directory.entries.size();
                }
                result.valid = true;
            } else {
                ZeroCopySnapshot parsed;
                std::vector<DirectoryView> directories;
                std::vector<EntryView> entries;
                const bool ok = ParseZeroCopy(scratch.data(), scratch.size(), parsed, directories, entries);
                result.times.parseMs = ElapsedMs(parseStart);
                if (!ok) return result;
                result.directoriesSeen = parsed.directories.size();
                result.entriesSeen = parsed.entries.size();
                result.valid = true;
            }
            result.payloadBytes = scratch.size();
            break;
        }
    }
    result.times.totalMs = result.times.buildMs + result.times.serializeMs + result.times.parseMs;
    return result;
}

// ---------------------------------------------------------------------------
// Identity validation: the treatment bytes must equal the production bytes
// exactly (payload identity is a precondition of the allocation-only claim),
// and parsed names must equal the fixture names entry by entry.
// ---------------------------------------------------------------------------
bool ValidateIdentity(const std::vector<uint8_t>& productionBytes,
                      const std::vector<uint8_t>& treatmentBytes) {
    if (productionBytes.size() != treatmentBytes.size()) {
        std::fprintf(stderr, "payload byte count mismatch: production=%zu treatment=%zu\n",
                     productionBytes.size(), treatmentBytes.size());
        return false;
    }
    if (std::memcmp(productionBytes.data(), treatmentBytes.data(), productionBytes.size()) != 0) {
        std::fprintf(stderr, "payload bytes differ between production serializer and bench writer\n");
        return false;
    }
    return true;
}

bool ValidateParsedNames(const Fixture& fixture, const uint8_t* data, size_t size) {
    ZeroCopySnapshot parsed;
    std::vector<DirectoryView> directories;
    std::vector<EntryView> entries;
    if (!ParseZeroCopy(data, size, parsed, directories, entries)) return false;
    size_t directoryIndex = 0;
    for (const auto& [path, logical] : fixture.directories) {
        if (directoryIndex >= parsed.directories.size()) return false;
        const auto& view = parsed.directories[directoryIndex];
        if (view.pathBytes.size() != path.size() * sizeof(wchar_t)
            || std::memcmp(view.pathBytes.data(), path.data(), view.pathBytes.size()) != 0) {
            std::fprintf(stderr, "directory path mismatch at %zu\n", directoryIndex);
            return false;
        }
        if (view.entryCount != logical.entries.size()) return false;
        for (size_t e = 0; e < logical.entries.size(); ++e) {
            const auto& source = logical.entries[e];
            const auto& entryView = parsed.entries[view.entryOffset + e];
            if (entryView.nameBytes.size() != source.name.size() * sizeof(wchar_t)
                || std::memcmp(entryView.nameBytes.data(), source.name.data(), entryView.nameBytes.size()) != 0) {
                std::fprintf(stderr, "entry name mismatch in '%ls' at %zu\n", path.c_str(), e);
                return false;
            }
            if (entryView.isDirectory != source.isDirectory
                || entryView.sizeBytes != source.sizeBytes
                || entryView.allocatedSizeBytes != source.allocatedSizeBytes
                || entryView.allocatedSizeKnown != source.allocatedSizeKnown
                || entryView.attributes != source.attributes
                || entryView.creationTime != source.creationTime
                || entryView.lastModifiedTime != source.lastModifiedTime
                || entryView.volumeRowId != source.volumeRowId
                || entryView.fileIdLow != source.fileIdLow
                || entryView.fileIdHigh != source.fileIdHigh
                || entryView.parentIdLow != source.parentIdLow
                || entryView.parentIdHigh != source.parentIdHigh) {
                std::fprintf(stderr, "entry field mismatch in '%ls' at %zu\n", path.c_str(), e);
                return false;
            }
        }
        ++directoryIndex;
    }
    return directoryIndex == parsed.directories.size();
}

struct Options {
    size_t entries = 200'000;
    size_t warmup = 5;
    size_t measured = 5;
    std::wstring outputPath = L"allocation-isolation.json";
    std::optional<Arm> arm;
    bool countAllocations = false;
};

bool ParseOptions(int argc, wchar_t** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring argument(argv[i]);
        if (argument == L"--entries" && i + 1 < argc) {
            options.entries = static_cast<size_t>(_wtoi(argv[++i]));
        } else if (argument == L"--warmup" && i + 1 < argc) {
            options.warmup = static_cast<size_t>(_wtoi(argv[++i]));
        } else if (argument == L"--measured" && i + 1 < argc) {
            options.measured = static_cast<size_t>(_wtoi(argv[++i]));
        } else if (argument == L"--output" && i + 1 < argc) {
            options.outputPath = argv[++i];
        } else if (argument == L"--arm" && i + 1 < argc) {
            options.arm = ParseArm(argv[++i]);
            if (!options.arm) {
                std::fprintf(stderr, "unknown --arm value\n");
                return false;
            }
        } else if (argument == L"--count-allocations") {
            options.countAllocations = true;
        } else {
            std::fprintf(stderr,
                         "Usage: %s [--entries N] [--warmup N] [--measured N] [--output path] "
                         "[--arm control|export-eliminated|parse-eliminated|full-eliminated] [--count-allocations]\n",
                         "ffengine_bench_allocation_isolation");
            return false;
        }
    }
    return options.measured > 0 && options.measured <= 4096;
}

void WriteStats(FILE* out, const char* key, std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2];
    const double p90 = samples[(samples.size() * 9 + 9) / 10 - 1];
    const double max = samples.back();
    std::fprintf(out, "\"%s\": {\"median\": %.4f, \"p90\": %.4f, \"max\": %.4f}", key, median, p90, max);
}

}  // namespace

// Bench-scoped allocator (count pass). Replaceable global operators; the gate
// keeps latency passes at one relaxed load per allocation.
void* operator new(std::size_t size) {
    void* memory = std::malloc(size != 0 ? size : 1);
    if (memory == nullptr) throw std::bad_alloc{};
    if (gCountingEnabled.load(std::memory_order_relaxed)) {
        gAllocationCount.fetch_add(1, std::memory_order_relaxed);
        gAllocationBytes.fetch_add(size, std::memory_order_relaxed);
    }
    return memory;
}
void* operator new[](std::size_t size) {
    void* memory = std::malloc(size != 0 ? size : 1);
    if (memory == nullptr) throw std::bad_alloc{};
    if (gCountingEnabled.load(std::memory_order_relaxed)) {
        gAllocationCount.fetch_add(1, std::memory_order_relaxed);
        gAllocationBytes.fetch_add(size, std::memory_order_relaxed);
    }
    return memory;
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!ParseOptions(argc, argv, options)) return 2;

    const auto fixtureStart = Clock::now();
    const Fixture fixture = BuildFixture(options.entries);
    const double fixtureMs = ElapsedMs(fixtureStart);

    // Reference production bytes (untimed): every arm self-validates against
    // this in the same process, so the payload-identity precondition is
    // mechanical, not assumed.
    std::vector<uint8_t> referenceBytes;
    {
        auto map = BuildOwningMap(fixture);
        referenceBytes = SerializeOwning(map);
        if (referenceBytes.empty()) {
            std::fprintf(stderr, "production serializer rejected the fixture\n");
            return 1;
        }
    }
    if (!ValidateParsedNames(fixture, referenceBytes.data(), referenceBytes.size())) {
        std::fprintf(stderr, "reference bytes failed name/field identity validation\n");
        return 3;
    }

    std::vector<Arm> arms;
    if (options.arm) {
        arms.push_back(*options.arm);
    } else {
        arms = {Arm::Control, Arm::ExportEliminated, Arm::ParseEliminated, Arm::FullEliminated};
    }

    std::printf("FastFiles allocation-isolation benchmark (%zu entries, %zu directories, %zu child entries)\n",
                options.entries, fixture.directories.size(), fixture.totalEntries);
    std::printf("fixture generation token 0x%016llX; payload %zu bytes; fixture build %.2f ms\n",
                static_cast<unsigned long long>(kFixtureGenerationToken), referenceBytes.size(), fixtureMs);
    std::printf("counting=%s; %zu warm-up and %zu measured repetitions per arm\n",
                options.countAllocations ? "instrumented" : "off (latency pass)",
                options.warmup, options.measured);
    std::fflush(stdout);

    FILE* out = nullptr;
    if (_wfopen_s(&out, options.outputPath.c_str(), L"w") != 0 || out == nullptr) {
        std::fprintf(stderr, "unable to open output path\n");
        return 1;
    }

    std::fprintf(out, "{\n \"schema\": \"fastfiles-allocation-isolation-v1\",\n");
    std::fprintf(out, " \"entries\": %zu,\n", options.entries);
    std::fprintf(out, " \"directories\": %zu,\n", fixture.directories.size());
    std::fprintf(out, " \"child_entries\": %zu,\n", fixture.totalEntries);
    std::fprintf(out, " \"payload_bytes\": %zu,\n", referenceBytes.size());
    std::fprintf(out, " \"fixture_generation_token\": \"0x%016llX\",\n",
                 static_cast<unsigned long long>(kFixtureGenerationToken));
    std::fprintf(out, " \"allocation_counting\": \"%s\",\n",
                 options.countAllocations ? "instrumented-global-new-gate" : "unmeasured");
    std::fprintf(out, " \"warmup\": %zu,\n \"measured\": %zu,\n \"arms\": {\n",
                 options.warmup, options.measured);

    bool firstArm = true;
    for (const Arm arm : arms) {
        std::vector<double> buildSamples;
        std::vector<double> serializeSamples;
        std::vector<double> parseSamples;
        std::vector<double> totalSamples;
        std::vector<uint8_t> scratch;
        unsigned long long armAllocCount = 0;
        unsigned long long armAllocBytes = 0;
        bool armValid = true;

        for (size_t repetition = 0; repetition < options.warmup + options.measured; ++repetition) {
            const bool measured = repetition >= options.warmup;
            if (measured && options.countAllocations) {
                gAllocationCount.store(0, std::memory_order_relaxed);
                gAllocationBytes.store(0, std::memory_order_relaxed);
                gCountingEnabled.store(true, std::memory_order_relaxed);
            }
            ArmResult result = RunArm(arm, fixture, scratch);
            if (measured && options.countAllocations) {
                gCountingEnabled.store(false, std::memory_order_relaxed);
                armAllocCount += gAllocationCount.load(std::memory_order_relaxed);
                armAllocBytes += gAllocationBytes.load(std::memory_order_relaxed);
            }
            if (!result.valid) {
                armValid = false;
                break;
            }
            // Payload identity is a precondition: any byte mismatch
            // invalidates the allocation-only conclusion (exit 3).
            if (!ValidateIdentity(referenceBytes, scratch)) {
                std::fprintf(stderr, "arm %s produced different bytes; run invalid\n", ArmName(arm));
                std::fclose(out);
                return 3;
            }
            if (result.directoriesSeen != fixture.directories.size()
                || result.entriesSeen != fixture.totalEntries) {
                std::fprintf(stderr, "arm %s changed published counts; run invalid\n", ArmName(arm));
                std::fclose(out);
                return 3;
            }
            if (measured) {
                buildSamples.push_back(result.times.buildMs);
                serializeSamples.push_back(result.times.serializeMs);
                parseSamples.push_back(result.times.parseMs);
                totalSamples.push_back(result.times.totalMs);
                std::printf("arm %-18s rep %zu: build %.3f serialize %.3f parse %.3f total %.3f ms\n",
                            ArmName(arm), repetition - options.warmup,
                            result.times.buildMs, result.times.serializeMs,
                            result.times.parseMs, result.times.totalMs);
                std::fflush(stdout);
            }
        }
        if (!armValid || totalSamples.empty()) {
            std::fprintf(stderr, "arm %s failed to execute\n", ArmName(arm));
            std::fclose(out);
            return 1;
        }
        if (options.countAllocations && !totalSamples.empty()) {
            const size_t reps = totalSamples.size();
            armAllocCount /= reps;
            armAllocBytes /= reps;
        }

        if (!firstArm) std::fprintf(out, ",\n");
        firstArm = false;
        std::fprintf(out, "  \"%s\": {\n   \"payload_bytes\": %zu,\n", ArmName(arm), referenceBytes.size());
        std::fprintf(out, "   \"directories\": %zu,\n   \"child_entries\": %zu,\n",
                     fixture.directories.size(), fixture.totalEntries);
        std::fprintf(out, "   \"allocations_per_repetition\": %s,\n",
                     options.countAllocations ? std::to_string(armAllocCount).c_str() : "\"unmeasured\"");
        std::fprintf(out, "   \"allocated_bytes_per_repetition\": %s,\n",
                     options.countAllocations ? std::to_string(armAllocBytes).c_str() : "\"unmeasured\"");
        std::fprintf(out, "   ");
        WriteStats(out, "build_ms", buildSamples);
        std::fprintf(out, ",\n   ");
        WriteStats(out, "serialize_ms", serializeSamples);
        std::fprintf(out, ",\n   ");
        WriteStats(out, "parse_ms", parseSamples);
        std::fprintf(out, ",\n   ");
        WriteStats(out, "total_ms", totalSamples);
        std::fprintf(out, "\n  }");
    }
    std::fprintf(out, "\n }\n}\n");
    std::fclose(out);
    std::printf("WROTE %ls\n", options.outputPath.c_str());
    return 0;
}
