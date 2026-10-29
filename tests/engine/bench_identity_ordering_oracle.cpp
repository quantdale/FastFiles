// durable-cold-search-index-and-hybrid-residency 4.1: exact ordered-result
// differential over an internally-created disposable copy. The reference
// bypasses ALL name prefilters; route/status/row-count premises fail closed.
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

#include "IndexPipeline.h"
#include "ffsearch/Search.h"
#include "ffsetup/BuildIdentity.h"
#include "sqlite3.h"

namespace {

constexpr wchar_t kScratchMarker[] = L".fastfiles-oracle-owned";
constexpr char kResidencySchema[] = "fastfiles-residency-canonical-oracle-v3";

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::string Json(const std::string& value) {
    std::string out = "\"";
    for (const unsigned char ch : value) {
        if (ch == '\\' || ch == '"') out += '\\';
        if (ch < 0x20) {
            char escape[7]{};
            std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(ch));
            out += escape;
        } else {
            out += static_cast<char>(ch);
        }
    }
    return out + "\"";
}

bool RemoveOwnedScratch(const std::filesystem::path& scratch) {
    wchar_t temp[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp)) return false;
    std::error_code error;
    const auto root = std::filesystem::canonical(temp, error);
    if (error) return false;
    const auto target = std::filesystem::canonical(scratch, error);
    if (error || target.parent_path() != root ||
        !target.filename().native().starts_with(L"ffo") ||
        !std::filesystem::is_regular_file(target / kScratchMarker, error) || error) {
        std::fprintf(stderr, "refusing cleanup of an unverified oracle scratch directory\n");
        return false;
    }
    std::filesystem::remove_all(target, error);
    return !error;
}

// SQLite backup includes committed WAL content; never copy a live .db alone.
// The source is read-only. The scratch directory is new and owned by this run.
bool CopySnapshot(const std::string& source, std::filesystem::path& scratch, uint64_t& rows) {
    wchar_t temp[MAX_PATH]{};
    wchar_t unique[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"ffo", 0, unique)) return false;
    DeleteFileW(unique);
    scratch = unique;
    if (!std::filesystem::create_directory(scratch)) return false;
    std::ofstream marker(scratch / kScratchMarker, std::ios::binary);
    marker << "owned FastFiles canonical oracle scratch\n";
    marker.close();
    if (!marker) return false;
    sqlite3* input = nullptr;
    sqlite3* output = nullptr;
    bool ok = sqlite3_open_v2(source.c_str(), &input, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK;
    const std::string destination = Utf8((scratch / L"oracle.db").wstring());
    if (ok) ok = sqlite3_open(destination.c_str(), &output) == SQLITE_OK;
    if (ok) {
        sqlite3_backup* backup = sqlite3_backup_init(output, "main", input, "main");
        if (!backup) ok = false;
        else {
            const int step = sqlite3_backup_step(backup, -1);
            const int finish = sqlite3_backup_finish(backup);
            ok = step == SQLITE_DONE && finish == SQLITE_OK;
        }
    }
    if (ok) {
        sqlite3_stmt* stmt = nullptr;
        ok = sqlite3_prepare_v2(output, "SELECT COUNT(*) FROM entries;", -1, &stmt, nullptr) == SQLITE_OK;
        if (ok) ok = sqlite3_step(stmt) == SQLITE_ROW;
        if (ok) rows = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        sqlite3_finalize(stmt);
    }
    if (output) sqlite3_close(output);
    if (input) sqlite3_close(input);
    return ok && rows != 0;
}

// Optional low-disk mode for an existing LOCAL synthetic fixture. SQLite
// enforces mode=ro on every connection; locking/change detection stay ON.
// No immutable/nolock shortcuts, migrations, or source-copy deletion.
int RequireReadOnly(sqlite3* db, char**, const sqlite3_api_routines*) {
    return sqlite3_db_readonly(db, "main") == 1 ? SQLITE_OK : SQLITE_READONLY;
}

bool OpenReadOnlyFixture(const std::filesystem::path& source, std::string& uri, uint64_t& rows) {
    const std::wstring absolute = std::filesystem::absolute(source).generic_wstring();
    if (absolute.size() < 3 || absolute[1] != L':') return false;
    uri = "file:///";
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char ch : Utf8(absolute)) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '/' || ch == ':' || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            uri += static_cast<char>(ch);
        } else {
            uri += '%';
            uri += hex[ch >> 4];
            uri += hex[ch & 15];
        }
    }
    uri += "?mode=ro";
    if (sqlite3_config(SQLITE_CONFIG_URI, 1) != SQLITE_OK ||
        sqlite3_auto_extension(reinterpret_cast<void(*)(void)>(RequireReadOnly)) != SQLITE_OK) return false;
    sqlite3* db = nullptr;
    bool ok = sqlite3_open_v2(uri.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI,
                             nullptr) == SQLITE_OK;
    sqlite3_stmt* stmt = nullptr;
    if (ok) ok = sqlite3_db_readonly(db, "main") == 1 &&
                 sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM entries;", -1, &stmt, nullptr) == SQLITE_OK;
    if (ok) ok = sqlite3_step(stmt) == SQLITE_ROW;
    if (ok) rows = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok && rows != 0;
}

struct Cell {
    const char* name;
    const wchar_t* query;
    uint8_t sort = 0;
    uint32_t cap = 4096;
    bool descending = false;
    bool cancel = false;
    bool requireIndex = false;
};

struct Run {
    ffprotocol::IndexedSearchResponse response;
    size_t coldRows = 0;
    size_t hotRows = 0;
    size_t candidateRoutes = 0;
    size_t fallbackRoutes = 0;
    size_t generationRestarts = 0;
    size_t durableFallbackRows = 0;
    bool durableFallbackUsed = false;
    double elapsedMs = 0;
};

Run Search(ffengine::IndexPipeline& pipeline, const Cell& cell, bool canonical,
           const std::function<void(ffengine::IndexPipeline::SearchPhase)>& onPhase = {}) {
    Run run;
    ffengine::IndexPipeline::SearchTestHooks hooks;
    hooks.forceCanonicalColdScan = canonical;
    hooks.onPhase = onPhase;
    hooks.candidatePlannerProbe = [&](bool used, size_t) {
        if (used) ++run.candidateRoutes;
        else ++run.fallbackRoutes;
    };
    pipeline.SetSearchTestHooks(std::move(hooks));
    ffprotocol::IndexedSearchRequest request;
    request.requestId = canonical ? 2 : 1;
    request.scope = 3;
    request.sortField = cell.sort;
    request.descending = cell.descending;
    request.maxResults = cell.cap;
    request.query = cell.query;
    ffengine::IndexPipeline::SearchTimings timings;
    const auto start = std::chrono::steady_clock::now();
    run.response = pipeline.SearchIndexed(request, &timings, [&] { return cell.cancel; });
    run.elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    run.coldRows = timings.coldRowsExamined;
    run.hotRows = timings.hotRowsExamined;
    run.generationRestarts = timings.generationRestarts;
    run.durableFallbackRows = timings.durableFallbackRowsStreamed;
    run.durableFallbackUsed = timings.durableFallbackUsed;
    pipeline.SetSearchTestHooks({});
    return run;
}

bool Same(const ffprotocol::IndexedSearchCandidate& a, const ffprotocol::IndexedSearchCandidate& b) {
    return a.name == b.name && a.folder == b.folder && a.volumeId == b.volumeId &&
        a.id == b.id && a.parentId == b.parentId && a.sizeBytes == b.sizeBytes &&
        a.modifiedTime == b.modifiedTime && a.createdTime == b.createdTime &&
        a.isDirectory == b.isDirectory && a.persistentIndexed == b.persistentIndexed &&
        a.allocatedSizeBytes == b.allocatedSizeBytes && a.allocatedSizeKnown == b.allocatedSizeKnown;
}

bool SequenceEqual(const ffprotocol::IndexedSearchResponse& a,
                   const ffprotocol::IndexedSearchResponse& b) {
    return a.candidates.size() == b.candidates.size() &&
        std::equal(a.candidates.begin(), a.candidates.end(), b.candidates.begin(), Same);
}

bool ComparatorSelfTest() {
    ffprotocol::IndexedSearchResponse a;
    ffprotocol::IndexedSearchCandidate first;
    first.id = 7;
    first.parentId = 5;
    first.volumeId = 1;
    first.name = L"tie.txt";
    a.candidates.push_back(first);
    first.id = 8;
    a.candidates.push_back(first);
    auto b = a;
    if (!SequenceEqual(a, b)) return false;
    std::swap(b.candidates[0], b.candidates[1]);
    if (SequenceEqual(a, b)) return false; // equal counts, different order
    b = a;
    ++b.candidates[0].parentId;
    if (SequenceEqual(a, b)) return false;
    b = a;
    ++b.candidates[0].volumeId;
    if (SequenceEqual(a, b)) return false;
    b = a;
    b.candidates.pop_back();
    return !SequenceEqual(a, b);
}

std::vector<Cell> MakeCells(bool extended) {
    const auto sizeSort = static_cast<uint8_t>(ffsearch::SortField::Size);
    const auto modifiedSort = static_cast<uint8_t>(ffsearch::SortField::Modified);
    const auto createdSort = static_cast<uint8_t>(ffsearch::SortField::Created);
    std::vector<Cell> cells{
        {"selective", L"unique_component_7391", 0, 4096, false, false, true},
        {"exact_name", L"unique_component_7391.txt", 0, 4096, false, false, true},
        {"no_match", L"zzzz_never_present", 0, 4096, false, false, true},
        {"broad", L"common"}, {"extension", L"ext:txt", 3},
        {"unicode", L"\u0441\u043f\u0438\u0441\u043e\u043a"},
        {"moderate", L"report"}, {"prefix", L"photo"},
        {"worst_substring", L"component", 3}, {"high_limit_broad", L"common", 0, 50000},
        {"mixed_case", L"CoMmOn_CoMpOnEnT"}, {"short_prefix", L"co"}
    };
    if (extended) {
        cells.push_back({"size_asc", L"report", sizeSort});
        cells.push_back({"size_desc", L"report", sizeSort, 4096, true});
        cells.push_back({"modified_asc", L"report", modifiedSort});
        cells.push_back({"modified_desc", L"report", modifiedSort, 4096, true});
        cells.push_back({"created_asc", L"report", createdSort});
        cells.push_back({"created_desc", L"report", createdSort, 4096, true});
        cells.push_back({"cap_1", L"report", 0, 1});
        cells.push_back({"cap_2", L"report", 0, 2});
        cells.push_back({"cap_17", L"report", 0, 17});
        cells.push_back({"cap_4096", L"report"});
    }
    cells.push_back({"cancel_immediate", L"common", 0, 4096, false, true});
    return cells;
}

bool SortPremisesValid(const Run& reference, const Cell& cell) {
    const std::string name(cell.name);
    if (!name.starts_with("size_") && !name.starts_with("modified_") &&
        !name.starts_with("created_")) return true;
    const auto key = [&](const ffprotocol::IndexedSearchCandidate& item) {
        if (cell.sort == static_cast<uint8_t>(ffsearch::SortField::Size)) return item.sizeBytes;
        if (cell.sort == static_cast<uint8_t>(ffsearch::SortField::Modified)) return item.modifiedTime;
        return item.createdTime;
    };
    return reference.response.candidates.size() > 1 &&
        std::is_sorted(reference.response.candidates.begin(), reference.response.candidates.end(),
            [&](const auto& left, const auto& right) {
                return cell.descending ? key(left) > key(right) : key(left) < key(right);
            }) &&
        std::any_of(reference.response.candidates.begin() + 1, reference.response.candidates.end(),
            [&](const auto& item) { return key(item) != key(reference.response.candidates.front()); });
}

bool SortPremisesSelfTest() {
    for (const auto& cell : MakeCells(true)) {
        const std::string name(cell.name);
        if (!name.starts_with("size_") && !name.starts_with("modified_") &&
            !name.starts_with("created_")) continue;
        Run reference;
        reference.response.candidates.resize(2);
        auto& first = reference.response.candidates[0];
        auto& second = reference.response.candidates[1];
        first.sizeBytes = first.modifiedTime = first.createdTime = cell.descending ? 2 : 1;
        second.sizeBytes = second.modifiedTime = second.createdTime = cell.descending ? 1 : 2;
        if (!SortPremisesValid(reference, cell)) return false;
        std::swap(first, second);
        if (SortPremisesValid(reference, cell)) return false;
        second = first;
        if (SortPremisesValid(reference, cell)) return false;
    }
    return true;
}

// durable-cold-search-index-and-hybrid-residency 4.1: equal rows alone
// cannot certify an independent reference or its status/cap premises.
bool CanonicalCellEqual(const Run& actual, const Run& reference,
                        const Cell& cell, uint64_t rows) {
    const auto status = cell.cancel ? ffprotocol::IndexedSearchStatus::Cancelled
                                    : ffprotocol::IndexedSearchStatus::Completed;
    const bool referenceScan = reference.hotRows == 0 &&
        reference.coldRows == (cell.cancel ? uint64_t{0} : rows) &&
        reference.candidateRoutes == 0 && reference.fallbackRoutes == 0;
    const bool nonvacuous = cell.cancel
        ? actual.response.candidates.empty() && reference.response.candidates.empty() &&
            !actual.response.truncated && !reference.response.truncated
        : (std::string(cell.name) == "no_match" ? reference.response.candidates.empty()
                                               : !reference.response.candidates.empty());
    return actual.response.status == status && reference.response.status == status &&
        actual.response.requestId == 1 && reference.response.requestId == 2 &&
        referenceScan && nonvacuous && SortPremisesValid(reference, cell) &&
        SequenceEqual(actual.response, reference.response) &&
        actual.response.truncated == reference.response.truncated &&
        actual.response.candidates.size() <= cell.cap &&
        reference.response.candidates.size() <= cell.cap;
}

bool ResidencyValidationSelfTest() {
    const Cell cell{"probe", L"alpha", 0, 17};
    Run actual;
    actual.response.requestId = 1;
    actual.response.status = ffprotocol::IndexedSearchStatus::Completed;
    actual.response.candidates.emplace_back().name = L"alpha";
    Run reference = actual;
    reference.response.requestId = 2;
    reference.coldRows = 2;
    if (!CanonicalCellEqual(actual, reference, cell, 2)) return false;
    actual.response.requestId = 2;
    if (CanonicalCellEqual(actual, reference, cell, 2)) return false;
    actual.response.requestId = 1;
    reference.response.requestId = 1;
    if (CanonicalCellEqual(actual, reference, cell, 2)) return false;
    reference.response.requestId = 2;
    reference.coldRows = 1;
    if (CanonicalCellEqual(actual, reference, cell, 2)) return false;
    reference.coldRows = 2;
    reference.candidateRoutes = 1;
    if (CanonicalCellEqual(actual, reference, cell, 2)) return false;
    reference.candidateRoutes = 0;
    reference.hotRows = 1;
    if (CanonicalCellEqual(actual, reference, cell, 2)) return false;
    reference.hotRows = 0;
    actual.response.truncated = true;
    if (CanonicalCellEqual(actual, reference, cell, 2)) return false;
    actual.response.truncated = false;
    actual.response.candidates.front().allocatedSizeKnown = true;
    if (CanonicalCellEqual(actual, reference, cell, 2)) return false;
    actual.response.candidates.front().allocatedSizeKnown = false;
    actual.response.status = ffprotocol::IndexedSearchStatus::Unavailable;
    if (CanonicalCellEqual(actual, reference, cell, 2)) return false;
    actual.response.status = ffprotocol::IndexedSearchStatus::Cancelled;
    reference.response.status = ffprotocol::IndexedSearchStatus::Cancelled;
    actual.response.candidates.clear();
    reference.response.candidates.clear();
    reference.coldRows = 0;
    Cell cancelled = cell;
    cancelled.cancel = true;
    if (!CanonicalCellEqual(actual, reference, cancelled, 2)) return false;
    actual.response.truncated = true;
    reference.response.truncated = true;
    if (CanonicalCellEqual(actual, reference, cancelled, 2)) return false;
    actual.response.truncated = false;
    reference.response.truncated = false;
    reference.response.requestId = 1;
    return !CanonicalCellEqual(actual, reference, cancelled, 2);
}

enum class ResidencyTransition { Keep, UnloadFirst, UnloadAll, Rebuild };

struct ResidencyStage {
    const char* name;
    size_t expectedCold;
    ResidencyTransition transition;
};

// durable-cold-search-index-and-hybrid-residency 4.1: a default-budget rebuild
// may genuinely retain just one volume. Never label an all-cold rebuild warm.
std::vector<ResidencyStage> PlanResidencyStages(size_t volumeCount, size_t initialCold) {
    if (volumeCount != 2 || initialCold > 1) return {};
    if (initialCold == 1) {
        return {{"mixed", 1, ResidencyTransition::Keep},
                {"cold", 2, ResidencyTransition::UnloadAll},
                {"reloaded_mixed", 1, ResidencyTransition::Rebuild}};
    }
    return {{"resident", 0, ResidencyTransition::Keep},
            {"mixed", 1, ResidencyTransition::UnloadFirst},
            {"cold", 2, ResidencyTransition::UnloadAll},
            {"reloaded_resident", 0, ResidencyTransition::Rebuild}};
}

bool ResidencyPlanSelfTest() {
    const auto resident = PlanResidencyStages(2, 0);
    const auto mixed = PlanResidencyStages(2, 1);
    if (resident.size() != 4 || mixed.size() != 3 ||
        !PlanResidencyStages(2, 2).empty() || !PlanResidencyStages(2, 3).empty() ||
        !PlanResidencyStages(0, 0).empty() || !PlanResidencyStages(1, 0).empty() ||
        !PlanResidencyStages(3, 1).empty()) return false;
    // Follow each transition independently: an unchanged mixed start must not
    // unload its remaining hot volume until the explicit all-cold stage.
    for (const size_t initial : {size_t{0}, size_t{1}}) {
        size_t cold = initial;
        for (const auto& stage : PlanResidencyStages(2, initial)) {
            switch (stage.transition) {
            case ResidencyTransition::Keep: break;
            case ResidencyTransition::UnloadFirst: ++cold; break;
            case ResidencyTransition::UnloadAll: cold = 2; break;
            case ResidencyTransition::Rebuild: cold = initial; break;
            }
            if (cold != stage.expectedCold) return false;
        }
    }
    return std::string(resident.front().name) == "resident" &&
        std::string(resident.back().name) == "reloaded_resident" &&
        std::string(mixed.front().name) == "mixed" &&
        std::string(mixed.back().name) == "reloaded_mixed";
}

size_t ColdVolumeCount(ffengine::IndexPipeline& pipeline,
                       const std::vector<ffindexstore::VolumeMetadata>& volumes) {
    size_t cold = 0;
    for (const auto& volume : volumes) if (pipeline.IsVolumeEvicted(volume.rowId)) ++cold;
    return cold;
}

bool ValidResidency(ffengine::IndexPipeline& pipeline,
                    const std::vector<ffindexstore::VolumeMetadata>& volumes,
                    size_t expectedCold) {
    const auto memory = pipeline.GetProjectionMemoryDiagnostics();
    constexpr size_t kProductionMaximum = size_t{500} * 1024 * 1024;
    return memory.configuredMaximumBytes == kProductionMaximum &&
        memory.estimatedBytes <= memory.configuredMaximumBytes &&
        ColdVolumeCount(pipeline, volumes) == expectedCold;
}

bool RunResidencyOracle(const std::string& database, const std::string& source,
                        const std::string& output, uint64_t rows, bool extended) {
    std::ofstream out(output, std::ios::binary);
    if (!out) return false;
    const auto fail = [&](const char* reason) {
        out << "{\"schema\":" << Json(kResidencySchema) << ","
            << "\"all_passed\":false,\"failure\":" << Json(reason) << "}\n";
        std::fprintf(stderr, "%s\n", reason);
        return false;
    };
    ffengine::IndexPipeline tested;
    ffengine::IndexPipeline reference;
    if (!tested.Open(database) || !reference.Open(database)) return fail("pipeline open failed");
    const auto volumes = tested.GetAllVolumes();
    if (volumes.size() != 2) return fail("residency routes require exactly two nonempty logical volumes");
    uint64_t volumeRows = 0;
    for (const auto& volume : volumes) {
        if (volume.entryCount == 0 || volume.entryCount > rows || volumeRows > rows - volume.entryCount) {
            return fail("fixture volume cardinality is invalid");
        }
        volumeRows += volume.entryCount;
    }
    if (volumeRows != rows) return fail("volume cardinalities do not cover the complete fixture");
    tested.RebuildAll({});
    reference.RebuildAll({});
    for (size_t index = 0; index < volumes.size(); ++index) {
        ffprotocol::VolumeSetting setting;
        setting.key = index == 0 ? L"C:" : L"D:";
        setting.enabled = true;
        tested.SetVolumeRules(volumes[index].rowId, setting);
        reference.SetVolumeRules(volumes[index].rowId, setting);
        reference.UnloadVolumeFromProjection(volumes[index].rowId);
    }
    if (!ValidResidency(reference, volumes, volumes.size())) return fail("reference is not all-cold at the production budget");
    const auto cells = MakeCells(extended);
    const size_t initialCold = ColdVolumeCount(tested, volumes);
    const auto stages = PlanResidencyStages(volumes.size(), initialCold);
    if (stages.empty() || !ValidResidency(tested, volumes, initialCold)) {
        return fail("tested rebuild must retain a complete resident volume at the production budget");
    }
    const size_t expectedCells = stages.size() * cells.size() + 1;
    out << "{\n\"schema\":" << Json(kResidencySchema) << ",\n\"build_identity\":"
        << Json(ffsetup::kBuildIdentityAscii) << ",\n\"input\":" << Json(source)
        << ",\n\"fixture_access\":\"owned-backup-copy\",\n\"fixture_rows\":" << rows
        << ",\n\"initial_cold_volumes\":" << initialCold
        << ",\n\"expected_cells\":" << expectedCells
        << ",\n\"reference\":\"separate all-cold pipeline; all candidate prefilters bypassed\","
        << "\n\"volumes\":[";
    for (size_t index = 0; index < volumes.size(); ++index) {
        if (index != 0) out << ",";
        out << "{\"row_id\":" << volumes[index].rowId << ",\"rows\":" << volumes[index].entryCount
            << ",\"prefix\":" << Json(index == 0 ? "C:" : "D:") << "}";
    }
    out << "],\n\"stages\":[\n";
    bool passed = true;
    size_t passedCells = 0;
    for (size_t stage = 0; stage < stages.size(); ++stage) {
        const auto& planned = stages[stage];
        switch (planned.transition) {
        case ResidencyTransition::Keep: break;
        case ResidencyTransition::UnloadFirst:
            tested.UnloadVolumeFromProjection(volumes.front().rowId);
            break;
        case ResidencyTransition::UnloadAll:
            for (const auto& volume : volumes) tested.UnloadVolumeFromProjection(volume.rowId);
            break;
        case ResidencyTransition::Rebuild: tested.RebuildAll({}); break;
        }
        const size_t expectedCold = planned.expectedCold;
        const auto memory = tested.GetProjectionMemoryDiagnostics();
        const bool setup = ValidResidency(tested, volumes, expectedCold) &&
            ValidResidency(reference, volumes, volumes.size());
        if (stage != 0) out << ",\n";
        out << "{\"route\":" << Json(planned.name) << ",\"configured_maximum_bytes\":"
            << memory.configuredMaximumBytes << ",\"estimated_bytes\":" << memory.estimatedBytes
            << ",\"actual_cold_volumes\":" << ColdVolumeCount(tested, volumes)
            << ",\"expected_cold_volumes\":" << expectedCold
            << ",\"residency_premises_valid\":" << (setup ? "true" : "false") << ",\"cells\":[";
        if (!setup) {
            out << "],\"all_passed\":false}";
            passed = false;
            break;
        }
        bool stagePassed = true;
        size_t hotRows = 0;
        size_t coldRows = 0;
        for (size_t index = 0; index < cells.size(); ++index) {
            const auto& cell = cells[index];
            const Run actual = Search(tested, cell, false);
            const Run canonical = Search(reference, cell, true);
            hotRows += actual.hotRows;
            coldRows += actual.coldRows + actual.durableFallbackRows;
            const bool route = (expectedCold != 0 || actual.coldRows == 0) &&
                (expectedCold != volumes.size() || actual.hotRows == 0) &&
                (cell.cancel || !cell.requireIndex || expectedCold == 0 || actual.candidateRoutes != 0);
            const bool valid = route && CanonicalCellEqual(actual, canonical, cell, rows);
            stagePassed = stagePassed && valid;
            if (valid) ++passedCells;
            if (index != 0) out << ",";
            out << "\n{\"cell\":" << Json(cell.name) << ",\"passed\":" << (valid ? "true" : "false")
                << ",\"sort_field\":" << static_cast<unsigned>(cell.sort)
                << ",\"descending\":" << (cell.descending ? "true" : "false")
                << ",\"sort_premises_valid\":" << (SortPremisesValid(canonical, cell) ? "true" : "false")
                << ",\"route_valid\":" << (route ? "true" : "false")
                << ",\"hot_rows_examined\":" << actual.hotRows << ",\"cold_rows_examined\":" << actual.coldRows
                << ",\"canonical_rows_examined\":" << canonical.coldRows
                << ",\"canonical_hot_rows\":" << canonical.hotRows
                << ",\"canonical_candidate_admissions\":" << canonical.candidateRoutes
                << ",\"candidate_admissions\":" << actual.candidateRoutes
                << ",\"actual_count\":" << actual.response.candidates.size()
                << ",\"canonical_count\":" << canonical.response.candidates.size()
                << ",\"actual_status\":" << static_cast<unsigned>(actual.response.status)
                << ",\"canonical_status\":" << static_cast<unsigned>(canonical.response.status)
                << ",\"truncated\":" << (actual.response.truncated ? "true" : "false") << "}";
            out.flush();
            std::printf("%s %s/%s hot=%zu cold=%zu canonical=%zu\n",
                valid ? "PASS" : "FAIL", planned.name, cell.name,
                actual.hotRows, actual.coldRows, canonical.coldRows);
            std::fflush(stdout);
        }
        const bool observedRoutes = (expectedCold == volumes.size() || hotRows != 0) &&
            (expectedCold == 0 || coldRows != 0);
        const bool released = tested.GetReadPlaneDiagnostics().activeDurableReaders == 0 &&
            reference.GetReadPlaneDiagnostics().activeDurableReaders == 0;
        stagePassed = stagePassed && observedRoutes && released &&
            ValidResidency(tested, volumes, expectedCold);
        out << "],\"actual_hot_rows\":" << hotRows << ",\"actual_cold_rows\":" << coldRows
            << ",\"required_routes_observed\":" << (observedRoutes ? "true" : "false")
            << ",\"reader_leases_released\":" << (released ? "true" : "false")
            << ",\"all_passed\":" << (stagePassed ? "true" : "false") << "}";
        passed = passed && stagePassed;
        if (!stagePassed) break;
    }
    out << "\n],\n\"inflight_eviction\":";
    if (passed) {
        tested.RebuildAll({});
        const bool before = ValidResidency(tested, volumes, initialCold);
        bool evicted = false;
        const Cell cell{"generation_restart", L"common"};
        const Run actual = Search(tested, cell, false, [&](ffengine::IndexPipeline::SearchPhase phase) {
            if (phase != ffengine::IndexPipeline::SearchPhase::WorkStarted || evicted) return;
            evicted = true;
            for (const auto& volume : volumes) tested.UnloadVolumeFromProjection(volume.rowId);
        });
        const Run canonical = Search(reference, cell, true);
        const bool valid = before && evicted && actual.generationRestarts >= 1 &&
            ValidResidency(tested, volumes, volumes.size()) &&
            CanonicalCellEqual(actual, canonical, cell, rows) &&
            tested.GetReadPlaneDiagnostics().activeDurableReaders == 0 &&
            reference.GetReadPlaneDiagnostics().activeDurableReaders == 0;
        if (valid) ++passedCells;
        out << "{\"passed\":" << (valid ? "true" : "false")
            << ",\"mutation_observed\":" << (evicted ? "true" : "false")
            << ",\"generation_restarts\":" << actual.generationRestarts
            << ",\"durable_fallback_used\":" << (actual.durableFallbackUsed ? "true" : "false")
            << ",\"durable_fallback_rows\":" << actual.durableFallbackRows
            << ",\"canonical_rows_examined\":" << canonical.coldRows << "}";
        passed = passed && valid;
        std::printf("%s inflight_eviction restarts=%zu canonical=%zu\n",
            valid ? "PASS" : "FAIL", actual.generationRestarts, canonical.coldRows);
    } else {
        out << "null";
    }
    passed = passed && passedCells == expectedCells;
    out << ",\n\"passed_cells\":" << passedCells << ",\n\"all_passed\":" << (passed ? "true" : "false") << "\n}\n";
    reference.Close();
    tested.Close();
    return passed;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::string source;
    std::wstring sourceWide;
    std::string output;
    bool readOnlyFixture = false;
    bool extended = false;
    bool selfTest = false;
    bool residencyRoutes = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring option = argv[index];
        if (option == L"--db" && index + 1 < argc) {
            sourceWide = argv[++index];
            source = Utf8(sourceWide);
        }
        else if (option == L"--output" && index + 1 < argc) output = Utf8(argv[++index]);
        else if (option == L"--extended") extended = true;
        else if (option == L"--self-test") selfTest = true;
        else if (option == L"--residency-routes") residencyRoutes = true;
        else if (option == L"--readonly-fixture") readOnlyFixture = true;
        else return 2;
    }
    if (!ComparatorSelfTest() || !ResidencyValidationSelfTest() || !SortPremisesSelfTest() || !ResidencyPlanSelfTest()) return 1;
    if (selfTest) return 0;
    if (source.empty() || output.empty()) return 2;
    if (residencyRoutes && readOnlyFixture) {
        std::fprintf(stderr, "residency routes require the owned-backup-copy mode\n");
        return 2;
    }

    uint64_t rows = 0;
    std::filesystem::path scratch;
    std::string database;
    if (readOnlyFixture) {
        if (!OpenReadOnlyFixture(sourceWide, database, rows)) return 3;
    } else {
        if (!CopySnapshot(source, scratch, rows)) {
            std::fprintf(stderr, "cannot create a nonempty, consistent scratch snapshot\n");
            return 3;
        }
        database = Utf8((scratch / L"oracle.db").wstring());
    }
    if (residencyRoutes) {
        const bool passed = RunResidencyOracle(database, source, output, rows, extended);
        const bool cleaned = RemoveOwnedScratch(scratch);
        return passed && cleaned ? 0 : 1;
    }
    std::ofstream out(output, std::ios::binary);
    if (!out) return 3;
    ffengine::IndexPipeline pipeline;
    if (!pipeline.Open(database)) return 3;
    pipeline.RebuildAll({});
    const auto volumes = pipeline.GetAllVolumes();
    if (volumes.empty()) return 3;
    for (const auto& volume : volumes) {
        ffprotocol::VolumeSetting setting;
        setting.key = L"C:";
        setting.enabled = true;
        pipeline.SetVolumeRules(volume.rowId, setting);
        pipeline.UnloadVolumeFromProjection(volume.rowId);
    }

    const auto cells = MakeCells(extended);

    out << "{\n\"schema\":\"fastfiles-identity-ordering-oracle-v3\",\n\"build_identity\":"
        << Json(ffsetup::kBuildIdentityAscii) << ",\n\"input\":" << Json(source)
        << ",\n\"fixture_access\":" << Json(readOnlyFixture ? "sqlite-enforced-read-only-uri" : "owned-backup-copy")
        << ",\n\"fixture_rows\":" << rows << ",\n\"cells\":[\n";
    bool passed = true;
    bool firstCell = true;
    for (const auto& cell : cells) {
        std::printf("START %s (%llu rows)\n", cell.name, static_cast<unsigned long long>(rows));
        std::fflush(stdout);
        const Run indexed = Search(pipeline, cell, false);
        const Run canonical = Search(pipeline, cell, true);
        const auto expectedStatus = cell.cancel ? ffprotocol::IndexedSearchStatus::Cancelled
                                               : ffprotocol::IndexedSearchStatus::Completed;
        const bool status = indexed.response.status == expectedStatus &&
                            canonical.response.status == expectedStatus;
        const bool route = cell.cancel ||
            (canonical.coldRows == rows && canonical.hotRows == 0 && indexed.hotRows == 0 &&
             canonical.candidateRoutes == 0 && canonical.fallbackRoutes == 0 &&
             (!cell.requireIndex || indexed.candidateRoutes != 0));
        const bool nonvacuous = cell.cancel ? canonical.response.candidates.empty() :
            (std::string(cell.name) == "no_match" ? canonical.response.candidates.empty() :
                                                  !canonical.response.candidates.empty());
        const bool order = SequenceEqual(indexed.response, canonical.response);
        const bool caps = indexed.response.truncated == canonical.response.truncated &&
                          indexed.response.candidates.size() <= cell.cap &&
                          canonical.response.candidates.size() <= cell.cap;
        const bool valid = status && route && nonvacuous && order && caps &&
            CanonicalCellEqual(indexed, canonical, cell, rows);
        passed = passed && valid;
        if (!firstCell) out << ",\n";
        firstCell = false;
        out << "{\"cell\":" << Json(cell.name) << ",\"premises_valid\":" << (status && route && nonvacuous ? "true" : "false")
            << ",\"sort_field\":" << static_cast<unsigned>(cell.sort)
            << ",\"descending\":" << (cell.descending ? "true" : "false")
            << ",\"sort_premises_valid\":" << (SortPremisesValid(canonical, cell) ? "true" : "false")
            << ",\"identities_equal\":" << (valid ? "true" : "false")
            << ",\"ordering_equal\":" << (valid ? "true" : "false")
            << ",\"caps_equal\":" << (status && route && caps ? "true" : "false")
            << ",\"no_silent_truncation\":" << (valid ? "true" : "false")
            << ",\"count_indexed\":" << indexed.response.candidates.size()
            << ",\"count_canonical\":" << canonical.response.candidates.size()
            << ",\"truncated\":" << (canonical.response.truncated ? "true" : "false")
            << ",\"requested_cap\":" << cell.cap
            << ",\"canonical_rows_examined\":" << canonical.coldRows
            << ",\"indexed_rows_examined\":" << indexed.coldRows
            << ",\"candidate_admissions\":" << indexed.candidateRoutes
            << ",\"fallback_admissions\":" << indexed.fallbackRoutes
            << ",\"indexed_ms\":" << indexed.elapsedMs
            << ",\"canonical_ms\":" << canonical.elapsedMs << "}";
        out.flush();
        std::printf("%s %s rows=%zu/%zu scanned=%zu/%zu candidate=%zu premises=%d\n",
            valid ? "PASS" : "FAIL", cell.name, indexed.response.candidates.size(),
            canonical.response.candidates.size(), indexed.coldRows, canonical.coldRows,
            indexed.candidateRoutes, status && route && nonvacuous ? 1 : 0);
        std::fflush(stdout);
    }
    out << "\n],\n\"all_passed\":" << (passed ? "true" : "false") << "\n}\n";
    out.close();
    pipeline.Close();
    // Only this run's newly-created scratch directory is removed.
    if (!readOnlyFixture && !RemoveOwnedScratch(scratch)) return 3;
    return passed ? 0 : 1;
}
