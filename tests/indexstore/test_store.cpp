#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>

#include "sqlite3.h"

#include "ffindexstore/Store.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {
using namespace ffindexstore;

EntryRecord MakeEntry(uint64_t id, uint64_t parent, std::u16string name, uint32_t attributes = 0) {
    EntryRecord r;
    r.id = FileId{id, 0};
    r.parentId = FileId{parent, 0};
    r.name = std::move(name);
    r.attributes = attributes;
    return r;
}

// Builds a schema-version-1 database exactly as the pre-allocated-size
// store created it (volumes + entries without allocated_size_bytes,
// user_version=1), including one row, so Store::Open's in-place migration
// can be exercised against a faithful historical fixture.
bool CreateVersion1Fixture(const std::string& dbPath) {
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        return false;
    }
    const char* schemaV1 =
        "CREATE TABLE volumes ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  volume_guid BLOB NOT NULL,"
        "  serial_number INTEGER NOT NULL,"
        "  available INTEGER NOT NULL DEFAULT 1,"
        "  last_seen_time INTEGER NOT NULL DEFAULT 0,"
        "  journal_id INTEGER,"
        "  resume_usn INTEGER NOT NULL DEFAULT 0,"
        "  scan_cursor BLOB,"
        "  scan_complete INTEGER NOT NULL DEFAULT 0,"
        "  last_reconciliation_time INTEGER NOT NULL DEFAULT 0,"
        "  UNIQUE(volume_guid, serial_number)"
        ");"
        "CREATE TABLE entries ("
        "  volume_id INTEGER NOT NULL REFERENCES volumes(id),"
        "  frn_low INTEGER NOT NULL,"
        "  frn_high INTEGER NOT NULL DEFAULT 0,"
        "  parent_frn_low INTEGER NOT NULL,"
        "  parent_frn_high INTEGER NOT NULL DEFAULT 0,"
        "  name TEXT NOT NULL,"
        "  size_bytes INTEGER NOT NULL DEFAULT 0,"
        "  creation_time INTEGER NOT NULL DEFAULT 0,"
        "  last_modified_time INTEGER NOT NULL DEFAULT 0,"
        "  last_access_time INTEGER NOT NULL DEFAULT 0,"
        "  attributes INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (volume_id, frn_low, frn_high)"
        ") WITHOUT ROWID;"
        "INSERT INTO volumes(id, volume_guid, serial_number) VALUES (1, x'0102030405060708090A0B0C0D0E0F10', 42);"
        "INSERT INTO entries(volume_id, frn_low, frn_high, parent_frn_low, parent_frn_high, name, size_bytes) "
        "  VALUES (1, 100, 0, 5, 0, 'legacy.txt', 4096);"
        "PRAGMA user_version=1;";
    const bool ok = sqlite3_exec(db, schemaV1, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(db);
    return ok;
}

// allocated-size-storage-accuracy 3.3: a version-1 database must migrate
// in place -- no rebuild, no data loss -- and pre-existing rows must read
// back as unknown (NULL), never as fabricated zeros.
void TestSchemaV1ToV2MigrationInPlace() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_migrate_v1.db");
    Check(CreateVersion1Fixture(dbPath), "version-1 fixture database is created");

    Store store;
    bool integrityFailed = false;
    Check(store.Open(dbPath, &integrityFailed), "a version-1 database opens and migrates in place");
    Check(!integrityFailed, "integrity check passes before/after migration");
    Check(store.RunIntegrityCheck(), "post-migration integrity check passes");

    Check(store.CountEntries(1) == 1, "the migrated database retains its existing row");

    EntryRecord legacy;
    Check(store.ForEachEntry(1, [&](const EntryRecord& entry) { legacy = entry; }),
          "the migrated row streams normally");
    Check(legacy.name == u"legacy.txt" && legacy.sizeBytes == 4096,
          "the migrated row retains its pre-existing fields");
    Check(!legacy.allocatedSizeKnown && legacy.allocatedSizeBytes == 0,
          "a row that predates the feature reads back as unknown, not zero");

    // New rows written after migration carry allocated size normally.
    EntryRecord fresh = MakeEntry(200, 5, u"fresh.bin");
    fresh.sizeBytes = 100;
    fresh.allocatedSizeBytes = 64;
    fresh.allocatedSizeKnown = true;
    Check(store.ApplyBatch(1, {{EntryChangeKind::Upsert, fresh}}),
          "post-migration rows upsert with allocated size");
    EntryRecord freshRead;
    int seen = 0;
    store.ForEachEntry(1, [&](const EntryRecord& entry) {
        if (entry.id.low == 200) freshRead = entry;
        ++seen;
    });
    Check(seen == 2 && freshRead.allocatedSizeKnown && freshRead.allocatedSizeBytes == 64,
          "post-migration rows round-trip allocated size as known");

    store.Close();
    Store reopened;
    Check(reopened.Open(dbPath), "the migrated database reopens");
    EntryRecord legacyAfterReopen;
    reopened.ForEachEntry(1, [&](const EntryRecord& entry) {
        if (entry.id.low == 100) legacyAfterReopen = entry;
    });
    Check(!legacyAfterReopen.allocatedSizeKnown,
          "the legacy row's unknown state survives a restart round trip");
}

// allocated-size-storage-accuracy 3.3: unknown must never be confused with
// a genuine zero, and a partial update lacking the allocated-size bit must
// preserve a previously known value.
void TestAllocatedSizeUnknownVsZeroAndPreservation() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_allocated.db");
    Store store;
    Check(store.Open(dbPath), "allocated-size fixture opens");
    VolumeKey key;
    key.serialNumber = 3;
    const VolumeRowId vol = *store.GetOrCreateVolume(key);

    EntryRecord zero = MakeEntry(1, 5, u"empty.bin");
    zero.sizeBytes = 0;
    zero.allocatedSizeBytes = 0;
    zero.allocatedSizeKnown = true; // a genuine zero-byte file -- known zero
    Check(store.ApplyBatch(vol, {{EntryChangeKind::Upsert, zero}}),
          "a zero-byte file with known allocated size is stored");
    EntryRecord zeroRead;
    store.ForEachEntry(vol, [&](const EntryRecord& entry) { if (entry.id.low == 1) zeroRead = entry; });
    Check(zeroRead.allocatedSizeKnown && zeroRead.allocatedSizeBytes == 0,
          "known zero reads back as known zero -- distinct from unknown");

    EntryRecord unknown = MakeEntry(2, 5, u"unknown.bin");
    unknown.sizeBytes = 10;
    unknown.allocatedSizeKnown = false;
    Check(store.ApplyBatch(vol, {{EntryChangeKind::Upsert, unknown}}),
          "an entry with unknown allocated size is stored");
    EntryRecord unknownRead;
    store.ForEachEntry(vol, [&](const EntryRecord& entry) { if (entry.id.low == 2) unknownRead = entry; });
    Check(!unknownRead.allocatedSizeKnown,
          "unknown reads back as unknown (NULL on disk), never as zero");

    // A partial update (USN-style, no allocated-size bit) must preserve the
    // previously known value rather than erasing it with zero.
    EntryRecord partial = MakeEntry(1, 5, u"empty.bin");
    partial.sizeBytes = 5; // logical size changed
    partial.allocatedSizeBytes = 0; // absent bit -- value must not land
    partial.allocatedSizeKnown = false;
    const EntryFieldMask partialFields = kEntryFieldSizeBytes | kEntryFieldLastModifiedTime;
    Check(store.ApplyBatch(vol, {{EntryChangeKind::Upsert, partial, partialFields}}),
          "a partial update without the allocated-size bit is applied");
    EntryRecord afterPartial;
    store.ForEachEntry(vol, [&](const EntryRecord& entry) { if (entry.id.low == 1) afterPartial = entry; });
    Check(afterPartial.sizeBytes == 5, "present logical-size field replaces its prior value");
    Check(afterPartial.allocatedSizeKnown && afterPartial.allocatedSizeBytes == 0,
          "the absent allocated-size bit preserves the previously known value");

    // A full scan-style upsert with the bit set corrects stale values.
    EntryRecord full = MakeEntry(1, 5, u"empty.bin");
    full.sizeBytes = 7;
    full.allocatedSizeBytes = 4096;
    full.allocatedSizeKnown = true;
    Check(store.ApplyBatch(vol, {{EntryChangeKind::Upsert, full}}),
          "a full upsert with the bit set applies");
    EntryRecord afterFull;
    store.ForEachEntry(vol, [&](const EntryRecord& entry) { if (entry.id.low == 1) afterFull = entry; });
    Check(afterFull.allocatedSizeKnown && afterFull.allocatedSizeBytes == 4096,
          "reconciliation-style upsert corrects the stored allocated size");
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

void TestPartialUpsertPreservesAbsentAuthoritativeFields() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_partial_upsert.db");
    VolumeKey key;
    key.serialNumber = 9;
    const FileId id{101, 0};
    VolumeRowId volume = 0;
    {
        Store store;
        Check(store.Open(dbPath), "partial-update fixture opens");
        volume = *store.GetOrCreateVolume(key);

        EntryRecord full = MakeEntry(id.low, 100, u"original.txt");
        full.sizeBytes = 1234;
        full.creationTime = 111;
        full.lastModifiedTime = 222;
        full.lastAccessTime = 333;
        Check(store.ApplyBatch(volume, {{EntryChangeKind::Upsert, full}}),
              "a complete MFT-style record is stored before a delta");

        EntryRecord rename = MakeEntry(id.low, 100, u"renamed.txt");
        rename.lastModifiedTime = 444;
        const EntryFieldMask renameFields = kEntryFieldParentId | kEntryFieldName |
            kEntryFieldLastModifiedTime;
        Check(store.ApplyBatch(volume, {{EntryChangeKind::Upsert, rename, renameFields}}),
              "a rename delta applies with an explicit partial field mask");

        EntryRecord observed;
        Check(store.ForEachEntry(volume, [&](const EntryRecord& entry) { observed = entry; }),
              "the partial-update row can be read back");
        Check(observed.name == u"renamed.txt" && observed.lastModifiedTime == 444,
              "present rename fields replace their prior values");
        Check(observed.creationTime == 111 && observed.lastAccessTime == 333 && observed.sizeBytes == 1234,
              "absent creation/access/size fields retain authoritative values");
    }

    Store reopened;
    Check(reopened.Open(dbPath), "partial-update fixture reopens after restart");
    EntryRecord afterRestart;
    Check(reopened.ForEachEntry(volume, [&](const EntryRecord& entry) { afterRestart = entry; }),
          "the restarted store reads the merged row");
    Check(afterRestart.creationTime == 111 && afterRestart.lastAccessTime == 333 &&
              afterRestart.lastModifiedTime == 444,
          "merged timestamps survive SQLite close and reopen");
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

void TestAllVolumeMetadataCanSkipEntryCounts() {
    Store store;
    store.Open(FreshDbPath("ffindexstore_test_metadata_without_counts.db"));
    VolumeKey key;
    key.serialNumber = 8;
    const auto vol = *store.GetOrCreateVolume(key);
    EntryRecord entry;
    entry.id = FileId{100, 0};
    entry.parentId = FileId{100, 0};
    entry.name = u"root";
    Check(store.ApplyBatch(vol, {{EntryChangeKind::Upsert, entry}}),
          "metadata count probe stores an entry");

    const auto counted = store.GetAllVolumes();
    const auto uncounted = store.GetAllVolumesWithoutEntryCounts();
    Check(counted.size() == 1 && counted[0].entryCount == 1,
          "the existing all-volume metadata API retains entry counts");
    Check(uncounted.size() == 1 && uncounted[0].rowId == vol && uncounted[0].entryCount == 0,
          "the search metadata API returns identity without scanning entry counts");
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

void TestVacuumGatingByIdleIntervalAndGrowth() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_vacuum_gating.db");
    Store store;
    Check(store.Open(dbPath), "database opens for VACUUM gating");
    VolumeKey key;
    key.serialNumber = 1;
    const auto vol = *store.GetOrCreateVolume(key);

    constexpr uint64_t kFarFuture = std::numeric_limits<uint64_t>::max();
    Check(!store.VacuumIfDue(false, kFarFuture, 0, 0), "VACUUM requires an idle window");
    Check(!store.VacuumIfDue(true, kFarFuture, 0, 10), "VACUUM skips when the database has not grown");

    std::vector<EntryChange> changes;
    changes.reserve(1'000);
    const std::u16string longName(256, u'x');
    for (uint64_t index = 0; index < 1'000; ++index) {
        changes.push_back({EntryChangeKind::Upsert, MakeEntry(1'000 + index, 1'000 + index, longName)});
    }
    Check(store.ApplyBatch(vol, changes), "bulk rows apply for VACUUM growth gating");
    Check(store.CheckpointPassive(), "the growth fixture is checkpointed before VACUUM evaluation");
    Check(!store.VacuumIfDue(true, 0, 24, 10), "VACUUM honors the interval gate");
    Check(store.VacuumIfDue(true, kFarFuture, 0, 10), "VACUUM runs after idle, interval, and growth gates pass");
    Check(!store.VacuumIfDue(true, kFarFuture, 0, 10), "a successful VACUUM resets the growth baseline");
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

    auto rootAgg = store.GetFolderAggregate(vol, FileId{5, 0});
    Check(rootAgg.itemCount == 6, "store root aggregate counts all descendants");
    Check(rootAgg.totalSizeBytes == 0, "store root aggregate sums to zero size");

    auto usersAgg = store.GetFolderAggregate(vol, FileId{100, 0});
    Check(usersAgg.itemCount == 3, "store Users aggregate has three descendants");
    Check(usersAgg.totalSizeBytes == 0, "store Users aggregate size is zero");

    auto unknown = store.GetFolderAggregate(vol, FileId{999, 0});
    Check(unknown.itemCount == 0 && unknown.totalSizeBytes == 0, "unknown folder returns zeroed aggregate");
}

// allocated-size-storage-accuracy 3.3/D7: the store-side aggregate reports
// the allocated subtotal over known descendants plus the explicit
// completeness pair -- a partial subtotal is never presented as exact.
void TestGetFolderAggregateWithAllocatedSizeAndCompleteness() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_folder_agg_allocated.db");
    Store store;
    store.Open(dbPath);
    VolumeKey key;
    key.serialNumber = 4;
    auto vol = *store.GetOrCreateVolume(key);

    auto sized = [](uint64_t id, uint64_t parent, std::u16string name, uint64_t logical,
                    bool allocatedKnown, uint64_t allocated) {
        EntryRecord r = MakeEntry(id, parent, std::move(name));
        r.sizeBytes = logical;
        r.allocatedSizeKnown = allocatedKnown;
        r.allocatedSizeBytes = allocated;
        return r;
    };
    // Root(5) -> dir(100) -> {known(200): 10/8, unknown(201): 20/?}, file(102): 30/24
    store.ApplyBatch(vol, {
        {EntryChangeKind::Upsert, MakeEntry(5, 5, u"C:", 0x10)},
        {EntryChangeKind::Upsert, MakeEntry(100, 5, u"dir", 0x10)},
        {EntryChangeKind::Upsert, sized(200, 100, u"known.bin", 10, true, 8)},
        {EntryChangeKind::Upsert, sized(201, 100, u"unknown.bin", 20, false, 0)},
        {EntryChangeKind::Upsert, sized(102, 5, u"file.bin", 30, true, 24)},
    });

    const auto rootAgg = store.GetFolderAggregate(vol, FileId{5, 0});
    Check(rootAgg.itemCount == 4, "root aggregate counts every descendant including directories");
    Check(rootAgg.totalSizeBytes == 60, "root logical total sums all descendants");
    Check(rootAgg.allocatedBytesTotalCount == 4, "every descendant contributes to the total count");
    Check(rootAgg.allocatedBytesKnownCount == 2, "only known descendants contribute to the known count");
    Check(rootAgg.totalAllocatedSizeBytes == 32, "allocated subtotal covers only known descendants");

    const auto dirAgg = store.GetFolderAggregate(vol, FileId{100, 0});
    Check(dirAgg.itemCount == 2 && dirAgg.totalAllocatedSizeBytes == 8 && dirAgg.allocatedBytesKnownCount == 1 &&
              dirAgg.allocatedBytesTotalCount == 2,
          "a subtree with one unknown descendant reports a partial allocated subtotal");
}

} // namespace

int main() {
    TestOpenCreatesSchemaAndIsReopenable();
    TestVolumeIdentityIsStableAndDeduplicated();
    TestEntryIdentityIsKeyedByVolumeAndFrnNotPath();
    TestBatchUpsertAndRemove();
    TestPartialUpsertPreservesAbsentAuthoritativeFields();
    TestVolumeMetadataRoundTrips();
    TestAllVolumeMetadataCanSkipEntryCounts();
    TestDisconnectDoesNotDeleteEntriesOrAffectOtherVolumes();
    TestForgetVolumeIsExplicitAndRemovesEntries();
    TestWalReplayRecoversCommittedDataWithoutAnExplicitCheckpoint();
    TestDataSurvivesReopenAfterClose();
    TestVacuumGatingByIdleIntervalAndGrowth();
    TestGetFolderAggregateFromStore();
    TestGetFolderAggregateWithAllocatedSizeAndCompleteness();
    TestSchemaV1ToV2MigrationInPlace();
    TestAllocatedSizeUnknownVsZeroAndPreservation();

    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
