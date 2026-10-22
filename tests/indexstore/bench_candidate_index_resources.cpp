// durable-cold-search-index-and-hybrid-residency 4.2: candidate-index
// resource measurement harness (bench-only; never a CTest gate). Measures on
// the PRODUCTION Store schema:
//   - ingestion A/B (candidate maintenance on vs off): wall time, rows/sec
//   - WAL growth peak (sampled file size during ingestion)
//   - database size deltas (entries-only vs entries+candidate structures)
//   - v2 -> v3 migration elapsed time (drop-v3 + user_version=2 simulation)
//   - rebuild-on-open elapsed time (tampered fold-signature = host change)
//   - peak private bytes (GetProcessMemoryInfo)
// Output: machine-readable JSON with explicit provenance fields (the
// reviewer-D/F lesson: identity is recorded HERE because the main harness
// does not embed it).

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <psapi.h>

#include "ffindexstore/Store.h"
#include "sqlite3.h"

namespace {

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), needed, nullptr, nullptr);
    return result;
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

uint64_t FileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
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

struct WalWatcher {
    std::wstring walPath;
    uint64_t peak = 0;
    bool stop = false;
    std::thread thread;

    void Start(const std::wstring& dbPath) {
        walPath = dbPath + L"-wal";
        thread = std::thread([this]() {
            while (!stop) {
                const uint64_t size = FileSize(walPath);
                if (size > peak) peak = size;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }
    void Stop() {
        stop = true;
        if (thread.joinable()) thread.join();
    }
};

struct ArmResult {
    double ingestMs = 0.0;
    double rowsPerSec = 0.0;
    uint64_t walPeak = 0;
    uint64_t privatePeak = 0;
    uint64_t dbBytes = 0;
    uint64_t walBytes = 0;
};

ArmResult RunArm(const std::wstring& dbPath, size_t scale, bool maintenanceEnabled) {
    DeleteFileW(dbPath.c_str());
    DeleteFileW((dbPath + L"-wal").c_str());
    DeleteFileW((dbPath + L"-shm").c_str());

    ArmResult result;
    ffindexstore::SetTestingCandidateMaintenanceDisabled(!maintenanceEnabled);
    {
        ffindexstore::Store store;
        if (!store.Open(ToUtf8(dbPath))) return result;
        const auto volume = store.GetOrCreateVolume(ffindexstore::VolumeKey{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}, 777});
        WalWatcher watcher;
        watcher.Start(dbPath);
        const uint64_t privateBefore = PrivateBytes();
        uint64_t privatePeak = privateBefore;
        const auto start = std::chrono::steady_clock::now();
        constexpr size_t kBatch = 5000;
        for (size_t offset = 0; offset < scale; offset += kBatch) {
            std::vector<ffindexstore::EntryChange> batch;
            const size_t end = (offset + kBatch < scale) ? offset + kBatch : scale;
            for (size_t index = offset; index < end; ++index) {
                ffindexstore::EntryRecord record;
                record.id = {static_cast<uint64_t>(index + 1), 0};
                record.parentId = {5, 0};
                record.name = FileName(index);
                record.sizeBytes = 4096;
                record.allocatedSizeKnown = true;
                record.allocatedSizeBytes = 4096;
                batch.push_back({ffindexstore::EntryChangeKind::Upsert, record});
            }
            if (!store.ApplyBatch(*volume, batch)) {
                std::fprintf(stderr, "arm ingest failed at %zu\n", offset);
                break;
            }
            // Mirror the production pipeline WAL policy (IndexPipeline calls
            // CheckpointIfWalExceeds at batch boundaries); without it the WAL
            // peak reflects an uncheckpointed bench, not production.
            store.CheckpointIfWalExceeds(32ull * 1024 * 1024);
            const uint64_t now = PrivateBytes();
            if (now > privatePeak) privatePeak = now;
        }
        const auto stop = std::chrono::steady_clock::now();
        result.ingestMs = std::chrono::duration<double, std::milli>(stop - start).count();
        result.rowsPerSec = result.ingestMs > 0 ? static_cast<double>(scale) * 1000.0 / result.ingestMs : 0.0;
        watcher.Stop();
        result.walPeak = watcher.peak;
        result.privatePeak = privatePeak;
    }
    ffindexstore::SetTestingCandidateMaintenanceDisabled(false);
    result.dbBytes = FileSize(dbPath);
    result.walBytes = FileSize(dbPath + L"-wal");
    return result;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    size_t scale = 200000;
    std::wstring outputPath = L"candidate-resources.json";
    std::string sourceSha;
    std::string sourceState;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--scale" && index + 1 < argc) scale = static_cast<size_t>(_wtol(argv[++index]));
        else if (argument == L"--output" && index + 1 < argc) outputPath = argv[++index];
        else if (argument == L"--source-sha" && index + 1 < argc) sourceSha = ToUtf8(argv[++index]);
        else if (argument == L"--source-state" && index + 1 < argc) sourceState = ToUtf8(argv[++index]);
        else { std::fwprintf(stderr, L"Unknown argument: %s\n", argument.c_str()); return 2; }
    }

    const std::wstring onPath = outputPath + L".on.db";
    const std::wstring offPath = outputPath + L".off.db";
    std::printf("arm ON (candidate maintenance enabled)...\n");
    const ArmResult on = RunArm(onPath, scale, true);
    std::printf("arm OFF (entries only)...\n");
    const ArmResult off = RunArm(offPath, scale, false);

    // Migration timing: take the ON database, drop the v3 structures, and
    // time the open that rebuilds them (the v2->v3 migration shape).
    double migrationMs = 0.0;
    {
        sqlite3* raw = nullptr;
        if (sqlite3_open16(onPath.c_str(), &raw) == SQLITE_OK) {
            (void)sqlite3_exec(raw,
                "DROP TABLE IF EXISTS entry_name_exceptions;"
                "DROP TABLE IF EXISTS entry_name_index_map;"
                "DROP TABLE IF EXISTS entry_name_index_meta;"
                "DROP TABLE IF EXISTS entry_name_terms;"
                "PRAGMA user_version=2;", nullptr, nullptr, nullptr);
            sqlite3_close(raw);
        }
        ffindexstore::Store store;
        const auto start = std::chrono::steady_clock::now();
        if (store.Open(ToUtf8(onPath))) {
            migrationMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        }
    }

    // Rebuild-on-open timing: tamper the fold signature (host change) and
    // time the reopen that rebuilds before consultation.
    double rebuildOnOpenMs = 0.0;
    {
        sqlite3* raw = nullptr;
        if (sqlite3_open16(onPath.c_str(), &raw) == SQLITE_OK) {
            (void)sqlite3_exec(raw, "UPDATE entry_name_index_meta SET fold_signature=-1;",
                               nullptr, nullptr, nullptr);
            sqlite3_close(raw);
        }
        ffindexstore::Store store;
        const auto start = std::chrono::steady_clock::now();
        if (store.Open(ToUtf8(onPath))) {
            rebuildOnOpenMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        }
    }

    FILE* out = nullptr;
    _wfopen_s(&out, outputPath.c_str(), L"wb");
    if (out != nullptr) {
        std::fprintf(out,
            "{\"schema\":\"fastfiles-candidate-resources-v1\","
            "\"source_sha\":\"%s\",\"source_state\":\"%s\",\"scale\":%zu,"
            "\"arm_on\":{\"ingest_ms\":%.1f,\"rows_per_sec\":%.1f,\"wal_peak_bytes\":%llu,"
            "\"private_peak_bytes\":%llu,\"db_bytes\":%llu,\"wal_bytes\":%llu},"
            "\"arm_off\":{\"ingest_ms\":%.1f,\"rows_per_sec\":%.1f,\"wal_peak_bytes\":%llu,"
            "\"private_peak_bytes\":%llu,\"db_bytes\":%llu,\"wal_bytes\":%llu},"
            "\"index_overhead\":{\"ingest_delta_ms\":%.1f,\"db_delta_bytes\":%lld,"
            "\"wal_peak_delta_bytes\":%lld,\"bytes_per_1m_rows\":%.1f},"
            "\"migration_ms\":%.1f,\"rebuild_on_open_ms\":%.1f}\n",
            sourceSha.c_str(), sourceState.c_str(), scale,
            on.ingestMs, on.rowsPerSec, (unsigned long long)on.walPeak,
            (unsigned long long)on.privatePeak, (unsigned long long)on.dbBytes, (unsigned long long)on.walBytes,
            off.ingestMs, off.rowsPerSec, (unsigned long long)off.walPeak,
            (unsigned long long)off.privatePeak, (unsigned long long)off.dbBytes, (unsigned long long)off.walBytes,
            on.ingestMs - off.ingestMs,
            (long long)((long long)on.dbBytes - (long long)off.dbBytes),
            (long long)((long long)on.walPeak - (long long)off.walPeak),
            static_cast<double>(on.dbBytes - off.dbBytes) * 1000000.0 / static_cast<double>(scale),
            migrationMs, rebuildOnOpenMs);
        std::fclose(out);
    }
    std::printf("wrote %ls\n", outputPath.c_str());
    return 0;
}
