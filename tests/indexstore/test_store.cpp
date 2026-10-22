#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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

// harden-index-store-concurrent-access: build a u16string name from an id so
// each seeded/inserted row carries a distinct, human-readable name.
std::u16string U16Id(int64_t id) {
    if (id == 0) return u"0";
    bool negative = id < 0;
    uint64_t value = negative ? static_cast<uint64_t>(-id) : static_cast<uint64_t>(id);
    std::u16string reversed;
    while (value > 0) {
        reversed += static_cast<char16_t>(u'0' + static_cast<char16_t>(value % 10));
        value /= 10;
    }
    if (negative) reversed += u'-';
    std::u16string out;
    for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) out += *it;
    return out;
}

// Shared synchronization state between the reader thread (which holds a live
// SQLite read statement) and the writer thread (which performs the concurrent
// mutation/maintenance). Designed so a single Store object is used by only one
// thread at a time: `reader` is owned by the reader thread, `writer` by the
// caller/main thread. No fixed sleep establishes the overlap -- the barrier and
// watchdog are purely condition-variable/timed.
struct HeldReadState {
    std::mutex mutex;
    std::condition_variable cv;
    bool holding = false;  // reader has started and is holding its read statement
    bool release = false;  // caller asks the held reader to continue
    bool finished = false; // reader ForEachEntry has completed
    // (id, name) of every row the held reader observed, in visit order.
    std::vector<std::pair<int64_t, std::u16string>> seen;
};

// Runs ForEachEntry on the (reader-owned) Store, blocking inside the first
// visitor callback while the SQLite statement is still active, so the caller
// can perform a concurrent writer action. Records every observed row.
void HeldReadWorker(HeldReadState& state, Store& reader, VolumeRowId vol) {
    reader.ForEachEntry(vol, [&](const EntryRecord& entry) {
        {
            std::unique_lock<std::mutex> lock(state.mutex);
            state.seen.emplace_back(static_cast<int64_t>(entry.id.low), entry.name);
            if (!state.holding) {
                state.holding = true;
                state.cv.notify_all();
                // Keep the read statement live until the writer is done.
                state.cv.wait(lock, [&]() { return state.release; });
            }
        }
    });
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.finished = true;
        state.cv.notify_all();
    }
}

using Steady = std::chrono::steady_clock;
using Sec = std::chrono::seconds;

int64_t ToMs(Steady::time_point a, Steady::time_point b) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
}

double MedianMs(const std::vector<int64_t>& values) {
    if (values.empty()) return 0.0;
    std::vector<int64_t> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const size_t n = sorted.size();
    if (n % 2 == 1) return static_cast<double>(sorted[n / 2]);
    return (static_cast<double>(sorted[n / 2 - 1]) + static_cast<double>(sorted[n / 2])) / 2.0;
}

std::optional<std::u16string> NameOf(const HeldReadState& state, int64_t id) {
    for (const auto& pair : state.seen) {
        if (pair.first == id) return pair.second;
    }
    return std::nullopt;
}

// harden-index-store-concurrent-access: drives one held-read scenario. Waits
// (bounded) for the reader statement to be live, runs `action` on the writer
// connection while the read is held, releases the reader, and waits (bounded)
// for it to finish. Either timeout is a deterministic FAIL with diagnostics
// (the reader is force-released and joined first so CTest never hangs).
template <class Action>
bool DriveHeldRead(HeldReadState& state, std::thread& readerThread, Action action,
                   Sec barrierTimeout, Sec doneTimeout, const char* label) {
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        if (!state.cv.wait_for(lock, barrierTimeout, [&]() { return state.holding; })) {
            Check(false, (std::string("concurrency: ") + label +
                          ": held read statement never became live (deadlock/watchdog)").c_str());
            state.release = true;
            state.cv.notify_all();
            state.cv.wait(lock, [&]() { return state.finished; });
            if (readerThread.joinable()) readerThread.join();
            return false;
        }
    }
    action();
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.release = true;
        state.cv.notify_all();
    }
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        if (!state.cv.wait_for(lock, doneTimeout, [&]() { return state.finished; })) {
            Check(false, (std::string("concurrency: ") + label +
                          ": held reader never finished after release (deadlock/watchdog)").c_str());
            if (readerThread.joinable()) readerThread.join();
            return false;
        }
    }
    if (readerThread.joinable()) readerThread.join();
    return true;
}

// harden-index-store-concurrent-access D1-D7: proves and hardens the one-writer
// + independent-reader WAL contract with deterministic, repeated, two-connection
// fixtures. A Store object is connection-owned and is NOT claimed thread-safe;
// each Store is used by exactly one thread while an operation is active.
void TestConcurrentReaderWriterWalContract() {
    // D7: repeat the supported held-reader + writer-commit sequence
    // deterministically; target >= 20 iterations when runtime is reasonable.
    constexpr int kIterations = 24;
    constexpr int64_t kSeedCount = 64;
    constexpr Sec kBarrierTimeout(10);
    constexpr Sec kDoneTimeout(10);

    std::vector<int64_t> writerDurationsMs;
    writerDurationsMs.reserve(kIterations * 3);
    std::vector<int64_t> checkpointDurationsMs;
    checkpointDurationsMs.reserve(kIterations);
    int passedIterations = 0;
    const auto allStart = Steady::now();

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        const std::string dbPath =
            FreshDbPath(("ffindexstore_test_concurrent_" + std::to_string(iteration) + ".db").c_str());
        const std::string label = "iter" + std::to_string(iteration);

        // Authoritative writer connection (owned by this/main thread).
        Store writer;
        Check(writer.Open(dbPath), (label + ": writer store opens").c_str());
        VolumeKey key;
        key.serialNumber = static_cast<uint32_t>(iteration + 1);
        const VolumeRowId vol = *writer.GetOrCreateVolume(key);

        // Seed a meaningful set of entries on the writer connection.
        std::vector<EntryChange> seed;
        seed.reserve(static_cast<size_t>(kSeedCount));
        for (int64_t i = 1; i <= kSeedCount; ++i) {
            EntryRecord r = MakeEntry(static_cast<uint64_t>(i), static_cast<uint64_t>(i),
                                      u"entry-" + U16Id(i));
            seed.push_back({EntryChangeKind::Upsert, r});
        }
        Check(writer.ApplyBatch(vol, seed), (label + ": seed batch commits").c_str());
        Check(writer.CheckpointPassive(), (label + ": seed checkpoint").c_str());

        // Independent reader connection to the SAME existing file (owned by the
        // reader thread). This is the two-independent-connection fixture.
        Store reader;
        Check(reader.Open(dbPath), (label + ": independent reader store opens same file").c_str());

        // --- Phase A: held read + concurrent INSERT (snapshot isolation) ---
        {
            HeldReadState state;
            const int64_t newId = 1000 + iteration; // distinct per iteration
            std::thread readerThread(HeldReadWorker, std::ref(state), std::ref(reader), vol);
            const bool ok = DriveHeldRead(state, readerThread, [&]() {
                std::vector<EntryChange> batch = {
                    {EntryChangeKind::Upsert,
                     MakeEntry(static_cast<uint64_t>(newId), static_cast<uint64_t>(newId),
                                u"inserted-" + U16Id(newId))}};
                const auto t0 = Steady::now();
                const bool committed = writer.ApplyBatch(vol, batch);
                const auto t1 = Steady::now();
                writerDurationsMs.push_back(ToMs(t0, t1));
                Check(committed, (label + ": INSERT batch commits while reader is held (no busy/block)").c_str());
                Check(writer.CountEntries(vol) == kSeedCount + 1,
                      (label + ": writer observes its committed row").c_str());
            }, kBarrierTimeout, kDoneTimeout, (label + "-insert").c_str());
            if (ok) {
                bool heldSawNew = false;
                for (const auto& pair : state.seen) {
                    if (pair.first == newId) heldSawNew = true;
                }
                Check(state.seen.size() == static_cast<size_t>(kSeedCount),
                      (label + ": held reader saw exactly the pre-commit snapshot (no new row)").c_str());
                Check(!heldSawNew,
                      (label + ": held reader did NOT observe the concurrently-committed row").c_str());
                // Fresh read on the SAME reader connection sees post-commit state.
                int freshCount = 0;
                bool freshSawNew = false;
                reader.ForEachEntry(vol, [&](const EntryRecord& e) {
                    ++freshCount;
                    if (e.id.low == static_cast<uint64_t>(newId)) freshSawNew = true;
                });
                Check(freshCount == kSeedCount + 1,
                      (label + ": fresh read sees the post-commit row count").c_str());
                Check(freshSawNew, (label + ": fresh read observes the committed row").c_str());
            }
        }

        // --- Phase A2: held read + concurrent UPDATE (snapshot isolation is not
        //     append-only-specific, D3.5) ---
        {
            HeldReadState state;
            const int64_t updateId = 7;
            std::thread readerThread(HeldReadWorker, std::ref(state), std::ref(reader), vol);
            const bool ok = DriveHeldRead(state, readerThread, [&]() {
                EntryRecord r = MakeEntry(static_cast<uint64_t>(updateId),
                                          static_cast<uint64_t>(updateId), u"updated-" + U16Id(updateId));
                const auto t0 = Steady::now();
                const bool committed = writer.ApplyBatch(vol, {{EntryChangeKind::Upsert, r}});
                const auto t1 = Steady::now();
                writerDurationsMs.push_back(ToMs(t0, t1));
                Check(committed, (label + ": UPDATE commits while reader is held").c_str());
            }, kBarrierTimeout, kDoneTimeout, (label + "-update").c_str());
            if (ok) {
                auto heldName = NameOf(state, updateId);
                Check(heldName.has_value() && *heldName == u"entry-" + U16Id(updateId),
                      (label + ": held reader saw the pre-update name").c_str());
                int freshCount = 0;
                std::optional<std::u16string> freshName;
                reader.ForEachEntry(vol, [&](const EntryRecord& e) {
                    ++freshCount;
                    if (e.id.low == static_cast<uint64_t>(updateId)) freshName = e.name;
                });
                Check(freshCount == kSeedCount + 1,
                      (label + ": UPDATE keeps the row count").c_str());
                Check(freshName.has_value() && *freshName == u"updated-" + U16Id(updateId),
                      (label + ": fresh read saw the post-update name").c_str());
            }
        }

        // --- Phase A3: held read + concurrent REMOVE (snapshot isolation) ---
        {
            HeldReadState state;
            const int64_t removeId = 13;
            std::thread readerThread(HeldReadWorker, std::ref(state), std::ref(reader), vol);
            const bool ok = DriveHeldRead(state, readerThread, [&]() {
                EntryRecord r;
                r.id = FileId{static_cast<uint64_t>(removeId), 0};
                const auto t0 = Steady::now();
                const bool committed = writer.ApplyBatch(vol, {{EntryChangeKind::Remove, r}});
                const auto t1 = Steady::now();
                writerDurationsMs.push_back(ToMs(t0, t1));
                Check(committed, (label + ": REMOVE commits while reader is held").c_str());
            }, kBarrierTimeout, kDoneTimeout, (label + "-remove").c_str());
            if (ok) {
                Check(NameOf(state, removeId).has_value(),
                      (label + ": held reader still saw the row before its removal").c_str());
                Check(state.seen.size() == static_cast<size_t>(kSeedCount + 1),
                      (label + ": held reader saw the pre-remove snapshot (65 rows)").c_str());
                int freshCount = 0;
                bool freshSawRemoved = false;
                reader.ForEachEntry(vol, [&](const EntryRecord& e) {
                    ++freshCount;
                    if (e.id.low == static_cast<uint64_t>(removeId)) freshSawRemoved = true;
                });
                Check(!freshSawRemoved, (label + ": fresh read does NOT see the removed row").c_str());
                Check(freshCount == static_cast<size_t>(kSeedCount),
                      (label + ": REMOVE reduced the row count to the seed count").c_str());
            }
        }

        // --- Phase B: held read + passive checkpoint on an independent
        //     connection (D6) ---
        {
            HeldReadState state;
            std::thread readerThread(HeldReadWorker, std::ref(state), std::ref(reader), vol);
            const bool ok = DriveHeldRead(state, readerThread, [&]() {
                const auto t0 = Steady::now();
                const bool ckptOk = writer.CheckpointPassive();
                const auto t1 = Steady::now();
                checkpointDurationsMs.push_back(ToMs(t0, t1));
                Check(ckptOk, (label + ": passive checkpoint returns while reader is held").c_str());
            }, kBarrierTimeout, kDoneTimeout, (label + "-checkpoint").c_str());
            if (ok) {
                // The reader must not be invalidated: it completed successfully
                // against a coherent snapshot.
                Check(state.seen.size() == static_cast<size_t>(kSeedCount),
                      (label + ": held reader (during checkpoint) saw a coherent snapshot").c_str());
                // Subsequent maintenance/read operations must still work.
                Check(writer.CheckpointPassive(),
                      (label + ": subsequent checkpoint after release works").c_str());
                int subsequent = 0;
                reader.ForEachEntry(vol, [&](const EntryRecord&) { ++subsequent; });
                Check(subsequent == static_cast<size_t>(kSeedCount),
                      (label + ": subsequent fresh read after release works").c_str());
            }
        }

        // --- Phase C: teardown hygiene (D7) -- close both stores, reopen, and
        //     confirm no stale lock/busy state survives. ---
        reader.Close();
        writer.Close();
        Store reopened;
        Check(reopened.Open(dbPath), (label + ": database reopens after both stores closed").c_str());
        Check(reopened.RunIntegrityCheck(),
              (label + ": integrity check passes after reopen").c_str());
        Check(reopened.CountEntries(vol) == static_cast<uint64_t>(kSeedCount),
              (label + ": durable row set is correct after reopen").c_str());

        ++passedIterations;
    }

    const auto allEnd = Steady::now();
    std::printf("concurrency: %d/%d iterations passed deterministically\n", passedIterations, kIterations);
    std::printf("concurrency: writer commit durations (ms) min=%lld median=%.1f max=%lld\n",
                static_cast<long long>(writerDurationsMs.empty() ? 0 : *std::min_element(writerDurationsMs.begin(), writerDurationsMs.end())),
                MedianMs(writerDurationsMs),
                static_cast<long long>(writerDurationsMs.empty() ? 0 : *std::max_element(writerDurationsMs.begin(), writerDurationsMs.end())));
    std::printf("concurrency: passive checkpoint durations (ms) min=%lld median=%.1f max=%lld\n",
                static_cast<long long>(checkpointDurationsMs.empty() ? 0 : *std::min_element(checkpointDurationsMs.begin(), checkpointDurationsMs.end())),
                MedianMs(checkpointDurationsMs),
                static_cast<long long>(checkpointDurationsMs.empty() ? 0 : *std::max_element(checkpointDurationsMs.begin(), checkpointDurationsMs.end())));
    std::printf("concurrency: total deterministic fixture runtime (ms) %lld\n",
                static_cast<long long>(ToMs(allStart, allEnd)));
    Check(passedIterations == kIterations, "concurrency: every iteration passed without retry-until-pass");
}

// index-durability-false-success-closure 5.4 (PA-006): the corrupt-vs-
// inconclusive boundary is a testable pure rule. Only SQLite's own
// corruption verdicts may ever qualify a database for the caller's
// delete-and-rebuild fallback; everything else must read as "unproven".
void TestIntegrityVerdictClassifier() {
    using Verdict = Store::IntegrityVerdict;
    Check(Store::ClassifyIntegrityStep(SQLITE_CORRUPT) == Verdict::Corrupt,
          "SQLITE_CORRUPT is a corruption verdict");
    Check(Store::ClassifyIntegrityStep(SQLITE_NOTADB) == Verdict::Corrupt,
          "SQLITE_NOTADB (not a database file) is a corruption verdict");
    Check(Store::ClassifyIntegrityStep(SQLITE_CORRUPT | (2 << 8)) == Verdict::Corrupt,
          "an extended corrupt code classifies with its primary family");
    Check(Store::ClassifyIntegrityStep(SQLITE_BUSY) == Verdict::Inconclusive,
          "a locked database is unproven, never corrupt");
    Check(Store::ClassifyIntegrityStep(SQLITE_LOCKED) == Verdict::Inconclusive, "LOCKED is unproven");
    Check(Store::ClassifyIntegrityStep(SQLITE_NOMEM) == Verdict::Inconclusive, "NOMEM is unproven");
    Check(Store::ClassifyIntegrityStep(SQLITE_READONLY) == Verdict::Inconclusive, "READONLY is unproven");
    Check(Store::ClassifyIntegrityStep(SQLITE_INTERRUPT) == Verdict::Inconclusive, "INTERRUPT is unproven");
    Check(Store::ClassifyIntegrityStep(SQLITE_FULL) == Verdict::Inconclusive, "FULL is unproven");
    Check(Store::ClassifyIntegrityStep(SQLITE_IOERR | (3 << 8)) == Verdict::Inconclusive,
          "the extended IOERR family never claims corruption");
    Check(Store::ClassifyIntegrityStep(0x5391) == Verdict::Inconclusive,
          "an unrecognized result code never claims corruption -- the classifier fails safe");
}

// PA-006: a healthy database under a transient exclusive lock must fail the
// open WITHOUT the integrity-failed flag -- the flag is what licenses the
// engine's delete path, and "could not check" is not "checked and corrupt".
void TestLockedDatabaseFailsOpenWithoutIntegrityFlag() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_locked_integrity.db");
    VolumeRowId vol = 0;
    {
        Store store;
        Check(store.Open(dbPath), "locked-integrity fixture database opens");
        VolumeKey key;
        key.serialNumber = 611;
        vol = *store.GetOrCreateVolume(key);
        store.ApplyBatch(vol, {{EntryChangeKind::Upsert, MakeEntry(1, 1, u"keep-me.db")}});
    }
    {
        sqlite3* locker = nullptr;
        Check(sqlite3_open(dbPath.c_str(), &locker) == SQLITE_OK, "lock-holder connection opens");
        if (!locker) return;
        // WAL readers ignore BEGIN EXCLUSIVE; only locking_mode=EXCLUSIVE
        // with an active read genuinely locks new connections out -- the
        // transient-contention shape PA-006 must survive without deletion.
        const int lockMode = sqlite3_exec(locker, "PRAGMA locking_mode=EXCLUSIVE;", nullptr, nullptr, nullptr);
        const int begin = sqlite3_exec(locker, "BEGIN;", nullptr, nullptr, nullptr);
        const int read = sqlite3_exec(locker, "SELECT count(*) FROM sqlite_master;", nullptr, nullptr, nullptr);
        Check(lockMode == SQLITE_OK && begin == SQLITE_OK && read == SQLITE_OK,
              "exclusive lock acquired on a healthy database");

        Store victim;
        bool integrityFailed = true;
        Check(!victim.Open(dbPath, &integrityFailed),
              "the integrity check under an exclusive lock fails the open (fail-closed)");
        Check(!integrityFailed,
              "a check that could not reach a verdict does NOT report corruption (PA-006)");
        Check(victim.RunIntegrityCheckDetailed() == Store::IntegrityVerdict::Inconclusive,
              "the failed-open store's verdict is inconclusive, never corrupt");

        sqlite3_exec(locker, "COMMIT;", nullptr, nullptr, nullptr);
        sqlite3_close(locker);
    }
    // The files survived: reopening once the lock releases finds the index intact.
    Check(std::filesystem::exists(dbPath), "the inconclusive open left the database file in place");
    Store revived;
    bool integrityFailed = true;
    Check(revived.Open(dbPath, &integrityFailed), "the same database opens cleanly once the lock is gone");
    Check(!integrityFailed, "the unlocked open reports a clean verdict");
    Check(revived.CountEntries(vol) == 1, "no rows were lost to the inconclusive interval");
    Check(revived.RunIntegrityCheckDetailed() == Store::IntegrityVerdict::Ok,
          "the healthy verdict is exactly Ok");
}

// The corruption half of the split: a file SQLite positively identifies as
// corrupt must set outIntegrityFailed true (the fallback stays available
// when earned). Pure garbage bytes would read as a brand-new empty
// database, so the fixture damages the page-1 b-tree header of a real one.
void TestCorruptDatabaseSetsIntegrityFailedFlag() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_corrupt_verdict.db");
    {
        Store store;
        Check(store.Open(dbPath), "corrupt-verdict fixture creates a real database");
        VolumeKey key;
        key.serialNumber = 612;
        const auto vol = *store.GetOrCreateVolume(key);
        store.ApplyBatch(vol, {{EntryChangeKind::Upsert, MakeEntry(1, 1, u"was-here.db")}});
        store.CheckpointPassive();
        store.Close();
    }
    std::filesystem::remove(dbPath + "-wal");
    std::filesystem::remove(dbPath + "-shm");
    {
        FILE* file = nullptr;
        Check(fopen_s(&file, dbPath.c_str(), "r+b") == 0 && file != nullptr,
              "corrupt-verdict fixture file is reopenable for damage");
        if (!file) return;
        // Overwrite the page-1 b-tree page header (SQLite magic stays
        // valid, so this is a corrupt database, not a non-database file).
        fseek(file, 100, SEEK_SET);
        const unsigned char damage[64] = {0xDE, 0xAD, 0xBE, 0xEF};
        fwrite(damage, 1, sizeof(damage), file);
        fclose(file);
    }
    Store store;
    bool integrityFailed = false;
    Check(!store.Open(dbPath, &integrityFailed), "a corrupted database fails the open");
    Check(integrityFailed,
          "a file SQLite positively verifies as corrupt reports corruption");
}

} // namespace

// memory-bounded-indexed-scale 2.1: durable point lookup round-trip.
void TestFindEntryPointLookup() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_find_entry.db");
    Store store;
    Check(store.Open(dbPath), "FindEntry fixture opens");
    VolumeKey key;
    key.serialNumber = 77;
    const auto vol = *store.GetOrCreateVolume(key);

    EntryRecord original = MakeEntry(4242, 5, u"point.txt", 0x20);
    original.sizeBytes = 123;
    original.allocatedSizeKnown = false; // explicit unknown must survive
    original.creationTime = 111;
    original.lastModifiedTime = 222;
    original.lastAccessTime = 333;
    Check(store.ApplyBatch(vol, {{EntryChangeKind::Upsert, original}}),
          "the point-lookup row commits through the normal batch path");

    const auto found = store.FindEntry(vol, FileId{4242, 0});
    Check(found.has_value(), "a committed row is found by (volume, FRN)");
    Check(found->id == FileId{4242, 0} && found->parentId == FileId{5, 0},
          "point lookup restores identity fields exactly");
    Check(found->name == u"point.txt" && found->sizeBytes == 123,
          "point lookup restores name and logical size");
    Check(!found->allocatedSizeKnown && found->allocatedSizeBytes == 0,
          "allocated-size unknown stays unknown (never fabricated)");
    Check(found->creationTime == 111 && found->lastModifiedTime == 222 && found->lastAccessTime == 333,
          "timestamp metadata round-trips");
    Check(found->attributes == 0x20, "attributes round-trip");

    Check(!store.FindEntry(vol, FileId{999'999, 0}).has_value(),
          "an absent row is nullopt, never a fabricated record");

    // Removal visibility.
    Check(store.ApplyBatch(vol, {{EntryChangeKind::Remove, original}}),
          "the removal commits");
    Check(!store.FindEntry(vol, FileId{4242, 0}).has_value(),
          "a removed row is no longer found");
}

// large-index-lifecycle-and-search-scalability review-lane C Finding 4: a
// partial upsert of a MISSING row must create it with zero/default values for
// absent fields (matching Projection::UpsertPartial's contract), never the
// wire values carried in the change.
void TestPartialUpsertOfMissingRowZeroesAbsentFields() {
    Store store;
    const std::string path = FreshDbPath("ffindexstore_partial_insert_mask.db");
    Check(store.Open(path), "partial-insert store opens");
    const auto volume = store.GetOrCreateVolume({{}, 606});
    Check(volume.has_value(), "partial-insert volume is created");
    EntryRecord partial = MakeEntry(50, 5, u"partial.bin");
    partial.sizeBytes = 42;           // carried in the wire record but ABSENT
    partial.lastModifiedTime = 999;   // from the field mask
    partial.attributes = 0x20;
    EntryChange change{EntryChangeKind::Upsert, partial};
    change.fields = kEntryFieldName | kEntryFieldParentId;
    Check(store.ApplyBatch(*volume, {change}), "partial insert commits");
    EntryRecord stored;
    bool found = false;
    store.ForEachEntry(*volume, [&](const EntryRecord& entry) {
        if (entry.id.low == 50) {
            stored = entry;
            found = true;
        }
        return true;
    });
    Check(found, "the partial insert created the missing row");
    Check(stored.name == u"partial.bin" && stored.parentId.low == 5,
          "present fields persist exactly");
    Check(stored.sizeBytes == 0,
          "an absent size field is created as zero, not the wire value");
    Check(stored.lastModifiedTime == 0,
          "an absent timestamp is created as zero, not the wire value");
    Check(stored.attributes == 0,
          "an absent attribute field is created as zero, not the wire value");
}

VolumeKey TestVolumeKey(uint32_t serial) {
    VolumeKey key;
    key.volumeGuid = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    key.serialNumber = serial;
    return key;
}

// durable-cold-search-index-and-hybrid-residency 4.x: the additive v3
// candidate structures must be rebuilt for a database that predates them
// (the v2 -> v3 migration), without touching the entries table.
void TestCandidateIndexMigratesFromV2Database() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_migrate_v2.db");
    {
        Store store;
        Check(store.Open(dbPath), "v2-simulation: store opens");
        const VolumeRowId vol = *store.GetOrCreateVolume(TestVolumeKey(1));
        std::vector<EntryChange> batch;
        batch.push_back({EntryChangeKind::Upsert, MakeEntry(100, 5, u"migrate_alpha.txt")});
        batch.push_back({EntryChangeKind::Upsert, MakeEntry(101, 5, u"migrate_beta.txt")});
        Check(store.ApplyBatch(vol, batch), "v2-simulation: seed rows commit");
        store.Close();
    }
    // Simulate a genuine v2 database: the v3 derived structures never
    // existed and the version marker says so.
    {
        sqlite3* raw = nullptr;
        Check(sqlite3_open(dbPath.c_str(), &raw) == SQLITE_OK, "raw sqlite opens the fixture");
        char* error = nullptr;
        const bool dropped = sqlite3_exec(raw,
            "DROP TABLE IF EXISTS entry_name_exceptions;"
            "DROP TABLE IF EXISTS entry_name_index_map;"
            "DROP TABLE IF EXISTS entry_name_index_meta;"
            "DROP TABLE IF EXISTS entry_name_terms;"
            "PRAGMA user_version=2;",
            nullptr, nullptr, &error) == SQLITE_OK;
        Check(dropped, (std::string("v3 structures dropped for the v2 simulation: ") + (error ? error : "")).c_str());
        sqlite3_close(raw);
    }
    Store store;
    bool integrityFailed = false;
    Check(store.Open(dbPath, &integrityFailed), "a v2 database opens and migrates to v3");
    Check(!integrityFailed, "v2 -> v3 migration does not report integrity failure");
    Check(store.CountEntries(1) == 2, "migration preserves all entries");
    auto reader = store.OpenReadSession();
    std::vector<std::u16string> streamed;
    Check(reader->ForEachEntryWithAsciiNameUntil(1, "migrate_", [&](const EntryRecord& entry) {
        streamed.push_back(entry.name);
        return true;
    }), "post-migration candidate rung streams");
    Check(streamed.size() == 2, "the rebuilt candidate index finds both canonical matches");
    {
        sqlite3* raw = nullptr;
        Check(sqlite3_open(dbPath.c_str(), &raw) == SQLITE_OK, "raw sqlite reopens");
        sqlite3_stmt* stmt = nullptr;
        Check(sqlite3_prepare_v2(raw, "PRAGMA user_version;", -1, &stmt, nullptr) == SQLITE_OK,
              "user_version query prepares");
        Check(sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 3,
              "migration bumps the schema version marker to 3");
        sqlite3_finalize(stmt);
        sqlite3_close(raw);
    }
}

// 4.x: a candidate-maintenance failure inside ApplyBatch must roll back the
// ENTIRE batch (entries and candidate structures together) -- the two must
// never disagree after a committed (or aborted) transaction.
void TestCandidateMaintenanceFaultRollsBackWholeBatch() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_candidate_fault.db");
    Store store;
    Check(store.Open(dbPath), "fault test: store opens");
    const VolumeRowId vol = *store.GetOrCreateVolume(TestVolumeKey(1));
    std::vector<EntryChange> seed;
    for (int index = 0; index < 10; ++index) {
        seed.push_back({EntryChangeKind::Upsert,
                        MakeEntry(100 + static_cast<uint32_t>(index), 5, u"seed_row.txt")});
    }
    Check(store.ApplyBatch(vol, seed), "fault test: seed commits");

    std::vector<EntryChange> batch;
    for (int index = 0; index < 100; ++index) {
        batch.push_back({EntryChangeKind::Upsert,
                         MakeEntry(1000 + static_cast<uint32_t>(index), 5, u"fault_batch.txt")});
    }
    ffindexstore::SetTestingCandidateMaintenanceFault(5, false);
    Check(!store.ApplyBatch(vol, batch), "the faulted batch fails");
    ffindexstore::SetTestingCandidateMaintenanceFault(0, false);
    Check(store.CountEntries(1) == 10, "no row of the faulted batch commits (all-or-nothing)");
    store.Close();

    Store reopened;
    Check(reopened.Open(dbPath), "the database reopens after the faulted batch");
    Check(reopened.CountEntries(1) == 10, "reopen sees only the committed seed rows");
    auto reader = reopened.OpenReadSession();
    size_t streamed = 0;
    Check(reader->ForEachEntryWithAsciiNameUntil(1, "seed_row", [&](const EntryRecord&) {
        ++streamed;
        return true;
    }), "the candidate index is consistent after the aborted batch");
    Check(streamed == 10, "the aborted batch left no phantom candidates");
}

// 4.x: REAL kill-during-commit evidence. The child process dies via
// ExitProcess inside an uncommitted ApplyBatch (no rollback path runs); the
// parent reopens and verifies WAL recovery left entries and the candidate
// index consistent. This is process-termination evidence, distinct from the
// graceful fault test above.
void TestCandidateMaintenanceHardExitRecoversOnReopen() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_candidate_kill.db");
    {
        Store store;
        Check(store.Open(dbPath), "kill test: store opens");
        const VolumeRowId vol = *store.GetOrCreateVolume(TestVolumeKey(1));
        std::vector<EntryChange> seed;
        for (int index = 0; index < 10; ++index) {
            seed.push_back({EntryChangeKind::Upsert,
                            MakeEntry(100 + static_cast<uint32_t>(index), 5, u"seed_row.txt")});
        }
        Check(store.ApplyBatch(vol, seed), "kill test: seed commits");
    }

    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::wstring command = std::wstring(L"\"") + modulePath + L"\" --hard-exit-child \"" +
        std::wstring(dbPath.begin(), dbPath.end()) + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    Check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                         &startup, &process) != 0,
          "the crash child process launches");
    const DWORD waitResult = WaitForSingleObject(process.hProcess, 60000);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Check(waitResult == WAIT_OBJECT_0, "the crash child terminates within the bound");
    Check(exitCode == 0xDEAD, "the child died inside the transaction (ExitProcess 0xDEAD)");

    Store reopened;
    bool integrityFailed = false;
    Check(reopened.Open(dbPath, &integrityFailed), "the database reopens after the kill");
    Check(!integrityFailed, "WAL recovery does not classify the kill as corruption");
    Check(reopened.CountEntries(1) == 10, "the uncommitted batch is gone after the kill (atomicity)");
    auto reader = reopened.OpenReadSession();
    size_t streamed = 0;
    Check(reader->ForEachEntryWithAsciiNameUntil(1, "seed_row", [&](const EntryRecord&) {
        ++streamed;
        return true;
    }), "the candidate index survives the kill consistently");
    Check(streamed == 10, "no phantom or missing candidates after kill-during-commit");
}

// 4.x: index-validity markers. If the derived structures are tampered with
// (simulating partial external damage), the open-time cross-check must
// rebuild them before any query can consult them.
void TestCandidateIndexRebuildsAfterTamper() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_candidate_tamper.db");
    Store store;
    Check(store.Open(dbPath), "tamper test: store opens");
    const VolumeRowId vol = *store.GetOrCreateVolume(TestVolumeKey(1));
    std::vector<EntryChange> batch;
    batch.push_back({EntryChangeKind::Upsert, MakeEntry(100, 5, u"tamper_alpha.txt")});
    batch.push_back({EntryChangeKind::Upsert, MakeEntry(101, 5, u"tamper_beta.txt")});
    Check(store.ApplyBatch(vol, batch), "tamper test: rows commit");
    store.Close();

    {
        sqlite3* raw = nullptr;
        Check(sqlite3_open(dbPath.c_str(), &raw) == SQLITE_OK, "raw sqlite opens for tampering");
        char* error = nullptr;
        const bool ok = sqlite3_exec(raw,
            "DELETE FROM entry_name_index_map WHERE frn_low=101;"
            "UPDATE entry_name_index_meta SET state=0;",
            nullptr, nullptr, &error) == SQLITE_OK;
        Check(ok, (std::string("derived structures tampered: ") + (error ? error : "")).c_str());
        sqlite3_close(raw);
    }

    Store reopened;
    Check(reopened.Open(dbPath), "the store reopens after tampering");
    auto reader = reopened.OpenReadSession();
    size_t streamed = 0;
    Check(reader->ForEachEntryWithAsciiNameUntil(1, "tamper_", [&](const EntryRecord&) {
        ++streamed;
        return true;
    }), "the rebuilt candidate rung streams");
    Check(streamed == 2, "the open-time rebuild restores full candidate coverage");
}

// --- durable-cold-search-index-and-hybrid-residency 4.x crash-consistency
// child mode: opens the given database and applies a 100-row batch with the
// hard-exit candidate-maintenance fault armed at operation 5. The process
// DIES inside the uncommitted transaction (ExitProcess, no rollback, no
// close) so the parent can verify real kill-during-commit recovery.
int RunHardExitChild(const std::string& dbPath) {
    Store store;
    if (!store.Open(dbPath)) return 3;
    const VolumeRowId vol = *store.GetOrCreateVolume(TestVolumeKey(1));
    std::vector<EntryChange> batch;
    for (int index = 0; index < 100; ++index) {
        batch.push_back({EntryChangeKind::Upsert,
                         MakeEntry(1000 + static_cast<uint32_t>(index), 5, u"crash_batch.txt")});
    }
    ffindexstore::SetTestingCandidateMaintenanceFault(5, true);
    const bool applied = store.ApplyBatch(vol, batch);
    ffindexstore::SetTestingCandidateMaintenanceFault(0, false);
    // Unreachable when the seam works; a return means the kill never fired.
    return applied ? 2 : 4;
}

// durable-cold-search reviewer-B P1: a version-1 database whose ALTER
// already ran (crash/failure between the ALTER and the version bump) must
// still open and migrate. Before the idempotence guard this state bricked
// every subsequent open with a duplicate-column ALTER failure.
void TestSchemaV1MigrationIsIdempotentAfterPartialRun() {
    const std::string dbPath = FreshDbPath("ffindexstore_test_migrate_v1_partial.db");
    Check(CreateVersion1Fixture(dbPath), "v1-partial: fixture database is created");
    {
        // Simulate the interrupted migration: column added, marker NOT bumped.
        sqlite3* raw = nullptr;
        Check(sqlite3_open(dbPath.c_str(), &raw) == SQLITE_OK, "v1-partial: raw sqlite opens");
        char* error = nullptr;
        const bool ok = sqlite3_exec(raw, "ALTER TABLE entries ADD COLUMN allocated_size_bytes INTEGER;",
                                     nullptr, nullptr, &error) == SQLITE_OK;
        Check(ok, (std::string("v1-partial: pre-run ALTER applied: ") + (error ? error : "")).c_str());
        sqlite3_close(raw);
    }
    Store store;
    bool integrityFailed = false;
    Check(store.Open(dbPath, &integrityFailed), "v1-partial: open survives the pre-run ALTER (no duplicate-column brick)");
    Check(store.CountEntries(1) == 1, "v1-partial: entries preserved through the idempotent migration");
    store.Close();
    Store reopened;
    Check(reopened.Open(dbPath, &integrityFailed), "v1-partial: database reopens after full migration");
    Check(reopened.CountEntries(1) == 1, "v1-partial: entries preserved across reopen");
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--hard-exit-child") == 0) {
        return RunHardExitChild(argv[2]);
    }
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
    TestConcurrentReaderWriterWalContract();
    TestFindEntryPointLookup();
    TestPartialUpsertOfMissingRowZeroesAbsentFields();
    TestIntegrityVerdictClassifier();
    TestLockedDatabaseFailsOpenWithoutIntegrityFlag();
    TestCorruptDatabaseSetsIntegrityFailedFlag();
    TestSchemaV1MigrationIsIdempotentAfterPartialRun();
    TestCandidateIndexMigratesFromV2Database();
    TestCandidateMaintenanceFaultRollsBackWholeBatch();
    TestCandidateMaintenanceHardExitRecoversOnReopen();
    TestCandidateIndexRebuildsAfterTamper();

    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
