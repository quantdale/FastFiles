// durable-cold-search-index-and-hybrid-residency 4.1/5.x exact differential
// oracle: prove that cold-with-index search results match the canonical
// fallback in IDENTITY, ORDERING, and CAPS -- exact result sequences, not
// counts (the historical 5M acceptance only proved counts).
//
// Design: run every query class through the production IndexPipeline search
// TWICE over the same database copy:
//   session A -- candidate index READY (production routing), and
//   session B -- candidate index NOT READY (entry_name_index_meta.state=0),
//               where the query-time readiness gate disables the candidate
//               rung and every query takes the canonical stream.
// Both arms go through the identical match/filter/sort/cap pipeline, so the
// ordered candidate sequences must be element-wise identical:
//   IDENTITIES_EQUAL   -- same (volumeId,id,parentId) multiset and positions
//   ORDERING_EQUAL     -- element-wise equal ordered sequences
//   CAPS_EQUAL         -- equal counts, equal truncated flags, count <= cap
//   NO_SILENT_TRUNCATION -- equal status; a capped run never loses rows the
//                           other arm returns (positions compared to the cap)
// Sort-field sweeps and a cap sweep exercise tie-breaking; a cancellation
// cell compares cancelled semantics.
//
// The database is opened through the normal Store path; use a DISPOSABLE
// COPY (the harness may write maintenance state). Exit code != 0 on any
// mismatch.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "IndexPipeline.h"
#include "ffprotocol/UiProtocol.h"
#include "ffindexstore/Store.h"
#include "sqlite3.h"

namespace {

struct Options {
    std::string dbPath;
    std::string outputPath = "identity-ordering-oracle.json";
    std::string sourceSha;
    std::string sourceState;
    uint64_t volumeRows = 1;
};

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), size,
                        nullptr, nullptr);
    return out;
}

struct QueryCase {
    const char* name;
    const wchar_t* query;
    uint8_t sortField;
    uint32_t maxResults;
};

// The established 12 classes (same needles as bench_large_index_scalability).
const QueryCase kQueryCases[] = {
    {"selective", L"unique_component_7391", 0, 4096},
    {"moderate", L"report", 0, 4096},
    {"broad", L"common", 0, 4096},
    {"no_match", L"zzzz_never_present", 0, 4096},
    {"prefix", L"photo", 0, 4096},
    {"extension", L"ext:txt", 3, 4096},
    {"worst_substring", L"component", 3, 4096},
    {"exact_name", L"unique_component_7391.txt", 0, 4096},
    {"short_prefix", L"co", 0, 4096},
    {"mixed_case", L"CoMmOn_CoMpOnEnT", 0, 4096},
    {"unicode", L"\u0441\u043f\u0438\u0441\u043e\u043a", 0, 4096},
    {"high_limit_broad", L"common", 0, 50000},
};

struct CapturedRun {
    ffprotocol::IndexedSearchStatus status = ffprotocol::IndexedSearchStatus::Unavailable;
    bool truncated = false;
    uint64_t coldRowsExamined = 0;
    std::vector<ffprotocol::IndexedSearchCandidate> candidates;
};

CapturedRun RunSearch(ffengine::IndexPipeline& pipeline, const QueryCase& queryCase, uint8_t sortField,
                      bool descending, uint32_t maxResults, uint64_t requestId) {
    ffprotocol::IndexedSearchRequest request;
    request.requestId = requestId;
    request.scope = 3;  // AllIndexedLocations
    request.sortField = sortField;
    request.descending = descending;
    request.maxResults = maxResults;
    request.query = queryCase.query;
    ffengine::IndexPipeline::SearchTimings timings;
    CapturedRun run;
    ffprotocol::IndexedSearchResponse response = pipeline.SearchIndexed(request, &timings);
    run.status = response.status;
    run.truncated = response.truncated;
    run.coldRowsExamined = timings.coldRowsExamined;
    run.candidates = std::move(response.candidates);
    return run;
}

bool SameCandidate(const ffprotocol::IndexedSearchCandidate& a,
                   const ffprotocol::IndexedSearchCandidate& b) {
    return a.name == b.name && a.folder == b.folder && a.sizeBytes == b.sizeBytes &&
           a.modifiedTime == b.modifiedTime && a.isDirectory == b.isDirectory &&
           a.createdTime == b.createdTime && a.volumeId == b.volumeId && a.id == b.id &&
           a.parentId == b.parentId && a.allocatedSizeBytes == b.allocatedSizeBytes &&
           a.allocatedSizeKnown == b.allocatedSizeKnown;
}

struct CellVerdict {
    std::string cell;
    bool identitiesEqual = false;
    bool orderingEqual = false;
    bool capsEqual = false;
    bool noSilentTruncation = false;
    size_t countReady = 0;
    size_t countCanonical = 0;
    bool truncatedReady = false;
    bool truncatedCanonical = false;
    uint64_t coldRowsReady = 0;
    uint64_t coldRowsCanonical = 0;
    size_t firstDivergentIndex = static_cast<size_t>(-1);
};

CellVerdict CompareCells(const std::string& cell, const CapturedRun& ready, const CapturedRun& canonical,
                         uint32_t maxResults) {
    CellVerdict verdict;
    verdict.cell = cell;
    verdict.countReady = ready.candidates.size();
    verdict.countCanonical = canonical.candidates.size();
    verdict.truncatedReady = ready.truncated;
    verdict.truncatedCanonical = canonical.truncated;
    verdict.coldRowsReady = ready.coldRowsExamined;
    verdict.coldRowsCanonical = canonical.coldRowsExamined;

    // ORDERING_EQUAL: element-wise equal sequences (strongest form; implies
    // identity equality at every position).
    size_t limit = (ready.candidates.size() < canonical.candidates.size())
                       ? ready.candidates.size()
                       : canonical.candidates.size();
    bool orderedEqual = ready.candidates.size() == canonical.candidates.size();
    for (size_t index = 0; index < limit; ++index) {
        if (!SameCandidate(ready.candidates[index], canonical.candidates[index])) {
            orderedEqual = false;
            if (verdict.firstDivergentIndex == static_cast<size_t>(-1)) {
                verdict.firstDivergentIndex = index;
            }
        }
    }
    verdict.orderingEqual = orderedEqual;
    // IDENTITIES_EQUAL: the identity multiset at matching positions (the
    // ordered comparison above already pins positions; this records the
    // identity-only verdict as its own claim).
    bool identitiesEqual = ready.candidates.size() == canonical.candidates.size();
    for (size_t index = 0; index < limit && identitiesEqual; ++index) {
        const auto& a = ready.candidates[index];
        const auto& b = canonical.candidates[index];
        if (a.volumeId != b.volumeId || a.id != b.id || a.parentId != b.parentId) {
            identitiesEqual = false;
        }
    }
    verdict.identitiesEqual = identitiesEqual;
    verdict.capsEqual = ready.candidates.size() == canonical.candidates.size() &&
                        ready.truncated == canonical.truncated &&
                        ready.candidates.size() <= maxResults &&
                        canonical.candidates.size() <= maxResults;
    verdict.noSilentTruncation = ready.status == canonical.status && ready.truncated == canonical.truncated;
    return verdict;
}

bool TamperNotReady(const std::string& dbPath) {
    sqlite3* raw = nullptr;
    if (sqlite3_open(dbPath.c_str(), &raw) != SQLITE_OK) {
        if (raw != nullptr) sqlite3_close(raw);
        return false;
    }
    const bool ok = sqlite3_exec(raw, "UPDATE entry_name_index_meta SET state=0;", nullptr, nullptr,
                                 nullptr) == SQLITE_OK;
    sqlite3_close(raw);
    return ok;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--db" && index + 1 < argc) options.dbPath = ToUtf8(argv[++index]);
        else if (argument == L"--output" && index + 1 < argc) options.outputPath = ToUtf8(argv[++index]);
        else if (argument == L"--source-sha" && index + 1 < argc) options.sourceSha = ToUtf8(argv[++index]);
        else if (argument == L"--source-state" && index + 1 < argc) options.sourceState = ToUtf8(argv[++index]);
        else if (argument == L"--volume-rows" && index + 1 < argc) options.volumeRows = std::wcstoull(argv[++index], nullptr, 10);
        else {
            std::fwprintf(stderr, L"Usage: %s --db <copy.db> [--output out.json] [--source-sha sha] "
                                  L"[--source-state state]\n", argv[0]);
            return 2;
        }
    }
    if (options.dbPath.empty()) {
        std::fprintf(stderr, "--db is required (use a DISPOSABLE COPY)\n");
        return 2;
    }

    // Cells: the 12 classes, a tie-heavy sort sweep on "report", and a cap
    // sweep on "report" (positions compared at every cap).
    struct Cell {
        std::string name;
        const QueryCase* queryCase;
        uint8_t sortField;
        bool descending;
        uint32_t maxResults;
        bool cancelledImmediately = false;
    };
    std::vector<Cell> cells;
    for (const QueryCase& queryCase : kQueryCases) {
        cells.push_back({queryCase.name, &queryCase, queryCase.sortField, false, queryCase.maxResults});
    }
    const QueryCase* report = &kQueryCases[1];
    for (uint8_t sortField : {uint8_t{2}, uint8_t{3}, uint8_t{4}}) {
        for (const bool descending : {false, true}) {
            cells.push_back({std::string("sort_sweep_") + std::to_string(sortField) +
                                 (descending ? "_desc" : "_asc"),
                             report, sortField, descending, 4096});
        }
    }
    for (uint32_t cap : {1u, 2u, 17u, 4096u}) {
        cells.push_back({"cap_sweep_" + std::to_string(cap), report, report->sortField, false, cap});
    }
    cells.push_back({"cancel_immediate", &kQueryCases[2], uint8_t{0}, false, 4096, true});

    std::vector<CellVerdict> verdicts;
    std::vector<CapturedRun> readyRuns;
    std::vector<CapturedRun> canonicalRuns;
    bool allOpened = true;

    // Session A: candidate index READY (production routing).
    {
        ffengine::IndexPipeline pipeline;
        if (!pipeline.Open(options.dbPath)) {
            std::fprintf(stderr, "failed to open %s\n", options.dbPath.c_str());
            return 3;
        }
        // Mirror the production startup (Main.cpp): load the projection /
        // volume metadata from the durable store before serving searches,
        // and configure the stored volumes as searchable (the engine does
        // this from its indexing settings; bench_large_index_scalability
        // uses the same SetVolumeRules seam).
        pipeline.RebuildAll({});
        for (uint64_t row = 1; row <= options.volumeRows; ++row) {
            ffprotocol::VolumeSetting setting;
            setting.key = L"C:";
            setting.enabled = true;
            pipeline.SetVolumeRules(static_cast<ffindexstore::VolumeRowId>(row), setting);
        }
        std::printf("session A (candidate index ready): %zu cells\n", cells.size());
        std::fflush(stdout);
        for (const Cell& cell : cells) {
            if (!cell.cancelledImmediately) {
                readyRuns.push_back(RunSearch(pipeline, *cell.queryCase, cell.sortField, cell.descending,
                                              cell.maxResults, 700'000 + readyRuns.size()));
            } else {
                ffprotocol::IndexedSearchRequest request;
                request.requestId = 799'000;
                request.scope = 3;
                request.maxResults = cell.maxResults;
                request.query = cell.queryCase->query;
                CapturedRun run;
                ffprotocol::IndexedSearchResponse response =
                    pipeline.SearchIndexed(request, nullptr, [] { return true; });
                run.status = response.status;
                run.truncated = response.truncated;
                run.candidates = std::move(response.candidates);
                readyRuns.push_back(std::move(run));
            }
            std::printf("  A %s: %zu rows (coldRows %llu status=%d trunc=%d)\n", cell.name.c_str(),
                        readyRuns.back().candidates.size(),
                        static_cast<unsigned long long>(readyRuns.back().coldRowsExamined),
                        static_cast<int>(readyRuns.back().status), readyRuns.back().truncated ? 1 : 0);
            std::fflush(stdout);
        }
        pipeline.Close();
    }

    // Force the query-time readiness gate off -> every query takes the
    // canonical stream (the acceptance reference).
    if (!TamperNotReady(options.dbPath)) {
        std::fprintf(stderr, "failed to tamper entry_name_index_meta for the canonical arm\n");
        return 3;
    }

    // Session B: canonical reference.
    {
        ffengine::IndexPipeline pipeline;
        if (!pipeline.Open(options.dbPath)) {
            std::fprintf(stderr, "failed to reopen %s for the canonical arm\n", options.dbPath.c_str());
            return 3;
        }
        pipeline.RebuildAll({});
        for (uint64_t row = 1; row <= options.volumeRows; ++row) {
            ffprotocol::VolumeSetting setting;
            setting.key = L"C:";
            setting.enabled = true;
            pipeline.SetVolumeRules(static_cast<ffindexstore::VolumeRowId>(row), setting);
        }
        std::printf("session B (readiness gate off = canonical): %zu cells\n", cells.size());
        std::fflush(stdout);
        for (const Cell& cell : cells) {
            if (!cell.cancelledImmediately) {
                canonicalRuns.push_back(RunSearch(pipeline, *cell.queryCase, cell.sortField, cell.descending,
                                                  cell.maxResults, 800'000 + canonicalRuns.size()));
            } else {
                ffprotocol::IndexedSearchRequest request;
                request.requestId = 899'000;
                request.scope = 3;
                request.maxResults = cell.maxResults;
                request.query = cell.queryCase->query;
                CapturedRun run;
                ffprotocol::IndexedSearchResponse response =
                    pipeline.SearchIndexed(request, nullptr, [] { return true; });
                run.status = response.status;
                run.truncated = response.truncated;
                run.candidates = std::move(response.candidates);
                canonicalRuns.push_back(std::move(run));
            }
            std::printf("  B %s: %zu rows (coldRows %llu status=%d trunc=%d)\n", cell.name.c_str(),
                        canonicalRuns.back().candidates.size(),
                        static_cast<unsigned long long>(canonicalRuns.back().coldRowsExamined),
                        static_cast<int>(canonicalRuns.back().status), canonicalRuns.back().truncated ? 1 : 0);
            std::fflush(stdout);
        }
        pipeline.Close();
        allOpened = true;
    }

    for (size_t index = 0; index < cells.size(); ++index) {
        CellVerdict verdict =
            CompareCells(cells[index].name, readyRuns[index], canonicalRuns[index], cells[index].maxResults);
        if (!(verdict.identitiesEqual && verdict.orderingEqual && verdict.capsEqual &&
              verdict.noSilentTruncation)) {
            allOpened = false;
        }
        verdicts.push_back(std::move(verdict));
    }

    FILE* out = nullptr;
    if (fopen_s(&out, options.outputPath.c_str(), "wb") != 0) out = nullptr;
    if (out != nullptr) {
        std::fprintf(out,
                     "{\n  \"schema\": \"fastfiles-identity-ordering-oracle-v1\",\n"
                     "  \"identity\": {\"source_sha\": \"%s\", \"source_state\": \"%s\"},\n"
                     "  \"db\": \"%s\",\n  \"cells\": [\n",
                     options.sourceSha.c_str(), options.sourceState.c_str(), options.dbPath.c_str());
        for (size_t index = 0; index < verdicts.size(); ++index) {
            const CellVerdict& verdict = verdicts[index];
            std::fprintf(out,
                         "    {\"cell\": \"%s\", \"identities_equal\": %s, "
                         "\"ordering_equal\": %s, \"caps_equal\": %s, \"no_silent_truncation\": %s, "
                         "\"count_ready\": %zu, \"count_canonical\": %zu, \"truncated_ready\": %s, "
                         "\"truncated_canonical\": %s}%s\n",
                         verdict.cell.c_str(), verdict.identitiesEqual ? "true" : "false",
                         verdict.orderingEqual ? "true" : "false", verdict.capsEqual ? "true" : "false",
                         verdict.noSilentTruncation ? "true" : "false", verdict.countReady,
                         verdict.countCanonical, verdict.truncatedReady ? "true" : "false",
                         verdict.truncatedCanonical ? "true" : "false",
                         (index + 1 < verdicts.size()) ? "," : "");
        }
        std::fprintf(out, "  ],\n  \"all_passed\": %s\n}\n", allOpened ? "true" : "false");
        std::fclose(out);
    }

    for (const CellVerdict& verdict : verdicts) {
        std::printf("%s %-12s identities=%s ordering=%s caps=%s no_silent_truncation=%s "
                    "(ready=%zu canonical=%zu%s)\n",
                    (verdict.identitiesEqual && verdict.orderingEqual && verdict.capsEqual &&
                     verdict.noSilentTruncation)
                        ? "PASS"
                        : "FAIL",
                    verdict.cell.c_str(), verdict.identitiesEqual ? "IDENTITIES_EQUAL" : "IDENTITIES_DIFFER",
                    verdict.orderingEqual ? "ORDERING_EQUAL" : "ORDERING_DIFFER",
                    verdict.capsEqual ? "CAPS_EQUAL" : "CAPS_DIFFER",
                    verdict.noSilentTruncation ? "NO_SILENT_TRUNCATION" : "STATUS_DIFFER",
                    verdict.countReady, verdict.countCanonical,
                    verdict.firstDivergentIndex != static_cast<size_t>(-1)
                        ? (", firstDivergent=" + std::to_string(verdict.firstDivergentIndex)).c_str()
                        : "");
    }
    std::printf("oracle: %s\n", allOpened ? "ALL_CELLS_PASS" : "MISMATCHES_PRESENT");
    return allOpened ? 0 : 1;
}
