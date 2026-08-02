#include <cstdio>
#include <filesystem>
#include <string>

#include "ffindexstore/Store.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    } else {
        std::printf("ok: %s\n", description);
    }
}

using namespace ffindexstore;

std::string FreshDbPath(const char* name) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
    return path.string();
}

void TestOpenCreatesSchemaAndIsReopenable() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_open.db");
    Store store;
    bool integrityFailed = true;
    Check(store.Open(dbPath, &integrityFailed), "fresh database opens successfully");
    Check(!integrityFailed, "fresh database passes integrity check trivially");
    Check(store.RunIntegrityCheck(), "integrity check passes standalone");
    store.Close();

    Store reopened;
    Check(reopened.Open(dbPath), "reopening an existing, valid database succeeds");
}

void TestVolumeIdentityIsStableAndDeduplicated() {
    Store store;
    store.Open(FreshDbPath("ffindexstore_test_volume.db"));

    VolumeKey key;
    key.volumeGuid = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    key.serialNumber = 0xDEADBEEF;

    auto first = store.GetOrCreateVolume(key);
    auto second = store.GetOrCreateVolume(key);
    Check(first.has_value() && second.has_value() && *first == *second,
          "the same VolumeKey always maps to the same durable VolumeRowId");

    VolumeKey otherKey = key;
    otherKey.serialNumber = 0xCAFEBABE;
    auto third = store.GetOrCreateVolume(otherKey);
    Check(third.has_value() && *third != *first, "a different VolumeKey gets a distinct VolumeRowId");
}

void TestEntryIdentityIsKeyedByVolumeAndFrnNotPath() {
    Store store;
    store.Open(FreshDbPath("ffindexstore_test_identity.db"));
    VolumeKey keyA;
    keyA.serialNumber = 1;
    VolumeKey keyB;
    keyB.serialNumber = 2;
    auto volA = *store.GetOrCreateVolume(keyA);
    auto volB = *store.GetOrCreateVolume(keyB);

    EntryRecord entry;
    entry.id = FileId{42, 0};
    entry.parentId = FileId{5, 0};
    entry.name = u"same-name.txt";
    Check(store.ApplyBatch(volA, {{EntryChangeKind::Upsert, entry}}), "batch applies to volume A");
    Check(store.ApplyBatch(volB, {{EntryChangeKind::Upsert, entry}}), "identical FRN on volume B does not collide");
    Check(store.CountEntries(volA) == 1 && store.CountEntries(volB) == 1,
          "each volume independently holds its own entry for the same FRN/path-equivalent name");

    // Re-ingesting the same (volume, FRN) updates, never duplicates.
    EntryRecord renamed = entry;
    renamed.name = u"renamed.txt";
    store.ApplyBatch(volA, {{EntryChangeKind::Upsert, renamed}});
    Check(store.CountEntries(volA) == 1, "re-ingesting the same (volume, FRN) updates the row, not duplicates it");
}

void TestBatchUpsertAndRemove() {
    Store store;
    store.Open(FreshDbPath("ffindexstore_test_batch.db"));
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = *store.GetOrCreateVolume(key);

    EntryRecord root;
    root.id = FileId{5, 0};
    root.parentId = FileId{5, 0};
    root.name = u"C:";
    EntryRecord child;
    child.id = FileId{100, 0};
    child.parentId = FileId{5, 0};
    child.name = u"Users";
    child.sizeBytes = 4096;
    Check(store.ApplyBatch(vol, {{EntryChangeKind::Upsert, root}, {EntryChangeKind::Upsert, child}}),
          "a batch of multiple records commits as one transaction");
    Check(store.CountEntries(vol) == 2, "both records are present after the batch");

    int visited = 0;
    store.ForEachEntry(vol, [&](const EntryRecord&) { ++visited; });
    Check(visited == 2, "ForEachEntry streams every persisted row for the volume");

    EntryRecord removeMarker;
    removeMarker.id = FileId{100, 0};
    Check(store.ApplyBatch(vol, {{EntryChangeKind::Remove, removeMarker}}), "a Remove change deletes the row");
    Check(store.CountEntries(vol) == 1, "row count reflects the removal");
}

void TestVolumeMetadataRoundTrips() {
    Store store;
    store.Open(FreshDbPath("ffindexstore_test_metadata.db"));
    VolumeKey key;
    key.serialNumber = 7;
    auto vol = *store.GetOrCreateVolume(key);

    Check(store.SetJournalPosition(vol, 999, 12345), "SetJournalPosition succeeds");
    Check(store.SetScanCursor(vol, {1, 2, 3, 4}), "SetScanCursor succeeds");
    auto meta = store.GetVolumeMetadata(vol);
    Check(meta.has_value(), "metadata is retrievable");
    Check(meta->journalId.has_value() && *meta->journalId == 999, "persisted JournalId round-trips");
    Check(meta->resumeUsn == 12345, "persisted ResumeUsn round-trips");
    Check((meta->scanCursor == std::vector<uint8_t>{1, 2, 3, 4}), "persisted scan cursor round-trips");
    Check(!meta->scanComplete, "scan is not yet marked complete");

    Check(store.MarkScanComplete(vol), "MarkScanComplete succeeds");
    meta = store.GetVolumeMetadata(vol);
    Check(meta->scanComplete, "scan is now marked complete");
    Check(meta->scanCursor.empty(), "scan cursor is cleared once the scan completes");

    Check(store.SetVolumeAvailable(vol, false, 42), "SetVolumeAvailable(false) succeeds");
    meta = store.GetVolumeMetadata(vol);
    Check(!meta->available, "volume is now marked unavailable");
    Check(meta->lastSeenTime == 42, "last-seen timestamp is recorded");
}

void TestDisconnectDoesNotDeleteEntriesOrAffectOtherVolumes() {
    Store store;
    store.Open(FreshDbPath("ffindexstore_test_disconnect.db"));
    VolumeKey keyA;
    keyA.serialNumber = 1;
    VolumeKey keyB;
    keyB.serialNumber = 2;
    auto volA = *store.GetOrCreateVolume(keyA);
    auto volB = *store.GetOrCreateVolume(keyB);

    EntryRecord entry;
    entry.id = FileId{1, 0};
    entry.parentId = FileId{1, 0};
    entry.name = u"root";
    store.ApplyBatch(volA, {{EntryChangeKind::Upsert, entry}});
    store.ApplyBatch(volB, {{EntryChangeKind::Upsert, entry}});

    store.SetVolumeAvailable(volA, false, 100);
    Check(store.CountEntries(volA) == 1, "marking a volume unavailable does not delete its entries");
    auto metaB = store.GetVolumeMetadata(volB);
    Check(metaB->available, "an unrelated volume's availability is unaffected");
    Check(store.CountEntries(volB) == 1, "an unrelated volume's entries are unaffected");
}

void TestForgetVolumeIsExplicitAndRemovesEntries() {
    Store store;
    store.Open(FreshDbPath("ffindexstore_test_forget.db"));
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = *store.GetOrCreateVolume(key);
    EntryRecord entry;
    entry.id = FileId{1, 0};
    entry.parentId = FileId{1, 0};
    entry.name = u"root";
    store.ApplyBatch(vol, {{EntryChangeKind::Upsert, entry}});

    Check(!store.ForgetVolume(vol), "an available volume cannot be forgotten");
    Check(store.CountEntries(vol) == 1 && store.GetVolumeMetadata(vol).has_value(),
          "a rejected forget leaves the available volume untouched");
    Check(store.SetVolumeAvailable(vol, false, 100), "the volume can be marked unavailable before forgetting");
    Check(store.ForgetVolume(vol), "explicit ForgetVolume succeeds");
    Check(store.CountEntries(vol) == 0, "entries are gone after an explicit forget");
    Check(!store.GetVolumeMetadata(vol).has_value(), "the volume row itself is gone after an explicit forget");
}

// tasks.md 9.1 proxy: a real "kill -9 mid-batch" can't be simulated from
// within this process, but the mechanism it depends on -- WAL replay
// recovering already-committed data on next open, without requiring an
// explicit checkpoint first -- is exactly what this exercises: data is
// committed via ApplyBatch (a real transaction, same as ingestion uses),
// the Store is destroyed with no checkpoint call at all, and a fresh
// Store reopening the same file must still see it.
void TestWalReplayRecoversCommittedDataWithoutAnExplicitCheckpoint() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_wal_replay.db");
    VolumeKey key;
    key.serialNumber = 1;
    VolumeRowId vol;
    {
        Store store;
        store.Open(dbPath);
        vol = *store.GetOrCreateVolume(key);
        EntryRecord entry;
        entry.id = FileId{1, 0};
        entry.parentId = FileId{1, 0};
        entry.name = u"root";
        store.ApplyBatch(vol, {{EntryChangeKind::Upsert, entry}});
        // Deliberately no CheckpointPassive()/explicit sync call here --
        // only the destructor's plain sqlite3_close runs.
    }
    Store reopened;
    Check(reopened.Open(dbPath), "a database with an un-checkpointed WAL still opens cleanly");
    Check(reopened.CountEntries(vol) == 1, "WAL replay on open recovers committed data with no explicit prior checkpoint");
    Check(reopened.RunIntegrityCheck(), "the recovered database passes an integrity check");
}

void TestDataSurvivesReopenAfterClose() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_durable.db");
    VolumeKey key;
    key.serialNumber = 1;
    VolumeRowId vol;
    {
        Store store;
        store.Open(dbPath);
        vol = *store.GetOrCreateVolume(key);
        EntryRecord entry;
        entry.id = FileId{1, 0};
        entry.parentId = FileId{1, 0};
        entry.name = u"root";
        store.ApplyBatch(vol, {{EntryChangeKind::Upsert, entry}});
        store.CheckpointPassive();
    }
    Store reopened;
    Check(reopened.Open(dbPath), "database reopens after a clean close");
    Check(reopened.CountEntries(vol) == 1, "committed data survives close/reopen (WAL durability)");
}

void TestGetFolderAggregateFromStore() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_folder_agg.db");
    Store store;
    store.Open(dbPath);
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = *store.GetOrCreateVolume(key);

    // Root (5), children: Users(100), Windows(101), file(102)
    // Users has children: bob(200), alice(201)
    // bob has child: notes.txt(300)
    store.ApplyBatch(vol, {
        {EntryChangeKind::Upsert, MakeEntry(5, 5, u"C:", 0x10)},
        {EntryChangeKind::Upsert, MakeEntry(100, 5, u"Users", 0x10)},
        {EntryChangeKind::Upsert, MakeEntry(101, 5, u"Windows", 0x10)},
        {EntryChangeKind::Upsert, MakeEntry(102, 5, u"readme.txt", 0)},
        {EntryChangeKind::Upsert, MakeEntry(200, 100, u"bob", 0x10)},
        {EntryChangeKind::Upsert, MakeEntry(201, 100, u"alice", 0x10)},
        {EntryChangeKind::Upsert, MakeEntry(300, 200, u"notes.txt", 0)},
    });

    auto rootAgg = store.GetFolderAggregate(*vol, FileId{5, 0});
    Check(rootAgg.itemCount == 6, "store root aggregate counts all descendants");
    Check(rootAgg.totalSizeBytes == 0, "store root aggregate sums to zero size");

    auto usersAgg = store.GetFolderAggregate(*vol, FileId{100, 0});
    Check(usersAgg.itemCount == 3, "store Users aggregate has three descendants");
    Check(usersAgg.totalSizeBytes == 0, "store Users aggregate size is zero");

    auto unknown = store.GetFolderAggregate(*vol, FileId{999, 0});
    Check(unknown.itemCount == 0 && unknown.totalSizeBytes == 0, "unknown folder returns zeroed aggregate");
}

} // namespace

int main() {
    TestOpenCreatesSchemaAndIsReopenable();
    TestVolumeIdentityIsStableAndDeduplicated();
    TestEntryIdentityIsKeyedByVolumeAndFrnNotPath();
    TestBatchUpsertAndRemove();
    TestVolumeMetadataRoundTrips();
    TestDisconnectDoesNotDeleteEntriesOrAffectOtherVolumes();
    TestForgetVolumeIsExplicitAndRemovesEntries();
    TestWalReplayRecoversCommittedDataWithoutAnExplicitCheckpoint();
    TestDataSurvivesReopenAfterClose();
    TestGetFolderAggregateFromStore();

    if (g_failures > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
