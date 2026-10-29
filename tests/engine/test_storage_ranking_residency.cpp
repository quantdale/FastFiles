#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <windows.h>

#include "IndexPipeline.h"
#include "../TestSupport.h"

namespace {
using ffengine::IndexPipeline;
using ffindexstore::VolumeKey;
using ffprotocol::IndexedSearchCandidate;
using fftest::Check;

struct Fixture {
    std::vector<ffprotocol::MftRecordV2> known;
    std::vector<ffprotocol::MftRecordV1> unknown;
    std::vector<IndexedSearchCandidate> authored;

    void Add(int64_t volume, uint64_t id, uint64_t parent, const std::wstring& name,
             const std::wstring& folder, uint64_t bytes, bool directory = false,
             bool allocatedKnown = true) {
        const std::u16string wireName(name.begin(), name.end());
        const uint64_t created = 100000000000ull + id * 101;
        const uint64_t modified = 200000000000ull + id * 127;
        const uint64_t allocated = directory || bytes == 0 ? 0 : ((bytes + 4095) / 4096) * 4096;
        if (allocatedKnown) {
            ffprotocol::MftRecordV2 row{};
            row.fixed.fileReferenceNumber = id;
            row.fixed.parentFileReferenceNumber = parent;
            row.fixed.creationTime = created;
            row.fixed.lastModifiedTime = modified;
            row.fixed.sizeBytes = bytes;
            row.fixed.allocatedSizeBytes = allocated;
            row.fixed.fileAttributes = directory ? FILE_ATTRIBUTE_DIRECTORY : 0;
            row.fixed.fileNameLengthChars = static_cast<uint16_t>(wireName.size());
            row.fileName = wireName;
            known.push_back(std::move(row));
        } else {
            ffprotocol::MftRecordV1 row{};
            row.fixed.fileReferenceNumber = id;
            row.fixed.parentFileReferenceNumber = parent;
            row.fixed.creationTime = created;
            row.fixed.lastModifiedTime = modified;
            row.fixed.sizeBytes = bytes;
            row.fixed.fileAttributes = directory ? FILE_ATTRIBUTE_DIRECTORY : 0;
            row.fixed.fileNameLengthChars = static_cast<uint16_t>(wireName.size());
            row.fileName = wireName;
            unknown.push_back(std::move(row));
        }
        // The oracle owns explicitly authored parent paths. It never reads a
        // path, filtered row set, comparator or candidate from the pipeline.
        IndexedSearchCandidate expected;
        expected.volumeId = volume;
        expected.id = id;
        expected.parentId = parent;
        expected.name = name;
        expected.folder = folder;
        expected.sizeBytes = bytes;
        expected.createdTime = created;
        expected.modifiedTime = modified;
        expected.isDirectory = directory;
        expected.persistentIndexed = true;
        expected.allocatedSizeBytes = allocatedKnown ? allocated : 0;
        expected.allocatedSizeKnown = allocatedKnown;
        authored.push_back(std::move(expected));
    }
};

bool WithinAuthoredFolder(const std::wstring& folder, const std::wstring& scope) {
    if (folder == scope) return true;
    const std::wstring childPrefix = scope.back() == L'\\' ? scope : scope + L'\\';
    return folder.starts_with(childPrefix);
}

std::vector<IndexedSearchCandidate> ExpectedFiles(const Fixture& fixture, const std::wstring& scope) {
    std::vector<IndexedSearchCandidate> result;
    for (const auto& row : fixture.authored) {
        if (!row.isDirectory && WithinAuthoredFolder(row.folder, scope)) result.push_back(row);
    }
    // Canonical logical size is numeric descending. Equal sizes use ordinal
    // containing-folder order, then volume/id. All authored folders are ASCII
    // with consistent casing, so lexical comparison is an independent ordinal
    // oracle here; no production sort or scope helper is reused.
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.sizeBytes != right.sizeBytes) return left.sizeBytes > right.sizeBytes;
        return std::tie(left.folder, left.volumeId, left.id) < std::tie(right.folder, right.volumeId, right.id);
    });
    return result;
}

auto Signature(const std::vector<IndexedSearchCandidate>& rows) {
    std::vector<std::tuple<int64_t, uint64_t, uint64_t, std::wstring, std::wstring,
                          uint64_t, uint64_t, uint64_t, bool, bool, uint64_t, bool>> result;
    for (const auto& row : rows) {
        result.emplace_back(row.volumeId, row.id, row.parentId, row.name, row.folder,
                            row.sizeBytes, row.createdTime, row.modifiedTime,
                            row.isDirectory, row.persistentIndexed,
                            row.allocatedSizeBytes, row.allocatedSizeKnown);
    }
    return result;
}

void CheckRoute(IndexPipeline& pipeline, int64_t volume, const IndexPipeline::SearchTimings& timings,
                bool cold, bool canonical, size_t persistedRows, uint64_t reloadsBefore) {
    Check(cold ? pipeline.IsVolumeEvicted(volume) : !pipeline.IsVolumeEvicted(volume),
          "the expected residency remains in force after the query");
    if (cold) {
        Check(timings.coldSearchUsed && timings.coldVolumesSearched == 1 &&
                  timings.coldRowsExamined != 0 && timings.hotRowsExamined == 0,
              "the cold query actually streams exactly one durable volume with no hot rows");
        if (canonical) {
            Check(timings.coldRowsExamined == persistedRows,
                  "forced canonical cold scan examines all1030 rows, including directories and excluded scope");
        }
    } else {
        Check(timings.hotRowsExamined != 0 && timings.coldRowsExamined == 0 && !timings.coldSearchUsed,
              "the hot query actually reads resident rows without durable search");
    }
    const auto readers = pipeline.GetReadPlaneDiagnostics();
    Check(readers.activeSearches == 0 && readers.activeDurableReaders == 0,
          "search completion releases its read-plane lease and durable reader");
    Check(pipeline.GetProjectionMemoryDiagnostics().coldReloadCount == reloadsBefore,
          "ranking does not reload an evicted volume into the projection");
}

void RunRankingQualification(const std::string& dbPath) {
    IndexPipeline pipeline;
    if (!pipeline.Open(dbPath)) {
        Check(false, "the isolated synthetic ranking database opens");
        return;
    }
    VolumeKey key;
    key.serialNumber = 97271030;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    pipeline.SetVolumeRules(volume, setting);
    Fixture fixture;
    fixture.Add(volume, 5, 5, L".", L"", 0, true);
    fixture.Add(volume, 10, 5, L"RankScope", L"C:\\", 0, true);
    fixture.Add(volume, 11, 10, L"Nested", L"C:\\RankScope", 0, true);
    fixture.Add(volume, 12, 5, L"RankScope-other", L"C:\\", 0, true);
    fixture.Add(volume, 13, 10, L"huge-directory", L"C:\\RankScope", 1ull << 40, true);
    fixture.Add(volume, 20, 5, L"largest-root.bin", L"C:\\", 90000000);
    fixture.Add(volume, 21, 10, L"shared.bin", L"C:\\RankScope", 16);
    fixture.Add(volume, 22, 11, L"shared.bin", L"C:\\RankScope\\Nested", 2 * 1024 * 1024);
    fixture.Add(volume, 23, 11, L"deep-only.bin", L"C:\\RankScope\\Nested", 1024 * 1024);
    fixture.Add(volume, 24, 12, L"shared.bin", L"C:\\RankScope-other", 4 * 1024 * 1024);
    fixture.Add(volume, 25, 10, L"known-zero.bin", L"C:\\RankScope", 0);
    fixture.Add(volume, 26, 11, L"unknown-\u03a9.bin", L"C:\\RankScope\\Nested", 13, false, false);
    fixture.Add(volume, 27, 10, L"equal-one.bin", L"C:\\RankScope", 777);
    fixture.Add(volume, 28, 11, L"equal-two.bin", L"C:\\RankScope\\Nested", 777);
    for (uint64_t i = 0; i < 1016; ++i) {
        const bool nested = i % 2 != 0;
        // A permutation makes ingestion order visibly different from size
        // order; a large directory and outside file would beat the cap if
        // either file/scope filtering were applied only after admission.
        const uint64_t size = 10000 + ((i * 37) % 1016);
        fixture.Add(volume, 1000 + i, nested ? 11 : 10,
                    L"permuted-" + std::to_wstring(i) + L".bin",
                    nested ? L"C:\\RankScope\\Nested" : L"C:\\RankScope", size);
    }
    const size_t persistedRows = fixture.known.size() + fixture.unknown.size();
    Check(persistedRows == 1030 && fixture.authored.size() == persistedRows,
          "the fixture is exactly1030 synthetic records, not a large corpus or live scan");
    if (!pipeline.ApplyMftBatch(volume, fixture.known) || !pipeline.ApplyMftBatch(volume, fixture.unknown)) {
        Check(false, "all known-allocation V2 and one unknown-allocation V1 records persist");
        return;
    }
    pipeline.RebuildAll({});
    if (!pipeline.MarkScanComplete(volume)) {
        Check(false, "the synthetic volume becomes query-ready");
        return;
    }
    const auto scoped = ExpectedFiles(fixture, L"C:\\RankScope");
    const auto whole = ExpectedFiles(fixture, L"C:\\");
    Check(scoped.size() == 1023 && whole.size() == 1025,
          "independent oracle excludes every directory and the same-prefix outside scope");
    Check(scoped.size() > 100 && scoped[0].id == 22 && scoped[1].id == 23,
          "independent top100 is nonvacuous and starts with the larger nested files");

    uint64_t requestId = 972700;
    for (unsigned route = 0; route < 3; ++route) {
        const bool cold = route != 0;
        const bool canonical = route == 2;
        if (route == 1) pipeline.UnloadVolumeFromProjection(volume);
        IndexPipeline::SearchTestHooks hooks;
        hooks.forceCanonicalColdScan = canonical;
        pipeline.SetSearchTestHooks(hooks);
        const uint64_t reloadsBefore = pipeline.GetProjectionMemoryDiagnostics().coldReloadCount;
        for (unsigned arm = 0; arm < 3; ++arm) {
            const bool rootScope = arm == 2;
            const uint32_t cap = arm == 0 ? 100u : 4096u;
            auto expected = rootScope ? whole : scoped;
            const bool truncated = expected.size() > cap;
            if (truncated) expected.resize(cap);
            ffprotocol::IndexedSearchRequest request;
            request.requestId = ++requestId;
            request.scope = 1;
            request.scopePath = rootScope ? L"C:\\" : L"C:\\RankScope";
            request.sortField = 3;
            request.descending = true;
            request.maxResults = cap;
            request.query = L"kind:file";
            IndexPipeline::SearchTimings timings;
            const auto response = pipeline.SearchIndexed(request, &timings);
            Check(response.status == ffprotocol::IndexedSearchStatus::Completed && response.requestId == request.requestId,
                  "ranking completes and echoes the exact request identity");
            Check(response.truncated == truncated && response.candidates.size() == expected.size(),
                  "top100 truncation and uncapped full counts match the independent oracle");
            Check(Signature(response.candidates) == Signature(expected),
                  "every ordered result field equals the independently authored canonical result");
            if (rootScope) {
                const auto direct = std::find_if(response.candidates.begin(), response.candidates.end(),
                    [](const auto& row) { return row.id == 20; });
                Check(direct != response.candidates.end() && direct->folder == L"C:\\" &&
                          (std::filesystem::path(direct->folder) / direct->name).is_absolute(),
                      "the largest direct-root file retains an absolute folder on every route");
            } else if (cap == 4096) {
                const auto zero = std::find_if(response.candidates.begin(), response.candidates.end(),
                    [](const auto& row) { return row.id == 25; });
                const auto unknown = std::find_if(response.candidates.begin(), response.candidates.end(),
                    [](const auto& row) { return row.id == 26; });
                Check(zero != response.candidates.end() && zero->allocatedSizeKnown && zero->allocatedSizeBytes == 0 &&
                          unknown != response.candidates.end() && !unknown->allocatedSizeKnown && unknown->allocatedSizeBytes == 0,
                      "the full result distinguishes known-zero allocation from unknown allocation");
            }
            CheckRoute(pipeline, volume, timings, cold, canonical, persistedRows, reloadsBefore);
            std::printf("route=%s scope=%s cap=%u rows=%zu hot=%zu cold=%zu truncated=%d\n",
                        canonical ? "canonical-cold" : cold ? "cold" : "hot", rootScope ? "root" : "nested",
                        cap, response.candidates.size(), timings.hotRowsExamined, timings.coldRowsExamined,
                        response.truncated ? 1 : 0);
        }
    }
    pipeline.SetSearchTestHooks({});
}
} // namespace

int main() {
    const std::string name = "ff_storage_ranking_residency_" + std::to_string(GetCurrentProcessId()) + ".db";
    const std::string dbPath = fftest::FreshDbPath(name.c_str());
    RunRankingQualification(dbPath);
    for (const auto& suffix : {std::string{}, std::string{"-wal"}, std::string{"-shm"}}) {
        std::error_code error;
        std::filesystem::remove(dbPath + suffix, error);
        Check(!error, "the owned temporary ranking database and sidecars clean up");
    }
    return fftest::FailureCount() == 0 ? 0 : 1;
}
