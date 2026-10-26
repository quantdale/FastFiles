#include <algorithm>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "IndexPipeline.h"
#include "sqlite3.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {
using namespace ffengine;
using namespace ffindexstore;

ffprotocol::MftRecordV1 MakeMftRecord(uint64_t id, uint64_t parent, std::u16string name, uint32_t attrs, uint64_t size) {
    ffprotocol::MftRecordV1 r{};
    r.fixed.fileReferenceNumber = id;
    r.fixed.parentFileReferenceNumber = parent;
    r.fixed.fileAttributes = attrs;
    r.fixed.sizeBytes = size;
    r.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
    r.fileName = std::move(name);
    return r;
}

bool HasSqlitePlannerStats(const std::string& dbPath) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        if (db != nullptr) sqlite3_close(db);
        return false;
    }
    sqlite3_stmt* statement = nullptr;
    const bool prepared = sqlite3_prepare_v2(
                              db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='sqlite_stat1';", -1,
                              &statement, nullptr) == SQLITE_OK;
    const bool found = prepared && sqlite3_step(statement) == SQLITE_ROW;
    if (statement != nullptr) sqlite3_finalize(statement);
    sqlite3_close(db);
    return found;
}

#ifdef FF_INDEX_PIPELINE_TESTING
using SecondaryBoundary = IndexPipeline::SecondaryPruningBoundary;

struct SecondaryFailurePlan {
    SecondaryBoundary target = SecondaryBoundary::MftRulePrune;
    size_t failuresRemaining = 0;
    bool persistent = false;
    size_t targetCalls = 0;
    std::vector<SecondaryBoundary> observedBoundaries;

    bool ShouldFail(const SecondaryBoundary boundary, VolumeRowId volumeId,
                    const std::vector<EntryChange>& changes) {
        (void)volumeId;
        observedBoundaries.push_back(boundary);
        if (boundary != target || changes.empty()) {
            return false;
        }
        ++targetCalls;
        if (persistent) {
            return true;
        }
        if (failuresRemaining == 0) {
            return false;
        }
        --failuresRemaining;
        return true;
    }
};

void InstallSecondaryFailurePlan(IndexPipeline& pipeline, SecondaryFailurePlan& plan) {
    pipeline.SetSecondaryPruningFaultInjector(
        [&plan](const SecondaryBoundary boundary, const VolumeRowId volumeId,
                const std::vector<EntryChange>& changes) {
            return plan.ShouldFail(boundary, volumeId, changes);
        });
}
#endif

ffprotocol::UsnDeltaV1 MakeUsnRecord(uint64_t usn, uint64_t id, uint64_t parent,
                                     std::u16string name, uint64_t size) {
    ffprotocol::UsnDeltaV1 record{};
    record.fixed.usn = usn;
    record.fixed.fileReferenceNumber = id;
    record.fixed.parentFileReferenceNumber = parent;
    record.fixed.timestamp = usn;
    record.fixed.sizeBytes = size;
    record.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
    record.fileName = std::move(name);
    return record;
}

bool SnapshotHasName(const std::map<std::wstring, ffprotocol::SnapshotDirectory>& snapshot,
                     const std::wstring& name) {
    for (const auto& [path, directory] : snapshot) {
        (void)path;
        for (const auto& entry : directory.entries) {
            if (entry.name == name) {
                return true;
            }
        }
    }
    return false;
}

struct DurableEntryState {
    bool opened = false;
    uint64_t entryCount = 0;
    bool hasTarget = false;
};

DurableEntryState ReadDurableEntryState(const std::string& dbPath, VolumeRowId volumeId,
                                        FileId target) {
    DurableEntryState result;
    Store store;
    result.opened = store.Open(dbPath);
    if (result.opened) {
        result.entryCount = store.CountEntries(volumeId);
        result.hasTarget = store.FindEntry(volumeId, target).has_value();
        store.Close();
    }
    return result;
}

ffprotocol::IndexedSearchResponse SearchForName(IndexPipeline& pipeline, uint64_t requestId,
                                                 const std::wstring& name) {
    ffprotocol::IndexedSearchRequest request;
    request.requestId = requestId;
    request.scope = 3; // AllIndexedLocations
    request.sortField = 1; // Filename
    request.maxResults = 100;
    request.query = name;
    return pipeline.SearchIndexed(request);
}

// analyze-maintenance-scheduling: sustained bulk ingestion must NOT run a
// full-database ANALYZE per batch (the historical 10k-threshold remainder
// bookkeeping ran ANALYZE on ~6 of every 7 batches with 8,192-row wire
// batches -- O(n^2) over a large ingest; the 5M collapse). Planner
// statistics are refreshed at maintenance cadence instead, gated by the
// same threshold (no stats before 10,000 records -- preserved).
void TestBulkIngestionDefersAnalyzeToMaintenance() {
    const std::string dbPath = FreshDbPath("engine_test_analyze_threshold.db");
    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "the pipeline opens for ANALYZE scheduling validation");
    VolumeKey key;
    key.serialNumber = 1;
    const auto volume = pipeline.ResolveVolume(key);

    const auto applyBatches = [](IndexPipeline& targetPipeline, ffindexstore::VolumeRowId targetVolume,
                                  uint64_t firstId, size_t batchCount, size_t recordsPerBatch,
                                  const std::u16string& name) {
        for (size_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
            std::vector<ffprotocol::MftRecordV1> batch;
            batch.reserve(recordsPerBatch);
            for (size_t recordIndex = 0; recordIndex < recordsPerBatch; ++recordIndex) {
                const uint64_t offset = static_cast<uint64_t>(batchIndex * recordsPerBatch + recordIndex);
                const uint64_t id = firstId;
                batch.push_back(MakeMftRecord(id, id, name, 0, offset));
            }
            if (!targetPipeline.ApplyMftBatch(targetVolume, batch)) {
                return false;
            }
        }
        return true;
    };

    // 4 batches x 5,000 rows = 20,000 records: the threshold is crossed
    // three times over, yet no ANALYZE may run inside the ingestion path.
    Check(applyBatches(pipeline, volume, 1'000, 4, 5'000, u"entry"),
          "20,000 records apply across four ingestion batches");
    pipeline.Close();
    Check(!HasSqlitePlannerStats(dbPath),
          "sustained bulk ingestion does not run per-batch ANALYZE (deferred to maintenance)");

    // Fresh ingest crossing the threshold, then the maintenance pass the
    // scheduler would run (same process -- the production cadence).
    Check(pipeline.Open(dbPath), "the pipeline reopens for the deferred refresh");
    const auto reopenedVolume = pipeline.ResolveVolume(key);
    Check(applyBatches(pipeline, reopenedVolume, 30'000, 4, 5'000, u"entry"),
          "a further 20,000 records apply across four ingestion batches");
    pipeline.RunStoreMaintenance();
    pipeline.Close();
    Check(HasSqlitePlannerStats(dbPath),
          "maintenance refreshes planner statistics once the threshold accumulated");

    // Threshold semantics preserved: below-threshold ingestion still does not
    // create planner statistics, even across maintenance.
    const std::string smallDbPath = FreshDbPath("engine_test_analyze_threshold_small.db");
    IndexPipeline smallPipeline;
    Check(smallPipeline.Open(smallDbPath), "the pipeline opens for the below-threshold case");
    const auto smallVolume = smallPipeline.ResolveVolume(key);
    Check(applyBatches(smallPipeline, smallVolume, 1'000, 1, 9'999, u"entry"),
          "9,999 records apply in one ingestion batch");
    smallPipeline.RunStoreMaintenance();
    smallPipeline.Close();
    Check(!HasSqlitePlannerStats(smallDbPath),
          "ANALYZE is not triggered before 10,000 inserted records");
}

void TestVolumeResolutionIsStable() {
    IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_volume.db")), "pipeline opens a fresh database");
    VolumeKey key;
    key.serialNumber = 1;
    auto first = pipeline.ResolveVolume(key);
    auto second = pipeline.ResolveVolume(key);
    Check(first != 0 && first == second, "the same VolumeKey resolves to the same durable VolumeRowId");
}

void TestApplyMftBatchAndExportSnapshot() {
    IndexPipeline pipeline;
    pipeline.Open(FreshDbPath("engine_test_export.db"));
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = pipeline.ResolveVolume(key);

    std::vector<ffprotocol::MftRecordV1> batch{
        MakeMftRecord(5, 5, u".", 0x10, 0),
        MakeMftRecord(100, 5, u"Users", 0x10, 0),
        MakeMftRecord(101, 100, u"notes.txt", 0, 42),
        MakeMftRecord(102, 100, u"Alice", 0x10, 0),
        MakeMftRecord(103, 102, u"nested.txt", 0, 7),
    };
    Check(pipeline.ApplyMftBatch(vol, batch), "a batch of MFT records applies successfully");

    auto snapshot = pipeline.ExportDirectorySnapshot(vol, L"C:");
    Check(snapshot.count(L"C:") == 1, "the volume root is exported under the caller-supplied drive-letter prefix");
    Check(snapshot[L"C:"].entries.size() == 1, "the root lists its one child directory");
    Check(snapshot.count(L"C:\\Users") == 1, "a subdirectory is exported at its full path");
    const auto& userEntries = snapshot[L"C:\\Users"].entries;
    const auto alice = std::find_if(userEntries.begin(), userEntries.end(),
                                    [](const auto& entry) { return entry.name == L"Alice"; });
    const auto notes = std::find_if(userEntries.begin(), userEntries.end(),
                                    [](const auto& entry) { return entry.name == L"notes.txt"; });
    Check(userEntries.size() == 2 && alice != userEntries.end() && notes != userEntries.end(),
          "direct children are listed under their parent directory");
    Check(notes != userEntries.end() && !notes->isDirectory, "a file entry is not marked as a directory");
    Check(snapshot.count(L"C:\\Users\\Alice") == 1 && snapshot[L"C:\\Users\\Alice"].entries.size() == 1 &&
              snapshot[L"C:\\Users\\Alice"].entries[0].name == L"nested.txt",
          "nested directories retain every ancestor component in the published snapshot key");
}

void TestRebuildAfterRestartMatchesPriorState() {
    const std::string dbPath = FreshDbPath("engine_test_restart.db");
    VolumeKey key;
    key.serialNumber = 1;
    VolumeRowId vol;
    {
        IndexPipeline pipeline;
        pipeline.Open(dbPath);
        vol = pipeline.ResolveVolume(key);
        pipeline.ApplyMftBatch(vol, {MakeMftRecord(5, 5, u"", 0x10, 0), MakeMftRecord(100, 5, u"a.txt", 0, 1)});
    }

    IndexPipeline restarted;
    restarted.Open(dbPath);
    int rebuiltVolumes = 0;
    restarted.RebuildAll([&](VolumeRowId) { ++rebuiltVolumes; });
    Check(rebuiltVolumes == 1, "RebuildAll invokes the callback once per known volume");

    auto snapshot = restarted.ExportDirectorySnapshot(vol, L"C:");
    Check(snapshot[L"C:"].entries.size() == 1 && snapshot[L"C:"].entries[0].name == L"a.txt",
          "state rebuilt from the durable store after a simulated restart matches what was persisted");
}

// allocated-size-storage-accuracy 5.5: V2 MFT scan records ingest both
// sizes; the exported snapshot carries them; a restart rebuild restores
// them from the durable store.
void TestMftV2BatchIngestsAllocatedSizeAndExportsToSnapshot() {
    const std::string dbPath = FreshDbPath("engine_test_mft_v2_allocated.db");
    VolumeKey key;
    key.serialNumber = 21;
    VolumeRowId vol = 0;
    {
        IndexPipeline pipeline;
        Check(pipeline.Open(dbPath), "the V2 scan fixture opens");
        vol = pipeline.ResolveVolume(key);

        std::vector<ffprotocol::MftRecordV2> batch;
        auto make = [&](uint64_t id, uint64_t parent, std::u16string name, uint64_t logical,
                        uint64_t allocated, bool directory) {
            ffprotocol::MftRecordV2 record{};
            record.fixed.fileReferenceNumber = id;
            record.fixed.parentFileReferenceNumber = parent;
            record.fixed.fileAttributes = directory ? 0x10 : 0;
            record.fixed.sizeBytes = logical;
            record.fixed.allocatedSizeBytes = allocated;
            record.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
            record.fileName = std::move(name);
            batch.push_back(std::move(record));
        };
        make(5, 5, u"", 0, 0, true);
        make(100, 5, u"Users", 0, 0, true);
        make(101, 100, u"sparse.vhdx", 40960, 8192, false); // sparse-style divergence
        make(102, 100, u"plain.bin", 4096, 4096, false);    // logical == allocated
        make(103, 100, u"empty.bin", 0, 0, false);          // genuine zeros
        Check(pipeline.ApplyMftBatch(vol, batch), "a V2 MFT batch applies");

        auto snapshot = pipeline.ExportDirectorySnapshot(vol, L"C:");
        const auto& entries = snapshot[L"C:\\Users"].entries;
        const auto find = [&](const std::wstring& name) {
            return std::find_if(entries.begin(), entries.end(),
                                [&](const auto& entry) { return entry.name == name; });
        };
        const auto sparse = find(L"sparse.vhdx");
        const auto plain = find(L"plain.bin");
        const auto empty = find(L"empty.bin");
        Check(sparse != entries.end() && sparse->allocatedSizeKnown && sparse->allocatedSizeBytes == 8192 &&
                  sparse->sizeBytes == 40960,
              "the snapshot carries the sparse file's allocated size beside its logical size");
        Check(plain != entries.end() && plain->allocatedSizeKnown && plain->allocatedSizeBytes == 4096,
              "logical == allocated round-trips as a known allocated value");
        Check(empty != entries.end() && empty->allocatedSizeKnown && empty->allocatedSizeBytes == 0,
              "a genuine zero-byte file is known-zero, not unknown");
    }

    // Restart: the durable store restores both values through RebuildAll.
    IndexPipeline restarted;
    Check(restarted.Open(dbPath), "the V2 scan fixture reopens");
    int rebuiltVolumes = 0;
    restarted.RebuildAll([&](VolumeRowId) { ++rebuiltVolumes; });
    Check(rebuiltVolumes == 1, "the rebuilt volume is reported once");
    auto snapshot = restarted.ExportDirectorySnapshot(vol, L"C:");
    const auto& entries = snapshot[L"C:\\Users"].entries;
    const auto sparse = std::find_if(entries.begin(), entries.end(),
                                     [](const auto& entry) { return entry.name == L"sparse.vhdx"; });
    Check(sparse != entries.end() && sparse->allocatedSizeKnown && sparse->allocatedSizeBytes == 8192,
          "allocated size survives a restart rebuild from SQLite");
}

// allocated-size-storage-accuracy 5.5: V3 USN deltas update allocated size
// only when the presence bit is set; an absent bit preserves the previously
// known value end-to-end (store + projection + snapshot).
void TestUsnV3PreservesAndCorrectsAllocatedSize() {
    const std::string dbPath = FreshDbPath("engine_test_usn_v3_allocated.db");
    VolumeKey key;
    key.serialNumber = 22;
    const FileId fileId{101, 0};
    VolumeRowId volume = 0;
    {
        IndexPipeline pipeline;
        Check(pipeline.Open(dbPath), "the V3 USN fixture opens");
        volume = pipeline.ResolveVolume(key);

        // Full scan first: authoritative allocated size 8192.
        std::vector<ffprotocol::MftRecordV2> scan;
        auto scanRecord = [&](uint64_t id, uint64_t parent, std::u16string name, uint64_t logical,
                              uint64_t allocated, bool directory) {
            ffprotocol::MftRecordV2 record{};
            record.fixed.fileReferenceNumber = id;
            record.fixed.parentFileReferenceNumber = parent;
            record.fixed.fileAttributes = directory ? 0x10 : 0;
            record.fixed.sizeBytes = logical;
            record.fixed.allocatedSizeBytes = allocated;
            record.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
            record.fileName = std::move(name);
            scan.push_back(std::move(record));
        };
        scanRecord(5, 5, u"", 0, 0, true);
        scanRecord(100, 5, u"Users", 0, 0, true);
        scanRecord(101, 100, u"sparse.vhdx", 40960, 8192, false);
        Check(pipeline.ApplyMftBatch(volume, scan), "the full V2 scan applies");

        // Re-enrichment failed: the V3 delta carries NO allocated-size bit.
        // The previously known value must survive, not be erased to zero.
        ffprotocol::UsnDeltaV3 delta{};
        delta.fixed.fileReferenceNumber = fileId.low;
        delta.fixed.parentFileReferenceNumber = 100;
        delta.fixed.sizeBytes = 50000; // logical size changed
        delta.fixed.fileAttributes = 0;
        delta.fixed.fileNameLengthChars = static_cast<uint16_t>(delta.fileName.size());
        delta.fileName = u"sparse.vhdx";
        delta.fixed.reason = 0x00000001; // USN_REASON_DATA_OVERWRITE
        delta.fixed.timestamp = 777;
        delta.fixed.fieldMask = ffprotocol::kUsnFieldParentId | ffprotocol::kUsnFieldName |
                                ffprotocol::kUsnFieldSizeBytes | ffprotocol::kUsnFieldLastModifiedTime |
                                ffprotocol::kUsnFieldAttributes;
        Check(pipeline.ApplyUsnBatch(volume, {delta}), "a V3 delta without the allocated-size bit applies");

        auto snapshot = pipeline.ExportDirectorySnapshot(volume, L"C:");
        const auto& entries = snapshot[L"C:\\Users"].entries;
        const auto sparse = std::find_if(entries.begin(), entries.end(),
                                         [](const auto& entry) { return entry.name == L"sparse.vhdx"; });
        Check(sparse != entries.end() && sparse->sizeBytes == 50000,
              "the present logical-size field updates");
        Check(sparse->allocatedSizeKnown && sparse->allocatedSizeBytes == 8192,
              "an absent allocated-size bit preserves the previously known value through the snapshot");

        // Successful re-enrichment: the bit IS set and corrects the value.
        ffprotocol::UsnDeltaV3 correction = delta;
        correction.fixed.allocatedSizeBytes = 12288;
        correction.fixed.fieldMask = delta.fixed.fieldMask | ffprotocol::kUsnFieldAllocatedSizeBytes;
        Check(pipeline.ApplyUsnBatch(volume, {correction}), "a V3 delta with the allocated-size bit applies");
        snapshot = pipeline.ExportDirectorySnapshot(volume, L"C:");
        const auto& corrected = snapshot[L"C:\\Users"].entries;
        const auto fixed = std::find_if(corrected.begin(), corrected.end(),
                                        [](const auto& entry) { return entry.name == L"sparse.vhdx"; });
        Check(fixed != corrected.end() && fixed->allocatedSizeKnown && fixed->allocatedSizeBytes == 12288,
              "a present allocated-size bit corrects the stored value");
    }

    // Restart: the corrected value persists.
    IndexPipeline restarted;
    Check(restarted.Open(dbPath), "the V3 USN fixture reopens");
    restarted.RebuildAll([](VolumeRowId) {});
    auto snapshot = restarted.ExportDirectorySnapshot(volume, L"C:");
    const auto& entries = snapshot[L"C:\\Users"].entries;
    const auto sparse = std::find_if(entries.begin(), entries.end(),
                                     [](const auto& entry) { return entry.name == L"sparse.vhdx"; });
    Check(sparse != entries.end() && sparse->allocatedSizeKnown && sparse->allocatedSizeBytes == 12288,
          "the corrected allocated size survives a restart");
}

void TestReconciliationRemovesEntryNotSeenInFullScan() {    IndexPipeline pipeline;
    pipeline.Open(FreshDbPath("engine_test_reconcile.db"));
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = pipeline.ResolveVolume(key);
    pipeline.ApplyMftBatch(vol, {
                                    MakeMftRecord(5, 5, u"", 0x10, 0),
                                    MakeMftRecord(100, 5, u"Users", 0x10, 0),
                                    MakeMftRecord(101, 100, u"notes.txt", 0, 42),
                                });

    pipeline.BeginReconciliationPass(vol);
    Check(pipeline.IsReconciliationPassActive(vol), "a reconciliation pass is marked active once begun");
    // A from-scratch scan that never re-observes FRN 101 -- as if the
    // file was deleted while the engine wasn't watching.
    pipeline.ApplyMftBatch(vol, {MakeMftRecord(5, 5, u"", 0x10, 0), MakeMftRecord(100, 5, u"Users", 0x10, 0)});
    pipeline.FinishReconciliationPass(vol);
    Check(!pipeline.IsReconciliationPassActive(vol), "the pass is no longer active once finished");

    auto snapshot = pipeline.ExportDirectorySnapshot(vol, L"C:");
    Check(snapshot[L"C:\\Users"].entries.empty(),
          "an entry never observed during a completed from-scratch scan is removed (missed-delete self-correction)");
}

void TestUsnDeleteReasonRemovesEntry() {
    IndexPipeline pipeline;
    pipeline.Open(FreshDbPath("engine_test_usn_delete.db"));
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = pipeline.ResolveVolume(key);
    pipeline.ApplyMftBatch(vol, {
                                    MakeMftRecord(5, 5, u"", 0x10, 0),
                                    MakeMftRecord(100, 5, u"Users", 0x10, 0),
                                    MakeMftRecord(101, 100, u"notes.txt", 0, 42),
                                });

    ffprotocol::UsnDeltaV1 deleteDelta{};
    deleteDelta.fixed.fileReferenceNumber = 101;
    deleteDelta.fixed.parentFileReferenceNumber = 100;
    deleteDelta.fixed.reason = 0x00000200; // USN_REASON_FILE_DELETE
    deleteDelta.fixed.fileNameLengthChars = 9;
    deleteDelta.fileName = u"notes.txt";
    Check(pipeline.ApplyUsnBatch(vol, {deleteDelta}), "a USN batch with a delete-flagged record applies successfully");

    auto snapshot = pipeline.ExportDirectorySnapshot(vol, L"C:");
    Check(snapshot[L"C:\\Users"].entries.empty(), "a USN_REASON_FILE_DELETE record removes the entry, not upserts it");
}

void TestUsnPartialUpdatesPreserveAuthoritativeMetadataAcrossRestart() {
    const std::string dbPath = FreshDbPath("engine_test_usn_partial_metadata.db");
    VolumeKey key;
    key.serialNumber = 12;
    const FileId fileId{101, 0};
    VolumeRowId volume = 0;
    {
        IndexPipeline pipeline;
        Check(pipeline.Open(dbPath), "the USN partial-update fixture opens");
        volume = pipeline.ResolveVolume(key);

        auto file = MakeMftRecord(101, 100, u"original.txt", 0, 1234);
        file.fixed.creationTime = 111;
        file.fixed.lastModifiedTime = 222;
        file.fixed.lastAccessTime = 333;
        Check(pipeline.ApplyMftBatch(volume, {
                                         MakeMftRecord(5, 5, u"", 0x10, 0),
                                         MakeMftRecord(100, 5, u"Users", 0x10, 0),
                                         file,
                                     }),
              "the authoritative MFT metadata is stored before incremental updates");

        const auto checkSnapshot = [&](const std::wstring& expectedName, uint64_t expectedModified,
                                       const char* description) {
            const auto snapshot = pipeline.ExportDirectorySnapshot(volume, L"C:");
            const auto directory = snapshot.find(L"C:\\Users");
            if (directory == snapshot.end()) {
                Check(false, description);
                return;
            }
            const auto entry = std::find_if(directory->second.entries.begin(), directory->second.entries.end(),
                                            [&](const auto& candidate) {
                                                return candidate.name == expectedName;
                                            });
            Check(entry != directory->second.entries.end() && entry->creationTime == 111 &&
                      entry->lastModifiedTime == expectedModified,
                  description);
        };

        ffprotocol::UsnDeltaV1 delta{};
        delta.fixed.fileReferenceNumber = fileId.low;
        delta.fixed.parentFileReferenceNumber = 100;
        delta.fixed.sizeBytes = 1234;
        delta.fixed.fileNameLengthChars = 11;
        delta.fileName = u"renamed.txt";
        delta.fixed.reason = 0x00002000; // USN_REASON_RENAME_NEW_NAME
        delta.fixed.timestamp = 444;
        Check(pipeline.ApplyUsnBatch(volume, {delta}), "a rename USN delta applies successfully");
        checkSnapshot(L"renamed.txt", 444, "a rename preserves creation time and updates last-modified time");

        delta.fixed.reason = 0x00000001; // USN_REASON_DATA_OVERWRITE
        delta.fixed.timestamp = 555;
        Check(pipeline.ApplyUsnBatch(volume, {delta}), "a write USN delta applies successfully");
        checkSnapshot(L"renamed.txt", 555, "a write preserves creation time and updates last-modified time");

        delta.fixed.reason = 0x00000800; // USN_REASON_BASIC_INFO_CHANGE
        delta.fixed.timestamp = 666;
        delta.fixed.fileAttributes = 0x20;
        Check(pipeline.ApplyUsnBatch(volume, {delta}), "an attribute USN delta applies successfully");
        checkSnapshot(L"renamed.txt", 666, "an attribute change preserves creation time and updates last-modified time");
    }

    Store reopened;
    Check(reopened.Open(dbPath), "the USN partial-update fixture reopens after restart");
    EntryRecord persisted;
    Check(reopened.ForEachEntry(volume, [&](const EntryRecord& entry) { persisted = entry; }),
          "the restarted store reads the USN-merged entry");
    Check(persisted.name == u"renamed.txt" && persisted.creationTime == 111 && persisted.lastModifiedTime == 666 &&
              persisted.lastAccessTime == 333 && persisted.sizeBytes == 1234,
          "creation/access/size metadata survives USN deltas and SQLite restart");
    reopened.Close();

    IndexPipeline reconciled;
    Check(reconciled.Open(dbPath), "the pipeline reopens for full-reconciliation compatibility");
    const auto reconciledVolume = reconciled.ResolveVolume(key);
    auto authoritative = MakeMftRecord(101, 100, u"reconciled.txt", 0, 1234);
    authoritative.fixed.creationTime = 777;
    authoritative.fixed.lastModifiedTime = 888;
    authoritative.fixed.lastAccessTime = 999;
    reconciled.BeginReconciliationPass(reconciledVolume);
    Check(reconciled.ApplyMftBatch(reconciledVolume, {
                                       MakeMftRecord(5, 5, u"", 0x10, 0),
                                       MakeMftRecord(100, 5, u"Users", 0x10, 0),
                                       authoritative,
                                   }),
          "a full reconciliation can replace the partial USN record with authoritative metadata");
    reconciled.FinishReconciliationPass(reconciledVolume);
    const auto fullSnapshot = reconciled.ExportDirectorySnapshot(reconciledVolume, L"C:");
    const auto fullDirectory = fullSnapshot.find(L"C:\\Users");
    Check(fullDirectory != fullSnapshot.end() && fullDirectory->second.entries.size() == 1 &&
              fullDirectory->second.entries[0].name == L"reconciled.txt" &&
              fullDirectory->second.entries[0].creationTime == 777 &&
              fullDirectory->second.entries[0].lastModifiedTime == 888,
          "full reconciliation remains compatible with the partial-update representation");

    ffprotocol::UsnDeltaV1 deleteDelta{};
    deleteDelta.fixed.fileReferenceNumber = fileId.low;
    deleteDelta.fixed.parentFileReferenceNumber = 100;
    deleteDelta.fixed.reason = 0x00000200; // USN_REASON_FILE_DELETE
    deleteDelta.fixed.fileNameLengthChars = 14;
    deleteDelta.fileName = u"reconciled.txt";
    Check(reconciled.ApplyUsnBatch(reconciledVolume, {deleteDelta}),
          "a delete USN delta applies after a full reconciliation");
    Check(reconciled.ExportDirectorySnapshot(reconciledVolume, L"C:")[L"C:\\Users"].entries.empty(),
          "delete semantics remove the entry instead of merging a zero-valued partial record");
}

void TestNewUsnOnlyEntryPersistsMftEnrichedMetadata() {
    const std::string dbPath = FreshDbPath("engine_test_usn_new_enriched.db");
    VolumeKey key;
    key.serialNumber = 77;
    VolumeRowId volume = 0;
    {
        IndexPipeline pipeline;
        Check(pipeline.Open(dbPath), "the new-USN enriched fixture opens");
        volume = pipeline.ResolveVolume(key);
        Check(pipeline.ApplyMftBatch(volume, {
                                         MakeMftRecord(5, 5, u"", 0x10, 0),
                                         MakeMftRecord(100, 5, u"Users", 0x10, 0),
                                     }),
              "the parent chain exists before the first USN observation");

        ffprotocol::UsnDeltaV2 enriched{};
        enriched.fixed.fileReferenceNumber = 201;
        enriched.fixed.parentFileReferenceNumber = 100;
        enriched.fixed.reason = 0x00000001;
        enriched.fixed.timestamp = 900;
        enriched.fixed.creationTime = 1111;
        enriched.fixed.lastModifiedTime = 2222;
        enriched.fixed.lastAccessTime = 3333;
        enriched.fixed.sizeBytes = 4444;
        enriched.fixed.fileAttributes = 0x20;
        enriched.fixed.fieldMask = ffprotocol::kUsnFieldMaskAll;
        enriched.fileName = u"created-after-scan.txt";
        enriched.fixed.fileNameLengthChars = static_cast<uint16_t>(enriched.fileName.size());
        Check(pipeline.ApplyUsnBatch(volume, {enriched}),
              "a brand-new file first observed through an enriched USN event applies");

        const auto snapshot = pipeline.ExportDirectorySnapshot(volume, L"C:");
        const auto directory = snapshot.find(L"C:\\Users");
        const ffprotocol::SnapshotDirectoryEntry* snapshotEntry = nullptr;
        if (directory != snapshot.end()) {
            const auto entry = std::find_if(directory->second.entries.begin(), directory->second.entries.end(),
                                            [](const auto& candidate) {
                                                return candidate.name == L"created-after-scan.txt";
                                            });
            if (entry != directory->second.entries.end()) snapshotEntry = &*entry;
        }
        Check(snapshotEntry != nullptr && snapshotEntry->sizeBytes == 4444 && snapshotEntry->attributes == 0x20 &&
                  snapshotEntry->creationTime == 1111 && snapshotEntry->lastModifiedTime == 2222,
              "the projection and published snapshot receive all enriched metadata for a new USN-only entry");
    }

    Store reopened;
    Check(reopened.Open(dbPath), "the enriched new-USN database reopens");
    EntryRecord persisted;
    Check(reopened.ForEachEntry(volume, [&](const EntryRecord& candidate) {
              if (candidate.id.low == 201) persisted = candidate;
          }),
          "the restarted store reads the enriched new-USN row");
    Check(persisted.name == u"created-after-scan.txt" && persisted.parentId.low == 100 &&
              persisted.sizeBytes == 4444 && persisted.creationTime == 1111 &&
              persisted.lastModifiedTime == 2222 && persisted.lastAccessTime == 3333 &&
              persisted.attributes == 0x20,
          "SQLite preserves every authoritative field obtained during MFT re-enrichment");
    reopened.Close();

    IndexPipeline reconciled;
    Check(reconciled.Open(dbPath), "the enriched new-USN database reopens for reconciliation");
    const auto reconciledVolume = reconciled.ResolveVolume(key);
    auto authoritative = MakeMftRecord(201, 100, u"reconciled-after-usn.txt", 0x40, 5555);
    authoritative.fixed.creationTime = 6666;
    authoritative.fixed.lastModifiedTime = 7777;
    authoritative.fixed.lastAccessTime = 8888;
    reconciled.BeginReconciliationPass(reconciledVolume);
    Check(reconciled.ApplyMftBatch(reconciledVolume, {
                                       MakeMftRecord(5, 5, u"", 0x10, 0),
                                       MakeMftRecord(100, 5, u"Users", 0x10, 0),
                                       authoritative,
                                   }),
          "a later full MFT reconciliation applies newer authoritative metadata");
    reconciled.FinishReconciliationPass(reconciledVolume);
    const auto afterReconcile = reconciled.ExportDirectorySnapshot(reconciledVolume, L"C:");
    const auto reconciledDirectory = afterReconcile.find(L"C:\\Users");
    Check(reconciledDirectory != afterReconcile.end() && reconciledDirectory->second.entries.size() == 1 &&
              reconciledDirectory->second.entries[0].name == L"reconciled-after-usn.txt" &&
              reconciledDirectory->second.entries[0].sizeBytes == 5555 &&
              reconciledDirectory->second.entries[0].creationTime == 6666 &&
              reconciledDirectory->second.entries[0].lastModifiedTime == 7777,
          "later reconciliation can replace the enriched USN row with current MFT truth");

    const std::string fallbackDb = FreshDbPath("engine_test_usn_new_fallback.db");
    IndexPipeline fallback;
    Check(fallback.Open(fallbackDb), "the fallback USN fixture opens");
    VolumeKey fallbackKey;
    fallbackKey.serialNumber = 88;
    const auto fallbackVolume = fallback.ResolveVolume(fallbackKey);
    Check(fallback.ApplyMftBatch(fallbackVolume, {
                                     MakeMftRecord(5, 5, u"", 0x10, 0),
                                     MakeMftRecord(100, 5, u"Users", 0x10, 0),
                                 }),
          "the fallback fixture parent chain exists");
    ffprotocol::UsnDeltaV2 fallbackDelta{};
    fallbackDelta.fixed.fileReferenceNumber = 202;
    fallbackDelta.fixed.parentFileReferenceNumber = 100;
    fallbackDelta.fixed.timestamp = 9999;
    fallbackDelta.fixed.lastModifiedTime = 9999;
    fallbackDelta.fixed.fileAttributes = 0x20;
    fallbackDelta.fixed.fieldMask = ffprotocol::kUsnFieldParentId | ffprotocol::kUsnFieldName |
                                    ffprotocol::kUsnFieldLastModifiedTime | ffprotocol::kUsnFieldAttributes;
    fallbackDelta.fileName = u"mft-read-failed.txt";
    fallbackDelta.fixed.fileNameLengthChars = static_cast<uint16_t>(fallbackDelta.fileName.size());
    Check(fallback.ApplyUsnBatch(fallbackVolume, {fallbackDelta}),
          "a fallback USN event stores only explicitly known fields");
    Store fallbackStore;
    Check(fallbackStore.Open(fallbackDb), "the fallback USN database reopens");
    EntryRecord fallbackRecord;
    Check(fallbackStore.ForEachEntry(fallbackVolume, [&](const EntryRecord& candidate) {
              if (candidate.id.low == 202) fallbackRecord = candidate;
          }),
          "the fallback USN row can be read after opening the database");
    Check(fallbackRecord.lastModifiedTime == 9999 && fallbackRecord.creationTime == 0 &&
              fallbackRecord.lastAccessTime == 0 && fallbackRecord.sizeBytes == 0,
          "an MFT re-read failure does not invent absent creation/access/size metadata");
    fallbackStore.Close();
}

void TestVolumeAvailabilityAndForget() {
    IndexPipeline pipeline;
    pipeline.Open(FreshDbPath("engine_test_lifecycle.db"));
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = pipeline.ResolveVolume(key);
    pipeline.ApplyMftBatch(vol, {MakeMftRecord(5, 5, u"", 0x10, 0)});

    Check(pipeline.SetVolumeAvailable(vol, false, 123), "SetVolumeAvailable succeeds");
    auto meta = pipeline.GetVolumeMetadata(vol);
    Check(meta.has_value() && !meta->available, "the volume is reflected as unavailable");
    Check(meta->entryCount == 1, "entries are retained while a volume is unavailable");

    Check(pipeline.ForgetVolume(vol), "an explicit ForgetVolume succeeds");
    Check(!pipeline.GetVolumeMetadata(vol).has_value(), "the volume is gone after an explicit forget");
}

void TestForgetVolumePurgesProjection() {
    IndexPipeline pipeline;
    pipeline.Open(FreshDbPath("engine_test_forget_projection.db"));
    VolumeKey keyA;
    keyA.serialNumber = 1;
    VolumeKey keyB;
    keyB.serialNumber = 2;
    auto volA = pipeline.ResolveVolume(keyA);
    auto volB = pipeline.ResolveVolume(keyB);
    pipeline.ApplyMftBatch(volA, {
                                     MakeMftRecord(5, 5, u"", 0x10, 0),
                                     MakeMftRecord(100, 5, u"Users", 0x10, 0),
                                     MakeMftRecord(101, 100, u"notes.txt", 0, 42),
                                 });
    pipeline.ApplyMftBatch(volB, {
                                     MakeMftRecord(5, 5, u"", 0x10, 0),
                                     MakeMftRecord(200, 5, u"Data", 0x10, 0),
                                     MakeMftRecord(201, 200, u"keep.txt", 0, 7),
                                 });

    Check(!pipeline.ForgetVolume(volB), "an available volume is not eligible for the explicit forget action");
    Check(pipeline.SetVolumeAvailable(volA, false, 123), "the target volume is marked unavailable before forgetting");
    Check(pipeline.ForgetVolume(volA), "an explicit ForgetVolume succeeds");

    auto snapshotA = pipeline.ExportDirectorySnapshot(volA, L"C:");
    Check(snapshotA.empty(), "a forgotten volume's entries are purged from the in-memory projection, not just the store");
    auto snapshotB = pipeline.ExportDirectorySnapshot(volB, L"D:");
    Check(snapshotB.count(L"D:") == 1 && snapshotB.count(L"D:\\Data") == 1,
          "another volume's directories are unaffected by the forget");
    Check(snapshotB[L"D:\\Data"].entries.size() == 1 && snapshotB[L"D:\\Data"].entries[0].name == L"keep.txt",
          "another volume's file entries are unaffected by the forget");
}

void TestIndexedSearchUsesProjectionPathsAndScopes() {
    IndexPipeline pipeline;
    pipeline.Open(FreshDbPath("engine_test_indexed_search.db"));
    VolumeKey key;
    key.serialNumber = 11;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {
                                      MakeMftRecord(5, 5, u".", 0x10, 0),
                                      MakeMftRecord(100, 5, u"Users", 0x10, 0),
                                      MakeMftRecord(101, 100, u"report.txt", 0, 42),
                                      MakeMftRecord(102, 100, u"photo.jpg", 0, 99),
                                      MakeMftRecord(103, 100, u"Alice", 0x10, 0),
                                      MakeMftRecord(104, 103, u"nested.txt", 0, 7),
                                      MakeMftRecord(105, 5, u"Archive", 0x10, 0),
                                      MakeMftRecord(106, 105, u"nested.txt", 0, 8),
                                  }),
          "indexed-search fixture is persisted through the normal ingestion path");

    ffprotocol::IndexedSearchRequest all;
    all.requestId = 1;
    all.scope = 3; // AllIndexedLocations
    all.sortField = 0; // Relevance
    all.maxResults = 100;
    all.query = L"report";
    IndexPipeline::SearchTimings reportTimings;
    auto result = pipeline.SearchIndexed(all, &reportTimings);
    Check(result.status == ffprotocol::IndexedSearchStatus::Completed && result.candidates.size() == 1 &&
              result.candidates[0].name == L"report.txt" && result.candidates[0].folder == L"C:\\Users" &&
              result.candidates[0].persistentIndexed,
          "indexed search returns a durable candidate with its reconstructed parent folder");
    Check(reportTimings.filenameIndexUsed && reportTimings.filenameIndexCandidateCount < 8,
          "eligible filename search uses a bounded projection-owned trigram candidate set");

    all.query = L"zzzz_not_present";
    IndexPipeline::SearchTimings noMatchTimings;
    result = pipeline.SearchIndexed(all, &noMatchTimings);
    Check(result.status == ffprotocol::IndexedSearchStatus::Completed && result.candidates.empty() &&
              noMatchTimings.filenameIndexUsed && noMatchTimings.filenameIndexCandidateCount == 0,
          "an indexed no-match query uses an empty trigram candidate set without changing the result");

    all.query = L"ph";
    IndexPipeline::SearchTimings shortTermTimings;
    result = pipeline.SearchIndexed(all, &shortTermTimings);
    Check(result.status == ffprotocol::IndexedSearchStatus::Completed && !result.candidates.empty() &&
              !shortTermTimings.filenameIndexUsed,
          "short literal terms retain the canonical full-scan fallback");

    all.requestId = 4;
    all.query = L"nested.txt";
    result = pipeline.SearchIndexed(all);
    const auto nestedAlice = std::find_if(result.candidates.begin(), result.candidates.end(),
                                          [](const auto& candidate) {
                                              return candidate.folder == L"C:\\Users\\Alice";
                                          });
    Check(result.status == ffprotocol::IndexedSearchStatus::Completed && result.candidates.size() == 2 &&
              nestedAlice != result.candidates.end(),
          "indexed search preserves every component of a nested reconstructed parent folder");

    all.scope = 3; // AllIndexedLocations
    all.scopePath.clear();
    all.sortField = 0; // Relevance
    all.maxResults = 100;
    all.query = L"FOLDER:ALICE";
    IndexPipeline::SearchTimings folderTimings;
    result = pipeline.SearchIndexed(all, &folderTimings);
    Check(result.status == ffprotocol::IndexedSearchStatus::Completed && result.candidates.size() == 1 &&
              result.candidates[0].name == L"nested.txt" && result.candidates[0].folder == L"C:\\Users\\Alice",
          "a folder-only query matches a parent path when the filename itself does not match");
    Check(!folderTimings.filenameIndexUsed, "folder predicates use the canonical fallback path");

    all.query = L"ext:TXT";
    result = pipeline.SearchIndexed(all);
    const size_t textCount = static_cast<size_t>(std::count_if(
        result.candidates.begin(), result.candidates.end(),
        [](const auto& candidate) { return candidate.name == L"report.txt" || candidate.name == L"nested.txt"; }));
    Check(result.status == ffprotocol::IndexedSearchStatus::Completed && result.candidates.size() == 3 &&
              textCount == 3,
          "extension filtering remains case-insensitive and returns duplicate names from distinct folders");

    all.query = L"REPORT";
    result = pipeline.SearchIndexed(all);
    Check(result.candidates.size() == 1 && result.candidates[0].name == L"report.txt",
          "filename matching remains case-insensitive");

    all.query = L"report TXT";
    result = pipeline.SearchIndexed(all);
    Check(result.candidates.size() == 1 && result.candidates[0].name == L"report.txt",
          "multiple query terms remain ANDed against the filename");

    all.query = L"nested.txt";
    all.sortField = 2; // Path
    result = pipeline.SearchIndexed(all);
    Check(result.candidates.size() == 2 && result.candidates[0].folder == L"C:\\Archive" &&
              result.candidates[1].folder == L"C:\\Users\\Alice",
          "path sorting remains deterministic after lazy folder reconstruction");

    std::vector<ffprotocol::MftRecordV1> cappedRecords;
    cappedRecords.reserve(4100);
    for (uint64_t index = 0; index < 4100; ++index) {
        cappedRecords.push_back(MakeMftRecord(1000 + index, 5, u"cap.txt", 0, index));
    }
    Check(pipeline.ApplyMftBatch(volume, cappedRecords), "the broad-search cap fixture applies successfully");
    all.query = L"cap";
    all.sortField = 1; // Filename
    all.maxResults = 5000;
    result = pipeline.SearchIndexed(all);
    Check(result.status == ffprotocol::IndexedSearchStatus::Completed && result.candidates.size() == 4096 &&
              result.truncated,
          "indexed search preserves the 4,096 result cap and truncation flag");

    all.requestId = 2;
    all.scope = 1; // CurrentFolderAndSubfolders
    all.scopePath = L"C:\\Users";
    all.query = L"*.jpg";
    result = pipeline.SearchIndexed(all);
    Check(result.status == ffprotocol::IndexedSearchStatus::Completed && result.candidates.size() == 1 &&
              result.candidates[0].name == L"photo.jpg",
          "indexed search applies current-folder scope and wildcard matching against the projection");

    all.requestId = 3;
    all.query = L"report";
    all.maxResults = 0;
    result = pipeline.SearchIndexed(all);
    Check(result.status == ffprotocol::IndexedSearchStatus::InvalidRequest,
          "indexed search rejects an unbounded zero-result request at the engine boundary");

    all.maxResults = 100;
    setting.enabled = false;
    pipeline.SetVolumeRules(volume, setting);
    result = pipeline.SearchIndexed(all);
    Check(result.status == ffprotocol::IndexedSearchStatus::Unavailable,
          "disabled volumes are not exposed through the indexed-search scope");
}

void TestIndexedSearchViewFirstCaptureReducesMaterialization() {
    IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_view_first.db")),
          "the pipeline opens for the view-first materialization-reduction test");
    VolumeKey key;
    key.serialNumber = 42;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    pipeline.SetVolumeRules(volume, setting);

    // 601 resident entries under the volume root: one root plus 590 "noise-NNN"
    // and 10 "photo-NN". The selective literal "ph" is two characters, too short
    // for the filename secondary index, so the hot path full-scans every row;
    // only the 10 "photo" entries match. This exercises the view-first rejection
    // where most examined rows must avoid an owned Candidate / name copy.
    std::vector<ffprotocol::MftRecordV1> batch;
    batch.reserve(601);
    batch.push_back(MakeMftRecord(5, 5, u".", 0x10, 0));
    for (uint64_t index = 0; index < 590; ++index) {
        std::wstring wname = L"noise-" + std::to_wstring(index);
        batch.push_back(MakeMftRecord(1000 + index, 5,
                                      std::u16string(wname.begin(), wname.end()), 0, index));
    }
    for (uint64_t index = 0; index < 10; ++index) {
        std::wstring wname = L"photo-" + std::to_wstring(index);
        batch.push_back(MakeMftRecord(2000 + index, 5,
                                      std::u16string(wname.begin(), wname.end()), 0, index));
    }
    Check(pipeline.ApplyMftBatch(volume, batch),
          "the view-first fixture is persisted through the normal ingestion path");

    struct SelectiveOutcome {
        ffprotocol::IndexedSearchResponse response;
        IndexPipeline::SearchTimings timings;
    };
    const auto runSelective = [&](const std::wstring& query) -> SelectiveOutcome {
        SelectiveOutcome outcome;
        ffprotocol::IndexedSearchRequest request;
        request.requestId = 1;
        request.scope = 3; // AllIndexedLocations
        request.sortField = 1; // Filename
        request.maxResults = 4096;
        request.query = query;
        outcome.timings.detailedDiagnostics = true;
        outcome.response = pipeline.SearchIndexed(request, &outcome.timings);
        return outcome;
    };

    const auto selective = runSelective(L"ph");
    Check(selective.response.status == ffprotocol::IndexedSearchStatus::Completed,
          "the selective view-first query completes");
    Check(selective.response.candidates.size() == 10, "only the 10 photo entries match 'ph'");
    Check(selective.timings.viewPathUsed, "the selective query used the view-first hot path");
    Check(selective.timings.hotLightweightRowsCaptured == 601,
          "every resident row (including the root) was captured as a lightweight row");
    Check(selective.timings.hotRowsRejectedBeforeMaterialization == 591,
          "the 590 noise rows and the root were rejected before owned materialization");
    Check(selective.timings.candidateNameCopies == 10,
          "only the 10 surviving rows incurred an owned name copy");
    Check(selective.timings.candidateNameCopies < selective.timings.hotLightweightRowsCaptured,
          "rejected rows avoided the per-row name copy (core campaign invariant)");
    Check(selective.timings.hotRowsRejectedBeforeMaterialization +
              selective.timings.candidateObjectsMaterialized ==
              selective.timings.hotLightweightRowsCaptured,
          "every captured lightweight row is accounted for as rejected or materialized");
    for (const auto& candidate : selective.response.candidates) {
        Check(candidate.name.rfind(L"photo-", 0) == 0,
              "every selective result is a photo entry (exact identity preserved)");
    }

    const auto noMatch = runSelective(L"qq");
    Check(noMatch.response.candidates.empty(), "an all-reject query returns no candidates");
    Check(noMatch.timings.hotRowsRejectedBeforeMaterialization == 601 &&
              noMatch.timings.candidateNameCopies == 0,
          "an all-reject query materializes zero owned names");

    const auto broad = runSelective(L"o");
    Check(broad.response.status == ffprotocol::IndexedSearchStatus::Completed &&
              broad.response.candidates.size() == 600,
          "the broad query matches every non-root row without false rejection");
    Check(broad.timings.candidateNameCopies == 600 &&
              broad.timings.hotRowsRejectedBeforeMaterialization == 1,
          "the broad query materializes every matching row and rejects only the root");
}

void TestIndexedSearchCancellationStopsProjectionWork() {
    IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_indexed_search_cancellation.db")),
          "the pipeline opens for indexed-search cancellation");
    VolumeKey key;
    key.serialNumber = 19;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    pipeline.SetVolumeRules(volume, setting);

    std::vector<ffprotocol::MftRecordV1> records;
    records.reserve(20'001);
    records.push_back(MakeMftRecord(5, 5, u".", 0x10, 0));
    for (uint64_t id = 1; id <= 20'000; ++id) {
        std::u16string name = u"entry-";
        for (const wchar_t digit : std::to_wstring(id)) {
            name.push_back(static_cast<char16_t>(digit));
        }
        records.push_back(MakeMftRecord(10'000 + id, 5,
                                        std::move(name), 0, id));
    }
    Check(pipeline.ApplyMftBatch(volume, records),
          "the cancellation fixture applies a large projection batch");

    ffprotocol::IndexedSearchRequest request;
    request.requestId = 91;
    request.scope = 3;
    request.sortField = 0;
    request.maxResults = 4096;
    request.query = L"entry";
    std::atomic<size_t> cancellationChecks{0};
    IndexPipeline::SearchTimings timings;
    const auto response = pipeline.SearchIndexed(request, &timings, [&cancellationChecks] {
        return cancellationChecks.fetch_add(1, std::memory_order_relaxed) >= 8;
    });
    Check(response.status == ffprotocol::IndexedSearchStatus::Cancelled,
          "an indexed search reports cooperative cancellation");
    Check(response.candidates.empty() && timings.cancelled,
          "a cancelled indexed search discards partial results");
    Check(timings.projectionRowsExamined < records.size(),
          "projection traversal stops before scanning the entire superseded request");

    ffprotocol::IndexedSearchRequest obsoleteRequest = request;
    obsoleteRequest.requestId = 92;
    ffprotocol::IndexedSearchResponse obsoleteResponse;
    std::atomic<bool> cancelObsolete{false};
    std::atomic<size_t> obsoleteChecks{0};
    // fastfiles-audit-remediation FF-AUDIT-06: make the test's supersession
    // assertion independent of optimization speed. The callback must remain
    // in-flight until the caller has published cancellation; otherwise a
    // fast optimized search can finish before the cancellation store.
    std::atomic<bool> releaseObsoleteCallback{false};
    std::thread obsoleteWorker([&] {
        obsoleteResponse = pipeline.SearchIndexed(obsoleteRequest, nullptr, [&] {
            obsoleteChecks.fetch_add(1, std::memory_order_relaxed);
            while (!releaseObsoleteCallback.load(std::memory_order_acquire)) Sleep(1);
            return cancelObsolete.load(std::memory_order_acquire);
        });
    });
    const auto waitStart = std::chrono::steady_clock::now();
    while (obsoleteChecks.load(std::memory_order_acquire) == 0
           && std::chrono::steady_clock::now() - waitStart < std::chrono::seconds(2)) {
        Sleep(1);
    }
    Check(obsoleteChecks.load(std::memory_order_acquire) != 0,
          "an obsolete large-index search reaches its cooperative cancellation checks");
    cancelObsolete.store(true, std::memory_order_release);
    releaseObsoleteCallback.store(true, std::memory_order_release);

    ffprotocol::IndexedSearchRequest finalRequest = request;
    finalRequest.requestId = 93;
    finalRequest.query = L"entry-19999";
    const auto finalStart = std::chrono::steady_clock::now();
    const auto finalResponse = pipeline.SearchIndexed(finalRequest);
    const auto finalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - finalStart);
    obsoleteWorker.join();
    std::printf("superseded indexed search final latency: %lld ms\n",
                static_cast<long long>(finalElapsed.count()));
    Check(obsoleteResponse.status == ffprotocol::IndexedSearchStatus::Cancelled,
          "the superseded indexed search exits as cancelled");
    Check(finalResponse.status == ffprotocol::IndexedSearchStatus::Completed
              && finalResponse.candidates.size() == 1,
          "the final indexed search completes after supersession");
    Check(finalElapsed < std::chrono::seconds(2),
          "final indexed-search latency is not dominated by obsolete work");
}

// indexed-search-allocated-size D1/D3/D5: projection-backed indexed Search
// carries allocated size + knownness for known, known-zero, and unknown sources,
// and the Size-on-Disk sort places unknown last in both directions.
void TestIndexedSearchPropagatesAllocatedSize() {
    const std::string dbPath = FreshDbPath("engine_test_indexed_allocated.db");
    VolumeKey key;
    key.serialNumber = 31;
    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "the indexed allocated-size fixture opens");
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    pipeline.SetVolumeRules(volume, setting);

    std::vector<ffprotocol::MftRecordV2> batch;
    auto v2 = [&](uint64_t id, uint64_t parent, std::u16string name, uint64_t logical,
                  uint64_t allocated, bool directory) {
        ffprotocol::MftRecordV2 record{};
        record.fixed.fileReferenceNumber = id;
        record.fixed.parentFileReferenceNumber = parent;
        record.fixed.fileAttributes = directory ? 0x10 : 0;
        record.fixed.sizeBytes = logical;
        record.fixed.allocatedSizeBytes = allocated;
        record.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
        record.fileName = std::move(name);
        batch.push_back(std::move(record));
    };
    v2(5, 5, u"", 0, 0, true);
    v2(100, 5, u"Users", 0, 0, true);
    v2(101, 100, u"known.txt", 100, 80, false);   // known allocated 80
    v2(102, 100, u"zero.txt", 0, 0, false);        // known zero
    Check(pipeline.ApplyMftBatch(volume, batch), "the known/zero allocated-size fixtures apply through the V2 scan path");
    // unknown.txt comes from a V1 record (no allocated-size field) -> unknown.
    std::vector<ffprotocol::MftRecordV1> unknownBatch;
    unknownBatch.push_back(MakeMftRecord(103, 100, u"unknown.txt", 50, 0));
    Check(pipeline.ApplyMftBatch(volume, unknownBatch), "the unknown allocated-size fixture applies through the V1 scan path");

    ffprotocol::IndexedSearchRequest request;
    request.requestId = 1;
    request.scope = 3; // AllIndexedLocations
    request.sortField = 6; // AllocatedSize
    request.descending = false;
    request.maxResults = 100;
    request.query = L"txt";
    const auto result = pipeline.SearchIndexed(request);
    Check(result.status == ffprotocol::IndexedSearchStatus::Completed && result.candidates.size() == 3,
          "indexed search over allocated-size fixtures completes with all three candidates");

    auto find = [&](const std::wstring& name) -> const ffprotocol::IndexedSearchCandidate* {
        for (const auto& c : result.candidates) {
            if (c.name == name) return &c;
        }
        return nullptr;
    };
    const auto* known = find(L"known.txt");
    const auto* zero = find(L"zero.txt");
    const auto* unknown = find(L"unknown.txt");
    Check(known != nullptr && known->allocatedSizeKnown && known->allocatedSizeBytes == 80,
          "indexed search propagates known allocated size to the response candidate");
    Check(zero != nullptr && zero->allocatedSizeKnown && zero->allocatedSizeBytes == 0,
          "indexed search propagates known-zero allocated size to the response candidate");
    Check(unknown != nullptr && !unknown->allocatedSizeKnown && unknown->allocatedSizeBytes == 0,
          "indexed search keeps a V1-source candidate allocated-unknown (never fabricated from logical size)");
    // AllocatedSize ascending: known-zero, known, then unknown last.
    Check(result.candidates[0].name == L"zero.txt" && result.candidates[1].name == L"known.txt" &&
              result.candidates[2].name == L"unknown.txt",
          "AllocatedSize ascending places known rows first and the unknown row last");

    request.sortField = 6;
    request.descending = true;
    const auto desc = pipeline.SearchIndexed(request);
    Check(desc.candidates[2].name == L"unknown.txt" && !desc.candidates[2].allocatedSizeKnown,
          "AllocatedSize descending still places the unknown row last");
}

// allocated-size-search-filter D1-D6: ondisk: indexed Search filtering uses
// projection-backed allocated metadata directly, with owning/view equivalence,
// metadata-only evaluation, and unknown never matching.
void TestIndexedSearchOndiskFilter() {
    const std::string dbPath = FreshDbPath("engine_test_ondisk_filter.db");
    VolumeKey key;
    key.serialNumber = 32;
    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "ondisk filter fixture opens");
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    pipeline.SetVolumeRules(volume, setting);

    std::vector<ffprotocol::MftRecordV2> batch;
    auto v2 = [&](uint64_t id, uint64_t parent, std::u16string name, uint64_t logical,
                  uint64_t allocated, bool directory) {
        ffprotocol::MftRecordV2 record{};
        record.fixed.fileReferenceNumber = id;
        record.fixed.parentFileReferenceNumber = parent;
        record.fixed.fileAttributes = directory ? 0x10 : 0;
        record.fixed.sizeBytes = logical;
        record.fixed.allocatedSizeBytes = allocated;
        record.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
        record.fileName = std::move(name);
        batch.push_back(std::move(record));
    };
    v2(5, 5, u"", 0, 0, true);
    v2(100, 5, u"Users", 0, 0, true);
    // Deliberately different logical vs allocated so size: and ondisk: diverge
    v2(101, 100, u"known_large.bin", 1 * 1024 * 1024, 8 * 1024 * 1024, false); // logical small, allocated large
    v2(102, 100, u"known_zero.bin", 512 * 1024, 0, false);                    // known zero
    v2(103, 100, u"known_small.bin", 10 * 1024 * 1024, 1 * 1024 * 1024, false); // logical large, allocated small
    Check(pipeline.ApplyMftBatch(volume, batch), "ondisk filter fixtures apply");
    std::vector<ffprotocol::MftRecordV1> unknownBatch;
    unknownBatch.push_back(MakeMftRecord(104, 100, u"unknown_stray.bin", 0, 9 * 1024 * 1024));
    Check(pipeline.ApplyMftBatch(volume, unknownBatch), "unknown stray fixture applies");

    auto search = [&](const std::wstring& query, IndexPipeline::SearchTimings* timings = nullptr) {
        ffprotocol::IndexedSearchRequest req;
        req.requestId = 1;
        req.scope = 3;
        req.sortField = 0;
        req.maxResults = 100;
        req.query = query;
        return pipeline.SearchIndexed(req, timings);
    };

    // View-compatible ondisk queries: no eager path materialization for metadata-only filters
    {
        IndexPipeline::SearchTimings timings;
        timings.detailedDiagnostics = true;
        auto result = search(L"ondisk:>1MB", &timings);
        Check(result.status == ffprotocol::IndexedSearchStatus::Completed,
              "ondisk:>1MB indexed search completes");
        // AllIndexedLocations + ondisk: (metadata-only) should not need path lookups beyond materialization
        // The pathNeededDecisions/pathLookups diagnostic counts should be comparable to size:>1MB
        IndexPipeline::SearchTimings sizeTimings;
        sizeTimings.detailedDiagnostics = true;
        auto sizeResult = search(L"size:>1MB", &sizeTimings);
        Check(sizeResult.status == ffprotocol::IndexedSearchStatus::Completed, "size:>1MB indexed search completes");
        // Both are metadata-only: candidate counts should be comparable
        Check(timings.candidateCount == sizeTimings.candidateCount,
              "ondisk: and size: traverse the same candidate count");
    }

    // Exact known nonzero
    {
        auto result = search(L"ondisk:8MB");
        Check(result.candidates.size() == 1 && result.candidates[0].name == L"known_large.bin",
              "ondisk:8MB matches only known_large");
    }
    // Comparison operators (anchored where directory zero rows would otherwise inflate counts)
    {
        auto result = search(L"ondisk:>7MB");
        Check(result.candidates.size() == 1 && result.candidates[0].name == L"known_large.bin",
              "ondisk:>7MB matches known_large only");
        result = search(L"ondisk:>=8MB");
        Check(result.candidates.size() == 1, "ondisk:>=8MB matches exact");
        result = search(L"ondisk:<2MB known");
        Check(result.candidates.size() == 2, "ondisk:<2MB with file anchor matches known_zero + known_small");
        result = search(L"known_zero ondisk:<=0B");
        Check(result.candidates.size() == 1 && result.candidates[0].name == L"known_zero.bin",
              "ondisk:<=0B matches known zero only");
    }
    // Known zero: directory entries (C:\, Users) also have allocated known-zero;
    // anchor the zero query to file candidates so the structural dirs do not
    // inflate the count. Directories correctly matching ondisk:0B is the
    // intended predicate semantics (known zero is zero regardless of kind).
    {
        auto result = search(L"known_zero ondisk:0B");
        Check(result.candidates.size() == 1 && result.candidates[0].name == L"known_zero.bin",
              "ondisk:0B matches known zero file (anchored)");
        result = search(L"ondisk:0B..1KB known_zero");
        Check(result.candidates.size() == 1, "ondisk:0B..1KB contains zero file");
        // Unanchored ondisk:0B also matches the structural directory rows whose
        // allocated size is legitimately known-zero (C:\ root + Users dir).
        auto unanchored = search(L"ondisk:0B");
        Check(unanchored.candidates.size() == 3, "unanchored ondisk:0B also matches directory known-zero rows");
    }
    // UNKNOWN: no numeric predicate matches unknown, even >=0B
    {
        auto result = search(L"ondisk:>=0B");
        // Should match all known rows (3 file + 2 directory known-zero dirs) — not unknown_stray
        for (const auto& c : result.candidates) {
            Check(c.name != L"unknown_stray.bin", "ondisk:>=0B does not fabricate unknown stray");
        }
        // known_large + known_zero + known_small + 2 directory rows (C:\, Users)
        Check(result.candidates.size() == 5, "ondisk:>=0B matches exactly the known rows (including directory known-zero)");
        result = search(L"known_zero ondisk:0B");
        Check(result.candidates.size() == 1, "ondisk:0B does not match unknown rows");
    }
    // size: vs ondisk: divergence (logical vs allocated)
    {
        auto sizeResult = search(L"size:>5MB");
        auto ondiskResult = search(L"ondisk:>5MB");
        // size:>5MB matches known_small (logical 10MB); ondisk:>5MB matches known_large (allocated 8MB)
        const bool sizeHasKnownSmall = std::any_of(sizeResult.candidates.begin(), sizeResult.candidates.end(),
                                                    [](const auto& c) { return c.name == L"known_small.bin"; });
        const bool sizeHasKnownLarge = std::any_of(sizeResult.candidates.begin(), sizeResult.candidates.end(),
                                                    [](const auto& c) { return c.name == L"known_large.bin"; });
        const bool ondiskHasKnownSmall = std::any_of(ondiskResult.candidates.begin(), ondiskResult.candidates.end(),
                                                      [](const auto& c) { return c.name == L"known_small.bin"; });
        const bool ondiskHasKnownLarge = std::any_of(ondiskResult.candidates.begin(), ondiskResult.candidates.end(),
                                                      [](const auto& c) { return c.name == L"known_large.bin"; });
        Check(sizeHasKnownSmall && !sizeHasKnownLarge, "size:>5MB selects logical-large allocated-small");
        Check(ondiskHasKnownLarge && !ondiskHasKnownSmall, "ondisk:>5MB selects allocated-large logical-small");
    }
    // Implicit AND via indexed search: scope + text term + ondisk
    {
        ffprotocol::IndexedSearchRequest req;
        req.requestId = 1;
        req.scope = 3;
        req.sortField = 0;
        req.maxResults = 100;
        req.query = L"known_large ondisk:>1MB";
        auto result = pipeline.SearchIndexed(req);
        Check(result.candidates.size() == 1 && result.candidates[0].name == L"known_large.bin",
              "text term + ondisk implicit AND via indexed search");
    }
    // Cancellation still chunk-bounded
    {
        ffprotocol::IndexedSearchRequest req;
        req.requestId = 1;
        req.scope = 3;
        req.sortField = 0;
        req.maxResults = 100;
        req.query = L"ondisk:>1MB";
        int checks = 0;
        auto result = pipeline.SearchIndexed(req, nullptr, [&checks] { return ++checks >= 2; });
        Check(result.status == ffprotocol::IndexedSearchStatus::Cancelled,
              "ondisk indexed search cancellation is still chunk-bounded");
    }
}

} // namespace

// memory-bounded-indexed-scale 3/4: forced-eviction search equivalence.
// A deliberately tiny projection budget forces the cold volume's subtrees to
// evict while the small hot volume stays resident. Every supported query
// class must return byte-identical result sets (identity, order, truncation)
// whether rows came from the projection or streamed from the durable store.
namespace {
std::u16string U16FromWide(const std::wstring& text) {
    std::u16string out;
    out.reserve(text.size());
    for (const wchar_t ch : text) out.push_back(static_cast<char16_t>(ch));
    return out;
}
} // namespace

void TestRootChildFoldersRemainAbsoluteAcrossResidency() {
    // durable-cold-search-index-and-hybrid-residency 4.1: a bare C: is
    // drive-relative; root children must retain the separator when evicted.
    IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_root_child_folders.db")),
          "root-child path fixture opens");
    VolumeKey key;
    key.serialNumber = 9401;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeMftRecord(5, 5, u".", 0x10, 0),
        MakeMftRecord(10, 5, u"boundary-file.txt", 0, 123),
        MakeMftRecord(11, 5, u"boundary-dir", 0x10, 0),
        MakeMftRecord(12, 11, u"boundary-nested.txt", 0, 456),
    }), "root-child path fixture persists");
    const auto signature = [](const ffprotocol::IndexedSearchResponse& response) {
        std::vector<std::tuple<int64_t, uint64_t, uint64_t, std::wstring, std::wstring,
                              uint64_t, uint64_t, uint64_t, bool, bool, uint64_t, bool>> result;
        for (const auto& item : response.candidates) {
            result.emplace_back(item.volumeId, item.id, item.parentId, item.name, item.folder,
                                item.sizeBytes, item.createdTime, item.modifiedTime,
                                item.isDirectory, item.persistentIndexed,
                                item.allocatedSizeBytes, item.allocatedSizeKnown);
        }
        return result;
    };
    const auto checkFolders = [](const ffprotocol::IndexedSearchResponse& response) {
        for (const auto& item : response.candidates) {
            if (item.id == 5) {
                Check(item.folder.empty() && item.name == L"C:\\",
                      "volume-root candidate retains its intentional display name and empty folder");
            } else {
                const auto expected = item.id == 12 ? L"C:\\boundary-dir" : L"C:\\";
                Check(item.folder == expected, "root and nested child folders are canonical and absolute");
                Check((std::filesystem::path(item.folder) / item.name).is_absolute(),
                      "joining a search result produces an absolute Windows path");
            }
        }
    };
    for (const auto* query : {L"boundary", L"kind:folder"}) {
        pipeline.RebuildAll({});
        Check(pipeline.MarkScanComplete(volume), "root-child fixture is search-ready after rebuild");
        ffprotocol::IndexedSearchRequest request;
        request.requestId = 9401;
        request.scope = 3;
        request.maxResults = 4096;
        request.query = query;
        IndexPipeline::SearchTimings hotTimings;
        const auto hot = pipeline.SearchIndexed(request, &hotTimings);
        Check(!pipeline.IsVolumeEvicted(volume) && hotTimings.hotRowsExamined != 0 &&
                  hotTimings.coldRowsExamined == 0,
              "root-child hot reference actually reads the resident projection");
        pipeline.UnloadVolumeFromProjection(volume);
        Check(pipeline.IsVolumeEvicted(volume), "root-child cold routes are explicitly evicted");
        IndexPipeline::SearchTimings coldTimings;
        const auto cold = pipeline.SearchIndexed(request, &coldTimings);
        IndexPipeline::SearchTestHooks hooks;
        hooks.forceCanonicalColdScan = true;
        pipeline.SetSearchTestHooks(hooks);
        IndexPipeline::SearchTimings canonicalTimings;
        const auto canonical = pipeline.SearchIndexed(request, &canonicalTimings);
        pipeline.SetSearchTestHooks({});
        Check(hot.status == ffprotocol::IndexedSearchStatus::Completed &&
                  cold.status == hot.status && canonical.status == hot.status,
              "root-child searches complete on all residency routes");
        Check(hot.candidates.size() == (request.query == L"boundary" ? 3u : 2u),
              "root-child and root candidate checks are nonvacuous");
        Check(signature(hot) == signature(cold) && signature(hot) == signature(canonical) &&
                  hot.truncated == cold.truncated && hot.truncated == canonical.truncated,
              "resident, indexed cold and canonical cold results match in order and every field");
        Check(coldTimings.coldRowsExamined != 0 && canonicalTimings.coldRowsExamined == 4 &&
                  coldTimings.hotRowsExamined == 0 && canonicalTimings.hotRowsExamined == 0,
              "cold routes actually enumerate durable records");
        checkFolders(hot);
        checkFolders(cold);
        checkFolders(canonical);
    }
}

void TestForcedEvictionSearchEquivalence() {
    IndexPipeline pipeline;
    const std::string dbPath = FreshDbPath("engine_test_eviction_equivalence.db");
    Check(pipeline.Open(dbPath), "the forced-eviction fixture opens");

    VolumeKey coldKey;
    coldKey.serialNumber = 41;
    const auto coldVolume = pipeline.ResolveVolume(coldKey);
    ffprotocol::VolumeSetting coldSetting;
    coldSetting.key = L"D:";
    pipeline.SetVolumeRules(coldVolume, coldSetting);
    // Ingest the cold volume FIRST so its LRU timestamps are the oldest;
    // shrinking the budget then deterministically evicts its subtrees.
    std::u16string emojiName = u"emoji-";
    emojiName.push_back(static_cast<char16_t>(0xD83D)); // U+1F600 pair
    emojiName.push_back(static_cast<char16_t>(0xDE00));
    emojiName += u"tailfrag.txt";
    Check(pipeline.ApplyMftBatch(coldVolume, {
        MakeMftRecord(1, 1, u".", 0x10, 0),
        MakeMftRecord(10, 1, u"ColdRoot", 0x10, 0),
        MakeMftRecord(11, 10, u"SubA", 0x10, 0),
        MakeMftRecord(100, 11, u"alpha-report.txt", 0, 120),
        MakeMftRecord(101, 11, u"photo.jpg", 0, 30),
        MakeMftRecord(102, 11, u"\u0444\u0430\u0439\u043b-\u03a9.txt", 0, 55),
        MakeMftRecord(103, 11, u"dup.txt", 0, 9),
        MakeMftRecord(12, 10, u"SubB", 0x10, 0),
        MakeMftRecord(110, 12, u"dup.txt", 0, 77),
        MakeMftRecord(111, 12, u"beta.txt", 0, 500),
        MakeMftRecord(120, 10, u"top.jpg", 0, 64),
        // large-index-lifecycle-and-search-scalability review-remediation 18.x:
        // cold-prefilter adversarial corpus -- LIKE metacharacters in names,
        // non-ASCII and surrogate names with ASCII tails, and the long-s
        // host-fold case (soaked by the hot/cold signature comparison).
        MakeMftRecord(121, 11, u"100%_done.txt", 0, 5),
        MakeMftRecord(122, 11, u"a_b.txt", 0, 6),
        MakeMftRecord(123, 11, u"axb.txt", 0, 7),
        MakeMftRecord(124, 11, u"\u017Foo.txt", 0, 8),
        MakeMftRecord(125, 11, emojiName, 0, 9),
        // Differential-matrix cells that had no named coverage: NFC vs NFD
        // forms of the same visible name (ordinal compare does NOT normalize,
        // so these are distinct rows) with an ASCII tail.
        MakeMftRecord(126, 11, u"o\u0308verlay.txt", 0, 10),
        MakeMftRecord(127, 11, u"\u00F6verlay.txt", 0, 11),
    }), "the cold volume fixture persists through normal ingestion");
    {
        // Repeated-prefix posting pressure: 20 rows sharing one long prefix.
        std::vector<ffprotocol::MftRecordV1> repeated;
        for (uint64_t rep = 1; rep <= 20; ++rep) {
            repeated.push_back(MakeMftRecord(200 + rep, 12,
                std::u16string(u"repeat-common-") + U16FromWide(std::to_wstring(rep)) + u".txt", 0, 1));
        }
        Check(pipeline.ApplyMftBatch(coldVolume, repeated),
              "the repeated-prefix rows persist through normal ingestion");
    }
    const auto diagnosticsColdOnly = pipeline.GetProjectionMemoryDiagnostics();
    (void)diagnosticsColdOnly;

    VolumeKey hotKey;
    hotKey.serialNumber = 42;
    const auto hotVolume = pipeline.ResolveVolume(hotKey);
    ffprotocol::VolumeSetting hotSetting;
    hotSetting.key = L"E:";
    pipeline.SetVolumeRules(hotVolume, hotSetting);
    Check(pipeline.ApplyMftBatch(hotVolume, {
        MakeMftRecord(1, 1, u".", 0x10, 0),
        MakeMftRecord(20, 1, u"HotRoot", 0x10, 0),
        MakeMftRecord(200, 20, u"hot-alpha.txt", 0, 8),
        MakeMftRecord(201, 20, u"gamma.jpg", 0, 15),
    }), "the hot volume fixture persists through normal ingestion");
    auto makeQuery = [&](const std::wstring& queryText, int scope = 3,
                         const std::wstring& scopePath = L"", int sortField = 0,
                         size_t maxResults = 4096) {
        ffprotocol::IndexedSearchRequest request;
        request.requestId = 700;
        request.scope = static_cast<uint8_t>(scope);
        request.scopePath = scopePath;
        request.sortField = static_cast<uint8_t>(sortField);
        request.maxResults = static_cast<uint32_t>(maxResults);
        request.query = queryText;
        return request;
    };
    struct QueryCase {
        const char* label;
        ffprotocol::IndexedSearchRequest request;
        bool trigramExpectedHot;
    };
    std::vector<QueryCase> cases = {
        {"selective literal", makeQuery(L"alpha"), true},
        {"no-match", makeQuery(L"zzznomatch"), false},
        {"trigram-eligible prefix", makeQuery(L"alph"), true},
        {"short canonical fallback", makeQuery(L"al"), false},
        {"glob filter", makeQuery(L"*.jpg"), false},
        {"extension predicate", makeQuery(L"ext:txt"), false},
        {"size predicate", makeQuery(L"size:>60"), false},
        {"folder predicate", makeQuery(L"FOLDER:SUBA"), false},
        {"unicode filename", makeQuery(L"\u0444\u0430\u0439\u043b"), false},
        {"duplicate basenames filename sort", makeQuery(L"dup.txt", 3, L"", 1), false},
        {"duplicate basenames path sort", makeQuery(L"dup.txt", 3, L"", 2), false},
        {"path-scoped subtree", makeQuery(L"report", 1, L"D:\\ColdRoot\\SubA"), false},
        {"broad capped", makeQuery(L"txt", 3, L"", 0, 3), false},
        // large-index-lifecycle-and-search-scalability review-remediation 18.x:
        // cold prefilter adversarial needles (LIKE metacharacters, ASCII case
        // variants, ASCII tails of non-ASCII names, long-s host-fold case).
        {"percent needle", makeQuery(L"100%"), false},
        {"underscore needle", makeQuery(L"a_b"), false},
        {"emoji tail needle", makeQuery(L"tailfrag"), false},
        {"long-s host-fold needle", makeQuery(L"soo"), false},
        {"upper-case selective", makeQuery(L"ALPHA"), true},
        // Differential-matrix cells that had no named coverage.
        {"empty query rejection parity", makeQuery(L""), false},
        {"repeated prefix needle", makeQuery(L"repeat-common"), false},
        {"NFD name ascii tail", makeQuery(L"verlay"), false},
        {"NFC name needle overlay", makeQuery(L"overlay"), false},
    };

    // Reference pass: everything fully resident.
    std::vector<ffprotocol::IndexedSearchResponse> reference;
    reference.reserve(cases.size());
    for (auto& testCase : cases) {
        IndexPipeline::SearchTimings timings;
        auto response = pipeline.SearchIndexed(testCase.request, &timings);
        if (testCase.trigramExpectedHot) {
            Check(timings.filenameIndexUsed,
                  "fully-resident eligible searches use the trigram accelerator");
        }
        // An empty query string is rejected by contract (InvalidRequest);
        // every other case must complete. Either way the same verdict must
        // hold after eviction (the equivalence loop re-checks).
        const bool expectRejected = testCase.request.query.empty();
        Check(response.status == (expectRejected
                                      ? ffprotocol::IndexedSearchStatus::InvalidRequest
                                      : ffprotocol::IndexedSearchStatus::Completed),
              "reference-pass indexed searches complete or reject consistently");
        reference.push_back(std::move(response));
    }
    Check(reference[2].candidates.size() >= reference[0].candidates.size(),
          "the reference prefix query is at least as broad as the selective literal");
    Check(!reference[12].candidates.empty() && reference[12].truncated,
          "the broad capped reference query reports honest truncation");
    Check(reference[9].candidates.size() == 2 && reference[9].candidates[0].folder != reference[9].candidates[1].folder,
          "duplicate-basename filename sorting orders deterministically across folders");

    // Force deterministic eviction of exactly the cold volume: an explicit
    // policy unload drops its resident rows while data stays durable.
    pipeline.UnloadVolumeFromProjection(coldVolume);
    Check(pipeline.IsVolumeEvicted(coldVolume), "unloading marks the large volume evicted");
    Check(!pipeline.IsVolumeEvicted(hotVolume), "the small volume stays projection-resident");
    const auto afterEviction = pipeline.GetProjectionMemoryDiagnostics();
    Check(afterEviction.subtreeEvictionCount > 0, "unload performs subtree evictions");
    Check(afterEviction.coldReloadCount == 0, "no search-driven reloads occurred during setup");
    Check(afterEviction.lastEvictionBytesBefore >= afterEviction.lastEvictionBytesAfter,
          "eviction diagnostics record the reclaimed bytes");

    auto signatures = [](const ffprotocol::IndexedSearchResponse& response) {
        std::vector<std::tuple<std::wstring, std::wstring, uint64_t, uint64_t, uint64_t, bool, bool, int64_t, uint64_t, uint64_t>> out;
        out.reserve(response.candidates.size());
        for (const auto& candidate : response.candidates) {
            out.emplace_back(candidate.name, candidate.folder, candidate.sizeBytes,
                             candidate.allocatedSizeBytes, candidate.modifiedTime,
                             candidate.isDirectory, candidate.allocatedSizeKnown,
                             candidate.volumeId, candidate.id, candidate.parentId);
        }
        return out;
    };

    // Equivalence pass: identical identities AND ordering from mixed sources.
    bool sawColdAttribution = false;
    for (size_t index = 0; index < cases.size(); ++index) {
        IndexPipeline::SearchTimings timings;
        auto response = pipeline.SearchIndexed(cases[index].request, &timings);
        const bool expectRejected = cases[index].request.query.empty();
        Check(response.status == (expectRejected
                                      ? ffprotocol::IndexedSearchStatus::InvalidRequest
                                      : ffprotocol::IndexedSearchStatus::Completed),
              "forced-eviction indexed searches complete or reject consistently");
        Check(response.status == reference[index].status,
              cases[index].label);
        Check(signatures(response) == signatures(reference[index]),
              cases[index].label);
        Check(timings.truncated == reference[index].truncated,
              "truncation flags agree between resident and evicted runs");
        if (timings.coldSearchUsed) {
            sawColdAttribution = true;
            Check(timings.coldVolumesSearched == 1,
                  "exactly one cold volume streams from the durable store");
            Check(timings.coldRowsExamined > 0, "cold stream examines durable rows");
        }
        if (!cases[index].trigramExpectedHot) continue;
        Check(!timings.filenameIndexUsed,
              "an evicted searchable set disables the trigram accelerator (no false negatives)");
    }
    Check(sawColdAttribution, "at least one query exercised the cold-store attribution path");
}

// memory-bounded-indexed-scale 3.3: repeated alternating searches over an
// evicted index must neither grow the projection nor reload/evict thrash.
// completion-review R1: a volume-root row's pre-materialization view name is
// empty while its owning candidate uses the display name `prefix + "\\"`.
// The bounded top-N admission must not judge root rows by that placeholder:
// a capped descending filename search must return the root the durable path
// returns, and hot/cold must agree for every ordering and direction.
void TestCappedRootSearchMatchesColdAndRetainsTopN() {
    IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_capped_root_search.db")), "the capped-root fixture opens");
    VolumeKey firstKey;
    firstKey.serialNumber = 0;
    const auto firstVolume = pipeline.ResolveVolume(firstKey);
    VolumeKey secondKey;
    secondKey.serialNumber = 2;
    const auto secondVolume = pipeline.ResolveVolume(secondKey);
    ffprotocol::VolumeSetting firstSetting;
    firstSetting.key = L"C:";
    firstSetting.enabled = true;
    pipeline.SetVolumeRules(firstVolume, firstSetting);
    ffprotocol::VolumeSetting secondSetting;
    secondSetting.key = L"Z:";
    secondSetting.enabled = true;
    pipeline.SetVolumeRules(secondVolume, secondSetting);
    const std::vector<ffprotocol::MftRecordV1> rows{
        MakeMftRecord(5, 5, u"", 0x10, 0),
        MakeMftRecord(100, 5, u"A", 0x10, 0),
        MakeMftRecord(101, 5, u"B", 0x10, 0),
    };
    Check(pipeline.ApplyMftBatch(firstVolume, rows) && pipeline.ApplyMftBatch(secondVolume, rows),
          "the capped-root fixture applies to both volumes");
    pipeline.RebuildAll({});
    Check(pipeline.MarkScanComplete(firstVolume) && pipeline.MarkScanComplete(secondVolume),
          "the capped-root fixture marks both volumes scan complete");

    struct Case {
        uint8_t sortField;
        bool descending;
        uint32_t maxResults;
    };
    const Case cases[] = {
        {1, true, 1},   // filename descending
        {1, false, 1},  // filename ascending
        {1, true, 2},   // filename descending, both roots in the capped set
        {1, false, 2},  // filename ascending, both roots in the capped set
        {0, true, 1},   // relevance descending
        {0, false, 1},  // relevance ascending
        {0, true, 2},   // relevance descending, capped set of two
        {0, false, 2},  // relevance ascending, capped set of two
    };
    for (const Case& testCase : cases) {
        ffprotocol::IndexedSearchRequest request;
        request.requestId = 1;
        request.scope = 3;  // AllIndexedLocations
        request.sortField = testCase.sortField;
        request.descending = testCase.descending;
        request.maxResults = testCase.maxResults;
        request.query = L"kind:folder";
        const auto hot = pipeline.SearchIndexed(request);
        pipeline.UnloadVolumeFromProjection(firstVolume);
        pipeline.UnloadVolumeFromProjection(secondVolume);
        ++request.requestId;
        const auto cold = pipeline.SearchIndexed(request);
        Check(hot.status == ffprotocol::IndexedSearchStatus::Completed &&
                  cold.status == ffprotocol::IndexedSearchStatus::Completed,
              "capped root searches complete on the hot and durable paths");
        bool identical = hot.candidates.size() == cold.candidates.size();
        for (size_t index = 0; identical && index < hot.candidates.size(); ++index) {
            identical = hot.candidates[index].id == cold.candidates[index].id &&
                        hot.candidates[index].name == cold.candidates[index].name &&
                        hot.candidates[index].volumeId == cold.candidates[index].volumeId;
        }
        Check(identical, "capped root search returns the same candidates on the hot and durable paths");
        pipeline.RebuildAll({});
        Check(pipeline.MarkScanComplete(firstVolume) && pipeline.MarkScanComplete(secondVolume),
              "the capped-root fixture re-arms both volumes after the durable pass");
    }

    // The reviewer's exact case: the capped descending filename search must
    // keep the volume root whose display name wins the ordering.
    ffprotocol::IndexedSearchRequest rootRequest;
    rootRequest.requestId = 99;
    rootRequest.scope = 3;
    rootRequest.sortField = 1;  // Filename
    rootRequest.descending = true;
    rootRequest.maxResults = 1;
    rootRequest.query = L"kind:folder";
    const auto rootResult = pipeline.SearchIndexed(rootRequest);
    Check(rootResult.candidates.size() == 1 && rootResult.candidates[0].id == 5 &&
              rootResult.candidates[0].name == L"Z:\\",
          "capped descending filename search retains the winning volume root");
}


void TestRepeatedColdSearchesDoNotThrashMemory() {
    IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_eviction_thrash.db")),
          "the thrash-regression fixture opens");
    VolumeKey key;
    key.serialNumber = 51;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    pipeline.SetVolumeRules(volume, setting);
    std::vector<ffprotocol::MftRecordV1> records;
    records.push_back(MakeMftRecord(1, 1, u".", 0x10, 0));
    for (uint64_t dirIndex = 0; dirIndex < 40; ++dirIndex) {
        records.push_back(MakeMftRecord(100 + dirIndex, 1, U16FromWide(L"dir-" + std::to_wstring(dirIndex)), 0x10, 0));
        for (uint64_t fileIndex = 0; fileIndex < 25; ++fileIndex) {
            records.push_back(MakeMftRecord(1000 + dirIndex * 25 + fileIndex, 100 + dirIndex,
                                            U16FromWide(L"file-" + std::to_wstring(dirIndex) + L"-" +
                                                        std::to_wstring(fileIndex) + L".dat"),
                                            0, fileIndex));
        }
    }
    Check(pipeline.ApplyMftBatch(volume, records), "the thrash fixture ingests 1041 entries");
    const auto beforeBudget = pipeline.GetProjectionMemoryDiagnostics();
    pipeline.SetProjectionMemoryBudget(beforeBudget.estimatedBytes / 2);
    Check(pipeline.IsVolumeEvicted(volume), "halving the budget evicts part of the volume");
    const auto baseline = pipeline.GetProjectionMemoryDiagnostics();
    Check(baseline.coldReloadCount == 0, "search setup performs no cold reloads");

    ffprotocol::IndexedSearchRequest request;
    request.requestId = 800;
    request.scope = 3;
    request.maxResults = 50;
    for (int iteration = 0; iteration < 25; ++iteration) {
        request.query = iteration % 2 == 0 ? L"file-1" : L"file-2-1.dat";
        const auto response = pipeline.SearchIndexed(request);
        Check(response.status == ffprotocol::IndexedSearchStatus::Completed,
              "repeated cold searches complete");
        const auto now = pipeline.GetProjectionMemoryDiagnostics();
        if (now.estimatedBytes > baseline.configuredMaximumBytes) {
            Check(false, "repeated cold searches never exceed the configured ceiling");
            break;
        }
        if (now.subtreeEvictionCount != baseline.subtreeEvictionCount
            || now.coldReloadCount != baseline.coldReloadCount) {
            Check(false, "searching never triggers eviction or cold-reload churn");
            break;
        }
    }
    const auto finalDiagnostics = pipeline.GetProjectionMemoryDiagnostics();
    Check(finalDiagnostics.subtreeEvictionCount == baseline.subtreeEvictionCount,
          "twenty-five cold searches perform zero additional evictions");
    Check(finalDiagnostics.coldReloadCount == baseline.coldReloadCount,
          "twenty-five cold searches perform zero whole-volume reloads");
}

// memory-bounded-indexed-scale 4.2: mutations committed after eviction are
// immediately visible to cold searches (they read the durable store), and the
// restarted-from-SQLite state produces the identical result set.
void TestColdMutationsAndRestartEquivalence() {
    IndexPipeline pipeline;
    const std::string dbPath = FreshDbPath("engine_test_eviction_mutations.db");
    Check(pipeline.Open(dbPath), "the mutation fixture opens");
    VolumeKey key;
    key.serialNumber = 61;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeMftRecord(1, 1, u".", 0x10, 0),
        MakeMftRecord(10, 1, u"Root", 0x10, 0),
        MakeMftRecord(11, 10, u"Src", 0x10, 0),
        MakeMftRecord(12, 10, u"Dst", 0x10, 0),
        MakeMftRecord(500, 11, u"moveme.txt", 0, 33),
        MakeMftRecord(501, 11, u"renameme.txt", 0, 44),
        MakeMftRecord(502, 12, u"deleteme.txt", 0, 55),
    }), "the mutation base fixture persists");
    const auto beforeBudget = pipeline.GetProjectionMemoryDiagnostics();
    pipeline.SetProjectionMemoryBudget((beforeBudget.estimatedBytes * 3) / 4);
    if (!pipeline.IsVolumeEvicted(volume)) {
        // Guarantee eviction regardless of estimation granularity: shrink
        // until the volume actually loses a subtree.
        pipeline.SetProjectionMemoryBudget(beforeBudget.estimatedBytes / 8);
    }
    Check(pipeline.IsVolumeEvicted(volume), "the mutation fixture volume is evicted before mutating");

    auto request = [](const wchar_t* queryText) {
        ffprotocol::IndexedSearchRequest r;
        r.requestId = 900;
        r.scope = 3;
        r.maxResults = 100;
        r.query = queryText;
        return r;
    };

    // Insert after eviction.
    Check(pipeline.ApplyMftBatch(volume, {MakeMftRecord(503, 11, u"inserted-cold.bin", 0, 66)}),
          "a post-eviction insert commits durably");
    auto response = pipeline.SearchIndexed(request(L"inserted-cold"));
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed
              && response.candidates.size() == 1
              && response.candidates[0].folder == L"C:\\Root\\Src",
          "a cold search observes a post-eviction insert with its reconstructed folder");

    // Rename after eviction (same FRN, new name).
    Check(pipeline.ApplyMftBatch(volume, {MakeMftRecord(501, 11, u"renamed-cold.txt", 0, 44)}),
          "a post-eviction rename commits durably");
    response = pipeline.SearchIndexed(request(L"renameme"));
    Check(response.candidates.empty(), "the old name disappears from cold search after rename");
    response = pipeline.SearchIndexed(request(L"renamed-cold"));
    Check(response.candidates.size() == 1 && response.candidates[0].id == 501,
          "the new name appears in cold search after rename");

    // Move after eviction (same FRN, new parent).
    Check(pipeline.ApplyMftBatch(volume, {MakeMftRecord(500, 12, u"moveme.txt", 0, 33)}),
          "a post-eviction move commits durably");
    response = pipeline.SearchIndexed(request(L"moveme"));
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed
              && response.candidates.size() == 1
              && response.candidates[0].folder == L"C:\\Root\\Dst",
          "a cold search reconstructs a post-eviction move through its new durable parent chain");

    // Delete after eviction (USN delete reason).
    ffprotocol::UsnDeltaV1 deletion{};
    deletion.fixed.fileReferenceNumber = 502;
    deletion.fixed.parentFileReferenceNumber = 12;
    deletion.fixed.reason = 0x00000200u; // USN_REASON_FILE_DELETE
    deletion.fixed.fileNameLengthChars = 13;
    deletion.fileName = u"deleteme.txt";
    Check(pipeline.ApplyUsnBatch(volume, std::vector{deletion}),
          "a post-eviction USN delete commits durably");
    response = pipeline.SearchIndexed(request(L"deleteme"));
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed && response.candidates.empty(),
          "a deleted row leaves cold search immediately");

    // Restart from SQLite: simulate a real process restart with a fresh
    // pipeline object (fresh projection, default budget) over the same
    // database file. The rebuilt state must equal the cold-mutated state.
    pipeline.Close();
    IndexPipeline restarted;
    Check(restarted.Open(dbPath), "the mutation fixture reopens after close");
    const auto reopenedVolume = restarted.ResolveVolume(key);
    Check(reopenedVolume == volume, "restart preserves the durable volume row id");
    restarted.SetVolumeRules(reopenedVolume, setting);
    int rebuiltVolumes = 0;
    restarted.RebuildAll([&rebuiltVolumes](ffindexstore::VolumeRowId) { ++rebuiltVolumes; });
    Check(rebuiltVolumes >= 1, "restart rebuilds persisted volumes");
    Check(!restarted.IsVolumeEvicted(reopenedVolume), "a fresh process starts with a fully resident rebuild");
    response = restarted.SearchIndexed(request(L"txt"));
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed
              && response.candidates.size() == 2,
          "the restarted-from-SQLite search matches the cold-mutated state exactly");
    bool sawMovedFolder = false;
    bool sawRenamed = false;
    for (const auto& candidate : response.candidates) {
        sawMovedFolder |= candidate.name == L"moveme.txt" && candidate.folder == L"C:\\Root\\Dst";
        sawRenamed |= candidate.name == L"renamed-cold.txt";
    }
    Check(sawMovedFolder && sawRenamed, "restart keeps every post-eviction mutation observable");

    // Reconciliation deletion: a completed full pass removes rows it never
    // observed, and the removal is visible to subsequent searches.
    restarted.BeginReconciliationPass(reopenedVolume);
    Check(restarted.ApplyMftBatch(reopenedVolume, {
        MakeMftRecord(1, 1, u".", 0x10, 0),
        MakeMftRecord(10, 1, u"Root", 0x10, 0),
        MakeMftRecord(11, 10, u"Src", 0x10, 0),
        MakeMftRecord(12, 10, u"Dst", 0x10, 0),
        MakeMftRecord(500, 12, u"moveme.txt", 0, 33),
        MakeMftRecord(501, 11, u"renamed-cold.txt", 0, 44),
        MakeMftRecord(503, 11, u"inserted-cold.bin", 0, 66),
    }), "the reconciliation ground-truth pass re-applies every surviving row");
    restarted.FinishReconciliationPass(reopenedVolume);
    response = restarted.SearchIndexed(request(L"inserted-cold"));
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed
              && response.candidates.size() == 1,
          "reconciliation keeps observed rows searchable");
}

// memory-bounded-indexed-scale 4.1: aggregates for an evicted volume come
// straight from the durable recursive CTE -- never from a whole-volume reload.
void TestColdFolderAggregateServedFromStore() {
    IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_eviction_aggregate.db")),
          "the cold-aggregate fixture opens");
    VolumeKey key;
    key.serialNumber = 71;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeMftRecord(1, 1, u".", 0x10, 0),
        MakeMftRecord(10, 1, u"Big", 0x10, 0),
        MakeMftRecord(11, 10, u"Inner", 0x10, 0),
        MakeMftRecord(600, 11, u"a.txt", 0, 10),
        MakeMftRecord(601, 11, u"b.txt", 0, 20),
        MakeMftRecord(602, 10, u"c.txt", 0, 40),
        MakeMftRecord(13, 1, u"EmptyDir", 0x10, 0),
    }), "the cold-aggregate fixture persists");
    const FileId innerFrn{11, 0};
    const auto hotAggregate = pipeline.GetFolderAggregate(volume, innerFrn);
    Check(hotAggregate.has_value() && hotAggregate->itemCount == 2
              && hotAggregate->totalSizeBytes == 30,
          "the hot projection aggregate covers the subtree walk");
    const auto emptyHot = pipeline.GetFolderAggregate(volume, FileId{13, 0});
    Check(emptyHot.has_value() && emptyHot->itemCount == 0,
          "an existing empty folder aggregates to zero items, not nullopt");
    Check(!pipeline.GetFolderAggregate(volume, FileId{987'654, 0}).has_value(),
          "an unknown folder stays nullopt while resident");

    pipeline.UnloadVolumeFromProjection(volume);
    Check(pipeline.IsVolumeEvicted(volume), "the aggregate fixture volume is evicted");

    const auto coldAggregate = pipeline.GetFolderAggregate(volume, innerFrn);
    Check(coldAggregate.has_value() && coldAggregate->itemCount == hotAggregate->itemCount
              && coldAggregate->totalSizeBytes == hotAggregate->totalSizeBytes,
          "the evicted-volume aggregate equals the durable recursive CTE result");
    Check(pipeline.IsVolumeEvicted(volume),
          "answering an aggregate does not reload the evicted volume");
    const auto emptyCold = pipeline.GetFolderAggregate(volume, FileId{13, 0});
    Check(emptyCold.has_value() && emptyCold->itemCount == 0,
          "an empty-folder aggregate survives eviction without turning into not-found");
    Check(!pipeline.GetFolderAggregate(volume, FileId{987'654, 0}).has_value(),
          "an unknown folder remains nullopt while evicted (never 'empty')");
}

// memory-bounded-indexed-scale 2.6: cancellation stops durable streaming.
void TestCancellationDuringColdStream() {
    IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_eviction_cancellation.db")),
          "the cold-cancellation fixture opens");
    VolumeKey key;
    key.serialNumber = 81;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    pipeline.SetVolumeRules(volume, setting);
    std::vector<ffprotocol::MftRecordV1> records;
    records.push_back(MakeMftRecord(1, 1, u".", 0x10, 0));
    for (uint64_t id = 1; id <= 4000; ++id) {
        records.push_back(MakeMftRecord(10'000 + id, 1,
                                        U16FromWide(L"bulk-" + std::to_wstring(id) + L".dat"), 0, id));
    }
    Check(pipeline.ApplyMftBatch(volume, records), "the cold-cancellation fixture ingests");
    pipeline.UnloadVolumeFromProjection(volume);
    Check(pipeline.IsVolumeEvicted(volume), "the cold-cancellation volume is evicted");

    ffprotocol::IndexedSearchRequest request;
    request.requestId = 950;
    request.scope = 3;
    request.maxResults = 4096;
    request.query = L"bulk";
    // durable-cold-search-index-and-hybrid-residency 4.1: establish actual
    // candidate path work before cancelling, rather than counting setup polls.
    std::atomic<size_t> durableLookups{0};
    pipeline.SetReadSessionFaultInjector([&durableLookups](const auto point) {
        if (point == ReadSessionFaultPoint::FindEntry) {
            durableLookups.fetch_add(1, std::memory_order_relaxed);
        }
        return false;
    });
    IndexPipeline::SearchTestHooks hooks;
    hooks.forceCanonicalColdScan = true;
    pipeline.SetSearchTestHooks(hooks);
    IndexPipeline::SearchTimings timings;
    const auto response = pipeline.SearchIndexed(request, &timings, [&durableLookups] {
        return durableLookups.load(std::memory_order_relaxed) >= 32;
    });
    pipeline.SetReadSessionFaultInjector({});
    pipeline.SetSearchTestHooks({});
    Check(response.status == ffprotocol::IndexedSearchStatus::Cancelled,
          "cancelling during cold streaming reports Cancelled");
    Check(timings.cancelled && response.candidates.empty() && !response.truncated &&
              response.requestId == request.requestId,
          "a cancelled cold search discards partial results");
    Check(durableLookups.load() >= 32 && durableLookups.load() < records.size(),
          "cold cancellation follows actual candidate materialization before corpus exhaustion");
    Check(pipeline.GetReadPlaneDiagnostics().activeDurableReaders == 0,
          "cancellation after candidate materialization releases its durable reader");
    Check(pipeline.IsVolumeEvicted(volume),
          "a cancelled cold search does not silently hydrate the volume");
    const auto recovered = pipeline.SearchIndexed(request);
    Check(recovered.status == ffprotocol::IndexedSearchStatus::Completed &&
              recovered.candidates.size() == 4000 && !recovered.truncated,
          "cold search completes normally after cancellation during materialization");
}

// rule-pruning-durability-and-reconciliation-failure-closure: MFT secondary
// pruning is fail-closed, boundary-specific, and immediately retryable.
void TestMftSecondaryFailureIsFailClosedAndRetryable() {
    const std::string dbPath = FreshDbPath("engine_test_mft_rule_pruning.db");
    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "MFT rule-pruning fixture opens");
    VolumeKey key;
    key.serialNumber = 91;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    setting.rules.push_back({L"C:\\blocked-mft.txt", false});
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {MakeMftRecord(1, 1, u"", 0x10, 0)}),
          "MFT rule-pruning fixture seeds its root");

    SecondaryFailurePlan plan{SecondaryBoundary::MftRulePrune, 1, false, 0, {}};
    InstallSecondaryFailurePlan(pipeline, plan);
    const uint64_t epochBeforeFailure = pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch;
    const FileId target{2, 0};
    const auto blocked = std::vector<ffprotocol::MftRecordV1>{
        MakeMftRecord(target.low, 1, u"blocked-mft.txt", 0, 10)};
    Check(!pipeline.ApplyMftBatch(volume, blocked),
          "an injected MFT secondary failure returns false");
    Check(plan.targetCalls == 1 && plan.observedBoundaries.size() == 1 &&
              plan.observedBoundaries[0] == SecondaryBoundary::MftRulePrune,
          "the MFT failure injector reaches only the named MFT boundary");
    const auto failedSnapshot = pipeline.ExportDirectorySnapshot(volume, L"C:");
    Check(SnapshotHasName(failedSnapshot, L"blocked-mft.txt"),
          "the excluded MFT row remains in the projection after failure");
    const auto failedAggregate = pipeline.GetFolderAggregate(volume, FileId{1, 0});
    Check(failedAggregate.has_value() && failedAggregate->itemCount == 1,
          "the failed MFT prune leaves the folder aggregate consistent with retained rows");
    const auto failedSearch = SearchForName(pipeline, 901, L"blocked-mft");
    Check(failedSearch.status == ffprotocol::IndexedSearchStatus::Completed &&
              failedSearch.candidates.empty(),
          "rule-filtered indexed search does not expose the retained excluded MFT row");
    const auto failedDurable = ReadDurableEntryState(dbPath, volume, target);
    Check(failedDurable.opened && failedDurable.entryCount == 2 && failedDurable.hasTarget,
          "the failed MFT prune leaves the durable row intact");
    Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch == epochBeforeFailure,
          "a failed MFT mutation does not publish a success epoch");

    pipeline.SetSecondaryPruningFaultInjector({});
    Check(pipeline.ApplyMftBatch(volume, blocked),
          "the same MFT batch succeeds after the one-shot fault clears");
    const auto recoveredSnapshot = pipeline.ExportDirectorySnapshot(volume, L"C:");
    Check(!SnapshotHasName(recoveredSnapshot, L"blocked-mft.txt"),
          "the excluded MFT row is removed on the immediate retry");
    const auto recoveredAggregate = pipeline.GetFolderAggregate(volume, FileId{1, 0});
    Check(recoveredAggregate.has_value() && recoveredAggregate->itemCount == 0,
          "the MFT folder aggregate converges after retry");
    const auto recoveredDurable = ReadDurableEntryState(dbPath, volume, target);
    Check(recoveredDurable.opened && recoveredDurable.entryCount == 1 && !recoveredDurable.hasTarget,
          "the MFT durable row is removed on retry");

    setting.rules.push_back({L"C:\\blocked-mft-persistent.txt", false});
    pipeline.SetVolumeRules(volume, setting);
    SecondaryFailurePlan persistentPlan;
    persistentPlan.target = SecondaryBoundary::MftRulePrune;
    persistentPlan.persistent = true;
    InstallSecondaryFailurePlan(pipeline, persistentPlan);
    const uint64_t epochBeforePersistentFailure = pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch;
    const FileId persistentTarget{4, 0};
    const auto persistentBlocked = std::vector<ffprotocol::MftRecordV1>{
        MakeMftRecord(persistentTarget.low, 1, u"blocked-mft-persistent.txt", 0, 12)};
    for (int attempt = 0; attempt < 2; ++attempt) {
        Check(!pipeline.ApplyMftBatch(volume, persistentBlocked),
              "persistent MFT secondary failure remains visible to the caller");
        Check(SnapshotHasName(pipeline.ExportDirectorySnapshot(volume, L"C:"),
                              L"blocked-mft-persistent.txt"),
              "persistent MFT failure retains the row in the projection");
    }
    Check(persistentPlan.targetCalls == 2,
          "persistent MFT injection reaches the named boundary twice");
    const auto persistentDurable = ReadDurableEntryState(dbPath, volume, persistentTarget);
    Check(persistentDurable.opened && persistentDurable.entryCount == 2 && persistentDurable.hasTarget,
          "persistent MFT failure leaves the primary durable row intact");
    Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch == epochBeforePersistentFailure,
          "persistent MFT failure does not publish a searchable epoch");
    pipeline.SetSecondaryPruningFaultInjector({});
    Check(pipeline.ApplyMftBatch(volume, persistentBlocked),
          "persistent MFT failure converges after the fault clears");
    Check(!SnapshotHasName(pipeline.ExportDirectorySnapshot(volume, L"C:"),
                           L"blocked-mft-persistent.txt"),
          "persistent MFT recovery removes the retained row");
    pipeline.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + "-shm");
}

void TestUsnSecondaryFailureIsBoundarySpecificAndRetryable() {
    const std::string dbPath = FreshDbPath("engine_test_usn_rule_pruning.db");
    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "USN rule-pruning fixture opens");
    VolumeKey key;
    key.serialNumber = 94;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    setting.rules.push_back({L"C:\\blocked-mft.txt", false});
    setting.rules.push_back({L"C:\\blocked-usn.txt", false});
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {MakeMftRecord(1, 1, u"", 0x10, 0)}),
          "USN rule-pruning fixture seeds its root");

    SecondaryFailurePlan plan{SecondaryBoundary::UsnRulePrune, 1, false, 0, {}};
    InstallSecondaryFailurePlan(pipeline, plan);
    const auto mftOther = std::vector<ffprotocol::MftRecordV1>{
        MakeMftRecord(2, 1, u"blocked-mft.txt", 0, 10)};
    Check(pipeline.ApplyMftBatch(volume, mftOther),
          "a fault plan for USN does not fail the adjacent MFT boundary");
    Check(plan.targetCalls == 0 && plan.observedBoundaries.size() == 1 &&
              plan.observedBoundaries[0] == SecondaryBoundary::MftRulePrune &&
              !SnapshotHasName(pipeline.ExportDirectorySnapshot(volume, L"C:"), L"blocked-mft.txt"),
          "the adjacent MFT prune completes while only USN injection is armed");

    const FileId target{3, 0};
    const auto blocked = std::vector<ffprotocol::UsnDeltaV1>{
        MakeUsnRecord(1, target.low, 1, u"blocked-usn.txt", 11)};
    const uint64_t epochBeforeFailure = pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch;
    Check(!pipeline.ApplyUsnBatch(volume, blocked),
          "an injected USN secondary failure returns false");
    Check(plan.targetCalls == 1 && plan.observedBoundaries.size() == 2 &&
              plan.observedBoundaries[1] == SecondaryBoundary::UsnRulePrune,
          "the USN failure injector reaches the named USN boundary");
    Check(SnapshotHasName(pipeline.ExportDirectorySnapshot(volume, L"C:"), L"blocked-usn.txt"),
          "the excluded USN row remains in the projection after failure");
    const auto failedAggregate = pipeline.GetFolderAggregate(volume, FileId{1, 0});
    Check(failedAggregate.has_value() && failedAggregate->itemCount == 1,
          "the failed USN prune leaves the folder aggregate consistent");
    const auto failedSearch = SearchForName(pipeline, 904, L"blocked-usn");
    Check(failedSearch.status == ffprotocol::IndexedSearchStatus::Completed &&
              failedSearch.candidates.empty(),
          "rule-filtered indexed search does not expose the retained excluded USN row");
    const auto failedDurable = ReadDurableEntryState(dbPath, volume, target);
    Check(failedDurable.opened && failedDurable.entryCount == 2 && failedDurable.hasTarget,
          "the failed USN prune leaves its durable row intact");
    Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch == epochBeforeFailure,
          "a failed USN mutation does not publish a success epoch");

    pipeline.SetSecondaryPruningFaultInjector({});
    Check(pipeline.ApplyUsnBatch(volume, blocked),
          "the same USN batch succeeds after the one-shot fault clears");
    Check(!SnapshotHasName(pipeline.ExportDirectorySnapshot(volume, L"C:"), L"blocked-usn.txt"),
          "the excluded USN row is removed on retry");
    const auto recoveredAggregate = pipeline.GetFolderAggregate(volume, FileId{1, 0});
    Check(recoveredAggregate.has_value() && recoveredAggregate->itemCount == 0,
          "the USN folder aggregate converges after retry");
    const auto recoveredDurable = ReadDurableEntryState(dbPath, volume, target);
    Check(recoveredDurable.opened && recoveredDurable.entryCount == 1 && !recoveredDurable.hasTarget,
          "the USN durable row is removed on retry");

    setting.rules.push_back({L"C:\\blocked-usn-persistent.txt", false});
    pipeline.SetVolumeRules(volume, setting);
    SecondaryFailurePlan persistentPlan;
    persistentPlan.target = SecondaryBoundary::UsnRulePrune;
    persistentPlan.persistent = true;
    InstallSecondaryFailurePlan(pipeline, persistentPlan);
    const uint64_t epochBeforePersistentFailure = pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch;
    const FileId persistentTarget{4, 0};
    const auto persistentBlocked = std::vector<ffprotocol::UsnDeltaV1>{
        MakeUsnRecord(2, persistentTarget.low, 1, u"blocked-usn-persistent.txt", 13)};
    for (int attempt = 0; attempt < 2; ++attempt) {
        Check(!pipeline.ApplyUsnBatch(volume, persistentBlocked),
              "persistent USN secondary failure remains visible to the caller");
        Check(SnapshotHasName(pipeline.ExportDirectorySnapshot(volume, L"C:"),
                              L"blocked-usn-persistent.txt"),
              "persistent USN failure retains the row in the projection");
    }
    Check(persistentPlan.targetCalls == 2,
          "persistent USN injection reaches the named boundary twice");
    const auto persistentDurable = ReadDurableEntryState(dbPath, volume, persistentTarget);
    Check(persistentDurable.opened && persistentDurable.entryCount == 2 && persistentDurable.hasTarget,
          "persistent USN failure leaves the primary durable row intact");
    Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch == epochBeforePersistentFailure,
          "persistent USN failure does not publish a searchable epoch");
    pipeline.SetSecondaryPruningFaultInjector({});
    Check(pipeline.ApplyUsnBatch(volume, persistentBlocked),
          "persistent USN failure converges after the fault clears");
    Check(!SnapshotHasName(pipeline.ExportDirectorySnapshot(volume, L"C:"),
                           L"blocked-usn-persistent.txt"),
          "persistent USN recovery removes the retained row");
    pipeline.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + "-shm");
}

void TestRebuildSecondaryFailureDoesNotPublishSuccess() {
    const std::string dbPath = FreshDbPath("engine_test_rebuild_rule_pruning.db");
    VolumeKey key;
    key.serialNumber = 95;
    VolumeRowId volume = 0;
    {
        IndexPipeline seed;
        Check(seed.Open(dbPath), "rebuild rule-pruning seed opens");
        volume = seed.ResolveVolume(key);
        Check(seed.ApplyMftBatch(volume, {
            MakeMftRecord(1, 1, u"", 0x10, 0),
            MakeMftRecord(2, 1, u"blocked-rebuild.txt", 0, 20),
            MakeMftRecord(3, 1, u"keep-rebuild.txt", 0, 30),
        }), "rebuild rule-pruning seed rows are durable");
        seed.Close();
    }

    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "rebuild rule-pruning fixture reopens");
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    setting.rules.push_back({L"C:\\blocked-rebuild.txt", false});
    pipeline.SetVolumeRules(volume, setting);
    SecondaryFailurePlan plan{SecondaryBoundary::RebuildRulePrune, 1, false, 0, {}};
    InstallSecondaryFailurePlan(pipeline, plan);
    const uint64_t epochBeforeFailure = pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch;
    int callbackCount = 0;
    pipeline.RebuildAll([&callbackCount](VolumeRowId) { ++callbackCount; });
    Check(callbackCount == 0,
          "RebuildAll does not callback a volume whose secondary prune failed");
    Check(plan.targetCalls == 1 && plan.observedBoundaries.size() == 1 &&
              plan.observedBoundaries[0] == SecondaryBoundary::RebuildRulePrune,
          "the rebuild failure injector reaches only the named rebuild boundary");
    const auto failedSnapshot = pipeline.ExportDirectorySnapshot(volume, L"C:");
    Check(SnapshotHasName(failedSnapshot, L"blocked-rebuild.txt") &&
              SnapshotHasName(failedSnapshot, L"keep-rebuild.txt"),
          "failed RebuildAll retains both durable rows in the projection");
    const auto failedAggregate = pipeline.GetFolderAggregate(volume, FileId{1, 0});
    Check(failedAggregate.has_value() && failedAggregate->itemCount == 2,
          "failed RebuildAll leaves the folder aggregate consistent");
    const auto failedSearch = SearchForName(pipeline, 905, L"blocked-rebuild");
    Check(failedSearch.status == ffprotocol::IndexedSearchStatus::Completed &&
              failedSearch.candidates.empty(),
          "rule-filtered search does not expose the retained rebuild-prune row");
    const auto failedDurable = ReadDurableEntryState(dbPath, volume, FileId{2, 0});
    Check(failedDurable.opened && failedDurable.entryCount == 3 && failedDurable.hasTarget,
          "failed RebuildAll leaves the durable pruning obligation intact");
    Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch == epochBeforeFailure,
          "failed RebuildAll does not advance the query-visible success epoch");

    pipeline.SetSecondaryPruningFaultInjector({});
    pipeline.RebuildAll([&callbackCount](VolumeRowId) { ++callbackCount; });
    Check(callbackCount == 1,
          "RebuildAll callbacks once after the retained prune succeeds");
    Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch > epochBeforeFailure,
          "successful rebuild pruning advances the query-visible epoch");
    const auto recoveredSnapshot = pipeline.ExportDirectorySnapshot(volume, L"C:");
    Check(!SnapshotHasName(recoveredSnapshot, L"blocked-rebuild.txt") &&
              SnapshotHasName(recoveredSnapshot, L"keep-rebuild.txt"),
          "the rebuild-prune row is removed after recovery");
    const auto recoveredAggregate = pipeline.GetFolderAggregate(volume, FileId{1, 0});
    Check(recoveredAggregate.has_value() && recoveredAggregate->itemCount == 1,
          "the rebuild aggregate converges after recovery");
    const auto recoveredDurable = ReadDurableEntryState(dbPath, volume, FileId{2, 0});
    Check(recoveredDurable.opened && recoveredDurable.entryCount == 2 && !recoveredDurable.hasTarget,
          "the rebuild durable row is removed after recovery");

    ffprotocol::VolumeSetting unrestricted = setting;
    unrestricted.rules.clear();
    pipeline.SetVolumeRules(volume, unrestricted);
    const FileId persistentTarget{4, 0};
    Check(pipeline.ApplyMftBatch(volume, {
              MakeMftRecord(persistentTarget.low, 1, u"blocked-rebuild-persistent.txt", 0, 31)}),
          "persistent rebuild fixture seeds a new durable row");
    setting.rules.push_back({L"C:\\blocked-rebuild-persistent.txt", false});
    pipeline.SetVolumeRules(volume, setting);
    SecondaryFailurePlan persistentPlan;
    persistentPlan.target = SecondaryBoundary::RebuildRulePrune;
    persistentPlan.persistent = true;
    InstallSecondaryFailurePlan(pipeline, persistentPlan);
    const uint64_t epochBeforePersistentFailure = pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch;
    int persistentCallbackCount = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        pipeline.RebuildAll([&persistentCallbackCount](VolumeRowId) { ++persistentCallbackCount; });
        Check(persistentCallbackCount == 0,
              "persistent rebuild failure never reports a successful volume");
        Check(SnapshotHasName(pipeline.ExportDirectorySnapshot(volume, L"C:"),
                              L"blocked-rebuild-persistent.txt"),
              "persistent rebuild failure retains the row in the projection");
    }
    Check(persistentPlan.targetCalls == 2,
          "persistent rebuild injection reaches the named boundary twice");
    const auto persistentDurable = ReadDurableEntryState(dbPath, volume, persistentTarget);
    Check(persistentDurable.opened && persistentDurable.entryCount == 3 && persistentDurable.hasTarget,
          "persistent rebuild failure leaves the durable pruning obligation intact");
    Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch == epochBeforePersistentFailure,
          "persistent rebuild failure does not publish a searchable epoch");
    pipeline.SetSecondaryPruningFaultInjector({});
    pipeline.RebuildAll([&persistentCallbackCount](VolumeRowId) { ++persistentCallbackCount; });
    Check(persistentCallbackCount == 1,
          "persistent rebuild failure converges after the fault clears");
    Check(!SnapshotHasName(pipeline.ExportDirectorySnapshot(volume, L"C:"),
                           L"blocked-rebuild-persistent.txt"),
          "persistent rebuild recovery removes the retained row");
    pipeline.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + "-shm");
}

void TestReconciliationRetentionOnSecondaryFailure() {
    const std::string dbPath = FreshDbPath("engine_test_reconcile_pruning.db");
    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "reconciliation-pruning fixture opens");
    VolumeKey key;
    key.serialNumber = 92;
    const auto volume = pipeline.ResolveVolume(key);
    const FileId target{200, 0};
    // Seed before configuring rules. The target must be present in the durable
    // store and projection so Finish has a real not-seen removal obligation.
    Check(pipeline.ApplyMftBatch(volume, {
        MakeMftRecord(1, 1, u".", 0x10, 0),
        MakeMftRecord(target.low, 1, u"toremove.txt", 0, 10),
        MakeMftRecord(201, 1, u"keep.txt", 0, 10),
    }), "reconciliation fixture seeds target and retained rows");
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    pipeline.SetVolumeRules(volume, setting);

    pipeline.BeginReconciliationPass(volume);
    Check(pipeline.IsReconciliationPassActive(volume), "reconciliation pass is active before scanning");
    Check(pipeline.ApplyMftBatch(volume, {
        MakeMftRecord(1, 1, u".", 0x10, 0),
        MakeMftRecord(201, 1, u"keep.txt", 0, 10),
    }), "reconciliation scan omits the target row");
    const auto preFinishDurable = ReadDurableEntryState(dbPath, volume, target);
    Check(preFinishDurable.opened && preFinishDurable.hasTarget && preFinishDurable.entryCount == 3,
          "the reconciliation target exists before Finish computes removals");

    SecondaryFailurePlan plan{SecondaryBoundary::ReconciliationRemoval, 1, false, 0, {}};
    InstallSecondaryFailurePlan(pipeline, plan);
    const uint64_t epochBeforeFinish = pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch;
    pipeline.FinishReconciliationPass(volume);
    Check(plan.targetCalls == 1 && plan.observedBoundaries.size() == 1 &&
              plan.observedBoundaries[0] == SecondaryBoundary::ReconciliationRemoval,
          "the injected failure targets only reconciliation removal");
    Check(pipeline.IsReconciliationPassActive(volume),
          "failed reconciliation retains its active repair obligation");
    const auto failedSnapshot = pipeline.ExportDirectorySnapshot(volume, L"C:");
    Check(SnapshotHasName(failedSnapshot, L"toremove.txt") && SnapshotHasName(failedSnapshot, L"keep.txt"),
          "failed reconciliation retains both target and observed rows in the projection");
    const auto failedAggregate = pipeline.GetFolderAggregate(volume, FileId{1, 0});
    Check(failedAggregate.has_value() && failedAggregate->itemCount == 2,
          "failed reconciliation leaves the folder aggregate consistent");
    const auto failedSearch = SearchForName(pipeline, 902, L"toremove");
    Check(failedSearch.status == ffprotocol::IndexedSearchStatus::Completed &&
              failedSearch.candidates.size() == 1,
          "failed reconciliation keeps the not-yet-removed row searchable");
    const auto failedDurable = ReadDurableEntryState(dbPath, volume, target);
    Check(failedDurable.opened && failedDurable.entryCount == 3 && failedDurable.hasTarget,
          "failed reconciliation leaves the durable removal transaction untouched");
    Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch == epochBeforeFinish,
          "failed reconciliation does not publish a searchable epoch");

    pipeline.SetSecondaryPruningFaultInjector({});
    pipeline.FinishReconciliationPass(volume);
    Check(!pipeline.IsReconciliationPassActive(volume),
          "reconciliation closes after the durable retry succeeds");
    const auto recoveredSnapshot = pipeline.ExportDirectorySnapshot(volume, L"C:");
    Check(!SnapshotHasName(recoveredSnapshot, L"toremove.txt") &&
              SnapshotHasName(recoveredSnapshot, L"keep.txt"),
          "the retained reconciliation target is pruned on retry");
    const auto recoveredAggregate = pipeline.GetFolderAggregate(volume, FileId{1, 0});
    Check(recoveredAggregate.has_value() && recoveredAggregate->itemCount == 1,
          "the reconciliation aggregate converges after retry");
    const auto recoveredDurable = ReadDurableEntryState(dbPath, volume, target);
    Check(recoveredDurable.opened && recoveredDurable.entryCount == 2 && !recoveredDurable.hasTarget,
          "the reconciliation durable row is removed on retry");
    pipeline.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + "-shm");
}

void TestRepeatedSecondaryFailure() {
    const std::string dbPath = FreshDbPath("engine_test_repeated_pruning.db");
    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "repeated-pruning fixture opens");
    VolumeKey key;
    key.serialNumber = 93;
    const auto volume = pipeline.ResolveVolume(key);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeMftRecord(1, 1, u".", 0x10, 0),
        MakeMftRecord(300, 1, u"repeat.txt", 0, 10),
    }), "repeated-pruning fixture seeds the target");
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    pipeline.SetVolumeRules(volume, setting);
    pipeline.BeginReconciliationPass(volume);
    Check(pipeline.ApplyMftBatch(volume, {MakeMftRecord(1, 1, u".", 0x10, 0)}),
          "repeated-pruning reconciliation observes only the root");

    SecondaryFailurePlan plan;
    plan.target = SecondaryBoundary::ReconciliationRemoval;
    plan.persistent = true;
    InstallSecondaryFailurePlan(pipeline, plan);
    const uint64_t epochBeforeFailure = pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch;
    for (int attempt = 0; attempt < 2; ++attempt) {
        pipeline.FinishReconciliationPass(volume);
        Check(pipeline.IsReconciliationPassActive(volume),
              "persistent reconciliation failure retains the obligation");
        Check(SnapshotHasName(pipeline.ExportDirectorySnapshot(volume, L"C:"), L"repeat.txt"),
              "persistent reconciliation failure never removes the target from the projection");
        Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch == epochBeforeFailure,
              "persistent reconciliation failure never advances the searchable epoch");
    }
    Check(plan.targetCalls == 2, "persistent injection fails both named reconciliation attempts");
    const auto failedDurable = ReadDurableEntryState(dbPath, volume, FileId{300, 0});
    Check(failedDurable.opened && failedDurable.entryCount == 2 && failedDurable.hasTarget,
          "persistent reconciliation failure leaves durable state unchanged");

    pipeline.SetSecondaryPruningFaultInjector({});
    pipeline.FinishReconciliationPass(volume);
    Check(!pipeline.IsReconciliationPassActive(volume) &&
              !SnapshotHasName(pipeline.ExportDirectorySnapshot(volume, L"C:"), L"repeat.txt"),
          "reconciliation converges in one pass after repeated failures clear");
    const auto recoveredDurable = ReadDurableEntryState(dbPath, volume, FileId{300, 0});
    Check(recoveredDurable.opened && recoveredDurable.entryCount == 1 && !recoveredDurable.hasTarget,
          "durable reconciliation state converges after repeated failures");
    pipeline.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + "-shm");
}

// wal-lifecycle: reconciliation mass removal applies in 5,000-row committed
// chunks (bounded-WAL transactions). Characterization guard for the chunk
// loop: a multi-chunk removal converges exactly like the historical
// single-transaction path (obligation cleared, durable + projection agree,
// idempotent re-run). The WAL-bounding property itself is evidenced by the
// lifecycle bench WAL matrix (evidence/wal-lifecycle.md).
void TestReconciliationMassRemovalConvergesAcrossChunks() {
    const std::string dbPath = FreshDbPath("engine_test_reconcile_chunks.db");
    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "chunked-removal fixture opens");
    VolumeKey key;
    key.serialNumber = 71;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    pipeline.SetVolumeRules(volume, setting);

    std::vector<ffprotocol::MftRecordV1> seed;
    seed.push_back(MakeMftRecord(1, 1, u".", 0x10, 0));
    for (uint64_t id = 1000; id < 13'000; ++id) {
        seed.push_back(MakeMftRecord(id, 1, u"chunk_row.txt", 0, static_cast<uint64_t>(id)));
    }
    Check(pipeline.ApplyMftBatch(volume, seed), "12,000 seeded rows apply");

    pipeline.BeginReconciliationPass(volume);
    Check(pipeline.ApplyMftBatch(volume, {MakeMftRecord(1, 1, u".", 0x10, 0)}),
          "reconciliation observes only the root");
    const uint64_t epochBeforePartial = pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch;
    // Fail 501 operations into chunk two. Chunk one must remain committed,
    // chunk two must roll back atomically, and retry must owe the remainder.
    ffindexstore::SetTestingCandidateMaintenanceFault(5'501, false);
    pipeline.FinishReconciliationPass(volume);
    ffindexstore::SetTestingCandidateMaintenanceFault(0, false);
    Check(pipeline.IsReconciliationPassActive(volume),
          "partial chunk failure retains the reconciliation obligation");
    const auto partial = ReadDurableEntryState(dbPath, volume, FileId{1, 0});
    Check(partial.opened && partial.entryCount == 7'001,
          "only the first 5,000-row chunk commits; the faulted second chunk rolls back");
    const auto partialAggregate = pipeline.GetFolderAggregate(volume, FileId{1, 0});
    Check(partialAggregate.has_value() && partialAggregate->itemCount == 7'000,
          "projection mirrors exactly the committed removal prefix on failure");
    Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch > epochBeforePartial,
          "a partially committed removal advances the searchable epoch");
    pipeline.FinishReconciliationPass(volume);
    Check(!pipeline.IsReconciliationPassActive(volume),
          "retry finishes the remaining chunks and clears the obligation");
    const auto durable = ReadDurableEntryState(dbPath, volume, FileId{1, 0});
    Check(durable.opened && durable.entryCount == 1,
          "durable state converges to the root across all removal chunks");
    const auto snapshot = pipeline.ExportDirectorySnapshot(volume, L"C:");
    Check(!SnapshotHasName(snapshot, L"chunk_row.txt"),
          "the projection mirrors every committed removal chunk");

    // Idempotence: re-running the pass after convergence is a no-op.
    pipeline.BeginReconciliationPass(volume);
    Check(pipeline.ApplyMftBatch(volume, {MakeMftRecord(1, 1, u".", 0x10, 0)}),
          "second reconciliation observes the root");
    pipeline.FinishReconciliationPass(volume);
    Check(!pipeline.IsReconciliationPassActive(volume), "the second pass converges immediately");
    const auto durableAfter = ReadDurableEntryState(dbPath, volume, FileId{1, 0});
    Check(durableAfter.opened && durableAfter.entryCount == 1,
          "durable state stays converged after the idempotent re-run");
    pipeline.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + "-shm");
}

void TestRulePruningFailureRecoversAfterCloseAndOpen() {
    const std::string dbPath = FreshDbPath("engine_test_rule_pruning_restart.db");
    VolumeKey key;
    key.serialNumber = 96;
    VolumeRowId volume = 0;
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    setting.rules.push_back({L"C:\\blocked-restart.txt", false});
    const FileId target{2, 0};
    {
        IndexPipeline pipeline;
        Check(pipeline.Open(dbPath), "restart rule-pruning fixture opens");
        volume = pipeline.ResolveVolume(key);
        pipeline.SetVolumeRules(volume, setting);
        Check(pipeline.ApplyMftBatch(volume, {MakeMftRecord(1, 1, u"", 0x10, 0)}),
              "restart rule-pruning fixture seeds its root");
        SecondaryFailurePlan plan{SecondaryBoundary::MftRulePrune, 1, false, 0, {}};
        InstallSecondaryFailurePlan(pipeline, plan);
        Check(!pipeline.ApplyMftBatch(volume, {
                  MakeMftRecord(target.low, 1, u"blocked-restart.txt", 0, 10)}),
              "the restart fixture records a failed secondary prune before shutdown");
        Check(SnapshotHasName(pipeline.ExportDirectorySnapshot(volume, L"C:"),
                              L"blocked-restart.txt"),
              "the failed restart fixture retains the row before shutdown");
        pipeline.Close();
    }

    IndexPipeline restarted;
    Check(restarted.Open(dbPath), "restart rule-pruning fixture reopens");
    restarted.SetVolumeRules(volume, setting);
    int callbackCount = 0;
    restarted.RebuildAll([&callbackCount](VolumeRowId) { ++callbackCount; });
    Check(callbackCount == 1,
          "a restart rebuilds and reports the outstanding pruning obligation");
    Check(!SnapshotHasName(restarted.ExportDirectorySnapshot(volume, L"C:"),
                           L"blocked-restart.txt"),
          "restart recovery removes the failed-prune row from the projection");
    const auto aggregate = restarted.GetFolderAggregate(volume, FileId{1, 0});
    Check(aggregate.has_value() && aggregate->itemCount == 0,
          "restart recovery leaves the folder aggregate consistent");
    const auto search = SearchForName(restarted, 906, L"blocked-restart");
    Check(search.status == ffprotocol::IndexedSearchStatus::Completed &&
              search.candidates.empty(),
          "restart recovery leaves no indexed-search result for the pruned row");
    const auto durable = ReadDurableEntryState(dbPath, volume, target);
    Check(durable.opened && durable.entryCount == 1 && !durable.hasTarget,
          "restart recovery removes the durable failed-prune row");
    restarted.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + "-shm");
}

// index-durability-false-success-closure 6.3 (PA-002): a file created by a
// journal delta while a reconciliation scan is running has already had its
// USN event consumed -- if the pass's seen-set only counts scan records,
// Finish removes the brand-new row as "stale" and nothing ever re-observes
// it. Journal upserts must count as observed, exactly like scan records.
void TestUsnUpsertDuringReconciliationPassSurvivesFinish() {
    const std::string dbPath = FreshDbPath("engine_test_usn_seen_reconcile.db");
    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "usn-seen reconciliation fixture opens");
    VolumeKey key;
    key.serialNumber = 97;
    const auto vol = pipeline.ResolveVolume(key);
    Check(pipeline.ApplyMftBatch(vol, {
        MakeMftRecord(5, 5, u"", 0x10, 0),
        MakeMftRecord(100, 5, u"Users", 0x10, 0),
    }), "usn-seen fixture seeds its root tree");

    pipeline.BeginReconciliationPass(vol);
    Check(pipeline.ApplyMftBatch(vol, {
        MakeMftRecord(5, 5, u"", 0x10, 0),
        MakeMftRecord(100, 5, u"Users", 0x10, 0),
    }), "the reconciliation scan re-observes the existing tree");
    Check(pipeline.ApplyUsnBatch(vol, {MakeUsnRecord(7, 150, 100, u"created-during-pass.txt", 8)}),
          "a journal delta creates a new file mid-pass");
    pipeline.FinishReconciliationPass(vol);
    Check(!pipeline.IsReconciliationPassActive(vol), "the pass finishes");

    const auto snapshot = pipeline.ExportDirectorySnapshot(vol, L"C:");
    Check(SnapshotHasName(snapshot, L"created-during-pass.txt"),
          "a journal-created entry is NOT deleted as stale by the pass that raced it (PA-002)");
    const auto durable = ReadDurableEntryState(dbPath, vol, FileId{150, 0});
    Check(durable.opened && durable.hasTarget && durable.entryCount == 3,
          "the mid-pass creation survives finalization in the durable store too");

    // And the normal correction path still works afterwards: a genuinely
    // gone row (never re-observed, no journal event) is still pruned.
    pipeline.BeginReconciliationPass(vol);
    Check(pipeline.ApplyMftBatch(vol, {MakeMftRecord(5, 5, u"", 0x10, 0)}),
          "the follow-up pass omits both former children");
    pipeline.FinishReconciliationPass(vol);
    const auto pruned = pipeline.ExportDirectorySnapshot(vol, L"C:");
    Check(!SnapshotHasName(pruned, L"Users") && !SnapshotHasName(pruned, L"created-during-pass.txt"),
          "seen-set semantics did not disable missed-delete self-correction");
    pipeline.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + "-shm");
}

// The exclusion half of the seen-set rule: a journal upsert that the
// volume's rules exclude must never enter the seen-set -- rule-excluded
// rows stay owed for removal exactly as in the MFT path.
void TestRuleExcludedUsnUpsertStaysOutOfSeenSet() {
    const std::string dbPath = FreshDbPath("engine_test_usn_excluded_reconcile.db");
    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "usn-excluded reconciliation fixture opens");
    VolumeKey key;
    key.serialNumber = 98;
    const auto vol = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    setting.rules.push_back({L"C:\\excluded-during-pass.txt", false});
    pipeline.SetVolumeRules(vol, setting);
    Check(pipeline.ApplyMftBatch(vol, {MakeMftRecord(1, 1, u"", 0x10, 0)}),
          "usn-excluded fixture seeds its root");

    pipeline.BeginReconciliationPass(vol);
    Check(pipeline.ApplyUsnBatch(vol, {MakeUsnRecord(9, 160, 1, u"excluded-during-pass.txt", 3)}),
          "a rule-excluded journal upsert applies during the pass");
    pipeline.FinishReconciliationPass(vol);
    const auto snapshot = pipeline.ExportDirectorySnapshot(vol, L"C:");
    Check(!SnapshotHasName(snapshot, L"excluded-during-pass.txt"),
          "a rule-excluded mid-pass upsert is not retained by the seen-set");
    const auto durable = ReadDurableEntryState(dbPath, vol, FileId{160, 0});
    Check(durable.opened && !durable.hasTarget,
          "the rule-excluded mid-pass row is not durable after finalization");
    pipeline.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + "-shm");
}

// D3/D5: an aborted pass (ScanFailed / paused / batch-failed completion)
// must drop the pass with zero removals and no searchable-epoch change --
// the whole point is that a partial scan's seen-set never drives deletions.
void TestAbortReconciliationPassDropsWithoutRemovals() {
    const std::string dbPath = FreshDbPath("engine_test_abort_reconcile.db");
    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "abort-reconciliation fixture opens");
    VolumeKey key;
    key.serialNumber = 99;
    const auto vol = pipeline.ResolveVolume(key);
    Check(pipeline.ApplyMftBatch(vol, {
        MakeMftRecord(5, 5, u"", 0x10, 0),
        MakeMftRecord(100, 5, u"live.txt", 0, 10),
    }), "abort fixture seeds a row the partial scan will miss");

    pipeline.BeginReconciliationPass(vol);
    Check(pipeline.ApplyMftBatch(vol, {MakeMftRecord(5, 5, u"", 0x10, 0)}),
          "the interrupted scan observed only the root");
    const uint64_t epochBeforeAbort = pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch;
    pipeline.AbortReconciliationPass(vol);
    Check(!pipeline.IsReconciliationPassActive(vol), "the pass is no longer active after abort");

    const auto snapshot = pipeline.ExportDirectorySnapshot(vol, L"C:");
    Check(SnapshotHasName(snapshot, L"live.txt"),
          "abort removes nothing -- the unobserved-but-live row survives");
    const auto durable = ReadDurableEntryState(dbPath, vol, FileId{100, 0});
    Check(durable.opened && durable.hasTarget && durable.entryCount == 2,
          "abort leaves the durable row set completely untouched");
    Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch == epochBeforeAbort,
          "abort is not a searchable mutation -- no epoch advance");

    // A later full reconciliation still finalizes normally.
    pipeline.BeginReconciliationPass(vol);
    Check(pipeline.ApplyMftBatch(vol, {
        MakeMftRecord(5, 5, u"", 0x10, 0),
        MakeMftRecord(100, 5, u"live.txt", 0, 10),
    }), "the retried pass re-observes everything");
    pipeline.FinishReconciliationPass(vol);
    Check(!pipeline.IsReconciliationPassActive(vol), "the retried pass completes normally");
    pipeline.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + "-shm");
}

// large-index-lifecycle-and-search-scalability review-lane D P1-1 / lane C
// Finding 1: memory-pressure enforcement triggered by an UNRELATED batch can
// evict and compact any volume. That structural change must surface as a
// query-visible residency change (the same ProjectionResidency epoch
// mechanism UnloadVolumeFromProjection uses), so in-flight and future
// searches re-capture or cold-stream instead of silently serving incomplete
// results as Completed.
void TestEnforcementEvictionAdvancesQueryVisibleEpochs() {
    IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_enforcement_epochs.db")),
          "enforcement-epoch pipeline opens");
    VolumeKey firstKey;
    firstKey.serialNumber = 313;
    const auto firstVolume = pipeline.ResolveVolume(firstKey);
    ffprotocol::VolumeSetting firstSetting;
    firstSetting.key = L"P:";
    pipeline.SetVolumeRules(firstVolume, firstSetting);
    VolumeKey secondKey;
    secondKey.serialNumber = 314;
    const auto secondVolume = pipeline.ResolveVolume(secondKey);
    ffprotocol::VolumeSetting secondSetting;
    secondSetting.key = L"Q:";
    pipeline.SetVolumeRules(secondVolume, secondSetting);
    Check(pipeline.ApplyMftBatch(firstVolume, {
        MakeMftRecord(5, 5, u".", 0x10, 0),
        MakeMftRecord(10, 5, u"first-victim.txt", 0, 10),
    }), "first volume commits (oldest LRU timestamps)");
    Check(pipeline.ApplyMftBatch(secondVolume, {
        MakeMftRecord(5, 5, u".", 0x10, 0),
        MakeMftRecord(20, 5, u"second-live.txt", 0, 10),
    }), "second volume commits");
    // A tiny budget forces enforcement during the NEXT batch to evict the
    // oldest victim (first volume's subtree).
    pipeline.SetProjectionMemoryBudget(512);
    const uint64_t before = pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch;
    Check(pipeline.ApplyMftBatch(secondVolume, {
        MakeMftRecord(21, 5, u"second-live-two.txt", 0, 10),
    }), "the unrelated batch commits");
    Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch > before,
          "enforcement-triggered eviction advances query-visible epochs "
          "(in-flight searches must re-capture, never truncate silently)");
    Check(pipeline.IsVolumeEvicted(firstVolume),
          "the LRU victim's volume is cold-marked by the enforcement pass");
    // The evicted volume's rows must still be findable through the cold
    // stream -- completeness preserved across the residency change.
    ffprotocol::IndexedSearchRequest request;
    request.requestId = 991;
    request.scope = 3;
    request.maxResults = 64;
    request.query = L"first-victim";
    IndexPipeline::SearchTimings timings;
    const auto response = pipeline.SearchIndexed(request, &timings);
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed,
          "search over the evicted volume completes through the cold stream");
    Check(response.candidates.size() == 1 &&
              response.candidates.front().name == L"first-victim.txt",
          "the evicted volume's rows remain complete in results");
    pipeline.Close();
}

int main() {

    TestBulkIngestionDefersAnalyzeToMaintenance();
    TestVolumeResolutionIsStable();
    TestApplyMftBatchAndExportSnapshot();
    TestMftV2BatchIngestsAllocatedSizeAndExportsToSnapshot();
    TestUsnV3PreservesAndCorrectsAllocatedSize();
    TestRebuildAfterRestartMatchesPriorState();
    TestReconciliationRemovesEntryNotSeenInFullScan();
    TestUsnDeleteReasonRemovesEntry();
    TestUsnPartialUpdatesPreserveAuthoritativeMetadataAcrossRestart();
    TestNewUsnOnlyEntryPersistsMftEnrichedMetadata();
    TestVolumeAvailabilityAndForget();
    TestForgetVolumePurgesProjection();
    TestIndexedSearchUsesProjectionPathsAndScopes();
    TestIndexedSearchViewFirstCaptureReducesMaterialization();
    TestIndexedSearchCancellationStopsProjectionWork();
        TestIndexedSearchPropagatesAllocatedSize();
        TestIndexedSearchOndiskFilter();
    TestRootChildFoldersRemainAbsoluteAcrossResidency();
        TestForcedEvictionSearchEquivalence();
        TestEnforcementEvictionAdvancesQueryVisibleEpochs();
    TestCappedRootSearchMatchesColdAndRetainsTopN();
        TestRepeatedColdSearchesDoNotThrashMemory();
        TestColdMutationsAndRestartEquivalence();
        TestColdFolderAggregateServedFromStore();
        TestCancellationDuringColdStream();
    TestMftSecondaryFailureIsFailClosedAndRetryable();
    TestUsnSecondaryFailureIsBoundarySpecificAndRetryable();
    TestRebuildSecondaryFailureDoesNotPublishSuccess();
    TestReconciliationRetentionOnSecondaryFailure();
    TestRepeatedSecondaryFailure();
    TestReconciliationMassRemovalConvergesAcrossChunks();
    TestRulePruningFailureRecoversAfterCloseAndOpen();
    TestUsnUpsertDuringReconciliationPassSurvivesFinish();
    TestRuleExcludedUsnUpsertStaysOutOfSeenSet();
    TestAbortReconciliationPassDropsWithoutRemovals();

    if (fftest::FailureCount() > 0) {
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
