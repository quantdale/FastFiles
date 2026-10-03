// performance-closure-and-multisurface-acceleration 6.3: exact output
// equivalence for the pooled name representation. The pooled serializer must
// emit exactly the bytes the owning serializer emits (wire format version 3
// unchanged), names must survive Unicode / mixed case / pathological
// maximum-length content, and pooled directory copy/move must not dangle or
// alter output. These tests are the mechanical byte-identity guardian for the
// shared layout implementation.

#include <cstdint>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "ffprotocol/SnapshotFormat.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

using OwningMap = std::map<std::wstring, ffprotocol::SnapshotDirectory>;
using PooledMap = ffprotocol::PooledSnapshotMap;

const std::wstring kUnicodeName = L"\u0391\u03bb\u03ac\u03c6\u03b1_123";  // Greek "Alpha_123"
const std::wstring kMixedCaseName = L"MixedCASE_NoNAscii_\u00c4\u00d6\u00dc";
const std::wstring kEmptyName = L"";

std::wstring MaxLengthName(wchar_t fill) {
    // NTFS caps names at 32,767 UTF-16 code units: the pathological case the
    // representation must round-trip unchanged.
    return std::wstring(32767, fill);
}

ffprotocol::SnapshotDirectoryEntry MakeOwningEntry(const std::wstring& name, size_t seed) {
    ffprotocol::SnapshotDirectoryEntry entry;
    entry.name = name;
    entry.isDirectory = (seed % 3) == 0;
    entry.sizeBytes = 1000 + seed;
    entry.allocatedSizeBytes = 4096 + seed;
    entry.allocatedSizeKnown = (seed % 2) == 0;
    entry.attributes = static_cast<uint32_t>(0x20 + (seed % 7));
    entry.creationTime = 5000 + seed;
    entry.lastModifiedTime = 6000 + seed;
    entry.volumeRowId = static_cast<int64_t>(7000 + seed);
    entry.fileIdLow = 8000 + seed;
    entry.fileIdHigh = 9000 + seed;
    entry.parentIdLow = 10000 + seed;
    entry.parentIdHigh = 11000 + seed;
    return entry;
}

OwningMap MakeFixture(unsigned seed) {
    std::mt19937 rng(seed);
    OwningMap map;
    const std::vector<std::wstring> nameShapes = {
        kEmptyName,
        L"a",
        kUnicodeName,
        kMixedCaseName,
        L"entry_12345.dat",
        L"very.long.name.exceeding.the.small.string.buffer.by.quite.a.lot.bin",
        MaxLengthName(L'Z'),
    };
    for (int d = 0; d < 8; ++d) {
        const std::wstring path = L"C:\\dir_" + std::to_wstring(d) + L"\\" + kMixedCaseName;
        ffprotocol::SnapshotDirectory directory;
        directory.status = d % 2 == 0 ? ffprotocol::DirectoryEnumerationStatus::Success
                                      : ffprotocol::DirectoryEnumerationStatus::AccessDenied;
        directory.volumeRowId = 100 + d;
        directory.directoryIdLow = 200 + d;
        directory.directoryIdHigh = 300 + d;
        directory.parentIdLow = 400 + d;
        directory.parentIdHigh = 500 + d;
        const int entryCount = 1 + static_cast<int>(rng() % 5);
        for (int e = 0; e < entryCount; ++e) {
            directory.entries.push_back(
                MakeOwningEntry(nameShapes[rng() % nameShapes.size()], d * 10 + e));
        }
        map.emplace(path, std::move(directory));
    }
    return map;
}

PooledMap PoolOf(const OwningMap& source) {
    PooledMap pooled;
    for (const auto& [path, directory] : source) {
        ffprotocol::PooledSnapshotDirectoryBuilder builder;
        builder.SetStatus(directory.status);
        builder.SetIdentity(directory.volumeRowId, directory.directoryIdLow, directory.directoryIdHigh,
                            directory.parentIdLow, directory.parentIdHigh);
        for (const auto& entry : directory.entries) {
            builder.AddEntry(entry.name, entry.isDirectory, entry.sizeBytes, entry.allocatedSizeBytes,
                             entry.allocatedSizeKnown, entry.attributes, entry.creationTime,
                             entry.lastModifiedTime, entry.volumeRowId, entry.fileIdLow,
                             entry.fileIdHigh, entry.parentIdLow, entry.parentIdHigh);
        }
        pooled.emplace(path, std::move(builder).Build());
    }
    return pooled;
}

bool SameEntry(const ffprotocol::SnapshotDirectoryEntry& a, const ffprotocol::PooledSnapshotEntry& b) {
    return std::wstring_view(a.name) == b.name
        && a.isDirectory == b.isDirectory
        && a.sizeBytes == b.sizeBytes
        && a.allocatedSizeBytes == b.allocatedSizeBytes
        && a.allocatedSizeKnown == b.allocatedSizeKnown
        && a.attributes == b.attributes
        && a.creationTime == b.creationTime
        && a.lastModifiedTime == b.lastModifiedTime
        && a.volumeRowId == b.volumeRowId
        && a.fileIdLow == b.fileIdLow
        && a.fileIdHigh == b.fileIdHigh
        && a.parentIdLow == b.parentIdLow
        && a.parentIdHigh == b.parentIdHigh;
}

}  // namespace

int main() {
    // Byte-equivalence across randomized fixtures, including Unicode,
    // mixed-case, empty, and 32,767-char pathological names.
    for (unsigned seed = 1; seed <= 12; ++seed) {
        const OwningMap owning = MakeFixture(seed);
        const PooledMap pooled = PoolOf(owning);
        const std::string label = "fixture " + std::to_string(seed) + ": ";

        const std::vector<uint8_t> owningBytes = ffprotocol::SerializeSnapshot(owning);
        const std::vector<uint8_t> pooledBytes = ffprotocol::SerializeSnapshot(pooled);
        Check(owningBytes.size() == pooledBytes.size(),
              (label + "pooled payload size equals owning payload size").c_str());
        Check(!owningBytes.empty()
                  && owningBytes.size() == pooledBytes.size()
                  && std::memcmp(owningBytes.data(), pooledBytes.data(), owningBytes.size()) == 0,
              (label + "pooled bytes are byte-identical to owning bytes").c_str());

        Check(ffprotocol::SerializedSnapshotSize(pooled) == owningBytes.size(),
              (label + "pooled size pass matches the owning size").c_str());

        // Two-pass writer agrees with the one-pass convenience writer.
        std::vector<uint8_t> twoPass(pooledBytes.size());
        Check(ffprotocol::SerializeSnapshotTo(pooled, twoPass.data(), twoPass.size()),
              (label + "pooled SerializeSnapshotTo succeeds at exact capacity").c_str());
        Check(twoPass == pooledBytes,
              (label + "pooled two-pass output equals one-pass output").c_str());

        // Capacity fail-closed: one byte short must refuse, never truncate.
        std::vector<uint8_t> tooSmall(pooledBytes.empty() ? 0 : pooledBytes.size() - 1);
        Check(!ffprotocol::SerializeSnapshotTo(pooled, tooSmall.data(), tooSmall.size()),
              (label + "pooled writer fails closed when capacity is short").c_str());
    }

    // Parse round-trip: pooled bytes parse to the owning model unchanged.
    {
        const OwningMap owning = MakeFixture(7);
        const PooledMap pooled = PoolOf(owning);
        const std::vector<uint8_t> pooledBytes = ffprotocol::SerializeSnapshot(pooled);
        const auto parsed = ffprotocol::ParseSnapshot(pooledBytes.data(), pooledBytes.size());
        Check(parsed.has_value(), "pooled bytes parse successfully");
        if (parsed) {
            Check(parsed->size() == owning.size(), "parsed directory count equals fixture");
            bool entriesMatch = parsed->size() == owning.size();
            auto owningIt = owning.begin();
            for (const auto& [path, directory] : *parsed) {
                if (owningIt == owning.end() || owningIt->first != path
                    || owningIt->second.entries.size() != directory.entries.size()) {
                    entriesMatch = false;
                    break;
                }
                for (size_t index = 0; index < directory.entries.size(); ++index) {
                    if (!SameEntry(owningIt->second.entries[index], PoolOf(owning).at(path).entries[index])) {
                        entriesMatch = false;
                        break;
                    }
                    if (std::wstring_view(owningIt->second.entries[index].name)
                            != std::wstring_view(directory.entries[index].name)) {
                        entriesMatch = false;
                        break;
                    }
                }
                if (!entriesMatch) break;
                ++owningIt;
            }
            Check(entriesMatch, "parsed pooled bytes preserve every name and field");
        }
    }

    // Copy and move of a pooled directory never dangle or change bytes.
    {
        const OwningMap owning = MakeFixture(3);
        const PooledMap pooled = PoolOf(owning);
        const std::vector<uint8_t> reference = ffprotocol::SerializeSnapshot(pooled);

        PooledMap copied = pooled;
        const std::vector<uint8_t> copiedBytes = ffprotocol::SerializeSnapshot(copied);
        Check(copiedBytes == reference, "copied pooled map serializes byte-identically (views remapped)");

        PooledMap moved = std::move(copied);
        const std::vector<uint8_t> movedBytes = ffprotocol::SerializeSnapshot(moved);
        Check(movedBytes == reference, "moved pooled map serializes byte-identically (buffer transfer)");

        // Re-assign a directory copy over another and re-serialize.
        PooledMap reassigned = pooled;
        auto first = reassigned.begin();
        first->second = PooledMap::mapped_type(pooled.begin()->second);
        const std::vector<uint8_t> reassignedBytes = ffprotocol::SerializeSnapshot(reassigned);
        Check(reassignedBytes == reference,
              "copy-assigned pooled directory serializes byte-identically");
    }

    // Builder growth: many names force buffer reallocations before views are
    // created; the final views must still reference exact name text.
    {
        ffprotocol::PooledSnapshotDirectoryBuilder builder;
        builder.SetStatus(ffprotocol::DirectoryEnumerationStatus::Success);
        builder.SetIdentity(1, 2, 3, 4, 5);
        std::wstring expected;
        for (size_t index = 0; index < 5000; ++index) {
            const std::wstring name = L"growth_probe_" + std::to_wstring(index) + L"_"
                + std::wstring(64, static_cast<wchar_t>(L'A' + (index % 26)));
            builder.AddEntry(name, false, index, index + 1, true, 0, index + 2, index + 3,
                             static_cast<int64_t>(index + 4), index + 5, index + 6, index + 7, index + 8);
            if (index == 4999) expected = name;
        }
        ffprotocol::PooledSnapshotDirectory directory = std::move(builder).Build();
        Check(directory.entries.size() == 5000, "builder growth preserves every entry");
        Check(directory.entries.back().name == expected,
              "builder growth preserves exact name text after buffer reallocation");
    }

    // review-B finding F5: the production container is the case-insensitive
    // pooled map; prove its serialization is byte-identical to the owning
    // case-insensitive map for the same mixed-case keys.
    {
        const OwningMap fixture = MakeFixture(5);
        ffprotocol::CaseInsensitiveSnapshotMap owningCi;
        ffprotocol::PooledCaseInsensitiveSnapshotMap pooledCi;
        for (const auto& [path, directory] : fixture) {
            ffprotocol::SnapshotDirectory owningDirectory = directory;
            ffprotocol::PooledSnapshotDirectoryBuilder builder;
            builder.SetStatus(directory.status);
            builder.SetIdentity(directory.volumeRowId, directory.directoryIdLow, directory.directoryIdHigh,
                                directory.parentIdLow, directory.parentIdHigh);
            for (const auto& entry : directory.entries) {
                builder.AddEntry(entry.name, entry.isDirectory, entry.sizeBytes,
                                 entry.allocatedSizeBytes, entry.allocatedSizeKnown, entry.attributes,
                                 entry.creationTime, entry.lastModifiedTime, entry.volumeRowId,
                                 entry.fileIdLow, entry.fileIdHigh, entry.parentIdLow, entry.parentIdHigh);
            }
            owningCi.emplace(path, std::move(owningDirectory));
            pooledCi.emplace(path, std::move(builder).Build());
        }
        const std::vector<uint8_t> owningBytes = ffprotocol::SerializeSnapshot(owningCi);
        const std::vector<uint8_t> pooledBytes = ffprotocol::SerializeSnapshot(pooledCi);
        Check(!owningBytes.empty() && owningBytes == pooledBytes,
              "case-insensitive pooled map serializes byte-identically to the owning case-insensitive map");
    }

    // Empty fixture: a valid empty snapshot is exactly four bytes both ways.
    {
        OwningMap emptyOwning;
        PooledMap emptyPooled;
        const std::vector<uint8_t> a = ffprotocol::SerializeSnapshot(emptyOwning);
        const std::vector<uint8_t> b = ffprotocol::SerializeSnapshot(emptyPooled);
        Check(a.size() == 4 && b == a, "empty pooled snapshot equals empty owning snapshot (4 bytes)");
    }

    // review-C finding: Check() reports through fftest::FailureCount(), so the
    // exit code must come from it; a local never-assigned counter made this
    // gate unable to fail.
    return FailureCount() == 0 ? 0 : 1;
}
