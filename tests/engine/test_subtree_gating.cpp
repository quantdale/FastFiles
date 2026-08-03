// zero-touch-autonomous-engineering: subtree-gating unit tests.
//
// Verifies that the ingestion pipeline (IndexPipeline) honors
// ffprotocol::IsPathIncluded / VolumeSetting::rules across initial MFT
// ingestion, USN deltas, reconciliation, and rebuild -- the complete-
// implementation resolution of the previously-unresolved subtree-gating
// decision (settings-and-appearance task 2.5).
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "IndexPipeline.h"

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

using namespace ffengine;
using namespace ffindexstore;

std::string FreshDbPath(const char* name) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
    return path.string();
}

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

ffprotocol::UsnDeltaV1 MakeUsnRecord(uint64_t id, uint64_t parent, std::u16string name, uint32_t attrs, uint64_t size, uint32_t reason) {
    ffprotocol::UsnDeltaV1 r{};
    r.fixed.fileReferenceNumber = id;
    r.fixed.parentFileReferenceNumber = parent;
    r.fixed.fileAttributes = attrs;
    r.fixed.sizeBytes = size;
    r.fixed.reason = reason;
    r.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
    r.fileName = std::move(name);
    return r;
}

ffprotocol::VolumeSetting MakeVolumeWithRules(std::wstring key, std::vector<ffprotocol::DirectoryRule> rules) {
    ffprotocol::VolumeSetting v;
    v.key = std::move(key);
    v.enabled = true;
    v.rules = std::move(rules);
    return v;
}

constexpr uint32_t kDir = 0x00000010;

void TestExcludeRuleDropsSubtreeFromSnapshot() {
    IndexPipeline pipeline;
    pipeline.Open(FreshDbPath("engine_subtree_exclude.db"));
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = pipeline.ResolveVolume(key);

    pipeline.SetVolumeRules(vol, MakeVolumeWithRules(L"C:", {{L"C:\\Excluded", false}}));

    pipeline.ApplyMftBatch(vol, {
        MakeMftRecord(5, 5, u"", kDir, 0),
        MakeMftRecord(100, 5, u"Kept", kDir, 0),
        MakeMftRecord(101, 100, u"kept.txt", 0, 11),
        MakeMftRecord(200, 5, u"Excluded", kDir, 0),
        MakeMftRecord(201, 200, u"secret.txt", 0, 99),
    });

    auto snapshot = pipeline.ExportDirectorySnapshot(vol, L"C:");
    Check(snapshot.count(L"C:\\Kept") == 1, "included subtree C:\\Kept is exported");
    Check(snapshot[L"C:\\Kept"].entries.size() == 1 && snapshot[L"C:\\Kept"].entries[0].name == L"kept.txt",
          "included subtree's child file is listed");
    Check(snapshot.count(L"C:\\Excluded") == 0, "excluded subtree C:\\Excluded is NOT exported");
    Check(snapshot[L"C:"].entries.size() == 1, "the root lists only the kept child directory, not the excluded one");
}

void TestLongestMatchWins() {
    IndexPipeline pipeline;
    pipeline.Open(FreshDbPath("engine_subtree_longest.db"));
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = pipeline.ResolveVolume(key);

    pipeline.SetVolumeRules(vol, MakeVolumeWithRules(L"C:", {{L"C:\\Work", true}, {L"C:\\Work\\Private", false}}));

    pipeline.ApplyMftBatch(vol, {
        MakeMftRecord(5, 5, u"", kDir, 0),
        MakeMftRecord(100, 5, u"Work", kDir, 0),
        MakeMftRecord(101, 100, u"readme.txt", 0, 1),
        MakeMftRecord(200, 100, u"Private", kDir, 0),
        MakeMftRecord(201, 200, u"notes.txt", 0, 2),
    });

    auto snapshot = pipeline.ExportDirectorySnapshot(vol, L"C:");
    Check(snapshot.count(L"C:\\Work") == 1, "C:\\Work is included (broader include rule)");
    Check(snapshot[L"C:\\Work"].entries.size() == 1, "C:\\Work lists its included children (readme.txt), not the excluded Private dir");
    Check(snapshot[L"C:\\Work"].entries[0].name == L"readme.txt", "C:\\Work lists readme.txt");
    Check(snapshot.count(L"C:\\Work\\Private") == 0, "C:\\Work\\Private is excluded (longest matching prefix wins)");
}

void TestNoRulesIncludesEverything() {
    IndexPipeline pipeline;
    pipeline.Open(FreshDbPath("engine_subtree_norules.db"));
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = pipeline.ResolveVolume(key);

    ffprotocol::VolumeSetting emptyRules;
    emptyRules.key = L"C:";
    emptyRules.enabled = true;
    pipeline.SetVolumeRules(vol, emptyRules);

    pipeline.ApplyMftBatch(vol, {
        MakeMftRecord(5, 5, u"", kDir, 0),
        MakeMftRecord(100, 5, u"Anything", kDir, 0),
        MakeMftRecord(101, 100, u"file.txt", 0, 1),
    });

    auto snapshot = pipeline.ExportDirectorySnapshot(vol, L"C:");
    Check(snapshot.count(L"C:\\Anything") == 1, "with no rules, everything is included (pre-change behavior preserved)");
    Check(snapshot[L"C:\\Anything"].entries.size() == 1, "with no rules, the child file is listed");
}

void TestReconciliationRemovesNowExcludedEntry() {
    IndexPipeline pipeline;
    pipeline.Open(FreshDbPath("engine_subtree_reconcile.db"));
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = pipeline.ResolveVolume(key);

    pipeline.ApplyMftBatch(vol, {
        MakeMftRecord(5, 5, u"", kDir, 0),
        MakeMftRecord(100, 5, u"Old", kDir, 0),
        MakeMftRecord(101, 100, u"file.txt", 0, 1),
    });

    pipeline.SetVolumeRules(vol, MakeVolumeWithRules(L"C:", {{L"C:\\Old", false}}));

    pipeline.BeginReconciliationPass(vol);
    pipeline.ApplyMftBatch(vol, {
        MakeMftRecord(5, 5, u"", kDir, 0),
        MakeMftRecord(100, 5, u"Old", kDir, 0),
        MakeMftRecord(101, 100, u"file.txt", 0, 1),
    });
    pipeline.FinishReconciliationPass(vol);

    auto snapshot = pipeline.ExportDirectorySnapshot(vol, L"C:");
    Check(snapshot.count(L"C:\\Old") == 0, "a now-excluded subtree is reconciled away even if re-observed");
}

void TestRebuildHonorsRules() {
    const std::string dbPath = FreshDbPath("engine_subtree_rebuild.db");
    VolumeKey key;
    key.serialNumber = 1;
    VolumeRowId vol;
    {
        IndexPipeline pipeline;
        pipeline.Open(dbPath);
        vol = pipeline.ResolveVolume(key);
        pipeline.ApplyMftBatch(vol, {
            MakeMftRecord(5, 5, u"", kDir, 0),
            MakeMftRecord(100, 5, u"Kept", kDir, 0),
            MakeMftRecord(200, 5, u"Excluded", kDir, 0),
            MakeMftRecord(201, 200, u"secret.txt", 0, 5),
        });
    }

    IndexPipeline restarted;
    restarted.Open(dbPath);
    restarted.SetVolumeRules(vol, MakeVolumeWithRules(L"C:", {{L"C:\\Excluded", false}}));
    int rebuilt = 0;
    restarted.RebuildAll([&](VolumeRowId) { ++rebuilt; });
    Check(rebuilt == 1, "RebuildAll rebuilt the one known volume");

    auto snapshot = restarted.ExportDirectorySnapshot(vol, L"C:");
    Check(snapshot.count(L"C:\\Kept") == 1, "after rebuild with rules, the kept subtree is present");
    Check(snapshot.count(L"C:\\Excluded") == 0, "after rebuild with rules, the excluded subtree is pruned");
}

void TestUsnUpsertHonorsRules() {
    IndexPipeline pipeline;
    pipeline.Open(FreshDbPath("engine_subtree_usn.db"));
    VolumeKey key;
    key.serialNumber = 1;
    auto vol = pipeline.ResolveVolume(key);

    pipeline.SetVolumeRules(vol, MakeVolumeWithRules(L"C:", {{L"C:\\Excluded", false}}));

    pipeline.ApplyMftBatch(vol, {
        MakeMftRecord(5, 5, u"", kDir, 0),
        MakeMftRecord(100, 5, u"Kept", kDir, 0),
        MakeMftRecord(200, 5, u"Excluded", kDir, 0),
    });

    pipeline.ApplyUsnBatch(vol, {
        MakeUsnRecord(101, 100, u"kept.txt", 0, 1, 0),
        MakeUsnRecord(201, 200, u"secret.txt", 0, 2, 0),
    });

    auto snapshot = pipeline.ExportDirectorySnapshot(vol, L"C:");
    Check(snapshot.count(L"C:\\Kept") == 1 && snapshot[L"C:\\Kept"].entries.size() == 1,
          "USN upsert under the kept subtree is retained");
    Check(snapshot.count(L"C:\\Excluded") == 0 || snapshot[L"C:\\Excluded"].entries.empty(),
          "USN upsert under the excluded subtree is pruned");
}

} // namespace

int main() {
    TestExcludeRuleDropsSubtreeFromSnapshot();
    TestLongestMatchWins();
    TestNoRulesIncludesEverything();
    TestReconciliationRemovesNowExcludedEntry();
    TestRebuildHonorsRules();
    TestUsnUpsertHonorsRules();
    if (g_failures > 0) {
        std::printf("\n%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("\nall subtree-gating tests passed\n");
    return 0;
}