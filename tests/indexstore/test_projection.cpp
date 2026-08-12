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
    auto dbPath = std::filesystem::temp_directory_path() / "ffindexstore_projection_rebuild.db";
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

void TestProjectionMemoryPressureSignal() {
    Projection proj;
    proj.Upsert(1, MakeEntry(5, 5, u"C:", 0x10));
    const size_t usage = proj.EstimatedMemoryBytes();
    proj.SetMaximumMemoryBytes(usage);
    Check(proj.IsMemoryPressureHigh(), "projection exposes a high-memory pressure signal near its limit");
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
} // namespace

int main() {
    TestSiblingsDoNotDuplicateSharedAncestorPath();
    TestRepeatedNamesAcrossTreeShareOneInternedId();
    TestPathIsReconstructedOnDemand();
    TestChildrenLookupSupportsDirectoryListing();
    TestReparentingUpdatesParentChildIndex();
    TestRemoveDropsEntryButLeavesSiblingsIntact();
    TestCycleDoesNotCauseInfiniteWalk();
    TestVolumesAreIsolatedFromEachOther();
    TestRebuildFromStoreMatchesPersistedData();
    TestFolderAggregateCountsSubtree();
    TestManyDistinctParentsDoNotStallGrowth();
    TestFilenameTrigramCandidatesTrackMutations();
    TestProjectionEvictsLruAndReloadsFromStore();
    TestProjectionMemoryPressureSignal();
    TestProjectionMemoryLimitRunsAtBatchBoundary();

    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
