#include <algorithm>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

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

void TestBulkInsertTriggersAnalyzeAtTenThousandRecords() {
    const std::string dbPath = FreshDbPath("engine_test_analyze_threshold.db");
    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "the pipeline opens for ANALYZE threshold validation");
    VolumeKey key;
    key.serialNumber = 1;
    const auto volume = pipeline.ResolveVolume(key);

    const auto applyBatches = [&](ffindexstore::VolumeRowId targetVolume, uint64_t firstId, size_t batchCount,
                                  size_t recordsPerBatch, const std::u16string& name) {
        for (size_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
            std::vector<ffprotocol::MftRecordV1> batch;
            batch.reserve(recordsPerBatch);
            for (size_t recordIndex = 0; recordIndex < recordsPerBatch; ++recordIndex) {
                const uint64_t offset = static_cast<uint64_t>(batchIndex * recordsPerBatch + recordIndex);
                const uint64_t id = firstId;
                batch.push_back(MakeMftRecord(id, id, name, 0, offset));
            }
            if (!pipeline.ApplyMftBatch(targetVolume, batch)) {
                return false;
            }
        }
        return true;
    };

    Check(applyBatches(volume, 1'000, 1, 9'999, u"entry"),
          "9,999 records apply in one ingestion batch");
    pipeline.Close();
    Check(!HasSqlitePlannerStats(dbPath), "ANALYZE is not triggered before 10,000 inserted records");

    Check(pipeline.Open(dbPath), "the pipeline reopens for the threshold-crossing batches");
    const auto reopenedVolume = pipeline.ResolveVolume(key);
    Check(applyBatches(reopenedVolume, 20'000, 1, 10'000, u"threshold"),
          "10,000 records apply in one ingestion batch");
    pipeline.Close();
    Check(HasSqlitePlannerStats(dbPath), "ANALYZE creates planner statistics at 10,000 inserted records");
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

} // namespace

int main() {
    TestBulkInsertTriggersAnalyzeAtTenThousandRecords();
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
    TestIndexedSearchCancellationStopsProjectionWork();

    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
