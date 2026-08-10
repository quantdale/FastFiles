#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

#include "IndexPipeline.h"
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

void TestReconciliationRemovesEntryNotSeenInFullScan() {
    IndexPipeline pipeline;
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
    auto result = pipeline.SearchIndexed(all);
    Check(result.status == ffprotocol::IndexedSearchStatus::Completed && result.candidates.size() == 1 &&
              result.candidates[0].name == L"report.txt" && result.candidates[0].folder == L"C:\\Users" &&
              result.candidates[0].persistentIndexed,
          "indexed search returns a durable candidate with its reconstructed parent folder");

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
    result = pipeline.SearchIndexed(all);
    Check(result.status == ffprotocol::IndexedSearchStatus::Completed && result.candidates.size() == 1 &&
              result.candidates[0].name == L"nested.txt" && result.candidates[0].folder == L"C:\\Users\\Alice",
          "a folder-only query matches a parent path when the filename itself does not match");

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

} // namespace

int main() {
    TestVolumeResolutionIsStable();
    TestApplyMftBatchAndExportSnapshot();
    TestRebuildAfterRestartMatchesPriorState();
    TestReconciliationRemovesEntryNotSeenInFullScan();
    TestUsnDeleteReasonRemovesEntry();
    TestVolumeAvailabilityAndForget();
    TestForgetVolumePurgesProjection();
    TestIndexedSearchUsesProjectionPathsAndScopes();

    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
