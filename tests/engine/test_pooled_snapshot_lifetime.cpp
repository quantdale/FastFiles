// performance-closure-and-multisurface-acceleration 6.2/6.3: focused
// lifetime and equivalence proofs for the pooled name representation.
//
// 6.2: backing storage must outlive every reader across repeated generation
// replacement, disconnect-style teardown, and concurrent readers; shutdown
// must not wait indefinitely for a stalled reader; the engine must not retain
// every old generation; and the current-mapping / independently-held-client-
// mapping contract in SnapshotPublisher.h must be preserved.
//
// 6.3: the pooled export must produce exactly the bytes the owning export
// produces for the same fixture, including Unicode, mixed-case, and long
// names.

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "IndexPipeline.h"
#include "SnapshotPublisher.h"
#include "ffprotocol/Records.h"
#include "ffprotocol/SnapshotFormat.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

using Clock = std::chrono::steady_clock;

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::u16string ToUtf16(const std::wstring& value) {
    return std::u16string(value.begin(), value.end());
}

ffprotocol::PooledSnapshotDirectory MakePooledDirectory(const std::wstring& name, uint64_t seed) {
    ffprotocol::PooledSnapshotDirectoryBuilder builder;
    builder.SetStatus(ffprotocol::DirectoryEnumerationStatus::Success);
    builder.SetIdentity(static_cast<int64_t>(seed), seed, seed + 1, seed + 2, seed + 3);
    builder.AddEntry(name, false, seed + 4, seed + 5, true, 0x20, seed + 6, seed + 7,
                     static_cast<int64_t>(seed + 8), seed + 9, seed + 10, seed + 11, seed + 12);
    builder.AddEntry(L"secondary_entry_" + std::to_wstring(seed) + L".dat", true, seed, seed, true,
                     0x10, seed, seed, static_cast<int64_t>(seed), seed, seed, seed, seed);
    return std::move(builder).Build();
}

bool MapLooksConsistent(const uint8_t* view, uint64_t expectedGeneration) {
    const auto* header = reinterpret_cast<const ffprotocol::SnapshotSharedHeader*>(view);
    if (header->formatVersion != ffprotocol::kSnapshotFormatVersion) return false;
    if (header->generation != expectedGeneration) return false;
    return ffprotocol::ParseSnapshot(view + sizeof(*header),
                                     static_cast<size_t>(header->payloadSizeBytes)).has_value();
}

uint64_t GenerationOfSectionName(const std::wstring& sectionName) {
    const size_t dot = sectionName.rfind(L'.');
    if (dot == std::wstring::npos) return 0;
    return static_cast<uint64_t>(_wtoi64(sectionName.c_str() + dot + 1));
}

// 6.3: exact fixture output equivalence at the engine export seam, over a
// seeded pipeline whose names include Unicode, mixed case, and long text.
void TestPooledExportMatchesOwningExportExactly() {
    wchar_t tempPath[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempPath) == 0) return;
    const std::wstring rootPath = std::wstring(tempPath) +
        L"FastFiles-pooled-export-equivalence-" + std::to_wstring(GetCurrentProcessId());
    std::error_code ignored;
    std::filesystem::remove_all(rootPath, ignored);
    if (!std::filesystem::create_directories(rootPath, ignored)) return;
    const std::wstring databasePath = rootPath + L"\\index.db";

    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(ToUtf8(databasePath)), "equivalence fixture pipeline opens");
    const ffindexstore::VolumeRowId volumeId = pipeline.ResolveVolume(ffindexstore::VolumeKey{});
    Check(volumeId != 0, "equivalence fixture volume resolves");
    if (volumeId == 0) {
        pipeline.Close();
        std::filesystem::remove_all(rootPath, ignored);
        return;
    }
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    pipeline.SetVolumeRules(volumeId, setting);

    const std::vector<std::wstring> adversarialNames = {
        L"\u0391\u03bb\u03ac\u03c6\u03b1.txt",                 // Unicode
        L"MixedCASE_Trailing.dat",                            // mixed case
        L"a_very_long_file_name_exceeding_small_string_buffer_by_a_wide_margin_0123456789.bin",
        L"\u7d20\u6750.jpg",                                  // CJK
    };
    std::vector<ffprotocol::MftRecordV1> batch;
    batch.reserve(64);
    const auto makeRecord = [](uint64_t id, uint64_t parent, const std::wstring& name,
                               uint32_t attributes, uint64_t sizeBytes, uint64_t timestamp) {
        ffprotocol::MftRecordV1 record{};
        record.fixed.fileReferenceNumber = id;
        record.fixed.parentFileReferenceNumber = parent;
        record.fixed.creationTime = timestamp;
        record.fixed.lastModifiedTime = timestamp + 1;
        record.fixed.lastAccessTime = timestamp + 2;
        record.fixed.sizeBytes = sizeBytes;
        record.fixed.fileAttributes = attributes;
        record.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
        record.fileName = ToUtf16(name);
        return record;
    };
    batch.push_back(makeRecord(5, 5, L"", 0x10, 0, 1));
    batch.push_back(makeRecord(100, 5, L"folder_\u00dcnicode", 0x10, 0, 2));
    for (size_t index = 0; index < adversarialNames.size(); ++index) {
        batch.push_back(makeRecord(200 + index, 100, adversarialNames[index], 0x80,
                                   4096 + index, 10 + index));
    }
    Check(pipeline.ApplyMftBatch(volumeId, batch), "equivalence fixture batch applies");
    pipeline.RebuildAll({});
    Check(pipeline.MarkScanComplete(volumeId), "equivalence fixture marks scan complete");

    for (const size_t bound : {size_t{1}, size_t{8}, (std::numeric_limits<size_t>::max)()}) {
        const ffprotocol::PooledSnapshotMap pooled =
            pipeline.ExportDirectorySnapshotBoundedPooled(volumeId, L"C:", bound);
        const std::map<std::wstring, ffprotocol::SnapshotDirectory> owning =
            pipeline.ExportDirectorySnapshotBounded(volumeId, L"C:", bound);
        const std::vector<uint8_t> pooledBytes = ffprotocol::SerializeSnapshot(pooled);
        const std::vector<uint8_t> owningBytes = ffprotocol::SerializeSnapshot(owning);
        Check(pooledBytes.size() == owningBytes.size() && !pooledBytes.empty()
                  && std::memcmp(pooledBytes.data(), owningBytes.data(), pooledBytes.size()) == 0,
              "pooled export serializes byte-identically to the owning export at every bound");
    }

    const ffprotocol::PooledSnapshotMap pooled =
        pipeline.ExportDirectorySnapshotBoundedPooled(volumeId, L"C:",
                                                      (std::numeric_limits<size_t>::max)());
    const std::vector<uint8_t> pooledBytes = ffprotocol::SerializeSnapshot(pooled);
    const auto parsed = ffprotocol::ParseSnapshot(pooledBytes.data(), pooledBytes.size());
    Check(parsed.has_value(), "pooled export bytes parse");
    if (parsed) {
        bool foundUnicode = false;
        bool foundMixed = false;
        bool foundCjk = false;
        for (const auto& [path, directory] : *parsed) {
            for (const auto& entry : directory.entries) {
                if (entry.name == L"\u0391\u03bb\u03ac\u03c6\u03b1.txt") foundUnicode = true;
                if (entry.name == L"MixedCASE_Trailing.dat") foundMixed = true;
                if (entry.name == L"\u7d20\u6750.jpg") foundCjk = true;
            }
        }
        Check(foundUnicode && foundMixed && foundCjk,
              "adversarial Unicode, mixed-case, and CJK names survive export unchanged");
    }
    pipeline.Close();
    std::filesystem::remove_all(rootPath, ignored);
}

// 6.2: repeated generation replacement keeps every stale reader safe and the
// engine retains only the current mapping (intermediate names disappear once
// nobody holds them).
void TestRepeatedGenerationReplacementRetainsOnlyCurrentMapping() {
    ffengine::SnapshotPublisher publisher;
    Check(publisher.Start(GetCurrentProcessId()), "replacement publisher starts");
    if (publisher.SectionName().empty()) return;

    const ffprotocol::PooledSnapshotMap first{
        {L"C:\\", MakePooledDirectory(L"\u0391_first.txt", 1)}};
    const uint64_t firstGeneration = publisher.Publish(first);
    Check(firstGeneration != 0, "first pooled publication commits");

    // Stalled reader: holds generation one's mapping for the entire run.
    const std::wstring firstSectionName = publisher.CurrentSectionName();
    HANDLE stalledMapping = OpenFileMappingW(FILE_MAP_READ, FALSE, firstSectionName.c_str());
    Check(stalledMapping != nullptr, "stalled reader opens the first generation");
    const auto* stalledView = stalledMapping
        ? static_cast<const uint8_t*>(MapViewOfFile(stalledMapping, FILE_MAP_READ, 0, 0, 0))
        : nullptr;
    Check(stalledView != nullptr, "stalled reader maps the first generation");

    std::wstring earlyIntermediateName;
    for (uint64_t step = 0; step < 40; ++step) {
        const ffprotocol::PooledSnapshotMap next{
            {L"C:\\", MakePooledDirectory(L"replacement_" + std::to_wstring(step) + L".txt", step + 2)}};
        Check(publisher.Publish(next) != 0, "repeated pooled publication commits");
        if (step == 2) earlyIntermediateName = publisher.CurrentSectionName();
        if (step == 19) {
            // Mid-sequence owning publication must not matter to the
            // replacement/retention contract.
            ffprotocol::SnapshotDirectory owningDirectory;
            owningDirectory.status = ffprotocol::DirectoryEnumerationStatus::Success;
            owningDirectory.volumeRowId = 77;
            ffprotocol::SnapshotDirectoryEntry owningEntry;
            owningEntry.name = L"mid.txt";
            owningDirectory.entries.push_back(owningEntry);
            const std::map<std::wstring, ffprotocol::SnapshotDirectory> owning{
                {L"C:\\", owningDirectory}};
            Check(publisher.Publish(owning) != 0, "mid-sequence owning publication commits");
        }
    }

    if (stalledView != nullptr) {
        Check(MapLooksConsistent(stalledView, firstGeneration),
              "stalled reader's generation-one bytes stay valid across 40 replacements");
    }

    // The engine keeps only the current mapping: an intermediate generation
    // that nobody ever held must no longer resolve by name.
    if (!earlyIntermediateName.empty() && earlyIntermediateName != publisher.CurrentSectionName()) {
        HANDLE ghost = OpenFileMappingW(FILE_MAP_READ, FALSE, earlyIntermediateName.c_str());
        Check(ghost == nullptr,
              "unheld intermediate generations are released, not retained");
        if (ghost != nullptr) CloseHandle(ghost);
    }

    // Bounded teardown: Stop() must not wait for the stalled reader.
    const auto stopStart = Clock::now();
    publisher.Stop();
    const double stopMs = std::chrono::duration<double, std::milli>(Clock::now() - stopStart).count();
    Check(stopMs < 5000.0, "teardown completes without waiting for a stalled reader");

    // Independently-held client mapping contract: the stalled reader can
    // still finish reading after engine teardown.
    if (stalledView != nullptr) {
        Check(MapLooksConsistent(stalledView, firstGeneration),
              "stalled reader's mapping survives engine teardown (independent client mapping)");
        UnmapViewOfFile(stalledView);
    }
    if (stalledMapping != nullptr) CloseHandle(stalledMapping);
}

// 6.2: concurrent readers during replacement never observe a torn or
// inconsistent generation.
void TestConcurrentReadersDuringReplacement() {
    ffengine::SnapshotPublisher publisher;
    Check(publisher.Start(GetCurrentProcessId() + 7), "concurrent-reader publisher starts");
    if (publisher.SectionName().empty()) return;

    std::atomic<bool> running{true};
    std::atomic<unsigned> consistentReads{0};
    std::atomic<unsigned> inconsistentReads{0};
    std::vector<std::thread> readers;
    for (int reader = 0; reader < 2; ++reader) {
        readers.emplace_back([&publisher, &running, &consistentReads, &inconsistentReads] {
            while (running.load(std::memory_order_relaxed)) {
                const std::wstring sectionName = publisher.CurrentSectionName();
                HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, sectionName.c_str());
                if (mapping == nullptr) continue;  // name rolled between read and open
                const auto* view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
                if (view == nullptr) {
                    CloseHandle(mapping);
                    continue;
                }
                if (MapLooksConsistent(view, GenerationOfSectionName(sectionName))) {
                    consistentReads.fetch_add(1, std::memory_order_relaxed);
                } else {
                    inconsistentReads.fetch_add(1, std::memory_order_relaxed);
                }
                UnmapViewOfFile(view);
                CloseHandle(mapping);
            }
        });
    }

    for (uint64_t step = 0; step < 30; ++step) {
        const ffprotocol::PooledSnapshotMap next{
            {L"C:\\", MakePooledDirectory(L"live_" + std::to_wstring(step) + L".txt", step)}};
        Check(publisher.Publish(next) != 0, "concurrent-reader publication commits");
        if (step == 0) {
            // Bounded coordination: give the reader threads a window to
            // observe the first generation before the publisher races through
            // the rest. The progress assertion below must be deterministic,
            // not scheduling luck (the full-suite run exposed the race).
            const auto deadline = Clock::now() + std::chrono::seconds(2);
            while (consistentReads.load(std::memory_order_relaxed) == 0 && Clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
    running.store(false, std::memory_order_relaxed);
    for (auto& reader : readers) reader.join();
    publisher.Stop();

    Check(inconsistentReads.load() == 0,
          "no reader ever observed a torn or inconsistent generation");
    Check(consistentReads.load() > 0,
          "concurrent readers made progress during replacement");
}

// 6.2: pooled-directory copies rebuild name views against their own buffer,
// so copies used beside concurrent source readers are byte-identical and
// race-free on separate storage.
void TestPooledModelCopyMoveUnderConcurrentSerialization() {
    ffprotocol::PooledSnapshotMap source;
    for (size_t index = 0; index < 64; ++index) {
        source.emplace(L"C:\\dir_" + std::to_wstring(index),
                       MakePooledDirectory(L"name_\u00c4_" + std::to_wstring(index), index));
    }
    const std::vector<uint8_t> reference = ffprotocol::SerializeSnapshot(source);

    std::atomic<bool> running{true};
    std::atomic<bool> sourceBytesStable{true};
    std::atomic<unsigned> rounds{0};
    std::vector<std::thread> readers;
    for (int reader = 0; reader < 3; ++reader) {
        readers.emplace_back([&source, &reference, &running, &sourceBytesStable, &rounds] {
            while (running.load(std::memory_order_relaxed)) {
                const std::vector<uint8_t> bytes = ffprotocol::SerializeSnapshot(source);
                if (bytes != reference) {
                    sourceBytesStable.store(false, std::memory_order_relaxed);
                    break;
                }
                rounds.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::atomic<bool> copiesStable{true};
    for (int iteration = 0; iteration < 200; ++iteration) {
        ffprotocol::PooledSnapshotMap copy = source;
        const std::vector<uint8_t> copiedBytes = ffprotocol::SerializeSnapshot(copy);
        if (copiedBytes != reference) {
            copiesStable.store(false, std::memory_order_relaxed);
            break;
        }
        ffprotocol::PooledSnapshotMap moved = std::move(copy);
        const std::vector<uint8_t> movedBytes = ffprotocol::SerializeSnapshot(moved);
        if (movedBytes != reference) {
            copiesStable.store(false, std::memory_order_relaxed);
            break;
        }
    }
    running.store(false, std::memory_order_relaxed);
    for (auto& reader : readers) reader.join();
    Check(sourceBytesStable.load(), "concurrent source serialization stays byte-identical");
    Check(copiesStable.load(),
          "copied/moved pooled models serialize byte-identically beside live readers");
    Check(rounds.load() > 0, "concurrent source readers completed serialization rounds");
}

}  // namespace

int main() {
    TestPooledExportMatchesOwningExportExactly();
    TestRepeatedGenerationReplacementRetainsOnlyCurrentMapping();
    TestConcurrentReadersDuringReplacement();
    TestPooledModelCopyMoveUnderConcurrentSerialization();
    return FailureCount() == 0 ? 0 : 1;
}
