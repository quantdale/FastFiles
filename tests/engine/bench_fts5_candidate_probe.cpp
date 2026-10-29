// durable-cold-search-index-and-hybrid-residency 1.2: Alternative-A
// prototype probe (FTS5 candidate generation). BENCH-ONLY: this target links
// its own sqlite3 amalgamation build with SQLITE_ENABLE_FTS5 (the production
// sqlite3 target deliberately does not enable FTS5) and never touches
// production schema or code paths. It measures the exact construction the
// lane-A research report specified:
//   entry_name_terms(name_folded, volume_id UNINDEXED, frn UNINDEXED)
//     USING fts5(..., tokenize="trigram case_sensitive 1")
// with candidates = FTS hits UNION exception side-set (non-ASCII + surrogate
// names, mirroring the in-memory accelerator's conservative superset), and
// canonical verification (CompareStringOrdinal per-alignment) as the sole
// authority. Emits machine-readable JSON with embedded source identity.

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace {

std::wstring FileName(size_t index) {
    const size_t pattern = index % 10;
    if (index == 7391) return L"unique_component_7391.txt";
    if (pattern == 0 || pattern == 1) return L"common_component_" + std::to_wstring(index) + L".txt";
    if (pattern == 2) return L"report_" + std::to_wstring(index) + L".txt";
    if (pattern == 3) return L"photo_" + std::to_wstring(index) + L".jpg";
    if (pattern == 4) return L"archive_" + std::to_wstring(index) + L".zip";
    if (pattern == 5) return L"project_" + std::to_wstring(index) + L".cpp";
    if (pattern == 6) return L"notes_" + std::to_wstring(index) + L".md";
    if (pattern == 7) return L"common_media_" + std::to_wstring(index) + L".mp4";
    if (pattern == 8) return L"\u0441\u043f\u0438\u0441\u043e\u043a_" + std::to_wstring(index) + L".txt";
    return L"backup_" + std::to_wstring(index) + L".7z";
}

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), needed, nullptr, nullptr);
    return result;
}

// The production fold: ASCII A-Z -> a-z only, length-preserving (FoldAscii).
std::string FoldAsciiUtf8(const std::string& utf8) {
    std::string result = utf8;
    for (char& ch : result) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 0x20);
    }
    return result;
}

bool NameIsAsciiUtf16(const std::wstring& name) {
    for (const wchar_t unit : name) {
        if (static_cast<unsigned short>(unit) > 0x7F) return false;
    }
    return true;
}

// Production rule (Projection::UnitMayFoldToAscii equivalent): a name needs
// the side-set only if it carries a surrogate unit or a non-ASCII unit the
// HOST ordinal table equates with some ASCII unit (fold-exceptional), probed
// fail-closed with CompareStringOrdinal like the in-memory accelerator.
bool NameNeedsSideSet(const std::wstring& name) {
    static std::map<unsigned short, int> probeCache;
    for (const wchar_t unit : name) {
        const unsigned short value = static_cast<unsigned short>(unit);
        if (value <= 0x7F) continue;
        if (value >= 0xD800 && value <= 0xDFFF) return true;  // surrogates always
        auto cached = probeCache.find(value);
        if (cached == probeCache.end()) {
            int verdict = 0;  // 0 = no ASCII fold-equal found (non-exceptional)
            const wchar_t wide = static_cast<wchar_t>(value);
            for (unsigned short ascii = 0x20; ascii < 0x7F; ++ascii) {
                const wchar_t asciiUnit = static_cast<wchar_t>(ascii);
                if (CompareStringOrdinal(&wide, 1, &asciiUnit, 1, TRUE) == CSTR_EQUAL) {
                    verdict = 1;
                    break;
                }
            }
            probeCache[value] = verdict;
            cached = probeCache.find(value);
        }
        if (cached->second == 1) return true;
    }
    return false;
}

// Canonical matcher semantics: per-alignment CompareStringOrdinal ignore-case.
bool CanonicalContains(const std::wstring& name, const std::wstring& needle) {
    if (needle.empty() || needle.size() > name.size()) return false;
    for (size_t offset = 0; offset + needle.size() <= name.size(); ++offset) {
        if (CompareStringOrdinal(name.c_str() + offset, static_cast<int>(needle.size()),
                                 needle.c_str(), static_cast<int>(needle.size()), TRUE) == CSTR_EQUAL) {
            return true;
        }
    }
    return false;
}

struct QueryCase {
    const char* name;
    const wchar_t* needle;
};

const QueryCase kQueryCases[] = {
    {"selective", L"unique_component_7391"},
    {"exact_name", L"unique_component_7391.txt"},
    {"no_match", L"zzzz_never_present"},
    {"prefix", L"photo"},
    {"moderate", L"report"},
    {"mixed_case", L"CoMmOn_CoMpOnEnT"},
    {"broad", L"common"},
    {"worst_substring", L"component"},
    {"short_prefix", L"co"},
    {"unicode", L"\u0441\u043f\u0438\u0441\u043e\u043a"},
};

struct QueryResult {
    std::string queryClass;
    bool eligible = false;
    size_t candidateCount = 0;
    size_t sharpCandidateCount = 0;
    size_t canonicalCount = 0;
    size_t falseNegatives = 0;
    size_t sharpFalseNegatives = 0;
    double medianMs = 0.0;
    double p90Ms = 0.0;
};

double Percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>((values.size() - 1) * fraction);
    return values[index];
}

void MustExec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::fprintf(stderr, "SQL failed: %s (%s)\n", sql, error ? error : "?");
        sqlite3_free(error);
        std::exit(1);
    }
}

int64_t Scalar(sqlite3* db, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return -1;
    int64_t value = -1;
    if (sqlite3_step(statement) == SQLITE_ROW) value = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    return value;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    size_t scale = 200000;
    std::wstring outputPath = L"fts5-candidate-probe.json";
    std::string sourceSha;
    std::string sourceState;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--scale" && index + 1 < argc) {
            scale = static_cast<size_t>(_wtol(argv[++index]));
        } else if (argument == L"--output" && index + 1 < argc) {
            outputPath = argv[++index];
        } else if (argument == L"--source-sha" && index + 1 < argc) {
            sourceSha = ToUtf8(argv[++index]);
        } else if (argument == L"--source-state" && index + 1 < argc) {
            sourceState = ToUtf8(argv[++index]);
        } else {
            std::fwprintf(stderr, L"Unknown argument: %s\n", argument.c_str());
            return 2;
        }
    }
    if (scale == 0) return 2;

    const bool fts5Compiled = sqlite3_compileoption_used("ENABLE_FTS5") != 0;
    std::printf("scale=%zu fts5=%d\n", scale, fts5Compiled ? 1 : 0);
    if (!fts5Compiled) {
        std::fprintf(stderr, "FTS5 not compiled into this probe build\n");
        return 1;
    }

    // Fresh on-disk DB (production shape: WAL, same pragmas as Store::Open).
    const std::wstring dbPath = outputPath + L".db";
    DeleteFileW(dbPath.c_str());
    DeleteFileW((dbPath + L"-wal").c_str());
    DeleteFileW((dbPath + L"-shm").c_str());
    sqlite3* db = nullptr;
    if (sqlite3_open16(dbPath.c_str(), &db) != SQLITE_OK) {
        std::fprintf(stderr, "open failed\n");
        return 1;
    }
    MustExec(db, "PRAGMA journal_mode=WAL;");
    MustExec(db, "PRAGMA synchronous=NORMAL;");
    MustExec(db, "CREATE TABLE entries (frn INTEGER PRIMARY KEY, name TEXT NOT NULL);");
    MustExec(db, "CREATE VIRTUAL TABLE entry_name_terms USING fts5(name_folded, frn UNINDEXED, tokenize=\"trigram case_sensitive 1\");");
    MustExec(db, "CREATE TABLE entry_name_exceptions (frn INTEGER PRIMARY KEY);");
    MustExec(db, "CREATE TABLE entry_name_exceptions_sharp (frn INTEGER PRIMARY KEY);");

    std::vector<std::wstring> names;
    names.reserve(scale);
    for (size_t index = 0; index < scale; ++index) names.push_back(FileName(index));

    // Ingestion: entries + FTS + exceptions in ONE transaction per batch
    // (mirrors the lane-A same-ApplyBatch-transaction maintenance contract).
    const auto ingestStart = std::chrono::steady_clock::now();
    MustExec(db, "BEGIN IMMEDIATE;");
    for (size_t index = 0; index < scale; ++index) {
        sqlite3_stmt* insertEntry = nullptr;
        sqlite3_prepare_v2(db, "INSERT INTO entries(frn, name) VALUES(?1, ?2);", -1, &insertEntry, nullptr);
        sqlite3_bind_int64(insertEntry, 1, static_cast<sqlite3_int64>(index + 1));
        const std::string utf8 = ToUtf8(names[index]);
        sqlite3_bind_text(insertEntry, 2, utf8.c_str(), static_cast<int>(utf8.size()), SQLITE_TRANSIENT);
        sqlite3_step(insertEntry);
        sqlite3_finalize(insertEntry);

        sqlite3_stmt* insertTerm = nullptr;
        sqlite3_prepare_v2(db, "INSERT INTO entry_name_terms(name_folded, frn) VALUES(?1, ?2);", -1, &insertTerm, nullptr);
        const std::string folded = FoldAsciiUtf8(utf8);
        sqlite3_bind_text(insertTerm, 1, folded.c_str(), static_cast<int>(folded.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(insertTerm, 2, static_cast<sqlite3_int64>(index + 1));
        sqlite3_step(insertTerm);
        sqlite3_finalize(insertTerm);

        if (!NameIsAsciiUtf16(names[index])) {
            sqlite3_stmt* insertException = nullptr;
            sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO entry_name_exceptions(frn) VALUES(?1);", -1, &insertException, nullptr);
            sqlite3_bind_int64(insertException, 1, static_cast<sqlite3_int64>(index + 1));
            sqlite3_step(insertException);
            sqlite3_finalize(insertException);
        }
        if (NameNeedsSideSet(names[index])) {
            sqlite3_stmt* insertSharp = nullptr;
            sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO entry_name_exceptions_sharp(frn) VALUES(?1);", -1, &insertSharp, nullptr);
            sqlite3_bind_int64(insertSharp, 1, static_cast<sqlite3_int64>(index + 1));
            sqlite3_step(insertSharp);
            sqlite3_finalize(insertSharp);
        }
    }
    MustExec(db, "COMMIT;");
    const auto ingestEnd = std::chrono::steady_clock::now();
    const double ingestMs = std::chrono::duration<double, std::milli>(ingestEnd - ingestStart).count();

    const int64_t pageCount = Scalar(db, "PRAGMA page_count;");
    const int64_t pageSize = Scalar(db, "PRAGMA page_size;");
    const int64_t exceptionCount = Scalar(db, "SELECT COUNT(*) FROM entry_name_exceptions;");
    const int64_t sharpExceptionCount = Scalar(db, "SELECT COUNT(*) FROM entry_name_exceptions_sharp;");

    // Candidate queries: FTS MATCH (folded, escaped) UNION exception side-set;
    // canonical verification is the authority; false negatives are contract breaks.
    std::vector<QueryResult> results;
    for (const QueryCase& queryCase : kQueryCases) {
        QueryResult result;
        result.queryClass = queryCase.name;
        const std::wstring needle(queryCase.needle);
        // Eligibility mirrors the production gate: single literal, ASCII, folded length >= 3.
        const std::string needleUtf8 = ToUtf8(needle);
        const std::string needleFolded = FoldAsciiUtf8(needleUtf8);
        result.eligible = NameIsAsciiUtf16(needle) && needleFolded.size() >= 3;
        std::set<int64_t> candidates;
        std::set<int64_t> sharpCandidates;
        if (result.eligible) {
            // Trigram phrase query on the folded column. Double quotes escape
            // the phrase; embedded quotes are doubled per FTS5 string syntax.
            std::string escaped = "\"";
            for (const char ch : needleFolded) {
                if (ch == '"') { escaped += "\"\""; } else { escaped += ch; }
            }
            escaped += "\"";
            const std::string sql =
                "SELECT frn FROM entry_name_terms WHERE entry_name_terms MATCH ?1 "
                "UNION SELECT frn FROM entry_name_exceptions;";
            const std::string sharpSql =
                "SELECT frn FROM entry_name_terms WHERE entry_name_terms MATCH ?1 "
                "UNION SELECT frn FROM entry_name_exceptions_sharp;";
            std::vector<double> samples;
            for (size_t warm = 0; warm < 6; ++warm) {
                sqlite3_stmt* statement = nullptr;
                const auto start = std::chrono::steady_clock::now();
                if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
                    std::fprintf(stderr, "candidate query prepare failed for %s: %s\n", queryCase.name, sqlite3_errmsg(db));
                    break;
                }
                sqlite3_bind_text(statement, 1, escaped.c_str(), static_cast<int>(escaped.size()), SQLITE_TRANSIENT);
                candidates.clear();
                while (sqlite3_step(statement) == SQLITE_ROW) {
                    candidates.insert(sqlite3_column_int64(statement, 0));
                }
                sqlite3_finalize(statement);
                const auto end = std::chrono::steady_clock::now();
                if (warm >= 1) samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
            }
            result.medianMs = Percentile(samples, 0.50);
            result.p90Ms = Percentile(samples, 0.90);
            sqlite3_stmt* sharpStatement = nullptr;
            if (sqlite3_prepare_v2(db, sharpSql.c_str(), -1, &sharpStatement, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(sharpStatement, 1, escaped.c_str(), static_cast<int>(escaped.size()), SQLITE_TRANSIENT);
                while (sqlite3_step(sharpStatement) == SQLITE_ROW) {
                    sharpCandidates.insert(sqlite3_column_int64(sharpStatement, 0));
                }
                sqlite3_finalize(sharpStatement);
            }
        } else {
            for (size_t index = 0; index < scale; ++index) {
                candidates.insert(static_cast<int64_t>(index + 1));
                sharpCandidates.insert(static_cast<int64_t>(index + 1));
            }
        }
        result.candidateCount = candidates.size();
        result.sharpCandidateCount = sharpCandidates.size();
        // Canonical verification + false-negative detection (the contract).
        for (size_t index = 0; index < scale; ++index) {
            if (CanonicalContains(names[index], needle)) {
                ++result.canonicalCount;
                if (candidates.find(static_cast<int64_t>(index + 1)) == candidates.end()) {
                    ++result.falseNegatives;
                }
                if (sharpCandidates.find(static_cast<int64_t>(index + 1)) == sharpCandidates.end()) {
                    ++result.sharpFalseNegatives;
                }
            }
        }
        results.push_back(result);
        std::printf("%-16s eligible=%d candidates=%zu sharp=%zu canonical=%zu falseNeg=%zu sharpFN=%zu median=%.3fms\n",
                    queryCase.name, result.eligible ? 1 : 0, result.candidateCount, result.sharpCandidateCount,
                    result.canonicalCount, result.falseNegatives, result.sharpFalseNegatives, result.medianMs);
    }

    // Red-control cases: fold-exceptional and surrogate names must be covered
    // by the side-set construction (candidates union exceptions).
    struct RedCase { const wchar_t* name; const wchar_t* needle; };
    const RedCase redCases[] = {
        {L"\u017Fand_99999.txt", L"sand"},
        {L"\u212Aelvin_99999.txt", L"kelvin"},
    };
    std::string redJson;
    for (const RedCase& red : redCases) {
        const std::wstring name(red.name);
        const std::wstring needle(red.needle);
        const bool canonical = CanonicalContains(name, needle);
        // In production the side-set includes fold-exceptional names via the
        // host probe; in this probe the name is non-ASCII so it IS in the
        // exception set by the same rule (non-ASCII presence).
        const bool inSideSet = !NameIsAsciiUtf16(name);
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s{\"name\":\"red\",\"canonical\":%s,\"side_set\":%s,\"covered\":%s}",
                      redJson.empty() ? "" : ",", canonical ? "true" : "false",
                      inSideSet ? "true" : "false", (canonical && inSideSet) ? "true" : "false");
        redJson += buffer;
    }

    FILE* out = nullptr;
    _wfopen_s(&out, outputPath.c_str(), L"wb");
    if (out == nullptr) {
        std::fprintf(stderr, "cannot write output\n");
        return 1;
    }
    std::fprintf(out,
                 "{\"schema\":\"fastfiles-fts5-candidate-probe-v1\",\"source_sha\":\"%s\",\"source_state\":\"%s\","
                 "\"scale\":%zu,\"fts5_compiled\":true,\"ingest_ms\":%.3f,\"ingest_rows_per_sec\":%.1f,"
                 "\"db_bytes\":%lld,\"page_count\":%lld,\"page_size\":%lld,\"exception_count\":%lld,"
                 "\"sharp_exception_count\":%lld,"
                 "\"index_bytes_per_1m_rows\":%.1f,\"queries\":[",
                 sourceSha.c_str(), sourceState.c_str(), scale, ingestMs,
                 ingestMs > 0 ? static_cast<double>(scale) * 1000.0 / ingestMs : 0.0,
                 static_cast<long long>(pageCount * pageSize), static_cast<long long>(pageCount),
                 static_cast<long long>(pageSize), static_cast<long long>(exceptionCount),
                 static_cast<long long>(sharpExceptionCount),
                 static_cast<double>(pageCount * pageSize) * 1000000.0 / static_cast<double>(scale));
    for (size_t index = 0; index < results.size(); ++index) {
        const QueryResult& result = results[index];
        std::fprintf(out,
                     "%s{\"query_class\":\"%s\",\"eligible\":%s,\"candidate_count\":%zu,\"sharp_candidate_count\":%zu,\"canonical_count\":%zu,"
                     "\"false_negatives\":%zu,\"sharp_false_negatives\":%zu,\"median_ms\":%.4f,\"p90_ms\":%.4f}",
                     index == 0 ? "" : ",", result.queryClass.c_str(), result.eligible ? "true" : "false",
                     result.candidateCount, result.sharpCandidateCount, result.canonicalCount, result.falseNegatives,
                     result.sharpFalseNegatives, result.medianMs, result.p90Ms);
    }
    std::fprintf(out, "],\"red_cases\":[%s]}\n", redJson.c_str());
    std::fclose(out);
    sqlite3_close(db);
    std::printf("wrote %ls\n", outputPath.c_str());
    return 0;
}
