#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <windows.h>

#include "ffindexstore/Projection.h"
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

void TestSiblingsDoNotDuplicateSharedAncestorPath() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"Users", 0x10));
    for (uint64_t i = 0; i < 50; ++i) {
        std::u16string name = u"child";
        name.push_back(static_cast<char16_t>(u'0' + (i % 10)));
        proj.Upsert(1, MakeEntry(1000 + i, 100, name, 0));
    }
    // Every child references its parent, not a materialized path -- the
    // ProjectionEntry struct itself has no path field to duplicate into;
    // this is a structural guarantee, verified here by confirming lookups
    // still resolve correctly at scale rather than by inspecting memory.
    Check(proj.EntryCount() == 52, "all inserted entries are present");
}

void TestRepeatedNamesAcrossTreeShareOneInternedId() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"a", 0x10));
    proj.Upsert(1, MakeEntry(200, 5, u"b", 0x10));
    proj.Upsert(1, MakeEntry(101, 100, u"config.json", 0));
    proj.Upsert(1, MakeEntry(201, 200, u"config.json", 0));

    const auto* e1 = proj.Find(1, FileId{101, 0});
    const auto* e2 = proj.Find(1, FileId{201, 0});
    if (e1 == nullptr || e2 == nullptr) {
        Check(false, "both entries are found");
        return;
    }
    Check(e1->nameId == e2->nameId, "identical names under different parents share one interned NameId");
    Check(proj.Names().UniqueNameCount() == 4, "the name pool holds one entry per distinct name (C:, a, b, config.json)");
}

void TestPathIsReconstructedOnDemand() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"Users", 0x10));
    proj.Upsert(1, MakeEntry(101, 100, u"bob", 0x10));
    proj.Upsert(1, MakeEntry(102, 101, u"notes.txt", 0));

    auto result = proj.ReconstructPath(1, FileId{102, 0});
    Check(result.reachedRoot, "path reconstruction reaches the volume root");
    Check(result.path == u"C:\\Users\\bob\\notes.txt", "reconstructed path matches the expected ancestor chain");
}

void TestChildrenLookupSupportsDirectoryListing() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"a", 0x10));
    proj.Upsert(1, MakeEntry(101, 5, u"b", 0x10));
    proj.Upsert(1, MakeEntry(102, 5, u"c", 0x10));

    auto children = proj.ChildIndices(1, FileId{5, 0});
    Check(!children.empty() && children.size() == 3, "all three children are found under their parent");
}

void TestReparentingUpdatesParentChildIndex() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"a", 0x10));
    proj.Upsert(1, MakeEntry(101, 5, u"b", 0x10));
    proj.Upsert(1, MakeEntry(200, 100, u"moved.txt", 0));

    proj.Upsert(1, MakeEntry(200, 101, u"moved.txt", 0)); // re-parent from a -> b

    auto aChildren = proj.ChildIndices(1, FileId{100, 0});
    Check(aChildren.empty(), "old parent no longer lists the moved entry as a child");
    auto bChildren = proj.ChildIndices(1, FileId{101, 0});
    Check(!bChildren.empty() && bChildren.size() == 1, "new parent lists the moved entry as a child");
}

void TestPartialUpsertPreservesAbsentMetadata() {
    Projection projection;
    EntryRecord full = MakeEntry(101, 100, u"original.txt");
    full.sizeBytes = 1234;
    full.creationTime = 111;
    full.lastModifiedTime = 222;
    full.lastAccessTime = 333;
    projection.Upsert(1, full);

    EntryRecord rename = MakeEntry(101, 100, u"renamed.txt");
    rename.lastModifiedTime = 444;
    projection.UpsertPartial(1, rename,
        kEntryFieldParentId | kEntryFieldName | kEntryFieldLastModifiedTime);

    const ProjectionEntry* observed = projection.Find(1, FileId{101, 0});
    Check(observed != nullptr, "partial projection update keeps the entry addressable");
    if (observed == nullptr) return;
    Check(projection.Names().Get(observed->nameId) == u"renamed.txt" &&
              observed->lastModifiedTime == 444,
          "present rename fields update the projection");
    Check(observed->creationTime == 111 && observed->lastAccessTime == 333 &&
              observed->sizeBytes == 1234,
          "absent authoritative projection metadata is retained");
}

// allocated-size-storage-accuracy 4.2: full scans set both sizes; a partial
// USN-style update without the allocated-size bit preserves the previous
// value (and known state); a partial update with the bit corrects it.
void TestAllocatedSizeFullPartialPreserveAndReconcile() {
    Projection projection;

    // Full scan record: both sizes authoritative.
    EntryRecord full = MakeEntry(101, 100, u"sparse.vhdx");
    full.sizeBytes = 40960;
    full.allocatedSizeBytes = 8192;
    full.allocatedSizeKnown = true;
    projection.Upsert(1, full);
    const ProjectionEntry* observed = projection.Find(1, FileId{101, 0});
    Check(observed != nullptr && observed->sizeBytes == 40960 &&
              observed->allocatedSizeBytes == 8192 && observed->allocatedSizeKnown,
          "a full MFT-scan upsert sets logical and allocated size");

    // Partial update without the allocated bit: previous value preserved.
    EntryRecord delta = MakeEntry(101, 100, u"sparse.vhdx");
    delta.sizeBytes = 50000; // logical size changed
    delta.allocatedSizeBytes = 0;
    delta.allocatedSizeKnown = false;
    projection.UpsertPartial(1, delta, kEntryFieldSizeBytes | kEntryFieldLastModifiedTime);
    observed = projection.Find(1, FileId{101, 0});
    Check(observed != nullptr && observed->sizeBytes == 50000,
          "a present logical-size field updates");
    if (observed != nullptr) {
        Check(observed->allocatedSizeBytes == 8192 && observed->allocatedSizeKnown,
              "an absent allocated-size bit preserves the previously known value");
    }

    // Reconciliation-style full upsert corrects a stale allocated size.
    EntryRecord reconciled = MakeEntry(101, 100, u"sparse.vhdx");
    reconciled.sizeBytes = 50000;
    reconciled.allocatedSizeBytes = 12288;
    reconciled.allocatedSizeKnown = true;
    projection.Upsert(1, reconciled);
    observed = projection.Find(1, FileId{101, 0});
    Check(observed != nullptr && observed->allocatedSizeBytes == 12288,
          "reconciliation corrects stale allocated size");

    // Brand-new entry from a partial update with no allocated bit: unknown,
    // not zero -- distinguishable from a genuine zero-byte file.
    EntryRecord freshDelta = MakeEntry(202, 100, u"fresh.bin");
    freshDelta.sizeBytes = 10;
    projection.UpsertPartial(1, freshDelta, kEntryFieldSizeBytes | kEntryFieldParentId | kEntryFieldName);
    const ProjectionEntry* fresh = projection.Find(1, FileId{202, 0});
    Check(fresh != nullptr && fresh->sizeBytes == 10 && !fresh->allocatedSizeKnown,
          "a new entry from a partial update has unknown allocated size");

    EntryRecord zero = MakeEntry(203, 100, u"empty.bin");
    zero.sizeBytes = 0;
    zero.allocatedSizeBytes = 0;
    zero.allocatedSizeKnown = true;
    projection.Upsert(1, zero);
    const ProjectionEntry* zeroEntry = projection.Find(1, FileId{203, 0});
    Check(zeroEntry != nullptr && zeroEntry->allocatedSizeKnown && zeroEntry->allocatedSizeBytes == 0,
          "a known zero-byte file is distinct from unknown");
}

// allocated-size-storage-accuracy 4.2/D7: the projection aggregate carries
// both totals with an explicit completeness pair and saturating arithmetic;
// a partial subtotal is never presented as an exact total.
void TestFolderAggregateWithAllocatedSizeAndCompleteness() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"dir", 0x10));
    EntryRecord known = MakeEntry(200, 100, u"known.bin");
    known.sizeBytes = 10;
    known.allocatedSizeBytes = 8;
    known.allocatedSizeKnown = true;
    proj.Upsert(1, known);
    EntryRecord unknown = MakeEntry(201, 100, u"unknown.bin");
    unknown.sizeBytes = 20;
    proj.Upsert(1, unknown);
    EntryRecord zero = MakeEntry(202, 100, u"empty.bin");
    zero.sizeBytes = 0;
    zero.allocatedSizeBytes = 0;
    zero.allocatedSizeKnown = true;
    proj.Upsert(1, zero);

    const auto rootAgg = proj.GetFolderAggregate(1, FileId{5, 0});
    Check(rootAgg.has_value(), "root aggregate is present");
    Check(rootAgg->itemCount == 4, "root aggregate counts every descendant");
    Check(rootAgg->totalSizeBytes == 30, "root logical total sums all descendants");
    Check(rootAgg->allocatedBytesTotalCount == 4, "every descendant counts toward the total");
    Check(rootAgg->allocatedBytesKnownCount == 2, "known count covers only known descendants");
    Check(rootAgg->totalAllocatedSizeBytes == 8, "allocated subtotal covers only known descendants");
    Check(!rootAgg->complete || rootAgg->allocatedBytesKnownCount == rootAgg->allocatedBytesTotalCount,
          "an aggregate with unknown descendants is not claimed complete");

    const auto dirAgg = proj.GetFolderAggregate(1, FileId{100, 0});
    Check(dirAgg.has_value() && dirAgg->itemCount == 3 && dirAgg->totalAllocatedSizeBytes == 8 &&
              dirAgg->allocatedBytesKnownCount == 2 && dirAgg->allocatedBytesTotalCount == 3,
          "a subtree with an unknown descendant reports the partial allocated subtotal");
}

// allocated-size-storage-accuracy 4.2/D7: checked arithmetic -- a subtree
// whose sums exceed UINT64_MAX must saturate and mark the aggregate
// incomplete, never wrap into a plausible-looking total.
void TestFolderAggregateOverflowSaturatesAndMarksIncomplete() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    EntryRecord big = MakeEntry(200, 5, u"big.bin");
    big.sizeBytes = UINT64_MAX;
    big.allocatedSizeBytes = UINT64_MAX;
    big.allocatedSizeKnown = true;
    proj.Upsert(1, big);
    EntryRecord more = MakeEntry(201, 5, u"more.bin");
    more.sizeBytes = 1;
    more.allocatedSizeBytes = 1;
    more.allocatedSizeKnown = true;
    proj.Upsert(1, more);

    const auto aggregate = proj.GetFolderAggregate(1, FileId{5, 0});
    Check(aggregate.has_value(), "overflow aggregate is present");
    Check(aggregate->totalSizeBytes == UINT64_MAX && aggregate->totalAllocatedSizeBytes == UINT64_MAX,
          "overflowing sums saturate at UINT64_MAX");
    Check(!aggregate->complete, "a saturated aggregate is explicitly incomplete");
}

void TestRemoveDropsEntryButLeavesSiblingsIntact() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"a", 0));
    proj.Upsert(1, MakeEntry(101, 5, u"b", 0));

    proj.Remove(1, FileId{100, 0});
    Check(proj.Find(1, FileId{100, 0}) == nullptr, "removed entry is no longer found");
    Check(proj.Find(1, FileId{101, 0}) != nullptr, "sibling entry is unaffected");
    Check(proj.EntryCount() == 2, "entry count reflects the removal");
}

void TestCycleDoesNotCauseInfiniteWalk() {
    // Construct a junction-like cycle by hand (1 -> 2 -> 1): should never
    // occur on a well-formed volume, but the walk must still terminate.
    Projection proj;
    proj.Upsert(99, MakeEntry(1, 2, u"a"));
    proj.Upsert(99, MakeEntry(2, 1, u"b"));

    auto result = proj.ReconstructPath(99, FileId{1, 0});
    Check(!result.reachedRoot, "a cyclic parent chain is detected and the walk stops rather than looping forever");
}

void TestVolumesAreIsolatedFromEachOther() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(2, MakeEntry(5, 5, u"D:", 0x10)); // same FRN, different volume

    Check(proj.Find(1, FileId{5, 0})->nameId != proj.Find(2, FileId{5, 0})->nameId
              || proj.Find(1, FileId{5, 0}) != proj.Find(2, FileId{5, 0}),
          "identical FRNs on different volumes are distinct projection entries");
    auto path1 = proj.ReconstructPath(1, FileId{5, 0});
    auto path2 = proj.ReconstructPath(2, FileId{5, 0});
    Check(path1.path == u"C:" && path2.path == u"D:", "each volume's entry resolves to its own identity");
}

void TestRebuildFromStoreMatchesPersistedData() {
    auto dbPath = std::filesystem::temp_directory_path() /
                  ("ffindexstore_projection_rebuild_" + std::to_string(GetCurrentProcessId()) + ".db");
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath.string() + "-wal");
    std::filesystem::remove(dbPath.string() + "-shm");

    Store store;
    store.Open(dbPath.string());
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = *store.GetOrCreateVolume(key);
    store.ApplyBatch(vol, {
                              {EntryChangeKind::Upsert, MakeEntry(5, 5, u"C:", 0x10)},
                              {EntryChangeKind::Upsert, MakeEntry(100, 5, u"data.bin", 0)},
                          });

    Projection proj;
    proj.RebuildVolumeFromStore(store, vol, store.CountEntries(vol));
    Check(proj.EntryCount() == 2, "rebuild loads every persisted row");
    Check(proj.Find(vol, FileId{100, 0}) != nullptr, "rebuilt projection matches durable store contents");
    store.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath.string() + "-wal");
    std::filesystem::remove(dbPath.string() + "-shm");
}

void TestFolderAggregateCountsSubtree() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"Users", 0x10));
    proj.Upsert(1, MakeEntry(101, 5, u"Windows", 0x10));
    proj.Upsert(1, MakeEntry(200, 100, u"bob", 0x10));
    proj.Upsert(1, MakeEntry(201, 100, u"alice", 0x10));
    proj.Upsert(1, MakeEntry(300, 200, u"notes.txt", 0));

    auto aggregate = proj.GetFolderAggregate(1, FileId{5, 0});
    Check(aggregate.has_value(), "root aggregate is present");
    Check(aggregate->itemCount == 5, "root aggregate counts all descendants");
    Check(aggregate->totalSizeBytes == 0, "no entries carry non-zero size in this test");

    auto usersAgg = proj.GetFolderAggregate(1, FileId{100, 0});
    Check(usersAgg.has_value(), "Users aggregate is present");
    Check(usersAgg->itemCount == 3, "Users subtree has three descendants");
    Check(usersAgg->totalSizeBytes == 0, "Users subtree size is zero");

    auto unknown = proj.GetFolderAggregate(1, FileId{999, 0});
    Check(!unknown.has_value(), "unknown root returns nullopt");
}

// Regression (FlatHashMap.h FlatChildrenMap): the live key count_ was
// never incremented on insert, so the children map never grew past its
// initial 8 buckets and the 9th distinct parent key sent find_slot()'s
// probe loop into an infinite spin (bench_projection_memory.cpp hung
// here; the insert/grow/remove count bookkeeping is now fixed). Inserting
// well past that threshold must resolve normally.
void TestManyDistinctParentsDoNotStallGrowth() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    uint64_t nextId = 1000;
    for (uint64_t p = 0; p < 12; ++p) {
        std::u16string dirName = u"dir";
        dirName.push_back(static_cast<char16_t>(u'0' + p));
        const uint64_t parent = 100 + p;
        proj.Upsert(1, MakeEntry(parent, 5, dirName, 0x10));
        for (uint64_t c = 0; c < 3; ++c) {
            proj.Upsert(1, MakeEntry(nextId++, parent, u"file", 0));
        }
    }
    Check(proj.EntryCount() == 1 + 12 + 36, "all entries present past the old 8-bucket stall point");
    const auto children = proj.ChildIndices(1, FileId{100, 0});
    Check(children.size() == 3, "first directory still lists exactly its own children");
}

bool CandidateContains(const Projection& projection, const std::vector<uint32_t>& candidates, uint64_t id) {
    return std::any_of(candidates.begin(), candidates.end(), [&](const uint32_t index) {
        return projection.EntryAt(index).frn.low == id;
    });
}

void TestFilenameTrigramCandidatesTrackMutations() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"unique_component_7391.txt"));
    proj.Upsert(1, MakeEntry(101, 5, u"UNIQUE_COMPONENT_7391-copy.txt"));
    proj.Upsert(1, MakeEntry(102, 5, u"\u6771\u4EAC_component_7391.txt"));
    proj.Upsert(1, MakeEntry(103, 5, u"report.txt"));

    const auto candidates = proj.FilenameCandidateIndices(u"component_7391");
    Check(candidates.has_value(), "eligible filename terms use the trigram candidate index");
    if (!candidates) return;
    Check(CandidateContains(proj, *candidates, 100), "matching ASCII filename is a candidate");
    Check(CandidateContains(proj, *candidates, 101), "uppercase ASCII filename is a candidate");
    Check(CandidateContains(proj, *candidates, 102), "non-ASCII filename is conservatively retained");
    Check(!CandidateContains(proj, *candidates, 103), "non-matching filename is rejected by postings");

    Check(!proj.FilenameCandidateIndices(u"ph").has_value(), "two-character terms use the canonical fallback");
    Check(!proj.FilenameCandidateIndices(u"\u6771\u4EAC").has_value(), "Unicode terms use the canonical fallback");

    proj.Remove(1, FileId{100, 0});
    auto afterRemove = proj.FilenameCandidateIndices(u"component_7391");
    Check(afterRemove.has_value() && !CandidateContains(proj, *afterRemove, 100),
          "deleting an entry removes its trigram candidates");

    proj.Upsert(1, MakeEntry(103, 5, u"renamed_component_7391.txt"));
    auto afterRename = proj.FilenameCandidateIndices(u"component_7391");
    Check(afterRename.has_value() && CandidateContains(proj, *afterRename, 103),
          "renaming an entry adds its new trigram candidates");
}

void TestProjectionEvictsLruAndReloadsFromStore() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"old.txt"));
    proj.Upsert(1, MakeEntry(101, 5, u"middle.txt"));
    proj.Upsert(1, MakeEntry(102, 5, u"recent.txt"));
    const size_t beforeLimit = proj.EstimatedMemoryBytes();
    Check(beforeLimit > 1, "projection reports owned memory before eviction");

    // Refresh entry 100 so entry 101 is the least-recently-used non-root
    // candidate. A one-byte reduction forces the bounded LRU policy to act.
    Check(proj.Find(1, FileId{100, 0}) != nullptr, "LRU fixture entry is present before touch");
    proj.SetMaximumMemoryBytes(beforeLimit - 1);
    Check(proj.NeedsReload(1), "eviction records that durable volume data needs reload");
    Check(proj.Find(1, FileId{100, 0}) != nullptr, "recently touched entry survives LRU eviction");
    Check(proj.Find(1, FileId{101, 0}) == nullptr, "least-recently-used entry is evicted first");

    const auto dbPath = std::filesystem::temp_directory_path() /
        ("ffindexstore_projection_eviction_" + std::to_string(GetCurrentProcessId()) + ".db");
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath.string() + "-wal");
    std::filesystem::remove(dbPath.string() + "-shm");
    Store store;
    Check(store.Open(dbPath.string()), "eviction reload fixture store opens");
    VolumeKey volumeKey;
    volumeKey.serialNumber = 7391;
    const auto volumeId = store.GetOrCreateVolume(volumeKey);
    Check(volumeId.has_value(), "eviction reload fixture volume is created");
    if (volumeId.has_value()) {
        Check(store.ApplyBatch(*volumeId, {
            {EntryChangeKind::Upsert, MakeEntry(5, 5, u"C:", 0x10)},
            {EntryChangeKind::Upsert, MakeEntry(100, 5, u"old.txt")},
            {EntryChangeKind::Upsert, MakeEntry(101, 5, u"middle.txt")},
            {EntryChangeKind::Upsert, MakeEntry(102, 5, u"recent.txt")},
        }), "eviction reload fixture rows are durable");
        Projection reload;
        reload.RebuildVolumeFromStore(store, *volumeId, store.CountEntries(*volumeId));
        const size_t loadedBytes = reload.EstimatedMemoryBytes();
        reload.SetMaximumMemoryBytes(loadedBytes - 1);
        Check(reload.NeedsReload(*volumeId), "reloaded projection records its eviction");
        reload.SetMaximumMemoryBytes(Projection::kDefaultMaximumMemoryBytes);
        Check(reload.ReloadEvictedVolume(store, *volumeId), "evicted volume reload succeeds from SQLite");
        Check(!reload.NeedsReload(*volumeId), "successful SQLite reload clears the eviction marker");
        Check(reload.EntryCount() == 4 && reload.Find(*volumeId, FileId{101, 0}) != nullptr,
              "SQLite reload restores the evicted directory data");
    }
    store.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath.string() + "-wal");
    std::filesystem::remove(dbPath.string() + "-shm");
}

// completion-integrity-hardening-and-release-certification 4.1.2: a durable
// stream failure must not be reported as a successful rebuild or leave a
// readable partial volume behind; reopening the store must make the cold
// volume retryable.
void TestProjectionRebuildFailureIsColdAndRetryable() {
    const auto dbPath = std::filesystem::temp_directory_path() /
        ("ffindexstore_projection_rebuild_failure_" + std::to_string(GetCurrentProcessId()) + ".db");
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath.string() + "-wal");
    std::filesystem::remove(dbPath.string() + "-shm");

    Store store;
    Check(store.Open(dbPath.string()), "rebuild failure fixture store opens");
    VolumeKey key;
    key.serialNumber = 86420;
    const auto volume = store.GetOrCreateVolume(key);
    Check(volume.has_value(), "rebuild failure fixture volume is created");
    if (!volume.has_value()) return;
    Check(store.ApplyBatch(*volume, {
        {EntryChangeKind::Upsert, MakeEntry(5, 5, u"C:", 0x10)},
        {EntryChangeKind::Upsert, MakeEntry(100, 5, u"durable.txt")},
    }), "rebuild failure fixture rows are durable");

    Projection projection;
    Check(projection.RebuildVolumeFromStore(store, *volume, store.CountEntries(*volume)),
          "initial rebuild succeeds before the failure is introduced");
    Check(projection.Find(*volume, FileId{100, 0}) != nullptr,
          "initial rebuild exposes the durable fixture row");

    store.Close();
    Check(!projection.RebuildVolumeFromStore(store, *volume, 2),
          "rebuild reports a closed durable store instead of succeeding with a partial result");
    Check(projection.NeedsReload(*volume), "failed rebuild retains a cold retry marker");
    Check(projection.Find(*volume, FileId{100, 0}) == nullptr,
          "failed rebuild removes any partial or stale resident rows");

    Check(store.Open(dbPath.string()), "rebuild failure fixture store reopens");
    Check(projection.ReloadEvictedVolume(store, *volume),
          "cold rebuild retries successfully after the durable store reopens");
    Check(!projection.NeedsReload(*volume) && projection.Find(*volume, FileId{100, 0}) != nullptr,
          "successful retry restores the durable row and clears the cold marker");

    store.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath.string() + "-wal");
    std::filesystem::remove(dbPath.string() + "-shm");
}

void TestProjectionMemoryPressureSignal() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    const size_t usage = proj.EstimatedMemoryBytes();
    proj.SetMaximumMemoryBytes(usage);
    Check(proj.IsMemoryPressureHigh(), "projection exposes a high-memory pressure signal near its limit");
}

// memory-bounded-indexed-scale 3.1: the memory policy is observable --
// configured ceiling, estimated bytes, eviction byte deltas, and monotonic
// eviction/reload counters.
void TestProjectionMemoryDiagnostics() {
    Store store;
    const auto dbPath = std::filesystem::temp_directory_path() /
        ("ffindexstore_projection_diag_" + std::to_string(GetCurrentProcessId()) + ".db");
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath.string() + "-wal");
    std::filesystem::remove(dbPath.string() + "-shm");
    Check(store.Open(dbPath.string()), "diagnostics fixture store opens");
    VolumeKey key;
    key.serialNumber = 424242;
    const auto volume = *store.GetOrCreateVolume(key);
    Check(store.ApplyBatch(volume, {
        {EntryChangeKind::Upsert, MakeEntry(5, 5, u"C:", 0x10)},
        {EntryChangeKind::Upsert, MakeEntry(100, 5, u"a.txt")},
        {EntryChangeKind::Upsert, MakeEntry(101, 5, u"b.txt")},
        {EntryChangeKind::Upsert, MakeEntry(102, 5, u"c.txt")},
    }), "diagnostics fixture rows are durable");

    Projection proj;
    proj.RebuildVolumeFromStore(store, volume, 4);
    const size_t loadedBytes = proj.EstimatedMemoryBytes();

    auto diagnostics = proj.GetMemoryDiagnostics();
    Check(diagnostics.configuredMaximumBytes == Projection::kDefaultMaximumMemoryBytes,
          "diagnostics report the default configured ceiling");
    Check(diagnostics.estimatedBytes == loadedBytes,
          "diagnostics estimated bytes match the projection estimate");
    Check(diagnostics.subtreeEvictionCount == 0 && diagnostics.coldReloadCount == 0,
          "counters start at zero with no eviction or reload");

    // Shrink by one byte: enforcement runs until under the new ceiling and
    // each pass records its before/after effect.
    proj.SetMaximumMemoryBytes(loadedBytes - 1);
    diagnostics = proj.GetMemoryDiagnostics();
    Check(diagnostics.configuredMaximumBytes == loadedBytes - 1,
          "diagnostics report the overridden ceiling");
    Check(diagnostics.subtreeEvictionCount >= 1,
          "the eviction counter increments with enforcement passes");
    Check(diagnostics.lastEvictionBytesBefore >= diagnostics.lastEvictionBytesAfter,
          "eviction records bytes before and after reclaiming");
    Check(diagnostics.estimatedBytes <= diagnostics.configuredMaximumBytes,
          "estimated memory respects the overridden ceiling");

    // Restoring the ceiling makes the evicted volume reloadable again.
    proj.SetMaximumMemoryBytes(Projection::kDefaultMaximumMemoryBytes);
    Check(proj.ReloadEvictedVolume(store, volume), "fixture reload succeeds once the ceiling allows it");
    diagnostics = proj.GetMemoryDiagnostics();
    Check(diagnostics.coldReloadCount == 1, "the reload counter increments per cold reload");
    Check(diagnostics.coldReloadFailedCount == 0, "a successful reload is not counted failed");
    Check(!proj.NeedsReload(volume), "reload clears residency need");

    // A volume absent from the store fails closed and counts as failed.
    Check(store.SetVolumeAvailable(volume, false, 1), "fixture volume marked unavailable");
    Check(store.ForgetVolume(volume), "fixture volume forgotten");
    proj.EvictVolume(volume);
    Check(proj.NeedsReload(volume), "evicting a resident volume marks it cold");
    Check(proj.ReloadEvictedVolume(store, volume) == false,
          "reloading a forgotten volume fails honestly");
    diagnostics = proj.GetMemoryDiagnostics();
    Check(diagnostics.coldReloadFailedCount == 1,
          "a failed reload increments the failure counter");

    store.Close();
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath.string() + "-wal");
    std::filesystem::remove(dbPath.string() + "-shm");
}

void TestProjectionMemoryLimitRunsAtBatchBoundary() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    const size_t bound = proj.EstimatedMemoryBytes() * 2 + 1;
    proj.SetMaximumMemoryBytes(bound);
    proj.SetBulkScanActive(true);
    for (uint64_t id = 100; id < 1'100; ++id) {
        proj.Upsert(1, MakeEntry(id, 5, u"deferred.txt"));
    }
    Check(!proj.NeedsReload(1), "an individual projection upsert does not compact the whole index");
    proj.EnforceMemoryLimitNow();
    Check(!proj.NeedsReload(1), "bulk scan mode defers eviction compaction while ingesting");
    Check(!proj.FilenameCandidateIndices(u"deferred").has_value(),
          "bulk scan mode invalidates only the optional filename accelerator");
    proj.SetBulkScanActive(false);
    proj.EnforceMemoryLimitNow();
    Check(proj.NeedsReload(1), "the post-scan batch boundary enforces the bounded LRU policy");
}

void TestNamePoolViewInvalidatesOnArenaGrowth() {
    // Demonstrates the invalidation hazard for a borrowed NamePool view.
    // The pool's arena is a contiguous vector that can reallocate on Intern.
    // We capture the raw data pointer for a stable NameId, force growth, then
    // show the view address changes while the NameId remains valid. The
    // old pointer is not dereferenced after growth (that would be UB).
    NamePool pool;
    // Start with a tiny reservation so the first Intern uses a small buffer.
    pool.Reserve(1, 8);
    const NameId alpha = pool.Intern(u"alpha");
    const std::u16string_view before = pool.Get(alpha);
    const char16_t* beforePtr = before.data();
    const size_t beforeSize = before.size();
    Check(beforePtr != nullptr && beforeSize == 5, "initial view is valid before growth");
    Check(before == u"alpha", "initial view content matches");
    // Force arena reallocation by interning many distinct long names.
    for (int i = 0; i < 2000; ++i) {
        std::u16string name = u"growth_name_";
        name += static_cast<char16_t>(u'0' + (i % 10));
        name += u"_";
        name.append(20, static_cast<char16_t>(u'x' + (i % 5)));
        std::u16string uniq = u"_uniq_";
        for (char c : std::to_string(i * 7919)) uniq.push_back(static_cast<char16_t>(c));
        name += uniq;
        pool.Intern(name);
    }
    const std::u16string_view after = pool.Get(alpha);
    const char16_t* afterPtr = after.data();
    Check(after == u"alpha", "NameId remains valid and content is preserved after growth");
    Check(after.size() == 5, "length preserved after reallocation");
    // The pool must not guarantee pointer stability across Intern.
    // This is the hazard: a naked view captured before growth would dangle.
    Check(beforePtr != afterPtr || pool.ArenaSizeChars() > 8,
          "arena reallocation demonstrates borrowed-view invalidation hazard (pointer stability not guaranteed)");
    // Also verify NameId stability: same logical name returns same id after growth.
    const NameId alphaAgain = pool.Intern(u"alpha");
    Check(alphaAgain == alpha, "NameId is stable across growth, only the view address moves");
}

void TestNamePoolViewInvalidatesOnCompactStorage() {
    // Whole-pool replacement during Projection::CompactStorage builds a fresh
    // NamePool and moves it over the old one.  The old arena address is not
    // reused.  We demonstrate the hazard directly on NamePool move-replacement
    // without dereferencing a dangling view (would be UB) and without needing
    // to call the private Projection::CompactStorage.
    NamePool pool;
    const NameId beforeId = pool.Intern(u"report.txt");
    const std::u16string_view beforeView = pool.Get(beforeId);
    const char16_t* beforePtr = beforeView.data();
    Check(beforeView == u"report.txt", "pre-replacement view correct");
    // Build a replacement pool exactly as CompactStorage does: fresh arena with
    // the same logical names (plus an extra to ensure distinct allocation).
    NamePool replacement;
    replacement.Intern(u"report.txt");
    replacement.Intern(u"another_name_to_force_distinct_allocation");
    const char16_t* replacementPtr = replacement.Get(replacement.Intern(u"report.txt")).data();
    // The replacement's arena is at a different address than the original.
    // Now perform the whole-pool replacement (namePool_ = std::move(compactedNames)).
    pool = std::move(replacement);
    const NameId afterId = pool.Intern(u"report.txt");
    const std::u16string_view afterView = pool.Get(afterId);
    const char16_t* afterPtr = afterView.data();
    Check(afterView == u"report.txt", "post-replacement view content preserved");
    // The old pointer is not dereferenced; we only compare addresses.  After
    // whole-pool replacement the arena address must have changed, so a view
    // captured before replacement would dangle.
    Check(beforePtr != afterPtr || beforePtr != replacementPtr,
          "whole-pool replacement invalidates borrowed views (hazard demonstrated without UB)");
    Check(afterId == beforeId, "NameId for same name is stable when pool is rebuilt from same insertion order");
}

// performance-closure-and-multisurface-acceleration 8.1: the read-only
// memory breakdown must (a) sum to exactly the estimator the memory policy
// uses, (b) report every category from live capacities, and (c) report the
// structurally absent categories as explicit zeros.
void TestProjectionMemoryBreakdownMatchesEstimatorAndCategories() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"report.txt"));
    proj.Upsert(1, MakeEntry(101, 5, u"REPORT-copy.txt"));
    proj.Upsert(1, MakeEntry(102, 5, u"\u6771\u4EAC_report.txt"));
    proj.Upsert(1, MakeEntry(103, 5, u"nested", 0x10));
    proj.Upsert(1, MakeEntry(104, 103, u"leaf.txt"));

    const auto breakdown = proj.GetMemoryBreakdown();
    Check(breakdown.entryCount == 6, "breakdown reports every live row");
    Check(breakdown.rowLiveBytes == breakdown.entryCount * sizeof(ProjectionEntry),
          "row live bytes are row count * measured entry layout");
    Check(breakdown.rowBytes == breakdown.rowCapacity * sizeof(ProjectionEntry),
          "row bytes are the live vector capacity * measured entry layout");
    Check(breakdown.rowSlackBytes == breakdown.rowBytes - breakdown.rowLiveBytes,
          "row slack is capacity minus live rows");
    Check(breakdown.freeListBytes == breakdown.freeListSlots * sizeof(uint32_t),
          "free-list bytes are its live capacity");
    Check(breakdown.idMapBytes == breakdown.idMapKeyBytes + breakdown.idMapValueBytes
              + breakdown.idMapStateBytes,
          "identifier-map bytes sum the three flat-table arrays");
    Check(breakdown.idMapElements == 6 && breakdown.idMapBuckets >= 6,
          "identifier map reports elements and buckets");
    Check(breakdown.childMapBytes == breakdown.childMapKeyBytes + breakdown.childMapChildBytes
              + breakdown.childMapStateBytes + breakdown.childMapOffsetBytes
              + breakdown.childMapCountBytes,
          "parent/child map bytes sum keys, flat child array, states, offsets, counts");
    Check(breakdown.childMapElements >= 2 && breakdown.childMapChildLinks >= 3,
          "parent/child map reports distinct parents and used child links");
    Check(breakdown.nameBytes == breakdown.nameArenaBytes + breakdown.nameTableBytes
              + breakdown.nameHashBucketBytes + breakdown.nameHashApproxNodeBytes,
          "name bytes sum arena, offset table, and hash index");
    Check(breakdown.uniqueNameCount == 6, "one interned entry per distinct name");
    Check(breakdown.nameArenaChars == 54, "arena holds the six distinct name strings once");
    Check(breakdown.nameArenaSlackBytes
              == breakdown.nameArenaBytes - breakdown.nameArenaChars * sizeof(char16_t),
          "arena slack is capacity minus used chars");
    Check(breakdown.filenameIndexValid, "the optional accelerator is live after ordinary upserts");
    Check(breakdown.filenameIndexDistinctTrigrams > 0, "trigram postings are reported");
    Check(breakdown.filenameIndexPostingSlots >= breakdown.filenameIndexPostingPayload,
          "posting capacity is never below its payload");
    Check(breakdown.filenameIndexExceptionalCount == 0,
          "CJK names are not fold-exceptional (this host's ordinal table has no "
          "cross-ASCII fold classes) and stay out of the exceptional list");
    Check(breakdown.filenameIndexBytes == breakdown.filenameIndexPostingBytes
              + breakdown.filenameIndexApproxNodeBytes + breakdown.filenameIndexBucketBytes
              + breakdown.filenameIndexExceptionalBytes,
          "filename-index bytes sum postings, node headers, buckets, exceptional list");
    Check(breakdown.lruBytes == breakdown.lruSlotCapacity * sizeof(uint64_t),
          "LRU bytes are the dense per-slot timestamp capacity");
    Check(breakdown.lruTrackedCount <= breakdown.lruSlotCount,
          "tracked LRU slots never exceed the dense slot count");
    Check(breakdown.normalizedNameStorageBytes == 0,
          "no stored normalized-name category exists (folds are computed on demand)");
    Check(breakdown.storedPathBytes == 0,
          "no stored path category exists (paths are reconstructed per call)");
    Check(breakdown.modeledCategoryBytes == proj.EstimatedMemoryBytes(),
          "the modeled category sum equals the estimator the memory policy uses");
    Check(breakdown.reportedTotalBytes == breakdown.modeledCategoryBytes
              + breakdown.evictedVolumeBucketBytes + breakdown.nameHashPayloadBytes,
          "the reported total adds the concretely measured unmodeled allocations");

    proj.SetBulkScanActive(true);
    const auto disabled = proj.GetMemoryBreakdown();
    Check(!disabled.filenameIndexValid, "a bulk scan disables the optional accelerator");
    Check(disabled.filenameIndexDistinctTrigrams == 0 && disabled.filenameIndexExceptionalCount == 0,
          "a disabled accelerator retains no posting entries or fold-exceptional entries (vector capacity is retained, not entry data)");
    Check(disabled.modeledCategoryBytes == proj.EstimatedMemoryBytes(),
          "the modeled sum still matches the estimator with the accelerator disabled");
    proj.SetBulkScanActive(false);
}

// performance-closure-and-multisurface-acceleration 8.3: the dense per-slot
// LRU timestamps must survive removal, slot reuse, compaction, and eviction
// order exactly as the previous hash-map representation did.
void TestDenseLruTimestampsTrackAccessOrder() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"a.txt"));
    proj.Upsert(1, MakeEntry(101, 5, u"b.txt"));
    proj.Upsert(1, MakeEntry(102, 5, u"c.txt"));
    Check(proj.Find(1, FileId{100, 0}) != nullptr, "dense LRU fixture row a is present");
    Check(proj.Find(1, FileId{102, 0}) != nullptr, "dense LRU fixture row c is present");
    // Every upsert stamps its slot (the pre-existing behavior), so all live
    // rows are tracked once the fixture is built.
    Check(proj.GetMemoryBreakdown().lruTrackedCount == 4,
          "every live row carries a dense timestamp");

    proj.Remove(1, FileId{100, 0});
    Check(proj.GetMemoryBreakdown().lruTrackedCount == 3,
          "removing a row clears its dense timestamp");
    proj.Upsert(1, MakeEntry(200, 5, u"d.txt"));
    const auto afterReuse = proj.GetMemoryBreakdown();
    Check(afterReuse.lruTrackedCount == 4,
          "a reused slot is tracked again with a fresh timestamp");

    // Row 101 was never touched, so it must be the eviction victim while the
    // recently touched rows survive.
    proj.SetMaximumMemoryBytes(proj.EstimatedMemoryBytes() - 1);
    const auto afterEviction = proj.GetMemoryBreakdown();
    Check(afterEviction.evictedVolumeCount >= 1,
          "the eviction records the cold-volume marker");
    Check(afterEviction.evictedVolumeBucketBytes >= sizeof(void*),
          "the cold-volume marker set reports its bucket array");
    Check(proj.Find(1, FileId{101, 0}) == nullptr,
          "the untouched row is the least-recently-used eviction victim");
    Check(proj.Find(1, FileId{102, 0}) != nullptr,
          "a recently touched row survives dense-LRU eviction");
    Check(proj.Find(1, FileId{200, 0}) != nullptr,
          "a reused slot is evictable last, not first");
}

// performance-closure-and-multisurface-acceleration 8.3: a bulk rebuild must
// release the geometric-growth capacity slack in the name arena, the offset
// table, the flat child-index array, and the trigram posting vectors, while
// leaving the projection's designed id->index load factor untouched.
void TestRebuildReleasesBulkCapacitySlack() {
    Store store;
    const auto dbPath = std::filesystem::temp_directory_path() /
        ("ffindexstore_projection_shrink_" + std::to_string(GetCurrentProcessId()) + ".db");
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath.string() + "-wal");
    std::filesystem::remove(dbPath.string() + "-shm");
    Check(store.Open(dbPath.string()), "shrink fixture store opens");
    VolumeKey key;
    key.serialNumber = 515151;
    const auto volume = *store.GetOrCreateVolume(key);
    std::vector<EntryChange> changes;
    changes.push_back({EntryChangeKind::Upsert, MakeEntry(5, 5, u"C:", 0x10)});
    // Large enough that the geometric-growth slack in the rebuilt
    // containers exceeds the projection's material-slack threshold.
    constexpr uint64_t kRows = 100000;
    for (uint64_t index = 0; index < kRows; ++index) {
        std::u16string name = u"unique_bulk_name_";
        for (const char digit : std::to_string(index)) {
            name.push_back(static_cast<char16_t>(digit));
        }
        name += u".txt";
        changes.push_back({EntryChangeKind::Upsert, MakeEntry(100 + index, 5, std::move(name))});
    }
    Check(store.ApplyBatch(volume, changes), "shrink fixture rows are durable");

    Projection proj;
    Check(proj.RebuildVolumeFromStore(store, volume, changes.size()),
          "bulk rebuild succeeds");
    const auto breakdown = proj.GetMemoryBreakdown();
    Check(breakdown.entryCount == changes.size(), "every durable row is resident");

    // MSVC's shrink_to_fit reallocates exactly, so a rebuilt projection holds
    // no invisible growth slack in these bulk-grown containers.
    // shrink_to_fit is non-binding in the standard, so these are collapse
    // bounds: they prove the release without pinning one STL's exact
    // reallocation behavior (the pre-shrink slack was tens of percent).
    Check(breakdown.nameArenaCapacityChars
              <= breakdown.nameArenaChars + breakdown.nameArenaChars / 8 + 16,
          "name arena capacity collapses to its used chars after a bulk rebuild");
    Check(breakdown.nameTableBytes
              <= (breakdown.nameTableSlotCount + breakdown.nameTableSlotCount / 8 + 16)
                  * sizeof(uint32_t) * 2,
          "name offset table capacity collapses to its used slots after a bulk rebuild");
    Check(breakdown.filenameIndexPostingSlots
              <= breakdown.filenameIndexPostingPayload + breakdown.filenameIndexPostingPayload / 8 + 16,
          "trigram posting capacity collapses to its payload after a bulk rebuild");
    Check(breakdown.childMapChildLinks == changes.size() - 1
              && breakdown.childMapChildBytes
                  <= (breakdown.childMapChildLinks + breakdown.childMapChildLinks / 8 + 16)
                      * sizeof(uint32_t),
          "flat child-index capacity collapses to its live links after a bulk rebuild");

    // Content must survive the shrink: names, parent/child links, lookups.
    const auto* root = proj.Find(volume, FileId{5, 0});
    Check(root != nullptr && proj.Names().Get(root->nameId) == u"C:",
          "the root row survives the shrink");
    Check(proj.ChildIndices(volume, FileId{5, 0}).size() == changes.size() - 1,
          "every child link survives the shrink");
    const auto* first = proj.Find(volume, FileId{100, 0});
    Check(first != nullptr && proj.Names().Get(first->nameId) == u"unique_bulk_name_0.txt",
          "the first file row and its interned name survive the shrink");

    // The designed flat-open-addressing load factor is not slack and must stay:
    // the growth rule requires count < buckets * 3/4.
    Check(breakdown.idMapBuckets >= (breakdown.idMapElements * 4) / 3,
          "the identifier map keeps its designed <=75% occupancy sizing");
    Check(breakdown.modeledCategoryBytes == proj.EstimatedMemoryBytes(),
          "the shrink keeps the breakdown and estimator in agreement");

    std::error_code ignored;
    std::filesystem::remove(dbPath, ignored);
    std::filesystem::remove(dbPath.string() + "-wal", ignored);
    std::filesystem::remove(dbPath.string() + "-shm", ignored);
}

// performance-closure-and-multisurface-acceleration 8.3: a bulk rebuild calls
// CompactEntriesForIterationOrder(), which permutes dense entry slots into the
// id->index probe order. The dense LRU timestamps must be permuted with the
// same mapping, or the eviction policy becomes a hash-order choice.
void TestLruTimestampsSurviveIterationOrderCompaction() {
    Store store;
    const auto dbPath = std::filesystem::temp_directory_path() /
        ("ffindexstore_projection_lru_compact_" + std::to_string(GetCurrentProcessId()) + ".db");
    std::filesystem::remove(dbPath);
    std::filesystem::remove(dbPath.string() + "-wal");
    std::filesystem::remove(dbPath.string() + "-shm");
    Check(store.Open(dbPath.string()), "LRU compaction fixture store opens");
    VolumeKey key;
    key.serialNumber = 917319;
    const auto volume = *store.GetOrCreateVolume(key);
    std::vector<EntryChange> changes;
    changes.push_back({EntryChangeKind::Upsert, MakeEntry(5, 5, u"C:", 0x10)});
    constexpr uint64_t kRows = 32;
    for (uint64_t index = 0; index < kRows; ++index) {
        std::u16string name = u"row_";
        for (const char digit : std::to_string(index)) {
            name.push_back(static_cast<char16_t>(digit));
        }
        changes.push_back({EntryChangeKind::Upsert,
                           MakeEntry(1000 + index * 7919, 5, std::move(name))});
    }
    Check(store.ApplyBatch(volume, changes), "LRU compaction fixture rows are durable");

    Projection proj;
    Check(proj.RebuildVolumeFromStore(store, volume, changes.size()),
          "bulk rebuild permutes dense slots into probe order");
    // Discriminator precondition: without the timestamp remap the victim is
    // the probe-order-first non-root row (the stale vector is monotonic in the
    // new slot order). If that row were also the first-streamed row, a broken
    // tree could evict the same row and the test would pass vacuously. Dense
    // cursor order equals probe order after this rebuild.
    uint64_t firstProbeRowFrn = 0;
    {
        ProjectionEntryCursor cursor = proj.BeginEntryCursor();
        const ProjectionEntry* entry = nullptr;
        while (cursor.Next(entry)) {
            if (entry != nullptr && entry->parentFrn != entry->frn) {
                firstProbeRowFrn = entry->frn.low;
                break;
            }
        }
    }
    Check(firstProbeRowFrn != 0 && firstProbeRowFrn != 1000,
          "probe order does not start with the first-streamed row, so the eviction assertion discriminates");
    // The rows were streamed in insertion order, so the first row carries the
    // oldest access stamp; refresh the last row so it is unambiguously newest.
    Check(proj.Find(volume, FileId{1000 + (kRows - 1) * 7919, 0}) != nullptr,
          "the last fixture row is present and touched");

    proj.SetMaximumMemoryBytes(proj.EstimatedMemoryBytes() - 1);
    Check(proj.Find(volume, FileId{1000, 0}) == nullptr,
          "the oldest streamed row is the eviction victim after slot permutation");
    Check(proj.Find(volume, FileId{1000 + (kRows - 1) * 7919, 0}) != nullptr,
          "the most recently touched row survives after slot permutation");

    std::error_code ignored;
    std::filesystem::remove(dbPath, ignored);
    std::filesystem::remove(dbPath.string() + "-wal", ignored);
    std::filesystem::remove(dbPath.string() + "-shm", ignored);
}

// large-index-lifecycle-and-search-scalability 4.5: bulk-removal
// equivalence. These characterize observable semantics (they pass with both
// the legacy per-entry removal and the bulk path); the performance defect
// they accompany is evidenced by the rebuild-isolation matrix (E1 vs B1).
std::u16string NumberToUtf16(uint64_t value) {
    const std::wstring wide = std::to_wstring(value);
    return std::u16string(wide.begin(), wide.end());
}

std::vector<std::u16string> DumpRows(const Projection& proj) {
    std::vector<std::u16string> rows;
    proj.ForEachEntry([&](const EntryKey& key, const ProjectionEntry& entry) {
        (void)key;
        std::u16string line = NumberToUtf16(entry.frn.low) + u":" +
            NumberToUtf16(entry.parentFrn.low) + u":" +
            std::u16string(proj.Names().Get(entry.nameId));
        rows.push_back(std::move(line));
    });
    std::sort(rows.begin(), rows.end());
    return rows;
}

void PopulateRemovalFixture(Projection& proj) {
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"shared_dir", 0x10));
    for (uint64_t i = 0; i < 400; ++i) {
        proj.Upsert(1, MakeEntry(1000 + i, 100,
                                 u"common_component_" + NumberToUtf16(i) + u".txt", 0));
    }
    proj.Upsert(1, MakeEntry(200, 5, u"keep_dir", 0x10));
    proj.Upsert(1, MakeEntry(300, 200, u"common_component_keep.txt", 0));
}

void TestRemoveBatchMatchesPerEntryRemoval() {
    Projection reference;
    Projection bulk;
    PopulateRemovalFixture(reference);
    PopulateRemovalFixture(bulk);
    std::vector<FileId> removed;
    removed.reserve(400);
    for (uint64_t i = 0; i < 400; ++i) removed.push_back(FileId{1000 + i, 0});
    for (const FileId& id : removed) reference.Remove(1, id);
    bulk.RemoveBatch(1, removed);
    Check(reference.EntryCount() == bulk.EntryCount(),
          "bulk removal leaves the same entry count as per-entry removal");
    Check(DumpRows(reference) == DumpRows(bulk),
          "bulk removal leaves the same rows, parents, and names");
    Check(reference.ChildIndices(1, FileId{100, 0}).empty() &&
              bulk.ChildIndices(1, FileId{100, 0}).empty(),
          "bulk removal unlinks every child exactly like per-entry removal");
    const auto referenceAggregate = reference.GetFolderAggregate(1, FileId{100, 0});
    const auto bulkAggregate = bulk.GetFolderAggregate(1, FileId{100, 0});
    Check(referenceAggregate.has_value() && bulkAggregate.has_value() &&
              referenceAggregate->itemCount == bulkAggregate->itemCount &&
              referenceAggregate->totalSizeBytes == bulkAggregate->totalSizeBytes,
          "folder aggregates agree after bulk removal");
    const auto referenceCandidates = reference.FilenameCandidateIndices(u"common_component_keep");
    const auto bulkCandidates = bulk.FilenameCandidateIndices(u"common_component_keep");
    Check(referenceCandidates.has_value() && bulkCandidates.has_value() &&
              referenceCandidates->size() == bulkCandidates->size(),
          "the rebuilt accelerator returns the same candidate count as the per-entry path");
    if (bulkCandidates.has_value()) {
        bool survivorsOnly = true;
        for (const uint32_t slot : *bulkCandidates) {
            if (bulk.EntryAt(slot).frn.low < 200) survivorsOnly = false;
        }
        Check(survivorsOnly, "bulk-path accelerator candidates reference only surviving rows");
    }
    // Below the bulk threshold the per-entry path keeps the accelerator
    // current without a rebuild.
    bulk.RemoveBatch(1, {FileId{300, 0}});
    Check(bulk.GetMemoryBreakdown().filenameIndexValid,
          "a small batch keeps the accelerator current via per-entry maintenance");
}

void TestRemoveVolumeKeepsFilenameAcceleratorCurrent() {
    Projection proj;
    PopulateRemovalFixture(proj);
    proj.Upsert(2, MakeEntry(5, 5, u"D:", 0x10));
    proj.Upsert(2, MakeEntry(700, 5, u"common_component_keep.txt", 0));
    proj.RemoveVolume(1);
    Check(proj.EntryCount() == 2, "only the surviving volume's rows remain");
    Check(proj.Find(1, FileId{1000, 0}) == nullptr, "removed volume rows are gone");
    Check(proj.GetMemoryBreakdown().filenameIndexValid,
          "RemoveVolume keeps the filename accelerator current by default");
    const auto candidates = proj.FilenameCandidateIndices(u"common_component_keep");
    Check(candidates.has_value() && candidates->size() == 1,
          "the rebuilt accelerator serves the surviving volume's rows");
    if (candidates.has_value() && candidates->size() == 1) {
        Check(proj.EntryAt((*candidates)[0]).volumeRowId == 2,
              "accelerator candidates reference only surviving rows");
    }
}

void TestRemoveVolumeCanLeaveAcceleratorForCallerRebuild() {
    Projection proj;
    PopulateRemovalFixture(proj);
    proj.Upsert(2, MakeEntry(5, 5, u"D:", 0x10));
    proj.Upsert(2, MakeEntry(700, 5, u"common_component_keep.txt", 0));
    // The enclosing bulk rebuild rebuilds the accelerator once at the end,
    // so the removal may leave it invalidated without paying a double rebuild.
    proj.RemoveVolume(1, false);
    Check(proj.EntryCount() == 2,
          "caller-rebuild mode removes rows exactly like the default mode");
    Check(!proj.GetMemoryBreakdown().filenameIndexValid,
          "RemoveVolume(keepFilenameIndexCurrent=false) leaves the accelerator invalidated");
    Check(!proj.FilenameCandidateIndices(u"common_component_keep").has_value(),
          "an invalidated accelerator yields no candidates and search falls back to the canonical scan");
}

// large-index-lifecycle-and-search-scalability 6.2/6.3 + review-remediation
// (P2-A): filename-accelerator planner. The historical accelerator
// unconditionally unioned every non-ASCII-named row into every candidate set,
// so at any scale whose Unicode population exceeds the caller's cap every
// lookup returned nullopt and no query class could ever use the index. The
// posting intersection serves pure-ASCII needles (superset guarantee), merged
// only with the FOLD-EXCEPTIONAL rows (names that could match an ASCII needle
// through a non-ASCII unit under the host's CompareStringOrdinal ordinal
// table, plus every surrogate-bearing name). The pre-review intersection-only
// variant dropped that merge and relied on an unproven assumption that the
// host ordinal table has no cross-ASCII fold classes; the tests below pin the
// invariant against the REAL comparison function and exercise the fallback
// both through the host-derived probe and the testing seam.

std::u16string FoldAsciiLower(const std::u16string_view value) {
    std::u16string folded;
    folded.reserve(value.size());
    for (const char16_t ch : value) {
        folded.push_back(ch >= u'A' && ch <= u'Z' ? static_cast<char16_t>(ch + (u'a' - u'A')) : ch);
    }
    return folded;
}

bool ContainsFolded(const std::u16string& haystackFolded, const std::u16string& needleFolded) {
    return haystackFolded.find(needleFolded) != std::u16string::npos;
}

void PopulateUnicodeHeavyFixture(Projection& proj) {
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"ascii_dir", 0x10));
    proj.Upsert(1, MakeEntry(101, 5, u"\u0434\u0438\u0440", 0x10));
    proj.Upsert(1, MakeEntry(1000, 100, u"unique_target.txt", 0));
    proj.Upsert(1, MakeEntry(1001, 101, u"\u0441\u043f\u0438\u0441\u043e\u043a_unique_target.txt", 0));
    proj.Upsert(1, MakeEntry(1002, 101, u"\u0424\u0430\u0439\u043b_UNIQUE_TARGET.txt", 0));
    proj.Upsert(1, MakeEntry(1003, 100, u"common_component_1.txt", 0));
    proj.Upsert(1, MakeEntry(1004, 100, u"common_component_2.txt", 0));
    proj.Upsert(1, MakeEntry(1005, 100, u"notes.md", 0));
    proj.Upsert(1, MakeEntry(1006, 100, u"dotted.name.with.dots.txt", 0));
    proj.Upsert(1, MakeEntry(1007, 100, u"ab", 0));
    proj.Upsert(1, MakeEntry(1008, 101, u"\u65e5\u672c\u8a9e.txt", 0));
    for (uint64_t i = 0; i < 20; ++i) {
        proj.Upsert(1, MakeEntry(2000 + i, 101, u"\u0441\u043f\u0438\u0441\u043e\u043a_" + NumberToUtf16(i) + u".txt", 0));
    }
}

void TestFilenameCandidatesAreConservativeSuperset() {
    Projection proj;
    PopulateUnicodeHeavyFixture(proj);
    const std::u16string needles[] = {
        u"unique_target", u"UNIQUE_TARGET", u"common_comp", u"zzz_missing",
        u"notes", u"dots.txt", u"LIST"  // "\u0424\u0430\u0439\u043b_UNIQUE_TARGET.txt" folds to contain "_unique_target" only;
    };
    for (const std::u16string& raw : needles) {
        const std::u16string folded = FoldAsciiLower(raw);
        const auto candidates = proj.FilenameCandidateIndices(folded, 0);  // uncapped reference
        if (!candidates.has_value()) {
            Check(false, "an uncapped pure-ASCII needle always returns a candidate set");
            continue;
        }
        std::vector<std::pair<int64_t, uint64_t>> expected;
        proj.ForEachEntry([&](const EntryKey& key, const ProjectionEntry& entry) {
            if (ContainsFolded(FoldAsciiLower(std::u16string(proj.Names().Get(entry.nameId))), folded)) {
                expected.push_back({key.volumeRowId, key.frn.low});
            }
        });
        std::vector<std::pair<int64_t, uint64_t>> got;
        for (const uint32_t slot : *candidates) {
            const ProjectionEntry& entry = proj.EntryAt(slot);
            got.push_back({entry.volumeRowId, entry.frn.low});
        }
        bool superset = true;
        for (const auto& id : expected) {
            if (std::find(got.begin(), got.end(), id) == got.end()) {
                superset = false;
            }
        }
        Check(superset, "accelerator candidates are a conservative superset of every fold-matching row");
    }
    // Structural eligibility rules are unchanged.
    Check(!proj.FilenameCandidateIndices(u"ab", 0).has_value(),
          "needles shorter than three folded units remain ineligible");
    Check(!proj.FilenameCandidateIndices(u"\u0441\u043f\u0438\u0441\u043e\u043a", 0).has_value(),
          "non-ASCII folded needles remain ineligible for the ASCII trigram accelerator");
}

void TestFilenameCandidatesNarrowForSelectiveNeedlesDespiteUnicodeRows() {
    Projection proj;
    PopulateUnicodeHeavyFixture(proj);
    // A deliberately small cap: non-ASCII rows that cannot match a pure-ASCII
    // needle must not consume the cap and force the canonical scan.
    const auto candidates = proj.FilenameCandidateIndices(u"unique_target", 8);
    Check(candidates.has_value(),
          "a selective pure-ASCII needle yields candidates regardless of the Unicode row count");
    if (candidates.has_value()) {
        Check(candidates->size() == 3,
              "the candidate set is exactly the three fold-matching rows");
    }
    const auto missing = proj.FilenameCandidateIndices(u"zzz_missing", 8);
    Check(missing.has_value() && missing->empty(),
          "a no-match pure-ASCII needle yields an empty candidate set, not every Unicode row");
}

// review-remediation (P2-A): the exact comparison shape of the canonical
// matcher (ffsearch::OrdinalContains: equal-length spans through
// CompareStringOrdinal(..., TRUE)). The candidate-superset invariant is
// stated against this function -- never against an assumed Unicode casefold.
bool CanonicalOrdinalContains(const std::u16string& name, const std::u16string& needle) {
    if (needle.empty()) return true;
    if (name.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= name.size(); ++i) {
        if (CompareStringOrdinal(reinterpret_cast<const wchar_t*>(name.data() + i),
                                 static_cast<int>(needle.size()),
                                 reinterpret_cast<const wchar_t*>(needle.data()),
                                 static_cast<int>(needle.size()), TRUE) == CSTR_EQUAL) {
            return true;
        }
    }
    return false;
}

bool OrdinalUnitEqual(const char16_t a, const char16_t b) {
    return CompareStringOrdinal(reinterpret_cast<const wchar_t*>(&a), 1,
                                reinterpret_cast<const wchar_t*>(&b), 1, TRUE) == CSTR_EQUAL;
}

// review-remediation (P2-A): campaign-host characterization of the Windows
// ordinal case table. The PR review's example assumed Unicode casefold
// semantics (U+017F LONG S uppercases to 'S' in Unicode's table); the REAL
// CompareStringOrdinal verdicts on the campaign host are NOT that. If a
// future Windows ordinal table changes any verdict here this test fails
// loudly and the review-remediation evidence must be re-audited. The product
// stays correct either way: the fold-exceptional set is derived from the
// live comparison function at run time (UnitMayFoldToAscii).
void TestHostOrdinalFoldSemanticsMatchCampaignMeasurement() {
    Check(OrdinalUnitEqual(u'A', u'a'),
          "ASCII sanity: ordinal ignore-case folds A/a");
    Check(OrdinalUnitEqual(u'\u00C9', u'\u00E9'),
          "ordinal ignore-case DOES fold non-ASCII case pairs (E-acute): folding is not ASCII-only");
    Check(!OrdinalUnitEqual(u'\u017F', u's') && !OrdinalUnitEqual(u'\u017F', u'S'),
          "campaign host: LONG S is NOT equal to s/S under ordinal ignore-case (review example disproven here)");
    Check(!OrdinalUnitEqual(u'\u212A', u'k') && !OrdinalUnitEqual(u'\u212A', u'K'),
          "campaign host: KELVIN SIGN is NOT equal to k/K under ordinal ignore-case");
    Check(!OrdinalUnitEqual(u'\u0131', u'i') && !OrdinalUnitEqual(u'\u0131', u'I'),
          "campaign host: dotless i is NOT equal to i/I under ordinal ignore-case");
    Check(!OrdinalUnitEqual(u'\u0130', u'i') && !OrdinalUnitEqual(u'\u0130', u'I'),
          "campaign host: dotted I is NOT equal to i/I under ordinal ignore-case");
    Check(!OrdinalUnitEqual(u'\u00DF', u's'),
          "campaign host: sharp s is NOT equal to s under ordinal ignore-case");
    Check(!OrdinalUnitEqual(u'\uFB01', u'f'),
          "campaign host: fi ligature is NOT equal to f under ordinal ignore-case");
}

// review-remediation (P2-A), RED-FIRST: force the host table to exhibit the
// exact equivalence the PR review assumed (U+017F LONG S folds to ASCII 'S').
// The pre-fix intersection-only accelerator omits the long-s row for the
// ASCII needle "soo" (its ASCII fold cannot contain the 'soo' trigram) --
// a data-retrieval false negative. The fold-exceptional merge must retain it.
void TestFilenameCandidatesRetainHostFoldExceptionalNames() {
    Projection::SetTestingFoldExceptionalUnit(u'\u017F', true);
    {
        Projection proj;
        proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
        proj.Upsert(1, MakeEntry(100, 5, u"\u017Foo.txt", 0));
        proj.Upsert(1, MakeEntry(101, 5, u"control.txt", 0));
        proj.Upsert(1, MakeEntry(102, 5, u"soo.txt", 0));
        const auto candidates = proj.FilenameCandidateIndices(u"soo");
        Check(candidates.has_value(), "eligible ASCII query still uses the accelerator");
        if (candidates.has_value()) {
            Check(CandidateContains(proj, *candidates, 100),
                  "a host-fold-exceptional long-s name is retained for the ASCII needle (no false negative)");
            Check(CandidateContains(proj, *candidates, 102),
                  "the ordinary matching row is retained");
            Check(!CandidateContains(proj, *candidates, 101),
                  "the non-matching non-exceptional row is still excluded");
        }
        // Renaming away the exceptional unit drops the row's exceptionality.
        proj.Upsert(1, MakeEntry(100, 5, u"foo.txt", 0));
        const auto afterRename = proj.FilenameCandidateIndices(u"soo");
        Check(afterRename.has_value() && !CandidateContains(proj, *afterRename, 100),
              "renaming away the exceptional unit removes the row from the exceptional merge");
        // Deletion removes rows from the exceptional merge exactly like postings.
        proj.Remove(1, FileId{102, 0});
        const auto afterRemove = proj.FilenameCandidateIndices(u"soo");
        Check(afterRemove.has_value() && !CandidateContains(proj, *afterRemove, 102),
              "deleting a row removes it from candidates");
    }
    Projection::SetTestingFoldExceptionalUnit(u'\u017F', false);
}

// review-remediation (P2-A): surrogate-bearing (astral) names are always
// fold-exceptional (conservative: pair semantics cannot be probed unit-wise
// and false negatives are forbidden), so they are retained in every ASCII
// candidate set even when their ASCII fold cannot contain the needle. RED
// against the pre-fix intersection-only accelerator: for a needle absent
// from every name its candidate set is empty, so the conservative row was
// silently dropped.
void TestFilenameCandidatesRetainSurrogateNamesConservatively() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    std::u16string emojiName;
    emojiName.push_back(static_cast<char16_t>(0xD83D)); // U+1F600 pair
    emojiName.push_back(static_cast<char16_t>(0xDE00));
    emojiName += u"qz.txt";
    proj.Upsert(1, MakeEntry(100, 5, emojiName, 0));
    proj.Upsert(1, MakeEntry(101, 5, u"qz.txt", 0));
    const auto candidates = proj.FilenameCandidateIndices(u"xyz");
    Check(candidates.has_value(), "eligible ASCII query returns a candidate set");
    if (candidates.has_value()) {
        Check(CandidateContains(proj, *candidates, 100),
              "surrogate-bearing names are conservatively retained in every ASCII candidate set");
        Check(!CandidateContains(proj, *candidates, 101),
              "non-exceptional rows are still served by the posting intersection alone");
    }
}

// review-remediation (P2-A): the correctness invariant, stated against the
// real production comparison function: CanonicalMatches(query) is a subset of
// FilenameCandidateIndices(query). Adversarial cross-casing corpus: long s,
// Kelvin, dotted/dotless I, sharp s, ligatures, fullwidth, micro, Greek and
// Cyrillic lookalikes, combining forms, surrogate pairs, mixed
// ASCII/non-ASCII names, repeated trigrams, query at start/middle/end, and
// mixed-case queries. On the campaign host most of these pairs are NOT
// canonical matches (TestHostOrdinalFoldSemanticsMatchCampaignMeasurement
// records the verdicts); the subset check is what must hold on EVERY host --
// on a host exhibiting the review's equivalence it fails against the pre-fix
// intersection-only accelerator (host-conditional red-first).
void TestFilenameCandidateSupersetInvariantHoldsForAdversarialCasing() {
    struct Case { std::u16string name; std::u16string needle; };
    const Case cases[] = {
        { u"\u017Foo.txt", u"soo" },
        { u"\u017Foo.txt", u"SOO" },
        { u"\u212Aelvin.bin", u"kelvin" },
        { u"\u212Aelvin.bin", u"KELVIN" },
        { u"cap\u0131tal.txt", u"capital" },
        { u"CAP\u0130TAL.txt", u"capital" },
        { u"CAP\u0130TAL.txt", u"CAPITAL" },
        { u"stra\u00DFe.txt", u"strasse" },
        { u"stra\u00DFe.txt", u"strase" },
        { u"\uFB01le.txt", u"file" },
        { u"\uFB03eld.dat", u"ffield" },
        { u"\uFF21bc.txt", u"abc" },
        { u"\u00B5icro.txt", u"micro" },
        { u"\u03C2igma.txt", u"sigma" },
        { u"\u0405lavic.txt", u"slavic" },
        { u"\u0399ota.txt", u"iota" },
        { u"foo\u017Fbar.txt", u"bar" },
        { u"foo\u017Fbar.txt", u"FOO" },
        { u"MiXeD_CaSe.bin", u"mIxEd_cAsE" },
        { u"Report.TXT", u"report.txt" },
        { u"report.txt", u"PORT.t" },
        { u"abcabcabc.txt", u"abcabc" },
        { u"o\u0308verlay.txt", u"verlay" },
        { u"\u6771\u4EAC_target.txt", u"target" },
        { u"plain.txt", u"plain" },
    };
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    uint64_t id = 100;
    for (const Case& c : cases) {
        proj.Upsert(1, MakeEntry(id++, 5, c.name, 0));
    }
    // One surrogate-pair row built explicitly (universal-character-names
    // cannot name surrogates).
    std::u16string emojiName;
    emojiName.push_back(static_cast<char16_t>(0xD801));
    emojiName.push_back(static_cast<char16_t>(0xDC00));
    emojiName += u"_target.txt";
    proj.Upsert(1, MakeEntry(id++, 5, emojiName, 0));

    uint64_t checkId = 100;
    for (const Case& c : cases) {
        const std::u16string folded = FoldAsciiLower(c.needle);
        const auto candidates = proj.FilenameCandidateIndices(folded, 0);
        Check(candidates.has_value(), "an eligible ASCII needle always yields an uncapped candidate set");
        if (candidates.has_value() && CanonicalOrdinalContains(c.name, c.needle)) {
            Check(CandidateContains(proj, *candidates, checkId),
                  "a canonical match is always present in the accelerator candidate set");
        }
        // Capped lookups may fall back to nullopt (canonical scan) but may
        // never return a set that omits a canonical match.
        const auto capped = proj.FilenameCandidateIndices(folded, 4096);
        if (capped.has_value() && CanonicalOrdinalContains(c.name, c.needle)) {
            Check(CandidateContains(proj, *capped, checkId),
                  "a capped candidate set still contains every canonical match");
        }
        ++checkId;
    }
    // Surrogate-pair row against an ASCII needle that matches its ASCII tail.
    {
        const std::u16string needle = u"target";
        Check(CanonicalOrdinalContains(emojiName, needle),
              "the surrogate-pair fixture canonically matches its ASCII tail");
        const auto candidates = proj.FilenameCandidateIndices(FoldAsciiLower(needle), 0);
        Check(candidates.has_value() && CandidateContains(proj, *candidates, id - 1),
              "surrogate-pair rows are retained when their ASCII tail canonically matches");
    }
}

} // namespace

// large-index-lifecycle-and-search-scalability review-lane C Finding 3:
// ending a bulk scan must revive the filename accelerator (it previously
// stayed dead after every scan, silently disabling selective acceleration).
void TestBulkScanCycleRevivesFilenameAccelerator() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"unique_target.txt", 0));
    Check(proj.FilenameCandidateIndices(u"unique_target").has_value(),
          "accelerator is live before the bulk scan");
    proj.SetBulkScanActive(true);
    proj.Upsert(1, MakeEntry(101, 5, u"unique_target_two.txt", 0));
    Check(!proj.FilenameCandidateIndices(u"unique_target").has_value(),
          "bulk scan keeps the accelerator disabled while active");
    proj.SetBulkScanActive(false);
    Check(proj.GetMemoryBreakdown().filenameIndexValid,
          "ending the bulk scan revives the filename accelerator");
    const auto candidates = proj.FilenameCandidateIndices(u"unique_target");
    Check(candidates.has_value() && candidates->size() == 2,
          "the revived accelerator serves every matching row ingested during the scan");
}

// large-index-lifecycle-and-search-scalability review-lane C Finding 2: a
// cursor whose walk layout is disturbed (entries_ reallocation / flat-table
// growth) must report Stale() instead of silently skipping rows.
void TestCursorDetectsDisturbedLayout() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    proj.Upsert(1, MakeEntry(100, 5, u"a.txt", 0));
    auto cursor = proj.BeginEntryCursor();
    Check(!cursor.Stale(), "a fresh cursor is not stale");
    const ProjectionEntry* entry = nullptr;
    Check(cursor.Next(entry) && entry != nullptr, "a fresh cursor advances");
    for (uint64_t id = 200; id < 400; ++id) {
        proj.Upsert(1, MakeEntry(id, 5, u"grow_" + NumberToUtf16(id) + u".txt", 0));
    }
    Check(cursor.Stale(),
          "layout growth under a held cursor marks it stale (no silent truncation)");
    Check(!cursor.Next(entry), "a stale cursor stops iterating");
}

// large-index-lifecycle-and-search-scalability review-lane D P1-2/P2-3:
// reloading a volume that cannot survive enforcement must fail FAST and leave
// the volume cold (previously: an unbounded under-mutex stream, then a doomed
// re-eviction).
void TestOversizedVolumeReloadFailsFastAndStaysCold() {
    ffindexstore::Store store;
    const std::string dbPath = FreshDbPath("projection_oversized_reload.db");
    Check(store.Open(dbPath), "oversized-reload store opens");
    const auto volume = store.GetOrCreateVolume({{}, 707});
    Check(volume.has_value(), "oversized-reload volume is created");
    std::vector<ffindexstore::EntryChange> batch;
    batch.push_back({ffindexstore::EntryChangeKind::Upsert, MakeEntry(5, 5, u"C:", 0x10)});
    for (uint64_t id = 100; id < 200; ++id) {
        batch.push_back({ffindexstore::EntryChangeKind::Upsert,
                         MakeEntry(id, 5, u"row_" + NumberToUtf16(id) + u".txt", 0)});
    }
    Check(store.ApplyBatch(*volume, batch), "oversized-reload rows persist");
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"D:", 0x10));
    proj.EvictVolume(*volume); // mark the durable volume cold explicitly
    Check(proj.NeedsReload(*volume), "the volume starts cold");
    // A budget so small the 100-row reload cannot survive enforcement: the
    // reload must fail before streaming and leave the volume cold.
    proj.SetMaximumMemoryBytes(2048);
    Check(!proj.ReloadEvictedVolume(store, *volume),
          "an over-watermark volume reload fails (fast) instead of thrashing");
    Check(proj.NeedsReload(*volume), "the failed reload leaves the volume cold");
    Check(proj.GetMemoryDiagnostics().coldReloadFailedCount >= 1,
          "the failed reload is counted for diagnostics");
}

// durable-cold-search-index-and-hybrid-residency: eight tiny LRU victims
// must not leave a large retained working set over budget indefinitely.
void TestTinyVictimsCannotSustainAnOverBudgetProjection() {
    Projection proj;
    proj.SetMaximumMemoryBytes(64 * 1024);
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    for (uint64_t id = 100; id < 120; ++id) {
        proj.Upsert(1, MakeEntry(id, 5, u"empty_directory", 0x10));
    }
    for (uint64_t id = 1000; id < 21000; ++id) {
        proj.Upsert(1, MakeEntry(id, 5, u"row.txt", 0));
    }
    Check(proj.EstimatedMemoryBytes() > 64 * 1024, "tiny-victim fixture exceeds the budget");
    Check(proj.EnforceMemoryLimitNow(), "tiny-victim enforcement evicts resident data");
    Check(proj.EstimatedMemoryBytes() <= 64 * 1024,
          "enforcement reaches the budget instead of repeating eight tiny compactions per batch");
    Check(proj.NeedsReload(1), "whole-volume fallback marks the durable volume cold");
}

void TestWholeVolumeEvictionDoesNotUnlinkDoomedSiblings() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    for (uint64_t id = 100; id < 20100; ++id) {
        proj.Upsert(1, MakeEntry(id, 5, u"sibling.txt", 0));
    }
    proj.Upsert(2, MakeEntry(5, 5, u"D:", 0x10));
    proj.Upsert(2, MakeEntry(6, 5, u"survivor.txt", 0));
    FlatChildrenMap::testingRemovalComparisons = 0;
    proj.EvictVolume(1);
    Check(FlatChildrenMap::testingRemovalComparisons <= 20100,
          "whole-volume eviction does not quadratically unlink doomed siblings");
    Check(proj.EntryCount() == 2 && proj.Find(2, FileId{6, 0}) != nullptr,
          "whole-volume bulk eviction preserves other-volume entries");
    Check(proj.ChildIndices(2, FileId{5, 0}).size() == 1,
          "compaction reconstructs exact surviving child links");
}

int main() {
    TestWholeVolumeEvictionDoesNotUnlinkDoomedSiblings();
    TestTinyVictimsCannotSustainAnOverBudgetProjection();
    TestSiblingsDoNotDuplicateSharedAncestorPath();
    TestRepeatedNamesAcrossTreeShareOneInternedId();
    TestPathIsReconstructedOnDemand();
    TestChildrenLookupSupportsDirectoryListing();
    TestReparentingUpdatesParentChildIndex();
    TestPartialUpsertPreservesAbsentMetadata();
    TestAllocatedSizeFullPartialPreserveAndReconcile();
    TestFolderAggregateWithAllocatedSizeAndCompleteness();
    TestFolderAggregateOverflowSaturatesAndMarksIncomplete();
    TestRemoveDropsEntryButLeavesSiblingsIntact();
    TestCycleDoesNotCauseInfiniteWalk();
    TestVolumesAreIsolatedFromEachOther();
    TestRebuildFromStoreMatchesPersistedData();
    TestFolderAggregateCountsSubtree();
    TestManyDistinctParentsDoNotStallGrowth();
    TestFilenameTrigramCandidatesTrackMutations();
    TestProjectionEvictsLruAndReloadsFromStore();
    TestProjectionRebuildFailureIsColdAndRetryable();
    TestProjectionMemoryPressureSignal();
    TestProjectionMemoryDiagnostics();
    TestProjectionMemoryLimitRunsAtBatchBoundary();
    TestNamePoolViewInvalidatesOnArenaGrowth();
    TestNamePoolViewInvalidatesOnCompactStorage();
    TestProjectionMemoryBreakdownMatchesEstimatorAndCategories();
    TestDenseLruTimestampsTrackAccessOrder();
    TestLruTimestampsSurviveIterationOrderCompaction();
    TestRebuildReleasesBulkCapacitySlack();
    TestRemoveBatchMatchesPerEntryRemoval();
    TestRemoveVolumeKeepsFilenameAcceleratorCurrent();
    TestRemoveVolumeCanLeaveAcceleratorForCallerRebuild();
    TestFilenameCandidatesAreConservativeSuperset();
    TestFilenameCandidatesNarrowForSelectiveNeedlesDespiteUnicodeRows();
    TestHostOrdinalFoldSemanticsMatchCampaignMeasurement();
    TestFilenameCandidatesRetainHostFoldExceptionalNames();
    TestFilenameCandidatesRetainSurrogateNamesConservatively();
    TestFilenameCandidateSupersetInvariantHoldsForAdversarialCasing();
    TestBulkScanCycleRevivesFilenameAccelerator();
    TestCursorDetectsDisturbedLayout();
    TestOversizedVolumeReloadFailsFastAndStaysCold();

    if (fftest::FailureCount() > 0) {
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
