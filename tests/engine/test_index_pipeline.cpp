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
    }), "the cold volume fixture persists through normal ingestion");
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
        Check(response.status == ffprotocol::IndexedSearchStatus::Completed,
              "reference-pass indexed searches complete");
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
        Check(response.status == ffprotocol::IndexedSearchStatus::Completed,
              "forced-eviction indexed searches complete");
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
    std::atomic<size_t> cancellationChecks{0};
    IndexPipeline::SearchTimings timings;
    const auto response = pipeline.SearchIndexed(request, &timings, [&cancellationChecks] {
        return cancellationChecks.fetch_add(1, std::memory_order_relaxed) >= 4;
    });
    Check(response.status == ffprotocol::IndexedSearchStatus::Cancelled,
          "cancelling during cold streaming reports Cancelled");
    Check(timings.cancelled && response.candidates.empty(),
          "a cancelled cold search discards partial results");
    Check(timings.coldRowsExamined < records.size(),
          "cold streaming stops before exhausting the volume when superseded");
    Check(pipeline.IsVolumeEvicted(volume),
          "a cancelled cold search does not silently hydrate the volume");
}

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
    TestIndexedSearchViewFirstCaptureReducesMaterialization();
    TestIndexedSearchCancellationStopsProjectionWork();
        TestIndexedSearchPropagatesAllocatedSize();
        TestIndexedSearchOndiskFilter();
        TestForcedEvictionSearchEquivalence();
        TestRepeatedColdSearchesDoNotThrashMemory();
        TestColdMutationsAndRestartEquivalence();
        TestColdFolderAggregateServedFromStore();
        TestCancellationDuringColdStream();

    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
