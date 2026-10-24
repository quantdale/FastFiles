// durable-cold-search-index-and-hybrid-residency 4.1: exact ordered-result
// differential over an internally-created disposable copy. The reference
// bypasses ALL name prefilters; route/status/row-count premises fail closed.
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "IndexPipeline.h"
#include "ffsetup/BuildIdentity.h"
#include "sqlite3.h"

namespace {

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

// SQLite backup includes committed WAL content; never copy a live .db alone.
// The source is read-only. The scratch directory is new and owned by this run.
bool CopySnapshot(const std::string& source, std::filesystem::path& scratch, uint64_t& rows) {
    wchar_t temp[MAX_PATH]{};
    wchar_t unique[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"ffo", 0, unique)) return false;
    DeleteFileW(unique);
    scratch = unique;
    if (!std::filesystem::create_directory(scratch)) return false;
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
    double elapsedMs = 0;
};

Run Search(ffengine::IndexPipeline& pipeline, const Cell& cell, bool canonical) {
    Run run;
    ffengine::IndexPipeline::SearchTestHooks hooks;
    hooks.forceCanonicalColdScan = canonical;
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

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::string source;
    std::wstring sourceWide;
    std::string output;
    bool readOnlyFixture = false;
    bool extended = false;
    bool selfTest = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring option = argv[index];
        if (option == L"--db" && index + 1 < argc) {
            sourceWide = argv[++index];
            source = Utf8(sourceWide);
        }
        else if (option == L"--output" && index + 1 < argc) output = Utf8(argv[++index]);
        else if (option == L"--extended") extended = true;
        else if (option == L"--self-test") selfTest = true;
        else if (option == L"--readonly-fixture") readOnlyFixture = true;
        else return 2;
    }
    if (!ComparatorSelfTest()) return 1;
    if (selfTest) return 0;
    if (source.empty() || output.empty()) return 2;

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
        cells.push_back({"size_asc", L"report", 2});
        cells.push_back({"size_desc", L"report", 2, 4096, true});
        cells.push_back({"modified_asc", L"report", 3});
        cells.push_back({"modified_desc", L"report", 3, 4096, true});
        cells.push_back({"created_asc", L"report", 4});
        cells.push_back({"created_desc", L"report", 4, 4096, true});
        cells.push_back({"cap_1", L"report", 0, 1});
        cells.push_back({"cap_2", L"report", 0, 2});
        cells.push_back({"cap_17", L"report", 0, 17});
        cells.push_back({"cap_4096", L"report"});
    }
    cells.push_back({"cancel_immediate", L"common", 0, 4096, false, true});

    out << "{\n\"schema\":\"fastfiles-identity-ordering-oracle-v2\",\n\"build_identity\":"
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
        const bool valid = status && route && nonvacuous && order && caps;
        passed = passed && valid;
        if (!firstCell) out << ",\n";
        firstCell = false;
        out << "{\"cell\":" << Json(cell.name) << ",\"premises_valid\":" << (status && route && nonvacuous ? "true" : "false")
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
    if (!readOnlyFixture) std::filesystem::remove_all(scratch);
    return passed ? 0 : 1;
}
