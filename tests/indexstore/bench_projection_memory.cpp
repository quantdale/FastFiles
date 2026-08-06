// index-storage-and-scanning tasks.md 9.7: informational benchmark, not a
// pass/fail test (not registered with add_test) -- reports the in-memory
// Projection's actual RAM footprint at a representative large-volume
// entry count, to check design.md D2's ~40-50 bytes/entry claim against
// reality rather than assuming it.
//
// Last measured result (5M entries, 10 recurring names, ~32 entries/dir):
//   Windows x64 release, 2026-08-06 (GetProcessMemoryInfo working set):
//   Entries: 5,000,000; RSS delta: 969,736 KB; ~198.6 bytes/entry.
//   Still above design.md's ~100-byte naive-full-path-per-entry estimate
//   this design is supposed to beat. idToIndex_/parentToChildren_ were
//   since converted to the flat open-addressing maps (FlatHashMap.h), but
//   the measured footprint barely moved: the dominant costs are now the
//   eager reserves -- idToIndex_'s 16.7M bucket arrays (~486 MB at the
//   5M-entry reserve hint) plus entries_' 5M x ~80-byte ProjectionEntry
//   array (~400 MB) and the 60M-char name arena. Re-run after a
//   memory-tuning follow-up (e.g. sizing reserves from the actual entry
//   count and shrinking the arena hint) to confirm the target.
//
//   Also note: running this benchmark on Windows exposed a FlatChildrenMap
//   live-count bug (count_ was never incremented on insert, so the table
//   never grew past its initial 8 buckets and find_slot() spun forever on
//   the 9th distinct parent key). Fixed in FlatHashMap.h; regression-
//   covered by ffindexstore_projection_tests (many-distinct-parents case).
#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <random>
#include <vector>

#include "ffindexstore/Projection.h"

using namespace ffindexstore;

// Reports process working-set size in KB via GetProcessMemoryInfo
// (psapi.h) -- Windows-native measurement of the same live-memory signal
// the benchmark needs to sanity-check the projection's real footprint at
// scale (this benchmark harness is not part of the shipped Windows
// product -- just used here to verify the projection's memory claims).
long GetRssKb() {
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) return -1;
    return static_cast<long>(counters.WorkingSetSize / 1024);
}

int main() {
    constexpr size_t kEntryCount = 5'000'000;
    constexpr size_t kDirWidth = 32; // entries per directory, roughly

    const long rssBefore = GetRssKb();

    Projection proj;
    proj.Reserve(kEntryCount);

    std::vector<std::u16string> commonNames = {
        u"index.js", u"package.json", u".git", u"node_modules", u"README.md",
        u"main.cpp", u"Makefile", u"build", u"src", u"test.txt",
    };

    EntryRecord root;
    root.id = FileId{5, 0};
    root.parentId = FileId{5, 0};
    root.name = u"C:";
    proj.Upsert(1, root);

    uint64_t nextId = 100;
    std::vector<uint64_t> directories = {5};
    size_t created = 1;

    while (created < kEntryCount) {
        uint64_t parent = directories[created % directories.size()];
        for (size_t i = 0; i < kDirWidth && created < kEntryCount; ++i) {
            EntryRecord entry;
            entry.id = FileId{nextId, 0};
            entry.parentId = FileId{parent, 0};
            entry.name = commonNames[nextId % commonNames.size()];
            entry.sizeBytes = nextId * 37;
            entry.attributes = (i == 0) ? 0x10 : 0; // first child of each batch is a directory
            proj.Upsert(1, entry);
            if (i == 0) {
                directories.push_back(nextId);
            }
            ++nextId;
            ++created;
        }
    }

    const long rssAfter = GetRssKb();
    const double bytesPerEntry = rssAfter > rssBefore
        ? static_cast<double>(rssAfter - rssBefore) * 1024.0 / static_cast<double>(proj.EntryCount())
        : 0.0;

    std::printf("Entries: %zu\n", proj.EntryCount());
    std::printf("Unique names: %zu\n", proj.Names().UniqueNameCount());
    std::printf("Name arena size (chars): %zu\n", proj.Names().ArenaSizeChars());
    std::printf("RSS before: %ld KB, after: %ld KB, delta: %ld KB\n", rssBefore, rssAfter, rssAfter - rssBefore);
    std::printf("Approx bytes/entry (process RSS delta): %.1f\n", bytesPerEntry);

    // Naive per-entry full-path baseline for comparison (not actually
    // allocated -- just the arithmetic design.md's rationale cites).
    constexpr double kNaiveAvgPathBytes = 100.0; // design.md's ~80-120 byte estimate, midpoint
    const double naiveTotalMb = kNaiveAvgPathBytes * proj.EntryCount() / (1024.0 * 1024.0);
    const double actualTotalMb = (rssAfter - rssBefore) / 1024.0;
    std::printf("Naive full-path-per-entry estimate: %.1f MB; actual projection: %.1f MB (%.1fx smaller)\n",
                naiveTotalMb, actualTotalMb, naiveTotalMb / actualTotalMb);

    return 0;
}
