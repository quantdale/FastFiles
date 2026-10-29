// large-index-lifecycle-and-search-scalability 4.2: differential isolation
// matrix for the repeated-rebuild removal pathology (Phase A).
//
// Informational benchmark only -- never registered with CTest (wall-clock
// measurement, not a gate). The matrix attributes the replacement-rebuild
// cost difference (fresh build ~4 s vs rebuild ~160 s at 1M rows) to its
// constituent operations:
//
//   A   fresh Projection + build                    (first-build baseline)
//   B1  existing Projection + Remove all            (removal half of a replace)
//   B2  rebuild after B1                            (stream + index-rebuild half)
//   C   existing Projection + logical Clear + build (object reset + build)
//   D   build into new Projection + swap            (double-buffer candidate)
//   E1  B1 with filename-accelerator maintenance skipped
//   E2  rebuild once after E1                       (accelerator rebuilt once)
//   F1  B1 with parent/child unlink skipped
//   G0  diagnostic fixture population (<=2-char names: no trigrams)
//   G1  G removal (production semantics)
//   G2  G removal with child unlink skipped         (residual ceiling)
//
// FF_INDEXSTORE_TESTING compiles the benchmark-only
// Projection::SetRemovalMaintenanceMode seam into this target's Projection.cpp
// only; production builds contain no seam and behave exactly as
// RemovalMaintenanceMode::Production.
//
// Fixture shape mirrors tests/engine/bench_large_index_scalability.cpp
// (root + 1/64 dirs + the FileName/DirectoryName name distribution with its
// shared-name trigram structure) so results are comparable across harnesses.

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "ffindexstore/EntryRecord.h"
#include "ffindexstore/Identity.h"
#include "ffindexstore/Projection.h"
#include "ffindexstore/Store.h"
#include "ffsetup/BuildIdentity.h"

namespace {

using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct MemorySample {
    uint64_t workingSetBytes = 0;
    uint64_t privateBytes = 0;
};

MemorySample ReadMemory() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters))) {
        return {};
    }
    return {static_cast<uint64_t>(counters.WorkingSetSize), static_cast<uint64_t>(counters.PrivateUsage)};
}

std::string ToUtf8(const wchar_t* value) {
    if (value == nullptr) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string result(static_cast<size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), length, nullptr, nullptr);
    return result;
}

std::string ToUtf8(const std::wstring& value) { return ToUtf8(value.c_str()); }

std::u16string ToUtf16(const std::wstring& value) {
    std::u16string result;
    result.reserve(value.size());
    for (const wchar_t ch : value) result.push_back(static_cast<char16_t>(ch));
    return result;
}

std::string JsonEscape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    result += "?";
                } else {
                    result.push_back(ch);
                }
                break;
        }
    }
    return result;
}

struct Options {
    std::vector<size_t> scales{200'000, 1'000'000};
    std::string arms = "A,B1,B2,C,D,E1,E2,F1,G0,G1,G2";
    std::filesystem::path outputPath = std::filesystem::path(L"rebuild-isolation.json");
    std::string commandLine;
    std::string sourceState;
    std::string sourceSha;
    std::string preset;
};

std::vector<size_t> ParseScales(const std::wstring& text) {
    std::vector<size_t> result;
    size_t start = 0;
    while (start < text.size()) {
        const size_t comma = text.find(L',', start);
        const std::wstring token = text.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
        if (token.empty()) return {};
        wchar_t* end = nullptr;
        const unsigned long long parsed = std::wcstoull(token.c_str(), &end, 10);
        if (end == token.c_str() || *end != L'\0' || parsed == 0) return {};
        result.push_back(static_cast<size_t>(parsed));
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return result;
}

bool ParseOptions(int argc, wchar_t** argv, Options& options) {
    for (int index = 0; index < argc; ++index) {
        if (!options.commandLine.empty()) options.commandLine += ' ';
        options.commandLine += ToUtf8(argv[index]);
    }
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--help") {
            std::wprintf(L"Usage: %s [--scales 200000,1000000] [--arms A,B1,B2,C,D,E1,E2,F1,G0,G1,G2]\n"
                         L"       [--output path] [--source-state clean] [--source-sha sha] [--preset name]\n",
                         argv[0]);
            return false;
        }
        if (argument == L"--scales" && index + 1 < argc) {
            options.scales = ParseScales(argv[++index]);
            if (options.scales.empty()) {
                std::fwprintf(stderr, L"Invalid --scales value\n");
                return false;
            }
            continue;
        }
        if (argument == L"--arms" && index + 1 < argc) {
            options.arms = ToUtf8(argv[++index]);
            continue;
        }
        if (argument == L"--output" && index + 1 < argc) {
            options.outputPath = argv[++index];
            continue;
        }
        if (argument == L"--source-state" && index + 1 < argc) {
            options.sourceState = ToUtf8(argv[++index]);
            continue;
        }
        if (argument == L"--source-sha" && index + 1 < argc) {
            options.sourceSha = ToUtf8(argv[++index]);
            continue;
        }
        if (argument == L"--preset" && index + 1 < argc) {
            options.preset = ToUtf8(argv[++index]);
            continue;
        }
        std::fwprintf(stderr, L"Unknown or incomplete argument: %s\n", argument.c_str());
        return false;
    }
    return true;
}

bool ArmSelected(const Options& options, const std::string& arm) {
    size_t start = 0;
    const std::string& list = options.arms;
    while (start <= list.size()) {
        const size_t comma = list.find(',', start);
        const std::string token = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (token == arm) return true;
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return false;
}

std::filesystem::path MakeTemporaryDirectory(size_t scale) {
    const std::filesystem::path base = std::filesystem::temp_directory_path();
    const std::wstring stem = L"FastFiles-rebuild-isolation-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(scale);
    for (unsigned int suffix = 0; suffix < 1000; ++suffix) {
        const std::filesystem::path candidate = base / (stem + L"-" + std::to_wstring(suffix));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) return candidate;
    }
    return {};
}

std::wstring DirectoryName(size_t index) {
    return L"folder_" + std::to_wstring(index % 997) + L"_branch_" + std::to_wstring(index);
}

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

ffindexstore::EntryRecord MakeEntry(uint64_t id, uint64_t parent, const std::wstring& name,
                                    uint32_t attributes, uint64_t sizeBytes, uint64_t timestamp) {
    ffindexstore::EntryRecord record;
    record.id = ffindexstore::FileId{id, 0};
    record.parentId = ffindexstore::FileId{parent, 0};
    record.name = ToUtf16(name);
    record.sizeBytes = sizeBytes;
    record.allocatedSizeBytes = sizeBytes;
    record.allocatedSizeKnown = true;
    record.creationTime = timestamp;
    record.lastModifiedTime = timestamp + 100;
    record.lastAccessTime = timestamp + 200;
    record.attributes = attributes;
    return record;
}

struct ArmResult {
    std::string name;
    std::string notes;
    size_t rows = 0;
    double elapsedMs = 0.0;
    double rowsPerSecond = 0.0;
    MemorySample memoryBefore;
    MemorySample memoryAfter;
    // large-index-lifecycle-and-search-scalability 4.3: self-explaining arms.
    // A removal arm measured against a silently invalidated accelerator would
    // otherwise look 'fast' for the wrong reason; every arm records the
    // projection's accelerator/eviction state and the rebuild verdict.
    bool filenameIndexValidBefore = false;
    bool filenameIndexValidAfter = false;
    bool rebuildSucceeded = true;
    uint64_t projectionEstimatedBytesAfter = 0;
    size_t evictedVolumeCountAfter = 0;
};

struct ScaleResult {
    size_t scale = 0;
    size_t directoryCount = 0;
    size_t fileCount = 0;
    uint64_t volumeRowIdDisplay = 0;
    ffindexstore::VolumeRowId volumeRowId = 0;
    double fixtureGenerationMs = 0.0;
    bool executed = false;
    std::string failureReason;
    std::vector<ArmResult> arms;
};

ArmResult MakeArm(const std::string& name, const std::string& notes, size_t rows,
                  double elapsedMs, MemorySample before, MemorySample after) {
    ArmResult arm;
    arm.name = name;
    arm.notes = notes;
    arm.rows = rows;
    arm.elapsedMs = elapsedMs;
    arm.rowsPerSecond = elapsedMs > 0.0 ? static_cast<double>(rows) / (elapsedMs / 1000.0) : 0.0;
    arm.memoryBefore = before;
    arm.memoryAfter = after;
    return arm;
}

void CaptureProjectionState(const ffindexstore::Projection& projection, ArmResult& arm) {
    const ffindexstore::Projection::MemoryBreakdown breakdown = projection.GetMemoryBreakdown();
    arm.filenameIndexValidAfter = breakdown.filenameIndexValid;
    arm.projectionEstimatedBytesAfter = breakdown.modeledCategoryBytes;
    arm.evictedVolumeCountAfter = breakdown.evictedVolumeCount;
}

bool BuildFixtureStore(ffindexstore::Store& store, size_t scale, ffindexstore::VolumeRowId& volumeId,
                       double& generationMs) {
    ffindexstore::VolumeKey key;
    key.serialNumber = static_cast<uint32_t>(scale ^ 0x15E15E15u);
    const auto created = store.GetOrCreateVolume(key);
    if (!created.has_value()) return false;
    volumeId = *created;

    const size_t directoryCount = (std::max)(size_t{1}, scale / 64);
    const auto generationStart = Clock::now();
    std::vector<ffindexstore::EntryChange> batch;
    batch.reserve(2000);
    auto flush = [&]() -> bool {
        if (batch.empty()) return true;
        const bool applied = store.ApplyBatch(volumeId, batch);
        batch.clear();
        return applied;
    };

    batch.push_back({ffindexstore::EntryChangeKind::Upsert, MakeEntry(5, 5, L"", 0x10, 0, 1)});
    for (size_t index = 0; index < directoryCount && 1 + index < scale; ++index) {
        const uint64_t id = 100 + static_cast<uint64_t>(index);
        const uint64_t parent = index < 16 ? 5 : 100 + static_cast<uint64_t>((index - 1) / 16);
        batch.push_back({ffindexstore::EntryChangeKind::Upsert,
                         MakeEntry(id, parent, DirectoryName(index), 0x10, 0, 10 + index)});
        if (batch.size() == 2000 && !flush()) return false;
    }
    const size_t filesToCreate = scale > 1 + directoryCount ? scale - 1 - directoryCount : 0;
    const uint64_t fileIdBase = 100 + static_cast<uint64_t>(directoryCount);
    for (size_t index = 0; index < filesToCreate; ++index) {
        const uint64_t parent = 100 + static_cast<uint64_t>(index % directoryCount);
        const uint64_t id = fileIdBase + static_cast<uint64_t>(index);
        const uint64_t sizeBytes = 1024 + static_cast<uint64_t>((index * 7919) % (64 * 1024 * 1024));
        batch.push_back({ffindexstore::EntryChangeKind::Upsert,
                         MakeEntry(id, parent, FileName(index), 0, sizeBytes, 1000 + index)});
        if (batch.size() == 2000 && !flush()) return false;
    }
    if (!flush()) return false;
    generationMs = ElapsedMs(generationStart);
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!ParseOptions(argc, argv, options)) return 2;

    std::ofstream output(options.outputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::fwprintf(stderr, L"Cannot open output path: %s\n", options.outputPath.c_str());
        return 1;
    }

    std::vector<ScaleResult> scales;
    for (const size_t scale : options.scales) {
        ScaleResult result;
        result.scale = scale;
        result.directoryCount = (std::max)(size_t{1}, scale / 64);
        result.fileCount = scale > 1 + result.directoryCount ? scale - 1 - result.directoryCount : 0;
        std::printf("== scale %zu ==\n", scale);
        std::fflush(stdout);

        const std::filesystem::path temporaryDirectory = MakeTemporaryDirectory(scale);
        if (temporaryDirectory.empty()) {
            result.failureReason = "temporary directory creation failed";
            scales.push_back(result);
            continue;
        }
        const std::filesystem::path database = temporaryDirectory / L"isolation.db";

        ffindexstore::Store store;
        if (!store.Open(ToUtf8(database.wstring()))) {
            result.failureReason = "store open failed";
            std::error_code ignored;
            std::filesystem::remove_all(temporaryDirectory, ignored);
            scales.push_back(result);
            continue;
        }
        if (!BuildFixtureStore(store, scale, result.volumeRowId, result.fixtureGenerationMs)) {
            result.failureReason = "fixture generation failed";
            std::error_code ignored;
            std::filesystem::remove_all(temporaryDirectory, ignored);
            scales.push_back(result);
            continue;
        }
        const auto metadata = store.GetVolumeMetadata(result.volumeRowId);
        const size_t rowCount = metadata.has_value() ? static_cast<size_t>(metadata->entryCount) : scale;
        std::printf("fixture rows=%zu in %.2f s\n", rowCount, result.fixtureGenerationMs / 1000.0);
        std::fflush(stdout);

        result.executed = true;
        ffindexstore::VolumeRowId volumeId = result.volumeRowId;

        // ---- A: fresh Projection + build -----------------------------------
        ffindexstore::Projection projectionA;
        if (ArmSelected(options, "A")) {
            const MemorySample before = ReadMemory();
            const auto start = Clock::now();
            const bool indexValidBeforeA = projectionA.GetMemoryBreakdown().filenameIndexValid;
            const bool okA = projectionA.RebuildVolumeFromStore(store, volumeId, rowCount);
            if (!okA) {
                result.failureReason = "arm A rebuild failed";
                result.executed = false;
            }
            result.arms.push_back(MakeArm("A_fresh_projection_build",
                "fresh Projection, RebuildVolumeFromStore over empty state",
                rowCount, ElapsedMs(start), before, ReadMemory()));
            result.arms.back().filenameIndexValidBefore = indexValidBeforeA;
            result.arms.back().rebuildSucceeded = okA;
            CaptureProjectionState(projectionA, result.arms.back());
            std::printf("A  fresh build: %.1f ms\n", result.arms.back().elapsedMs);
            std::fflush(stdout);
        }

        // ---- B1 + B2: existing Projection + Remove all + build -------------
        if (ArmSelected(options, "B1")) {
            const MemorySample before = ReadMemory();
            const bool indexValidBeforeB1 = projectionA.GetMemoryBreakdown().filenameIndexValid;
            const auto start = Clock::now();
            projectionA.RemoveVolume(volumeId);
            result.arms.push_back(MakeArm("B1_remove_all_production",
                "RemoveVolume on populated projection (per-entry Remove, production semantics)",
                rowCount, ElapsedMs(start), before, ReadMemory()));
            result.arms.back().filenameIndexValidBefore = indexValidBeforeB1;
            CaptureProjectionState(projectionA, result.arms.back());
            std::printf("B1 remove all: %.1f ms\n", result.arms.back().elapsedMs);
            std::fflush(stdout);
        }
        if (ArmSelected(options, "B2")) {
            const MemorySample before = ReadMemory();
            const auto start = Clock::now();
            const bool indexValidBeforeB2 = projectionA.GetMemoryBreakdown().filenameIndexValid;
            const bool okB2 = projectionA.RebuildVolumeFromStore(store, volumeId, rowCount);
            if (!okB2) {
                result.failureReason = "arm B2 rebuild failed";
                result.executed = false;
            }
            result.arms.push_back(MakeArm("B2_rebuild_after_removal",
                "RebuildVolumeFromStore after B1 (stream + iteration order + filename index + shrink)",
                rowCount, ElapsedMs(start), before, ReadMemory()));
            result.arms.back().filenameIndexValidBefore = indexValidBeforeB2;
            result.arms.back().rebuildSucceeded = okB2;
            CaptureProjectionState(projectionA, result.arms.back());
            std::printf("B2 rebuild after removal: %.1f ms\n", result.arms.back().elapsedMs);
            std::fflush(stdout);
        }

        // ---- C: logical Clear + build --------------------------------------
        if (ArmSelected(options, "C")) {
            const MemorySample before = ReadMemory();
            const auto start = Clock::now();
            projectionA = ffindexstore::Projection{};
            const bool okC = projectionA.RebuildVolumeFromStore(store, volumeId, rowCount);
            if (!okC) {
                result.failureReason = "arm C rebuild failed";
                result.executed = false;
            }
            result.arms.push_back(MakeArm("C_logical_clear_then_build",
                "object-level logical clear (fresh Projection assigned) then rebuild",
                rowCount, ElapsedMs(start), before, ReadMemory()));
            result.arms.back().rebuildSucceeded = okC;
            CaptureProjectionState(projectionA, result.arms.back());
            std::printf("C  clear + build: %.1f ms\n", result.arms.back().elapsedMs);
            std::fflush(stdout);
        }

        // ---- D: build into new Projection + swap ---------------------------
        if (ArmSelected(options, "D")) {
            const MemorySample before = ReadMemory();
            const auto start = Clock::now();
            ffindexstore::Projection fresh;
            bool ok = fresh.RebuildVolumeFromStore(store, volumeId, rowCount);
            // Peak double-buffer window: old + fresh both alive here.
            const MemorySample peak = ReadMemory();
            projectionA = std::move(fresh);
            const double elapsed = ElapsedMs(start);
            if (!ok) {
                result.failureReason = "arm D rebuild failed";
                result.executed = false;
            }
            ArmResult arm = MakeArm("D_build_into_new_then_swap",
                "fresh build beside populated projection, then move-assign swap (double-buffer window sampled)",
                rowCount, elapsed, before, peak);
            arm.rebuildSucceeded = ok;
            CaptureProjectionState(projectionA, arm);
            result.arms.push_back(arm);
            std::printf("D  build + swap: %.1f ms (double-buffer private %.1f MB)\n", elapsed,
                        static_cast<double>(peak.privateBytes) / (1024.0 * 1024.0));
            std::fflush(stdout);
        }

        // ---- E1 + E2: removal without filename-accelerator maintenance -----
        if (ArmSelected(options, "E1") || ArmSelected(options, "E2")) {
            ffindexstore::Projection projectionE;
            const bool okEPop = projectionE.RebuildVolumeFromStore(store, volumeId, rowCount);
            if (!okEPop) {
                result.failureReason = "arm E population failed";
                result.executed = false;
            }
            if (ArmSelected(options, "E1")) {
                projectionE.SetRemovalMaintenanceMode(
                    ffindexstore::Projection::RemovalMaintenanceMode::SkipFilenameMaintenance);
                const MemorySample before = ReadMemory();
                const bool indexValidBeforeE1 = projectionE.GetMemoryBreakdown().filenameIndexValid;
                const auto start = Clock::now();
                projectionE.RemoveVolume(volumeId);
                const double elapsed = ElapsedMs(start);
                projectionE.SetRemovalMaintenanceMode(
                    ffindexstore::Projection::RemovalMaintenanceMode::Production);
                result.arms.push_back(MakeArm("E1_remove_skip_filename_maintenance",
                    "RemoveVolume with per-entry trigram/non-ASCII accelerator removal skipped",
                    rowCount, elapsed, before, ReadMemory()));
                result.arms.back().filenameIndexValidBefore = indexValidBeforeE1;
                CaptureProjectionState(projectionE, result.arms.back());
                std::printf("E1 remove (skip filename maintenance): %.1f ms\n", elapsed);
                std::fflush(stdout);
            }
            if (ArmSelected(options, "E2")) {
                if (result.arms.empty() || result.arms.back().name != "E1_remove_skip_filename_maintenance") {
                    projectionE.RemoveVolume(volumeId);
                }
                const MemorySample before = ReadMemory();
                const auto start = Clock::now();
                const bool okE2 = projectionE.RebuildVolumeFromStore(store, volumeId, rowCount);
                if (!okE2) {
                    result.failureReason = "arm E2 rebuild failed";
                    result.executed = false;
                }
                result.arms.push_back(MakeArm("E2_rebuild_accelerator_once",
                    "rebuild after E1: filename accelerator rebuilt once in bulk at the end",
                    rowCount, ElapsedMs(start), before, ReadMemory()));
                result.arms.back().rebuildSucceeded = okE2;
                CaptureProjectionState(projectionE, result.arms.back());
                std::printf("E2 rebuild once: %.1f ms\n", result.arms.back().elapsedMs);
                std::fflush(stdout);
            }
        }

        // ---- F1: removal without parent/child unlink -----------------------
        if (ArmSelected(options, "F1")) {
            ffindexstore::Projection projectionF;
            const bool okFPop = projectionF.RebuildVolumeFromStore(store, volumeId, rowCount);
            if (!okFPop) {
                result.failureReason = "arm F population failed";
                result.executed = false;
            }
            projectionF.SetRemovalMaintenanceMode(
                ffindexstore::Projection::RemovalMaintenanceMode::SkipChildUnlink);
            const MemorySample before = ReadMemory();
            const bool indexValidBeforeF1 = projectionF.GetMemoryBreakdown().filenameIndexValid;
            const auto start = Clock::now();
            projectionF.RemoveVolume(volumeId);
            const double elapsed = ElapsedMs(start);
            projectionF.SetRemovalMaintenanceMode(
                ffindexstore::Projection::RemovalMaintenanceMode::Production);
            result.arms.push_back(MakeArm("F1_remove_skip_child_unlink",
                "RemoveVolume with per-entry parent/child unlink skipped (trigram removal retained)",
                rowCount, elapsed, before, ReadMemory()));
            result.arms.back().filenameIndexValidBefore = indexValidBeforeF1;
            CaptureProjectionState(projectionF, result.arms.back());
            std::printf("F1 remove (skip child unlink): %.1f ms\n", elapsed);
            std::fflush(stdout);
        }

        // ---- G0/G1/G2: no-trigram diagnostic ceiling -----------------------
        if (ArmSelected(options, "G0") || ArmSelected(options, "G1") || ArmSelected(options, "G2")) {
            ffindexstore::Projection projectionG;
            const MemorySample gBefore = ReadMemory();
            const auto gStart = Clock::now();
            for (size_t index = 0; index < result.directoryCount && 1 + index < scale; ++index) {
                const uint64_t id = 100 + static_cast<uint64_t>(index);
                const uint64_t parent = index < 16 ? 5 : 100 + static_cast<uint64_t>((index - 1) / 16);
                projectionG.Upsert(volumeId, MakeEntry(id, parent, L"d", 0x10, 0, 10 + index));
            }
            for (size_t index = 0; index < result.fileCount; ++index) {
                const uint64_t parent = 100 + static_cast<uint64_t>(index % result.directoryCount);
                const uint64_t id = 100 + static_cast<uint64_t>(result.directoryCount) + static_cast<uint64_t>(index);
                projectionG.Upsert(volumeId, MakeEntry(id, parent, L"x", 0, 1024, 1000 + index));
            }
            projectionG.Upsert(volumeId, MakeEntry(5, 5, L"", 0x10, 0, 1));
            const double gPopulate = ElapsedMs(gStart);
            if (ArmSelected(options, "G0")) {
                result.arms.push_back(MakeArm("G0_no_trigram_population",
                    "Upsert population with <=2-char names (no trigram postings, single NameId per kind)",
                    rowCount, gPopulate, gBefore, ReadMemory()));
                CaptureProjectionState(projectionG, result.arms.back());
                std::printf("G0 no-trigram population: %.1f ms\n", gPopulate);
                std::fflush(stdout);
            }
            if (ArmSelected(options, "G1")) {
                const MemorySample before = ReadMemory();
                const bool indexValidBeforeG1 = projectionG.GetMemoryBreakdown().filenameIndexValid;
                const auto start = Clock::now();
                projectionG.RemoveVolume(volumeId);
                result.arms.push_back(MakeArm("G1_no_trigram_remove",
                    "RemoveVolume on the no-trigram fixture (production semantics)",
                    rowCount, ElapsedMs(start), before, ReadMemory()));
                result.arms.back().filenameIndexValidBefore = indexValidBeforeG1;
                CaptureProjectionState(projectionG, result.arms.back());
                std::printf("G1 no-trigram remove: %.1f ms\n", result.arms.back().elapsedMs);
                std::fflush(stdout);
            } else if (ArmSelected(options, "G2")) {
                projectionG.RemoveVolume(volumeId);
            }
            if (ArmSelected(options, "G2")) {
                ffindexstore::Projection projectionG2;
                for (size_t index = 0; index < result.directoryCount && 1 + index < scale; ++index) {
                    const uint64_t id = 100 + static_cast<uint64_t>(index);
                    const uint64_t parent = index < 16 ? 5 : 100 + static_cast<uint64_t>((index - 1) / 16);
                    projectionG2.Upsert(volumeId, MakeEntry(id, parent, L"d", 0x10, 0, 10 + index));
                }
                for (size_t index = 0; index < result.fileCount; ++index) {
                    const uint64_t parent = 100 + static_cast<uint64_t>(index % result.directoryCount);
                    const uint64_t id = 100 + static_cast<uint64_t>(result.directoryCount) + static_cast<uint64_t>(index);
                    projectionG2.Upsert(volumeId, MakeEntry(id, parent, L"x", 0, 1024, 1000 + index));
                }
                projectionG2.Upsert(volumeId, MakeEntry(5, 5, L"", 0x10, 0, 1));
                projectionG2.SetRemovalMaintenanceMode(
                    ffindexstore::Projection::RemovalMaintenanceMode::SkipChildUnlink);
                const MemorySample before = ReadMemory();
                const auto start = Clock::now();
                projectionG2.RemoveVolume(volumeId);
                const double elapsed = ElapsedMs(start);
                projectionG2.SetRemovalMaintenanceMode(
                    ffindexstore::Projection::RemovalMaintenanceMode::Production);
                result.arms.push_back(MakeArm("G2_no_trigram_remove_skip_child_unlink",
                    "RemoveVolume on the no-trigram fixture with child unlink skipped (residual ceiling)",
                    rowCount, elapsed, before, ReadMemory()));
                CaptureProjectionState(projectionG2, result.arms.back());
                std::printf("G2 no-trigram remove (skip child unlink): %.1f ms\n", elapsed);
                std::fflush(stdout);
            }
        }

        store.Close();
        std::error_code ignored;
        std::filesystem::remove_all(temporaryDirectory, ignored);
        scales.push_back(std::move(result));
    }

    // ---- JSON report ------------------------------------------------------
    output << "{\n";
    output << "  \"schema\": \"large-index-rebuild-isolation-1\",\n";
    output << "  \"build_identity\": \"" << JsonEscape(ffsetup::kBuildIdentityAscii) << "\",\n";
    output << "  \"command_line\": \"" << JsonEscape(options.commandLine) << "\",\n";
    output << "  \"source_state\": \"" << JsonEscape(options.sourceState) << "\",\n";
    output << "  \"source_sha\": \"" << JsonEscape(options.sourceSha) << "\",\n";
    output << "  \"preset\": \"" << JsonEscape(options.preset) << "\",\n";
    output << "  \"units_are_binary\": true,\n";
    output << "  \"arms_requested\": \"" << JsonEscape(options.arms) << "\",\n";
    output << "  \"scales\": [";
    for (size_t s = 0; s < scales.size(); ++s) {
        const ScaleResult& scale = scales[s];
        if (s != 0) output << ",";
        output << "\n    {\n";
        output << "      \"scale\": " << scale.scale << ",\n";
        output << "      \"directory_count\": " << scale.directoryCount << ",\n";
        output << "      \"file_count\": " << scale.fileCount << ",\n";
        output << "      \"volume_row_id\": " << scale.volumeRowId << ",\n";
        output << "      \"fixture_generation_ms\": " << scale.fixtureGenerationMs << ",\n";
        output << "      \"executed\": " << (scale.executed ? "true" : "false") << ",\n";
        output << "      \"failure_reason\": \"" << JsonEscape(scale.failureReason) << "\",\n";
        output << "      \"arms\": [";
        for (size_t a = 0; a < scale.arms.size(); ++a) {
            const ArmResult& arm = scale.arms[a];
            if (a != 0) output << ",";
            output << "\n        {\n";
            output << "          \"name\": \"" << JsonEscape(arm.name) << "\",\n";
            output << "          \"notes\": \"" << JsonEscape(arm.notes) << "\",\n";
            output << "          \"rows\": " << arm.rows << ",\n";
            output << "          \"elapsed_ms\": " << arm.elapsedMs << ",\n";
            output << "          \"rows_per_second\": " << arm.rowsPerSecond << ",\n";
            output << "          \"filename_index_valid_before\": "
                   << (arm.filenameIndexValidBefore ? "true" : "false") << ",\n";
            output << "          \"filename_index_valid_after\": "
                   << (arm.filenameIndexValidAfter ? "true" : "false") << ",\n";
            output << "          \"rebuild_succeeded\": "
                   << (arm.rebuildSucceeded ? "true" : "false") << ",\n";
            output << "          \"projection_estimated_bytes_after\": "
                   << arm.projectionEstimatedBytesAfter << ",\n";
            output << "          \"evicted_volume_count_after\": "
                   << arm.evictedVolumeCountAfter << ",\n";
            output << "          \"working_set_before_bytes\": " << arm.memoryBefore.workingSetBytes << ",\n";
            output << "          \"private_before_bytes\": " << arm.memoryBefore.privateBytes << ",\n";
            output << "          \"working_set_after_bytes\": " << arm.memoryAfter.workingSetBytes << ",\n";
            output << "          \"private_after_bytes\": " << arm.memoryAfter.privateBytes << "\n";
            output << "        }";
        }
        output << "\n      ]\n";
        output << "    }";
    }
    output << "\n  ],\n";
    output << "  \"limitations\": \"Wall-clock single-run arms on one machine; comparable within one report "
              "more strongly than across machines. Trigram/child operation counts are derived analytically "
              "in the evidence document, not counted in-process. FF_INDEXSTORE_TESTING seam is compiled "
              "only into this benchmark target; production builds contain no seam.\"\n";
    output << "}\n";
    output.close();

    std::printf("Wrote %s\n", ToUtf8(options.outputPath.wstring()).c_str());
    return 0;
}
