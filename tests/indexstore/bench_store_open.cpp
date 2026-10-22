// durable-cold-search-index-and-hybrid-residency 5.1: fresh-process
// Store::Open() phase-profiling harness (bench-only; never a CTest gate).
//
// Purpose: decompose open cost across the campaign's startup phases --
// connection creation, integrity check, PRAGMA configuration, schema
// initialization, schema-version read, host-fold signature computation,
// stored-signature comparison, candidate-index consistency checks, and
// candidate-index rebuild -- across FRESH processes (not repeated calls in
// one process), so the constant per-process cost is separated from the
// row-count-dependent cost.
//
// Modes:
//   --build-fixture <N> --db <path> [--seed S] [--volumes K]
//       Deterministic synthetic fixture on the production Store schema.
//   --measure --db <path> --case <case> --processes <K> --output <json>
//       [--source-sha S] [--source-state clean|dirty] [--preset P]
//       [--warm-opens M] [--exe-sha256-expected H]
//       Spawns K fresh copies of ITSELF in --measure-child mode and
//       aggregates per-phase p50/p90/max with full identity provenance.
//   --measure-child --db <path> --case <case> [--warm-opens M]
//       One process: applies the case pre-state, runs Open() with phase
//       timings, and prints ONE JSON object to stdout.
//
// Cases (case pre-state applied by the child before every measured open):
//   valid-reopen       valid v3 database, no tampering
//   fold-mismatch      stored fold signature altered (host-change simulation)
//   corrupt-marker     entry_name_index_meta.state forced to 0
//   migration-v2       v3 structures dropped + user_version=2 (first v3 init)
//   migration-v2-interrupted
//                      migration-v2 + deterministic hard process exit inside
//                      the first candidate-maintenance operation (parent runs
//                      exactly ONE crash child, then K recovery children)
//   read-only          database file set read-only (open must fail closed)
//   empty-create       fresh empty database file per process
//
// Identity discipline (campaign section 14): this harness computes its own
// executable SHA-256 at runtime and records it alongside the caller-supplied
// source identity; the caller-supplied identity is labeled as asserted, and
// ffsetup::kBuildIdentityAscii binds the build to the source tree.

#include <windows.h>
#include <bcrypt.h>

#include <psapi.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "ffindexstore/EntryRecord.h"
#include "ffindexstore/Store.h"
#include "ffsetup/BuildIdentity.h"

#pragma comment(lib, "bcrypt.lib")

namespace {

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), needed, nullptr, nullptr);
    return result;
}

std::wstring ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), needed);
    return result;
}

std::string JsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char ch : text) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(ch));
                    out += buffer;
                } else {
                    out.push_back(ch);
                }
        }
    }
    return out;
}

std::string NowUtc() {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02uZ",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

std::string FileTimeUtc(const FILETIME& ft) {
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&ft, &st)) return "";
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02uZ",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

uint64_t FileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

std::string Sha256OfFile(const std::wstring& path, bool* outOk) {
    *outOk = false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string digest;
    std::vector<unsigned char> buffer(64 * 1024);
    do {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) break;
        DWORD objectSize = 0;
        DWORD cbSize = 0;
        if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                              sizeof(objectSize), &cbSize, 0) != 0) break;
        std::vector<unsigned char> object(objectSize);
        if (BCryptCreateHash(alg, &hash, object.data(), objectSize, nullptr, 0, 0) != 0) break;
        DWORD read = 0;
        bool readOk = true;
        while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            if (read == 0) break;
            if (BCryptHashData(hash, buffer.data(), read, 0) != 0) {
                readOk = false;
                break;
            }
        }
        if (!readOk) break;
        std::array<unsigned char, 32> digestBytes{};
        if (BCryptFinishHash(hash, digestBytes.data(), static_cast<ULONG>(digestBytes.size()), 0) != 0) break;
        char hex[3];
        for (const unsigned char byte : digestBytes) {
            std::snprintf(hex, sizeof(hex), "%02x", byte);
            digest += hex;
        }
        *outOk = true;
    } while (false);
    if (hash != nullptr) BCryptDestroyHash(hash);
    if (alg != nullptr) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(file);
    return digest;
}

std::u16string ToU16(size_t value) {
    std::u16string out;
    if (value == 0) return u"0";
    while (value > 0) {
        out.insert(out.begin(), static_cast<char16_t>(u'0' + (value % 10)));
        value /= 10;
    }
    return out;
}

// Same synthetic-name shape as the candidate/resource benches (10 patterns,
// 10% non-ASCII) so open costs are measured against realistic distributions.
std::u16string FileName(size_t index) {
    const size_t pattern = index % 10;
    if (index == 7391) return u"unique_component_7391.txt";
    if (pattern == 0 || pattern == 1) return u"common_component_" + ToU16(index) + u".txt";
    if (pattern == 2) return u"report_" + ToU16(index) + u".txt";
    if (pattern == 3) return u"photo_" + ToU16(index) + u".jpg";
    if (pattern == 4) return u"archive_" + ToU16(index) + u".zip";
    if (pattern == 5) return u"project_" + ToU16(index) + u".cpp";
    if (pattern == 6) return u"notes_" + ToU16(index) + u".md";
    if (pattern == 7) return u"common_media_" + ToU16(index) + u".mp4";
    if (pattern == 8) return u"\u0441\u043f\u0438\u0441\u043e\u043a_" + ToU16(index) + u".txt";
    return u"backup_" + ToU16(index) + u".7z";
}

bool BuildFixture(const std::wstring& dbPath, size_t rows, size_t volumes, uint64_t seed,
                  bool maintenanceEnabled) {
    DeleteFileW(dbPath.c_str());
    DeleteFileW((dbPath + L"-wal").c_str());
    DeleteFileW((dbPath + L"-shm").c_str());
    // 5.12 (task 3.3c): --maintenance-off builds an entries-only fixture so
    // 5M-scale migration/rebuild cost can be measured without paying the
    // engine-pipeline ingest path (the derived structures are exactly what
    // the migration then builds).
    ffindexstore::SetTestingCandidateMaintenanceDisabled(!maintenanceEnabled);
    ffindexstore::Store store;
    if (!store.Open(ToUtf8(dbPath))) {
        std::fprintf(stderr, "fixture open failed\n");
        ffindexstore::SetTestingCandidateMaintenanceDisabled(false);
        return false;
    }
    std::vector<ffindexstore::VolumeRowId> volumeIds;
    for (size_t volume = 0; volume < volumes; ++volume) {
        ffindexstore::VolumeKey key{};
        for (size_t byte = 0; byte < key.volumeGuid.size(); ++byte) {
            key.volumeGuid[byte] = static_cast<uint8_t>(
                ((seed + 1) * 131u + volume * 17u + byte) & 0xFFu);
        }
        key.serialNumber = static_cast<uint32_t>(9000 + volume);
        const auto id = store.GetOrCreateVolume(key);
        if (!id.has_value()) {
            std::fprintf(stderr, "fixture volume creation failed\n");
            ffindexstore::SetTestingCandidateMaintenanceDisabled(false);
            return false;
        }
        volumeIds.push_back(*id);
    }
    constexpr size_t kBatch = 5000;
    for (size_t offset = 0; offset < rows; offset += kBatch) {
        std::vector<ffindexstore::EntryChange> batch;
        const size_t end = (offset + kBatch < rows) ? offset + kBatch : rows;
        for (size_t index = offset; index < end; ++index) {
            ffindexstore::EntryRecord record;
            const size_t seeded = index + static_cast<size_t>(seed % 977u);
            record.id = {static_cast<uint64_t>(index + 1), 0};
            record.parentId = {5, 0};
            record.name = FileName(seeded);
            record.sizeBytes = 4096;
            record.allocatedSizeKnown = true;
            record.allocatedSizeBytes = 4096;
            batch.push_back({ffindexstore::EntryChangeKind::Upsert, record});
        }
        const ffindexstore::VolumeRowId targetVolume =
            volumeIds[(offset / kBatch) % volumeIds.size()];
        if (!store.ApplyBatch(targetVolume, batch)) {
            std::fprintf(stderr, "fixture ingest failed at %zu\n", offset);
            ffindexstore::SetTestingCandidateMaintenanceDisabled(false);
            return false;
        }
        store.CheckpointIfWalExceeds(32ull * 1024 * 1024);
    }
    ffindexstore::SetTestingCandidateMaintenanceDisabled(false);
    return true;
}

bool ExecDirect(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

// Case pre-state, applied before EVERY measured open (the child re-tampers so
// each fresh process observes the same starting state).
bool ApplyCasePreState(const std::wstring& dbPath, const std::string& testCase) {
    if (testCase == "valid-reopen" || testCase == "read-only" ||
        testCase == "empty-create" || testCase == "migration-v2-interrupted") {
        return true;
    }
    if (testCase == "migration-v2") {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(ToUtf8(dbPath).c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
            if (db != nullptr) sqlite3_close_v2(db);
            return false;
        }
        const bool ok =
            ExecDirect(db, "DROP TABLE IF EXISTS entry_name_exceptions;") &&
            ExecDirect(db, "DROP TABLE IF EXISTS entry_name_index_meta;") &&
            ExecDirect(db, "DROP TABLE IF EXISTS entry_name_index_map;") &&
            ExecDirect(db, "DROP TABLE IF EXISTS entry_name_terms;") &&
            ExecDirect(db, "PRAGMA user_version=2;");
        sqlite3_close_v2(db);
        return ok;
    }
    if (testCase == "fold-mismatch" || testCase == "corrupt-marker") {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(ToUtf8(dbPath).c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
            if (db != nullptr) sqlite3_close_v2(db);
            return false;
        }
        const char* sql = (testCase == "fold-mismatch")
            ? "UPDATE entry_name_index_meta SET fold_signature = fold_signature + 1;"
            : "UPDATE entry_name_index_meta SET state = 0;";
        const bool ok = ExecDirect(db, sql);
        sqlite3_close_v2(db);
        return ok;
    }
    return false;
}

bool PrepareMigrationV2(const std::wstring& dbPath) {
    return ApplyCasePreState(dbPath, "migration-v2");
}

struct ProcessCpuIo {
    double userMs = 0.0;
    double kernelMs = 0.0;
    uint64_t readBytes = 0;
    uint64_t writeBytes = 0;
    uint64_t readOps = 0;
    uint64_t writeOps = 0;
};

double FileTimeMs(const FILETIME& ft) {
    const ULARGE_INTEGER value{ { ft.dwLowDateTime, ft.dwHighDateTime } };
    return static_cast<double>(value.QuadPart) / 10000.0;
}

ProcessCpuIo SnapshotCpuIo() {
    ProcessCpuIo snapshot;
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        snapshot.userMs = FileTimeMs(user);
        snapshot.kernelMs = FileTimeMs(kernel);
    }
    IO_COUNTERS io{};
    if (GetProcessIoCounters(GetCurrentProcess(), &io)) {
        snapshot.readBytes = io.ReadTransferCount;
        snapshot.writeBytes = io.WriteTransferCount;
        snapshot.readOps = io.ReadOperationCount;
        snapshot.writeOps = io.WriteOperationCount;
    }
    return snapshot;
}

uint64_t PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        return static_cast<uint64_t>(counters.PrivateUsage);
    }
    return 0;
}

uint64_t WorkingSetBytes() {
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        return static_cast<uint64_t>(counters.WorkingSetSize);
    }
    return 0;
}

uint64_t PeakWorkingSetBytes() {
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        return static_cast<uint64_t>(counters.PeakWorkingSetSize);
    }
    return 0;
}

struct OpenRunRecord {
    int openIndex = 0;
    std::string cacheState;
    bool openOk = false;
    bool integrityFailed = false;
    ffindexstore::Store::OpenPhaseTimings timings;
    ProcessCpuIo cpuIoDelta;
    uint64_t workingSetBefore = 0;
    uint64_t workingSetAfter = 0;
    uint64_t privateBefore = 0;
    uint64_t privateAfter = 0;
    uint64_t peakWorkingSet = 0;
    std::string timestampUtc;
};

void WriteRun(std::FILE* out, const OpenRunRecord& run) {
    std::fprintf(out, "{");
    std::fprintf(out,
        "\"repetition\":%d,\"cache_state\":\"%s\",\"timestamp\":\"%s\","
        "\"open_ok\":%s,\"integrity_failed\":%s,"
        "\"timings_ms\":{"
        "\"connection_open\":%.4f,\"integrity_check\":%.4f,\"pragma_config\":%.4f,"
        "\"schema_init\":%.4f,\"schema_version_read\":%.4f,\"host_fold_signature\":%.4f,"
        "\"signature_compare\":%.4f,\"consistency_check\":%.4f,\"candidate_rebuild\":%.4f,"
        "\"candidate_map\":%.4f,\"candidate_terms\":%.4f,\"candidate_exception\":%.4f,"
        "\"total\":%.4f},"
        "\"candidate_rows_processed\":%llu,"
        "\"host_fold_signature_value\":%llu,\"prior_schema_version\":%d,\"candidate_index_rebuilt\":%s,"
        "\"cpu_user_ms\":%.3f,\"cpu_kernel_ms\":%.3f,"
        "\"io_read_bytes\":%llu,\"io_write_bytes\":%llu,\"io_read_ops\":%llu,\"io_write_ops\":%llu,"
        "\"working_set_before\":%llu,\"working_set_after\":%llu,"
        "\"private_before\":%llu,\"private_after\":%llu,\"peak_working_set\":%llu}",
        run.openIndex, JsonEscape(run.cacheState).c_str(), run.timestampUtc.c_str(),
        run.openOk ? "true" : "false", run.integrityFailed ? "true" : "false",
        run.timings.connectionOpenMs, run.timings.integrityCheckMs, run.timings.pragmaConfigMs,
        run.timings.schemaInitMs, run.timings.schemaVersionReadMs, run.timings.hostFoldSignatureMs,
        run.timings.signatureCompareMs, run.timings.consistencyCheckMs, run.timings.candidateRebuildMs,
        run.timings.candidateMapMs, run.timings.candidateTermsMs, run.timings.candidateExceptionMs,
        run.timings.totalMs,
        static_cast<unsigned long long>(run.timings.candidateRowsProcessed),
        static_cast<unsigned long long>(run.timings.hostFoldSignature),
        run.timings.priorSchemaVersion,
        run.timings.candidateIndexRebuilt ? "true" : "false",
        run.cpuIoDelta.userMs, run.cpuIoDelta.kernelMs,
        static_cast<unsigned long long>(run.cpuIoDelta.readBytes),
        static_cast<unsigned long long>(run.cpuIoDelta.writeBytes),
        static_cast<unsigned long long>(run.cpuIoDelta.readOps),
        static_cast<unsigned long long>(run.cpuIoDelta.writeOps),
        static_cast<unsigned long long>(run.workingSetBefore),
        static_cast<unsigned long long>(run.workingSetAfter),
        static_cast<unsigned long long>(run.privateBefore),
        static_cast<unsigned long long>(run.privateAfter),
        static_cast<unsigned long long>(run.peakWorkingSet));
}

int RunChild(const std::wstring& dbPath, const std::string& testCase, size_t warmOpens) {
    std::wstring effectivePath = dbPath;
    std::wstring scratchPath;
    if (testCase == "empty-create") {
        wchar_t tempDir[MAX_PATH];
        GetTempPathW(MAX_PATH, tempDir);
        scratchPath = std::wstring(tempDir) + L"ff-openbench-" +
                      std::to_wstring(GetCurrentProcessId()) + L".db";
        DeleteFileW(scratchPath.c_str());
        DeleteFileW((scratchPath + L"-wal").c_str());
        DeleteFileW((scratchPath + L"-shm").c_str());
        effectivePath = scratchPath;
    }
    if (testCase == "migration-v2-interrupted") {
        // Crash child: arm the deterministic hard-exit fault, downgrade, and
        // open; the process dies inside the rebuild's first maintenance op.
        if (!PrepareMigrationV2(effectivePath)) return 3;
        ffindexstore::SetTestingCandidateMaintenanceFault(1, true);
        ffindexstore::Store store;
        (void)store.Open(ToUtf8(effectivePath));
        return 4;  // unreachable when the seam fires
    }
    if (!ApplyCasePreState(effectivePath, testCase)) return 3;

    std::vector<OpenRunRecord> runs;
    const size_t totalOpens = 1 + warmOpens;
    for (size_t openIndex = 0; openIndex < totalOpens; ++openIndex) {
        if (testCase == "migration-v2" || testCase == "fold-mismatch" ||
            testCase == "corrupt-marker") {
            if (!ApplyCasePreState(effectivePath, testCase)) return 3;
        }
        OpenRunRecord run;
        run.openIndex = static_cast<int>(openIndex);
        run.cacheState = openIndex == 0 ? "cold-new-process" : "warm-same-process";
        run.timestampUtc = NowUtc();
        run.workingSetBefore = WorkingSetBytes();
        run.privateBefore = PrivateBytes();
        const ProcessCpuIo before = SnapshotCpuIo();
        ffindexstore::Store store;
        bool integrityFailed = false;
        run.openOk = store.Open(ToUtf8(effectivePath), &integrityFailed, &run.timings);
        run.integrityFailed = integrityFailed;
        const ProcessCpuIo after = SnapshotCpuIo();
        run.workingSetAfter = WorkingSetBytes();
        run.privateAfter = PrivateBytes();
        run.peakWorkingSet = PeakWorkingSetBytes();
        run.cpuIoDelta.userMs = after.userMs - before.userMs;
        run.cpuIoDelta.kernelMs = after.kernelMs - before.kernelMs;
        run.cpuIoDelta.readBytes = after.readBytes - before.readBytes;
        run.cpuIoDelta.writeBytes = after.writeBytes - before.writeBytes;
        run.cpuIoDelta.readOps = after.readOps - before.readOps;
        run.cpuIoDelta.writeOps = after.writeOps - before.writeOps;
        runs.push_back(run);
    }

    std::printf("{\"case\":\"%s\",\"runs\":[", JsonEscape(testCase).c_str());
    for (size_t index = 0; index < runs.size(); ++index) {
        if (index != 0) std::printf(",");
        WriteRun(stdout, runs[index]);
    }
    std::printf("]}");
    if (!scratchPath.empty()) {
        DeleteFileW(scratchPath.c_str());
        DeleteFileW((scratchPath + L"-wal").c_str());
        DeleteFileW((scratchPath + L"-shm").c_str());
    }
    return 0;
}

struct SpawnResult {
    bool spawned = false;
    bool exited = false;
    DWORD exitCode = 0;
    std::string stdoutText;
};

SpawnResult SpawnChild(const std::wstring& exePath, const std::wstring& dbPath,
                       const std::string& testCase, size_t warmOpens) {
    SpawnResult result;
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;
    if (!CreatePipe(&readHandle, &writeHandle, &security, 0)) return result;
    SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0);
    std::wstring command = L"\"" + exePath + L"\" --measure-child --db \"" + dbPath +
                           L"\" --case " + ToWide(testCase) +
                           L" --warm-opens " + std::to_wstring(warmOpens);
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writeHandle;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(exePath.c_str(), commandBuffer.data(), nullptr, nullptr,
                                        TRUE, 0, nullptr, nullptr, &startup, &process);
    CloseHandle(writeHandle);
    if (!created) {
        CloseHandle(readHandle);
        return result;
    }
    result.spawned = true;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readHandle, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        result.stdoutText.append(buffer, buffer + read);
    }
    CloseHandle(readHandle);
    WaitForSingleObject(process.hProcess, INFINITE);
    result.exited = GetExitCodeProcess(process.hProcess, &result.exitCode) != 0;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result;
}

double Percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    size_t index = static_cast<size_t>(fraction * static_cast<double>(values.size()));
    if (index >= values.size()) index = values.size() - 1;
    return values[index];
}

void WriteAggregate(std::FILE* out, const std::vector<OpenRunRecord>& runs) {
    struct TimingField {
        const char* name;
        double ffindexstore::Store::OpenPhaseTimings::*member;
    };
    static const TimingField kTimingFields[] = {
        {"connection_open", &ffindexstore::Store::OpenPhaseTimings::connectionOpenMs},
        {"integrity_check", &ffindexstore::Store::OpenPhaseTimings::integrityCheckMs},
        {"pragma_config", &ffindexstore::Store::OpenPhaseTimings::pragmaConfigMs},
        {"schema_init", &ffindexstore::Store::OpenPhaseTimings::schemaInitMs},
        {"schema_version_read", &ffindexstore::Store::OpenPhaseTimings::schemaVersionReadMs},
        {"host_fold_signature", &ffindexstore::Store::OpenPhaseTimings::hostFoldSignatureMs},
        {"signature_compare", &ffindexstore::Store::OpenPhaseTimings::signatureCompareMs},
        {"consistency_check", &ffindexstore::Store::OpenPhaseTimings::consistencyCheckMs},
        {"candidate_rebuild", &ffindexstore::Store::OpenPhaseTimings::candidateRebuildMs},
        {"candidate_map", &ffindexstore::Store::OpenPhaseTimings::candidateMapMs},
        {"candidate_terms", &ffindexstore::Store::OpenPhaseTimings::candidateTermsMs},
        {"candidate_exception", &ffindexstore::Store::OpenPhaseTimings::candidateExceptionMs},
        {"total", &ffindexstore::Store::OpenPhaseTimings::totalMs},
    };
    std::fprintf(out, "\"aggregate_ms\":{");
    for (size_t field = 0; field < sizeof(kTimingFields) / sizeof(kTimingFields[0]); ++field) {
        std::vector<double> values;
        for (const auto& run : runs) values.push_back(run.timings.*kTimingFields[field].member);
        std::fprintf(out, "%s\"%s\":{\"p50\":%.4f,\"p90\":%.4f,\"max\":%.4f,\"n\":%zu}",
                     field == 0 ? "" : ",", kTimingFields[field].name,
                     Percentile(values, 0.50), Percentile(values, 0.90),
                     *std::max_element(values.begin(), values.end()), values.size());
    }
    std::fprintf(out, "},");
    std::vector<double> cpuUser, cpuKernel, readBytes;
    for (const auto& run : runs) {
        cpuUser.push_back(run.cpuIoDelta.userMs);
        cpuKernel.push_back(run.cpuIoDelta.kernelMs);
        readBytes.push_back(static_cast<double>(run.cpuIoDelta.readBytes));
    }
    std::fprintf(out,
        "\"aggregate_cpu_ms\":{\"user_p50\":%.3f,\"user_max\":%.3f,"
        "\"kernel_p50\":%.3f,\"kernel_max\":%.3f},"
        "\"aggregate_io_read_bytes\":{\"p50\":%.0f,\"max\":%.0f}",
        Percentile(cpuUser, 0.50),
        *std::max_element(cpuUser.begin(), cpuUser.end()),
        Percentile(cpuKernel, 0.50),
        *std::max_element(cpuKernel.begin(), cpuKernel.end()),
        Percentile(readBytes, 0.50),
        *std::max_element(readBytes.begin(), readBytes.end()));
}

int RunParent(const std::wstring& dbPath, const std::string& testCase, size_t processCount,
              size_t warmOpens, const std::wstring& outputPath, const std::string& sourceSha,
              const std::string& sourceState, const std::string& preset,
              const std::string& exeSha256Expected) {
    wchar_t exePathBuffer[MAX_PATH];
    GetModuleFileNameW(nullptr, exePathBuffer, MAX_PATH);
    const std::wstring exePath(exePathBuffer);
    bool exeHashOk = false;
    const std::string exeSha256 = Sha256OfFile(exePath, &exeHashOk);
    WIN32_FILE_ATTRIBUTE_DATA exeData{};
    GetFileAttributesExW(exePath.c_str(), GetFileExInfoStandard, &exeData);

    std::vector<OpenRunRecord> allRuns;
    std::vector<SpawnResult> childResults;
    size_t crashChildren = 0;
    const bool isInterrupted = testCase == "migration-v2-interrupted";
    const bool isReadOnly = testCase == "read-only";
    if (isReadOnly) {
        // Access-denied failure mode: the file is made read-only so every
        // fresh open must fail closed (no delete, no rebuild, no write).
        SetFileAttributesW(dbPath.c_str(), GetFileAttributesW(dbPath.c_str()) | FILE_ATTRIBUTE_READONLY);
    }

    for (size_t index = 0; index < processCount; ++index) {
        if (isInterrupted) {
            // Every recovery sample is preceded by a REAL hard-exit crash
            // mid-migration so each measured open is the recovery open.
            SpawnResult crash = SpawnChild(exePath, dbPath, "migration-v2-interrupted", 0);
            ++crashChildren;
            childResults.push_back(crash);
        }
        SpawnResult child = SpawnChild(exePath, dbPath,
                                       isInterrupted ? "valid-reopen" : testCase, warmOpens);
        childResults.push_back(child);
        // Minimal JSON parsing for the run records (harness-controlled
        // output; each run object is located by brace balancing so nested
        // objects like timings_ms are not truncated).
        const std::string& text = child.stdoutText;
        size_t cursor = 0;
        while (true) {
            const size_t runStart = text.find("{\"repetition\":", cursor);
            if (runStart == std::string::npos) break;
            int depth = 0;
            size_t runEnd = std::string::npos;
            for (size_t at = runStart; at < text.size(); ++at) {
                if (text[at] == '{') {
                    ++depth;
                } else if (text[at] == '}') {
                    --depth;
                    if (depth == 0) {
                        runEnd = at;
                        break;
                    }
                }
            }
            if (runEnd == std::string::npos) break;
            const std::string runText = text.substr(runStart, runEnd - runStart + 1);
            cursor = runEnd + 1;
            OpenRunRecord run;
            auto extractDouble = [&](const char* key, double& target) {
                const size_t at = runText.find(key);
                if (at != std::string::npos) target = std::atof(runText.c_str() + at + std::strlen(key));
            };
            auto extractInt = [&](const char* key, long long& target) {
                const size_t at = runText.find(key);
                if (at != std::string::npos) target = std::atoll(runText.c_str() + at + std::strlen(key));
            };
            auto extractUint = [&](const char* key, unsigned long long& target) {
                const size_t at = runText.find(key);
                if (at != std::string::npos) {
                    target = std::strtoull(runText.c_str() + at + std::strlen(key), nullptr, 10);
                }
            };
            long long value = 0;
            extractInt("\"repetition\":", value);
            run.openIndex = static_cast<int>(value);
            const size_t timestampAt = runText.find("\"timestamp\":\"");
            if (timestampAt != std::string::npos) {
                const size_t timestampStart = timestampAt + std::strlen("\"timestamp\":\"");
                const size_t timestampEnd = runText.find('"', timestampStart);
                if (timestampEnd != std::string::npos) {
                    run.timestampUtc = runText.substr(timestampStart, timestampEnd - timestampStart);
                }
            }
            run.cacheState = runText.find("\"warm-same-process\"") != std::string::npos
                                 ? "warm-same-process" : "cold-new-process";
            run.openOk = runText.find("\"open_ok\":true") != std::string::npos;
            run.integrityFailed = runText.find("\"integrity_failed\":true") != std::string::npos;
            extractDouble("\"connection_open\":", run.timings.connectionOpenMs);
            extractDouble("\"integrity_check\":", run.timings.integrityCheckMs);
            extractDouble("\"pragma_config\":", run.timings.pragmaConfigMs);
            extractDouble("\"schema_init\":", run.timings.schemaInitMs);
            extractDouble("\"schema_version_read\":", run.timings.schemaVersionReadMs);
            extractDouble("\"host_fold_signature\":", run.timings.hostFoldSignatureMs);
            extractDouble("\"signature_compare\":", run.timings.signatureCompareMs);
            extractDouble("\"consistency_check\":", run.timings.consistencyCheckMs);
            extractDouble("\"candidate_rebuild\":", run.timings.candidateRebuildMs);
            extractDouble("\"candidate_map\":", run.timings.candidateMapMs);
            extractDouble("\"candidate_terms\":", run.timings.candidateTermsMs);
            extractDouble("\"candidate_exception\":", run.timings.candidateExceptionMs);
            extractDouble("\"total\":", run.timings.totalMs);
            unsigned long long unsignedValue = 0;
            extractUint("\"candidate_rows_processed\":", unsignedValue);
            run.timings.candidateRowsProcessed = static_cast<uint64_t>(unsignedValue);
            extractUint("\"host_fold_signature_value\":", unsignedValue);
            run.timings.hostFoldSignature = static_cast<uint64_t>(unsignedValue);
            extractInt("\"prior_schema_version\":", value);
            run.timings.priorSchemaVersion = static_cast<int>(value);
            run.timings.candidateIndexRebuilt =
                runText.find("\"candidate_index_rebuilt\":true") != std::string::npos;
            extractDouble("\"cpu_user_ms\":", run.cpuIoDelta.userMs);
            extractDouble("\"cpu_kernel_ms\":", run.cpuIoDelta.kernelMs);
            unsigned long long ioValue = 0;
            extractUint("\"io_read_bytes\":", ioValue);
            run.cpuIoDelta.readBytes = static_cast<uint64_t>(ioValue);
            extractUint("\"io_write_bytes\":", ioValue);
            run.cpuIoDelta.writeBytes = static_cast<uint64_t>(ioValue);
            extractUint("\"working_set_after\":", ioValue);
            run.workingSetAfter = static_cast<uint64_t>(ioValue);
            extractUint("\"private_after\":", ioValue);
            run.privateAfter = static_cast<uint64_t>(ioValue);
            extractUint("\"peak_working_set\":", ioValue);
            run.peakWorkingSet = static_cast<uint64_t>(ioValue);
            allRuns.push_back(run);
        }
    }

    std::FILE* out = nullptr;
    if (_wfopen_s(&out, outputPath.c_str(), L"wb") != 0 || out == nullptr) {
        std::fprintf(stderr, "cannot open output %s\n", ToUtf8(outputPath).c_str());
        return 1;
    }
    std::fprintf(out, "{\n  \"schema\":\"fastfiles-store-open-phases-v1\",\n");
    std::fprintf(out, "  \"generated_utc\":\"%s\",\n", NowUtc().c_str());
    std::fprintf(out, "  \"identity\":{"
                      "\"source_sha\":\"%s\",\"source_state\":\"%s\",\"source_state_asserted_by\":\"caller\","
                      "\"preset\":\"%s\",\"build_identity\":\"%s\","
                      "\"compiler_msvc\":\"%s\",\"build_configuration\":\"%s\","
                      "\"executable_path\":\"%s\",\"executable_sha256\":\"%s\","
                      "\"executable_sha256_computed\":\"runtime\",\"executable_bytes\":%llu,"
                      "\"executable_last_write_utc\":\"%s\"",
        JsonEscape(sourceSha).c_str(), JsonEscape(sourceState).c_str(),
        JsonEscape(preset).c_str(), JsonEscape(ffsetup::kBuildIdentityAscii).c_str(),
        JsonEscape([]() { char b[32]; std::snprintf(b, sizeof(b), "MSVC %d", _MSC_VER); return std::string(b); }()).c_str(),
#if defined(_DEBUG)
        "Debug",
#else
        "Release",
#endif
        JsonEscape(ToUtf8(exePath)).c_str(), JsonEscape(exeSha256).c_str(),
        static_cast<unsigned long long>((static_cast<uint64_t>(exeData.nFileSizeHigh) << 32) |
                                        exeData.nFileSizeLow),
        FileTimeUtc({exeData.ftLastWriteTime.dwLowDateTime, exeData.ftLastWriteTime.dwHighDateTime}).c_str());
    if (!exeSha256Expected.empty()) {
        std::fprintf(out, ",\"executable_sha256_expected\":\"%s\",\"executable_identity_matches\":%s",
                     JsonEscape(exeSha256Expected).c_str(),
                     (exeHashOk && exeSha256Expected == exeSha256) ? "true" : "false");
    }
    std::fprintf(out, "},\n");
    std::fprintf(out, "  \"fixture\":{\"db_path\":\"%s\",\"db_bytes\":%llu,"
                      "\"candidate_index_version\":3,\"projection_memory_budget_bytes\":524288000,"
                      "\"projection_memory_budget_note\":\"default 500 MiB unchanged\"},\n",
                 JsonEscape(ToUtf8(dbPath)).c_str(),
                 static_cast<unsigned long long>(FileSize(dbPath)));
    std::fprintf(out, "  \"case\":\"%s\",\n  \"fresh_processes\":%zu,\"warm_opens_per_process\":%zu,\n",
                 JsonEscape(testCase).c_str(), processCount, warmOpens);
    std::fprintf(out, "  \"crash_children\":%zu,\n", crashChildren);
    std::fprintf(out, "  \"children\":[");
    for (size_t index = 0; index < childResults.size(); ++index) {
        const auto& child = childResults[index];
        std::fprintf(out, "%s{\"spawned\":%s,\"exited\":%s,\"exit_code\":%llu,\"stdout_bytes\":%zu}",
                     index == 0 ? "" : ",",
                     child.spawned ? "true" : "false", child.exited ? "true" : "false",
                     static_cast<unsigned long long>(child.exitCode), child.stdoutText.size());
    }
    std::fprintf(out, "],\n  \"runs\":[");
    for (size_t index = 0; index < allRuns.size(); ++index) {
        std::fprintf(out, "%s\n    ", index == 0 ? "" : ",");
        WriteRun(out, allRuns[index]);
    }
    std::fprintf(out, "\n  ],\n  ");
    WriteAggregate(out, allRuns);
    std::fprintf(out, "\n}\n");
    std::fclose(out);
    if (isReadOnly) {
        SetFileAttributesW(dbPath.c_str(), GetFileAttributesW(dbPath.c_str()) & ~FILE_ATTRIBUTE_READONLY);
    }
    std::printf("wrote %s (%zu runs)\n", ToUtf8(outputPath).c_str(), allRuns.size());
    return allRuns.empty() ? 1 : 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring dbPath;
    std::wstring outputPath = L"store-open-phases.json";
    std::string testCase = "valid-reopen";
    std::string sourceSha = "UNKNOWN";
    std::string sourceState = "UNKNOWN";
    std::string preset = "UNKNOWN";
    std::string exeSha256Expected;
    size_t fixtureRows = 0;
    size_t fixtureVolumes = 1;
    uint64_t fixtureSeed = 1;
    size_t processCount = 5;
    size_t warmOpens = 0;
    bool fixtureMaintenanceOff = false;
    bool buildFixtureMode = false;
    bool measureMode = false;
    bool childMode = false;

    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        const auto next = [&]() -> std::wstring {
            return (index + 1 < argc) ? argv[++index] : std::wstring();
        };
        if (argument == L"--db") dbPath = next();
        else if (argument == L"--output") outputPath = next();
        else if (argument == L"--case") testCase = ToUtf8(next());
        else if (argument == L"--source-sha") sourceSha = ToUtf8(next());
        else if (argument == L"--source-state") sourceState = ToUtf8(next());
        else if (argument == L"--preset") preset = ToUtf8(next());
        else if (argument == L"--exe-sha256-expected") exeSha256Expected = ToUtf8(next());
        else if (argument == L"--build-fixture") { fixtureRows = static_cast<size_t>(_wtol(next().c_str())); buildFixtureMode = true; }
        else if (argument == L"--volumes") fixtureVolumes = static_cast<size_t>(_wtol(next().c_str()));
        else if (argument == L"--seed") fixtureSeed = static_cast<uint64_t>(_wtol(next().c_str()));
        else if (argument == L"--processes") processCount = static_cast<size_t>(_wtol(next().c_str()));
        else if (argument == L"--warm-opens") warmOpens = static_cast<size_t>(_wtol(next().c_str()));
        else if (argument == L"--measure") measureMode = true;
        else if (argument == L"--measure-child") childMode = true;
        else if (argument == L"--maintenance-off") fixtureMaintenanceOff = true;
        else {
            std::fprintf(stderr, "unknown argument\n");
            return 2;
        }
    }
    if (dbPath.empty() && !buildFixtureMode) {
        std::fprintf(stderr,
            "Usage: bench_store_open --build-fixture <N> --db <path> [--seed S] [--volumes K]\n"
            "       bench_store_open --measure --db <path> --case <case> --processes <K> --output <json>\n"
            "       bench_store_open --measure-child --db <path> --case <case> [--warm-opens M]\n");
        return 2;
    }
    if (buildFixtureMode) {
        return BuildFixture(dbPath, fixtureRows, fixtureVolumes, fixtureSeed, !fixtureMaintenanceOff) ? 0 : 1;
    }
    if (childMode) {
        return RunChild(dbPath, testCase, warmOpens);
    }
    if (measureMode) {
        return RunParent(dbPath, testCase, processCount, warmOpens, outputPath,
                         sourceSha, sourceState, preset, exeSha256Expected);
    }
    return 2;
}
