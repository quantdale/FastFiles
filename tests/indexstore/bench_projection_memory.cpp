// index-storage-and-scanning tasks.md 9.7: informational benchmark, not a
// pass/fail test (not registered with add_test) -- reports the in-memory
// Projection's actual RAM footprint at a representative large-volume
// entry count, to check design.md D2's ~40-50 bytes/entry claim against
// reality rather than assuming it.
//
// Last measured result (5M entries, 10 recurring names, ~32 entries/dir):
// ~161 bytes/entry -- WORSE than design.md's own ~100-byte naive-full-
// path-per-entry estimate this design is supposed to beat. Root cause:
// idToIndex_ (std::unordered_map<EntryKey, uint32_t>) duplicates the full
// 24-byte EntryKey per entry plus std::unordered_map's per-node
// allocation overhead, roughly doubling entries_'s own footprint. Fixing
// this means replacing idToIndex_/parentToChildren_ with an open-
// addressing (flat, no per-node heap allocation) hash structure -- not
// done in this change; re-run this benchmark after that follow-up to
// confirm the target is actually met.
#include <cstdio>
#include <random>
#include <vector>

#include "ffindexstore/Projection.h"

using namespace ffindexstore;

// Reports process RSS via /proc/self/status (Linux-only, this benchmark
// harness is not part of the shipped Windows product -- just used here to
// sanity-check the projection's real memory footprint at scale).
long GetRssKb() {
    FILE* f = nullptr;
    if (fopen_s(&f, "/proc/self/status", "r") != 0 || !f) return -1;
    char line[256];
    long rss = -1;
    while (std::fgets(line, sizeof(line), f)) {
        if (sscanf_s(line, "VmRSS: %ld kB", &rss) == 1) break;
    }
    std::fclose(f);
    return rss;
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
