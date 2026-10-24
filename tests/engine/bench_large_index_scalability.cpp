// Informational benchmark only. This executable is intentionally not
// registered with CTest: large synthetic indexes are repeatable evidence, not
// a wall-clock pass/fail gate.

#include <windows.h>
#include <psapi.h>
#include <intrin.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "IndexPipeline.h"
#include "SnapshotPublisher.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"
// performance-closure-and-multisurface-acceleration 8.1: benchmark-scoped
// allocator probe. The replacement global operators are defined in this
// translation unit only (FFBENCH_ALLOCATOR_PROBE_DEFINE).
#ifndef FFBENCH_NO_ALLOCATOR_PROBE
#define FFBENCH_ALLOCATOR_PROBE_DEFINE
#endif
#include "bench_allocator_probe.h"
#include "ffsetup/BuildIdentity.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t kBatchSize = 8192;
constexpr size_t kWarmRunsNormal = 10;
constexpr size_t kWarmRunsLarge = 5;
constexpr size_t kLargeSearchThreshold = 500'000;
constexpr size_t kCapacitySearchUpperBound = 3'000'000;

struct MemorySample {
    uint64_t workingSetBytes = 0;
    uint64_t privateBytes = 0;
};

struct ResourceSample {
    MemorySample memory;
    uint32_t handleCount = 0;
    uint32_t threadCount = 0;
    uint64_t walBytes = 0;
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

ResourceSample ReadResources(const std::filesystem::path& database = {}) {
    ResourceSample result;
    result.memory = ReadMemory();
    DWORD handles = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handles)) {
        result.handleCount = handles;
    }
    const DWORD processId = GetCurrentProcessId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 thread{};
        thread.dwSize = sizeof(thread);
        if (Thread32First(snapshot, &thread)) {
            do {
                if (thread.th32OwnerProcessID == processId) ++result.threadCount;
            } while (Thread32Next(snapshot, &thread));
        }
        CloseHandle(snapshot);
    }
    if (!database.empty()) {
        std::error_code error;
        const auto wal = std::filesystem::path(database.wstring() + L"-wal");
        if (std::filesystem::exists(wal, error)) {
            result.walBytes = std::filesystem::file_size(wal, error);
            if (error) result.walBytes = 0;
        }
    }
    return result;
}

double ElapsedMs(const Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int byteCount = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (byteCount <= 0) return {};
    std::string result(static_cast<size_t>(byteCount), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), byteCount, nullptr, nullptr);
    return result;
}

std::u16string ToUtf16(const std::wstring& value) {
    std::u16string result;
    result.reserve(value.size());
    for (wchar_t ch : value) result.push_back(static_cast<char16_t>(ch));
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

void WriteJsonString(std::ostream& output, std::string_view value) {
    output << '"' << JsonEscape(value) << '"';
}

void WriteJsonDouble(std::ostream& output, double value) {
    output << std::fixed << std::setprecision(4) << value;
}

struct Options {
    std::vector<size_t> scales{10'000, 100'000, 500'000, 1'000'000, 2'000'000};
    std::filesystem::path outputPath = std::filesystem::path(L"docs/benchmarks/large-index-scalability.json");
    bool detailedDiagnostics = false;
    bool searchAcceptanceOnly = false;
    // performance-closure-and-multisurface-acceleration 8.1: projection-memory
    // decomposition mode. When set, the benchmark runs only the memory report
    // (fixture generation -> rebuild cycles -> export weighing -> teardown) and
    // writes its machine-readable result to memoryOutputPath.
    std::filesystem::path memoryOutputPath;
    bool memoryReport = false;
    size_t rebuildCycles = 4;
    size_t exportBoundDirectories = 4096;
    // Provenance recorded into the memory report so the artifact identifies
    // its own build and command instead of relying on an ignored file name.
    std::string commandLine;
    std::string sourceState;   // caller-supplied (clean/dirty), verified by the runner
    std::string sourceSha;     // caller-supplied git revision
    std::string preset;        // caller-supplied configure preset
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
        if (end == token.c_str() || *end != L'\0' || parsed == 0 || parsed > std::numeric_limits<size_t>::max()) {
            return {};
        }
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
            std::wprintf(L"Usage: %s [--scales 10000,100000] [--output path] [--diagnostics]\n"
                         L"       %s --memory-report <path> [--scales 200000,1000000] "
                         L"[--rebuild-cycles 4] [--export-bound-directories 4096] [--diagnostics]\n",
                         argv[0], argv[0]);
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
        if (argument == L"--output" && index + 1 < argc) {
            options.outputPath = argv[++index];
            continue;
        }
        if (argument == L"--diagnostics") {
            options.detailedDiagnostics = true;
            continue;
        }
        if (argument == L"--search-acceptance-only") {
            options.searchAcceptanceOnly = true;
            continue;
        }
        if (argument == L"--memory-report" && index + 1 < argc) {
            options.memoryOutputPath = argv[++index];
            options.memoryReport = true;
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
        if (argument == L"--rebuild-cycles" && index + 1 < argc) {
            const long parsed = _wtol(argv[++index]);
            if (parsed < 1 || parsed > 64) {
                std::fwprintf(stderr, L"Invalid --rebuild-cycles value\n");
                return false;
            }
            options.rebuildCycles = static_cast<size_t>(parsed);
            continue;
        }
        if (argument == L"--export-bound-directories" && index + 1 < argc) {
            const long parsed = _wtol(argv[++index]);
            if (parsed < 1) {
                std::fwprintf(stderr, L"Invalid --export-bound-directories value\n");
                return false;
            }
            options.exportBoundDirectories = static_cast<size_t>(parsed);
            continue;
        }
        std::fwprintf(stderr, L"Unknown or incomplete argument: %s\n", argument.c_str());
        return false;
    }
    return true;
}

std::string PathToUtf8(const std::filesystem::path& path) {
    return ToUtf8(path.wstring());
}

std::filesystem::path MakeTemporaryDirectory(size_t scale) {
    const std::filesystem::path base = std::filesystem::temp_directory_path();
    const std::wstring stem = L"FastFiles-large-index-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(scale);
    for (unsigned int suffix = 0; suffix < 1000; ++suffix) {
        const std::filesystem::path candidate = base / (stem + L"-" + std::to_wstring(suffix));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) return candidate;
    }
    return {};
}

ffprotocol::MftRecordV1 MakeMftRecord(uint64_t id, uint64_t parent, const std::wstring& name,
                                      uint32_t attributes, uint64_t sizeBytes, uint64_t timestamp) {
    ffprotocol::MftRecordV1 record{};
    record.fixed.fileReferenceNumber = id;
    record.fixed.parentFileReferenceNumber = parent;
    record.fixed.creationTime = timestamp;
    record.fixed.lastModifiedTime = timestamp + 100;
    record.fixed.lastAccessTime = timestamp + 200;
    record.fixed.sizeBytes = sizeBytes;
    record.fixed.fileAttributes = attributes;
    record.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
    record.fileName = ToUtf16(name);
    return record;
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
    // performance-closure-and-multisurface-acceleration 7.1: a deterministic
    // Unicode-name subset (10% of the fixture) so the Unicode query class is
    // measured against real matches, not only the no-result path. Escape
    // sequences keep the source encoding-independent.
    if (pattern == 8) return L"\u0441\u043f\u0438\u0441\u043e\u043a_" + std::to_wstring(index) + L".txt";
    return L"backup_" + std::to_wstring(index) + L".7z";
}

struct DatasetResult {
    size_t scale = 0;
    std::wstring databaseLocation;
    uint64_t databaseBytes = 0;
    double generationMs = 0.0;
    double rebuildMs = 0.0;
    double snapshotExportMs = 0.0;
    double actualSnapshotSerializationMs = 0.0;
    double actualSnapshotPublishCopyAndCommitMs = 0.0;
    double actualSnapshotUiMappingAndParsingMs = 0.0;
    size_t actualSnapshotDirectoryCount = 0;
    size_t actualSnapshotEntryCount = 0;
    size_t actualSnapshotBytes = 0;
    bool actualSnapshotPublishSucceeded = false;
    MemorySample beforeFixture;
    MemorySample afterFixture;
    MemorySample afterDatabaseOpen;
    MemorySample afterRebuild;
    MemorySample afterBroadQuery;
    MemorySample postQuery;
    size_t projectionEstimatedBytes = 0;
    size_t projectionEntryBytes = sizeof(ffindexstore::ProjectionEntry);
    ResourceSample resourcesAfterRebuild;
    ResourceSample resourcesPostQuery;
    struct ContentionResult {
        bool executed = false;
        bool searchReachedColdStream = false;
        bool ingestionCompletedWhileHeld = false;
        bool aggregateCompletedWhileHeld = false;
        bool snapshotCompletedWhileHeld = false;
        bool secondSearchCompletedWhileHeld = false;
        bool cancelledSearch = false;
        size_t activeReadersAfterCleanup = 0;
        size_t peakReaders = 0;
        double heldSearchMs = 0.0;
        double ingestionMs = 0.0;
        double aggregateMs = 0.0;
        double snapshotMs = 0.0;
        double secondSearchMs = 0.0;
        double cancellationMs = 0.0;
    } contention;
    ffengine::IndexPipeline::ReadPlaneDiagnostics readPlane;
};

struct SearchRun {
    size_t scale = 0;
    std::string queryClass;
    std::string query;
    std::string temperature;
    size_t iteration = 0;
    size_t resultCount = 0;
    size_t candidateCount = 0;
    size_t candidatesVisited = 0;
    bool truncated = false;
    bool wireTruncated = false;
    size_t wireResultCount = 0;
    bool cancelled = false;
    bool unavailable = false;
    bool filenameIndexUsed = false;
    size_t filenameIndexCandidateCount = 0;
    uint64_t searchGeneration = 0;
    uint64_t queryVisibleEpoch = 0;
    uint64_t dependencySetEpoch = 0;
    size_t generationRestarts = 0;
    size_t relevantInvalidations = 0;
    size_t durableFallbackAttempts = 0;
    size_t durableFallbackRowsStreamed = 0;
    bool durableFallbackUsed = false;
    bool durableFallbackSucceeded = false;
    double stateLockWaitMs = 0.0;
    double maxStateLockHoldMs = 0.0;
    size_t peakSearches = 0;
    size_t peakDurableReaders = 0;
    double sqliteMs = 0.0;
    double projectionTraversalMs = 0.0;
    double projectionEnumerationMs = 0.0;
    double namePoolLookupMs = 0.0;
    double nameViewConstructionMs = 0.0;
    double materializationMs = 0.0;
    double lightweightCandidateMaterializationMs = 0.0;
    double lazyPathMaterializationMs = 0.0;
    size_t lazyPathMaterializationCount = 0;
    double queryParseMs = 0.0;
    double matchFilterSortMs = 0.0;
    double predicateDispatchMs = 0.0;
    double substringMatchingMs = 0.0;
    double globMatchingMs = 0.0;
    double extensionMatchingMs = 0.0;
    double metadataMatchingMs = 0.0;
    double folderMatchingMs = 0.0;
    double matchedRowHandlingMs = 0.0;
    double responseMaterializationMs = 0.0;
    double responseSerializationMs = 0.0;
    double responseParseMs = 0.0;
    double uiResultMaterializationMs = 0.0;
    double totalMs = 0.0;
    double engineAndResponseMs = 0.0;
    size_t projectionEntriesVisited = 0;
    size_t projectionRowsExamined = 0;
    // memory-bounded-indexed-scale 7.1: hot/cold attribution.
    size_t hotRowsExamined = 0;
    size_t coldRowsExamined = 0;
    size_t coldVolumesSearched = 0;
    double storeStreamMs = 0.0;
    bool coldSearchUsed = false;
    size_t namePoolLookups = 0;
    size_t emptyNamesRejected = 0;
    size_t nameViewsConstructed = 0;
    size_t queryPredicateEvaluations = 0;
    size_t queryTermEvaluations = 0;
    size_t caseInsensitiveComparisons = 0;
    size_t caseFoldOperations = 0;
    size_t substringSearches = 0;
    size_t globMatches = 0;
    size_t prefixChecks = 0;
    size_t extensionPredicates = 0;
    size_t metadataPredicates = 0;
    size_t earlyRejects = 0;
    size_t predicateMatches = 0;
    size_t pathNeededDecisions = 0;
    size_t pathLookups = 0;
    size_t matchedRows = 0;
    size_t candidateObjectsMaterialized = 0;
    size_t candidateNameCopies = 0;
    size_t searchResultObjectsMaterialized = 0;
    size_t wireBytes = 0;
    MemorySample memory;
};

struct SearchSummary {
    size_t scale = 0;
    std::string queryClass;
    size_t resultCount = 0;
    double minMs = 0.0;
    double medianMs = 0.0;
    double p90Ms = 0.0;
    double maxMs = 0.0;
};

struct IncrementalRow {
    size_t scale = 0;
    std::string step;
    size_t resultCount = 0;
    double medianMs = 0.0;
};

struct SnapshotRun {
    size_t rows = 0;
    size_t serializedBytes = 0;
    bool publishSucceeded = false;
    bool oversize = false;
    uint64_t generationBefore = 0;
    uint64_t generationAfter = 0;
    double modelBuildMs = 0.0;
    double serializationMs = 0.0;
    double publishCopyAndCommitMs = 0.0;
    double totalMs = 0.0;
    double uiMappingAndParsingMs = 0.0;
    size_t uiParsedRows = 0;
    MemorySample memoryBefore = {};
    MemorySample memoryAfterModel = {};
    MemorySample memoryAfterPublish = {};
    MemorySample memoryAfterUi = {};
    MemorySample memoryAfter = {};
};

struct MachineInfo {
    std::string cpu;
    uint32_t logicalCores = 0;
    uint64_t ramBytes = 0;
    std::string windowsVersion;
    std::string compiler;
    std::string buildConfiguration;
    std::string databaseStorage;
};

struct QueryCase {
    const char* name;
    const wchar_t* query;
    uint8_t sortField;
    uint32_t maxResults = 10000;
};

std::string ReadCpuName() {
    int registers[4]{};
    __cpuid(registers, 0x80000000);
    const unsigned int maxExtendedLeaf = static_cast<unsigned int>(registers[0]);
    if (maxExtendedLeaf < 0x80000004U) {
        return "unknown";
    }
    char brand[49]{};
    for (unsigned int leaf = 0; leaf < 3; ++leaf) {
        __cpuid(registers, static_cast<int>(0x80000002U + leaf));
        std::memcpy(brand + (leaf * sizeof(registers)), registers, sizeof(registers));
    }
    return brand;
}

std::string ReadWindowsVersion() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto getVersion = ntdll == nullptr
        ? nullptr
        : reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (getVersion == nullptr) return "unknown";
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (getVersion(&version) != 0) return "unknown";
    return std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion) + "." +
           std::to_string(version.dwBuildNumber);
}

MachineInfo ReadMachineInfo() {
    SYSTEM_INFO systemInfo{};
    GetNativeSystemInfo(&systemInfo);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);
    return {ReadCpuName(), systemInfo.dwNumberOfProcessors, memory.ullTotalPhys, ReadWindowsVersion(),
            "MSVC " + std::to_string(_MSC_VER) + "." + std::to_string(_MSC_FULL_VER),
#ifdef NDEBUG
            "Release",
#else
            "non-Release",
#endif
            "temporary directory (drive/model not collected)"};
}

uint64_t FileSizeIfPresent(const std::filesystem::path& path) {
    std::error_code error;
    const uint64_t size = std::filesystem::exists(path, error) ? std::filesystem::file_size(path, error) : 0;
    return error ? 0 : size;
}

uint64_t DatabaseBytes(const std::filesystem::path& database) {
    return FileSizeIfPresent(database) + FileSizeIfPresent(database.wstring() + L"-wal") +
           FileSizeIfPresent(database.wstring() + L"-shm");
}

bool BuildSyntheticDatabase(const std::filesystem::path& database, size_t scale, double& generationMs,
                            ffindexstore::VolumeRowId& volumeId, MemorySample& before,
                            MemorySample& after, bool detailedDiagnostics) {
    before = ReadMemory();
    const auto openStart = Clock::now();
    ffengine::IndexPipeline pipeline;
    if (!pipeline.Open(PathToUtf8(database))) return false;
    if (detailedDiagnostics) {
        std::printf("  [%zu] fixture open %.2f ms\n", scale, ElapsedMs(openStart));
        std::printf("  [%zu] durable insert start (target %zu records)\n", scale, scale);
        std::fflush(stdout);
    }

    ffindexstore::VolumeKey key;
    key.serialNumber = static_cast<uint32_t>(scale ^ 0xF457F17u);
    volumeId = pipeline.ResolveVolume(key);
    if (volumeId == 0) return false;
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    pipeline.SetVolumeRules(volumeId, setting);

    const size_t directoryCount = (std::max)(size_t{1}, scale / 64);
    const auto generationStart = Clock::now();
    std::vector<ffprotocol::MftRecordV1> batch;
    batch.reserve(kBatchSize);
    size_t insertedRecords = 0;
    size_t nextProgress = 100'000;
    auto flush = [&] {
        if (batch.empty()) return true;
        const size_t batchRecords = batch.size();
        const bool applied = pipeline.ApplyMftBatch(volumeId, batch);
        batch.clear();
        if (applied) {
            insertedRecords += batchRecords;
            if (detailedDiagnostics && (insertedRecords >= nextProgress || insertedRecords == scale)) {
                std::printf("  [%zu] durable insert progress %zu/%zu (%.2f s)\n", scale,
                            insertedRecords, scale, ElapsedMs(generationStart) / 1000.0);
                std::fflush(stdout);
                while (nextProgress <= insertedRecords) nextProgress += 100'000;
            }
        }
        return applied;
    };

    batch.push_back(MakeMftRecord(5, 5, L"", 0x10, 0, 1));
    for (size_t index = 0; index < directoryCount && 1 + index < scale; ++index) {
        const uint64_t id = 100 + static_cast<uint64_t>(index);
        const uint64_t parent = index < 16 ? 5 : 100 + static_cast<uint64_t>((index - 1) / 16);
        batch.push_back(MakeMftRecord(id, parent, DirectoryName(index), 0x10, 0, 10 + index));
        if (batch.size() == kBatchSize && !flush()) return false;
    }

    const size_t filesToCreate = scale > 1 + directoryCount ? scale - 1 - directoryCount : 0;
    const uint64_t fileIdBase = 100 + static_cast<uint64_t>(directoryCount);
    for (size_t index = 0; index < filesToCreate; ++index) {
        const uint64_t parent = 100 + static_cast<uint64_t>(index % directoryCount);
        const uint64_t id = fileIdBase + static_cast<uint64_t>(index);
        const uint64_t sizeBytes = 1024 + static_cast<uint64_t>((index * 7919) % (64 * 1024 * 1024));
        batch.push_back(MakeMftRecord(id, parent, FileName(index), 0, sizeBytes, 1000 + index));
        if (batch.size() == kBatchSize && !flush()) return false;
    }
    if (!flush()) return false;
    generationMs = ElapsedMs(generationStart);
    after = ReadMemory();
    if (detailedDiagnostics) {
        std::printf("  [%zu] durable insert complete %.2f s (%zu records)\n", scale,
                    generationMs / 1000.0, insertedRecords);
        std::fflush(stdout);
    }
    pipeline.Close();
    return true;
}

bool LoadPipeline(const std::filesystem::path& database, ffengine::IndexPipeline& pipeline,
                  ffindexstore::VolumeRowId volumeId, double& rebuildMs, MemorySample& afterOpen,
                  MemorySample& afterRebuild, size_t& projectionEstimatedBytes,
                  bool detailedDiagnostics, size_t scale) {
    if (!pipeline.Open(PathToUtf8(database))) return false;
    afterOpen = ReadMemory();
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    pipeline.SetVolumeRules(volumeId, setting);
    if (detailedDiagnostics) {
        std::printf("  [%zu] projection rebuild start\n", scale);
        std::fflush(stdout);
    }
    const auto rebuildStart = Clock::now();
    pipeline.RebuildAll({});
    rebuildMs = ElapsedMs(rebuildStart);
    afterRebuild = ReadMemory();
    // performance-closure-and-multisurface-acceleration 8.1: the projection's
    // own estimator total at this scale, for calibration against the measured
    // private-bytes delta (the estimator is not a working-set proof).
    projectionEstimatedBytes = pipeline.GetProjectionMemoryDiagnostics().estimatedBytes;
    if (detailedDiagnostics) {
        std::printf("  [%zu] projection rebuild complete %.2f s\n", scale, rebuildMs / 1000.0);
        std::fflush(stdout);
    }
    return true;
}

struct SearchOutcome {
    ffengine::IndexPipeline::SearchTimings timings;
    SearchRun run;
};

SearchOutcome RunSearch(ffengine::IndexPipeline& pipeline, size_t scale, const QueryCase& queryCase,
                        size_t iteration, const char* temperature, bool detailedDiagnostics,
                        bool projectionOnly = false) {
    ffprotocol::IndexedSearchRequest request;
    request.requestId = static_cast<uint64_t>(iteration + 1);
    request.scope = 3; // AllIndexedLocations
    request.scopePath = L"C:";
    request.sortField = queryCase.sortField;
    request.maxResults = queryCase.maxResults;
    request.query = queryCase.query;

    SearchOutcome outcome;
    outcome.timings.detailedDiagnostics = detailedDiagnostics;
    outcome.timings.projectionOnly = projectionOnly;
    const auto response = pipeline.SearchIndexed(request, &outcome.timings);
    const auto serializationStart = Clock::now();
    // indexed-search-allocated-size D5: the engine now emits the additive V2
    // response, so the wire measurement exercises V2 serialization/parse (an extra
    // 8-byte allocated field per candidate, no algorithmic change).
    const std::vector<uint8_t> payload = ffprotocol::SerializeIndexedSearchResponseV2(response);
    const double serializationMs = ElapsedMs(serializationStart);

    double parseMs = 0.0;
    double materializationMs = 0.0;
    size_t parsedCount = 0;
    if (!payload.empty()) {
        const auto parseStart = Clock::now();
        const auto parsed = ffprotocol::ParseIndexedSearchResponseV2(payload.data(), payload.size());
        parseMs = ElapsedMs(parseStart);
        if (parsed) {
            const auto materializationStart = Clock::now();
            std::vector<ffsearch::Candidate> uiCandidates;
            uiCandidates.reserve(parsed->candidates.size());
            for (const auto& candidate : parsed->candidates) {
                uiCandidates.push_back({candidate.name, candidate.folder, candidate.sizeBytes,
                                        candidate.modifiedTime, candidate.isDirectory, candidate.createdTime,
                                        candidate.volumeId, candidate.id, candidate.parentId,
                                        candidate.persistentIndexed});
            }
            parsedCount = uiCandidates.size();
            materializationMs = ElapsedMs(materializationStart);
            outcome.run.wireTruncated = parsed->truncated;
            outcome.run.wireResultCount = parsedCount;
        }
    }

    outcome.run.scale = scale;
    outcome.run.queryClass = queryCase.name;
    outcome.run.query = ToUtf8(queryCase.query);
    outcome.run.temperature = temperature;
    outcome.run.iteration = iteration;
    outcome.run.resultCount = outcome.timings.resultCount;
    outcome.run.cancelled = response.status == ffprotocol::IndexedSearchStatus::Cancelled;
    outcome.run.unavailable = response.status == ffprotocol::IndexedSearchStatus::Unavailable;
    outcome.run.candidateCount = outcome.timings.candidateCount;
    outcome.run.candidatesVisited = outcome.timings.candidatesVisited;
    outcome.run.filenameIndexUsed = outcome.timings.filenameIndexUsed;
    outcome.run.filenameIndexCandidateCount = outcome.timings.filenameIndexCandidateCount;
    outcome.run.searchGeneration = outcome.timings.searchGeneration;
    outcome.run.queryVisibleEpoch = outcome.timings.queryVisibleEpoch;
    outcome.run.dependencySetEpoch = outcome.timings.dependencySetEpoch;
    outcome.run.generationRestarts = outcome.timings.generationRestarts;
    outcome.run.relevantInvalidations = outcome.timings.relevantInvalidations;
    outcome.run.durableFallbackAttempts = outcome.timings.durableFallbackAttempts;
    outcome.run.durableFallbackRowsStreamed = outcome.timings.durableFallbackRowsStreamed;
    outcome.run.durableFallbackUsed = outcome.timings.durableFallbackUsed;
    outcome.run.durableFallbackSucceeded = outcome.timings.durableFallbackSucceeded;
    outcome.run.stateLockWaitMs = outcome.timings.stateLockWaitMs;
    outcome.run.maxStateLockHoldMs = outcome.timings.maxStateLockHoldMs;
    outcome.run.truncated = outcome.timings.truncated;
    outcome.run.sqliteMs = outcome.timings.sqliteMetadataMs;
    outcome.run.projectionTraversalMs = outcome.timings.projectionTraversalMs;
    outcome.run.projectionEnumerationMs = outcome.timings.projectionEnumerationMs;
    outcome.run.namePoolLookupMs = outcome.timings.namePoolLookupMs;
    outcome.run.nameViewConstructionMs = outcome.timings.nameViewConstructionMs;
    outcome.run.materializationMs = outcome.timings.candidateMaterializationMs;
    outcome.run.lightweightCandidateMaterializationMs = outcome.timings.lightweightCandidateMaterializationMs;
    outcome.run.lazyPathMaterializationMs = outcome.timings.lazyPathMaterializationMs;
    outcome.run.lazyPathMaterializationCount = outcome.timings.lazyPathMaterializationCount;
    outcome.run.queryParseMs = outcome.timings.queryParseMs;
    outcome.run.matchFilterSortMs = outcome.timings.matchFilterSortMs;
    outcome.run.predicateDispatchMs = outcome.timings.predicateDispatchMs;
    outcome.run.substringMatchingMs = outcome.timings.substringMatchingMs;
    outcome.run.globMatchingMs = outcome.timings.globMatchingMs;
    outcome.run.extensionMatchingMs = outcome.timings.extensionMatchingMs;
    outcome.run.metadataMatchingMs = outcome.timings.metadataMatchingMs;
    outcome.run.folderMatchingMs = outcome.timings.folderMatchingMs;
    outcome.run.matchedRowHandlingMs = outcome.timings.matchedRowHandlingMs;
    outcome.run.responseMaterializationMs = outcome.timings.responseMaterializationMs;
    outcome.run.responseSerializationMs = serializationMs;
    outcome.run.responseParseMs = parseMs;
    outcome.run.uiResultMaterializationMs = materializationMs;
    outcome.run.totalMs = outcome.timings.totalMs;
    outcome.run.engineAndResponseMs = outcome.timings.totalMs + serializationMs;
    outcome.run.projectionEntriesVisited = outcome.timings.projectionEntriesVisited;
    outcome.run.projectionRowsExamined = outcome.timings.projectionRowsExamined;
    outcome.run.hotRowsExamined = outcome.timings.hotRowsExamined;
    outcome.run.coldRowsExamined = outcome.timings.coldRowsExamined;
    outcome.run.coldVolumesSearched = outcome.timings.coldVolumesSearched;
    outcome.run.storeStreamMs = outcome.timings.storeStreamMs;
    outcome.run.coldSearchUsed = outcome.timings.coldSearchUsed;
    outcome.run.namePoolLookups = outcome.timings.namePoolLookups;
    outcome.run.emptyNamesRejected = outcome.timings.emptyNamesRejected;
    outcome.run.nameViewsConstructed = outcome.timings.nameViewsConstructed;
    outcome.run.queryPredicateEvaluations = outcome.timings.queryPredicateEvaluations;
    outcome.run.queryTermEvaluations = outcome.timings.queryTermEvaluations;
    outcome.run.caseInsensitiveComparisons = outcome.timings.caseInsensitiveComparisons;
    outcome.run.caseFoldOperations = outcome.timings.caseFoldOperations;
    outcome.run.substringSearches = outcome.timings.substringSearches;
    outcome.run.globMatches = outcome.timings.globMatches;
    outcome.run.prefixChecks = outcome.timings.prefixChecks;
    outcome.run.extensionPredicates = outcome.timings.extensionPredicates;
    outcome.run.metadataPredicates = outcome.timings.metadataPredicates;
    outcome.run.earlyRejects = outcome.timings.earlyRejects;
    outcome.run.predicateMatches = outcome.timings.predicateMatches;
    outcome.run.pathNeededDecisions = outcome.timings.pathNeededDecisions;
    outcome.run.pathLookups = outcome.timings.pathLookups;
    outcome.run.matchedRows = outcome.timings.matchedRows;
    outcome.run.candidateObjectsMaterialized = outcome.timings.candidateObjectsMaterialized;
    outcome.run.candidateNameCopies = outcome.timings.candidateNameCopies;
    outcome.run.searchResultObjectsMaterialized = outcome.timings.searchResultObjectsMaterialized;
    outcome.run.wireBytes = payload.size();
    outcome.run.memory = ReadMemory();
    const auto readPlane = pipeline.GetReadPlaneDiagnostics();
    outcome.run.peakSearches = readPlane.peakSearches;
    outcome.run.peakDurableReaders = readPlane.peakDurableReaders;
    return outcome;
}

double Percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>((values.size() - 1) * fraction);
    return values[index];
}

void AddSearchSummary(std::vector<SearchSummary>& summaries, size_t scale, const QueryCase& queryCase,
                      const std::vector<SearchRun>& runs) {
    std::vector<double> values;
    size_t resultCount = 0;
    for (const auto& run : runs) {
        if (run.scale == scale && run.queryClass == queryCase.name && run.temperature == "warm") {
            values.push_back(run.totalMs);
            resultCount = run.resultCount;
        }
    }
    if (values.empty()) return;
    std::sort(values.begin(), values.end());
    summaries.push_back({scale, queryCase.name, resultCount, values.front(),
                         Percentile(values, 0.50), Percentile(values, 0.90), values.back()});
}

class ContentionBarrier {
public:
    void ArriveAndWait() {
        std::unique_lock<std::mutex> lock(mutex_);
        arrived_ = true;
        condition_.notify_all();
        condition_.wait(lock, [this] { return released_; });
    }

    bool WaitForArrival(const std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, timeout, [this] { return arrived_; });
    }

    void Release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        condition_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool arrived_ = false;
    bool released_ = false;
};

SearchRun RunDurableFallbackCancellationProbe(ffengine::IndexPipeline& pipeline,
                                              ffindexstore::VolumeRowId volumeId,
                                              size_t scale, bool detailedDiagnostics) {
    SearchRun run;
    run.scale = scale;
    run.queryClass = "durable_fallback_cancellation";
    run.query = "common";
    run.temperature = "fallback-cancelled";

    ContentionBarrier fallbackBarrier;
    std::atomic<size_t> validationCount{0};
    pipeline.SetSearchTestHooks({[&](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase == ffengine::IndexPipeline::SearchPhase::GenerationValidation) {
            const size_t sequence = validationCount.fetch_add(1, std::memory_order_acq_rel);
            if (sequence < 4) {
                pipeline.ApplyMftBatch(volumeId, {
                    MakeMftRecord(20'000'000 + sequence, 5,
                                  L"benchmark_fallback_churn_" + std::to_wstring(sequence), 0, 1, 1)
                });
            }
        } else if (phase == ffengine::IndexPipeline::SearchPhase::DurableFallback) {
            fallbackBarrier.ArriveAndWait();
        }
    }});

    ffprotocol::IndexedSearchRequest request;
    request.requestId = 950'000 + static_cast<uint64_t>(scale % 100'000);
    request.scope = 2; // Keep the cancellation probe tied to the fixture volume.
    request.scopePath = L"C:";
    request.maxResults = 10'000;
    request.query = L"common";
    ffengine::IndexPipeline::SearchTimings timings;
    timings.detailedDiagnostics = detailedDiagnostics;
    std::atomic<bool> cancelled{false};
    ffprotocol::IndexedSearchResponse response;
    const auto start = Clock::now();
    std::thread worker([&] {
        response = pipeline.SearchIndexed(request, &timings, [&cancelled] {
            return cancelled.load(std::memory_order_acquire);
        });
    });
    // The four deterministic restart passes intentionally stream the full
    // dataset. This is an evidence probe, not a wall-clock gate; give the
    // condition-variable rendezvous enough room on a contended host while
    // still bounding a broken harness run.
    const bool reachedFallback = fallbackBarrier.WaitForArrival(std::chrono::seconds(30));
    cancelled.store(true, std::memory_order_release);
    fallbackBarrier.Release();
    worker.join();
    pipeline.SetSearchTestHooks({});

    run.queryVisibleEpoch = timings.queryVisibleEpoch;
    run.dependencySetEpoch = timings.dependencySetEpoch;
    run.generationRestarts = timings.generationRestarts;
    run.relevantInvalidations = timings.relevantInvalidations;
    run.durableFallbackAttempts = timings.durableFallbackAttempts;
    run.durableFallbackRowsStreamed = timings.durableFallbackRowsStreamed;
    run.durableFallbackUsed = timings.durableFallbackUsed;
    run.durableFallbackSucceeded = timings.durableFallbackSucceeded;
    run.cancelled = response.status == ffprotocol::IndexedSearchStatus::Cancelled;
    run.unavailable = response.status == ffprotocol::IndexedSearchStatus::Unavailable;
    run.resultCount = response.candidates.size();
    run.totalMs = timings.totalMs != 0.0 ? timings.totalMs : ElapsedMs(start);
    run.engineAndResponseMs = run.totalMs;
    run.memory = ReadMemory();
    if (detailedDiagnostics) {
        std::printf("  fallback cancellation: reached=%s, cancelled=%s, rows=%zu, %.2f ms\n",
                    reachedFallback ? "yes" : "no", run.cancelled ? "yes" : "no",
                    run.durableFallbackRowsStreamed, run.totalMs);
        std::fflush(stdout);
    }
    return run;
}

DatasetResult::ContentionResult RunContentionProbe(ffengine::IndexPipeline& pipeline,
                                                    ffindexstore::VolumeRowId volumeId,
                                                    size_t scale, bool detailedDiagnostics) {
    DatasetResult::ContentionResult result;
    result.executed = true;
    ContentionBarrier coldStreamBarrier;
    std::atomic<bool> firstSearchParked{false};
    pipeline.SetSearchTestHooks({[&](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase == ffengine::IndexPipeline::SearchPhase::ColdStreaming) {
            bool expected = false;
            if (firstSearchParked.compare_exchange_strong(expected, true,
                                                           std::memory_order_acq_rel)) {
                coldStreamBarrier.ArriveAndWait();
            }
        }
    }});

    ffprotocol::IndexedSearchRequest firstRequest;
    firstRequest.requestId = 900'000 + static_cast<uint64_t>(scale % 100'000);
    firstRequest.scope = 3;
    firstRequest.maxResults = 10'000;
    firstRequest.query = L"common";
    std::atomic<bool> cancelFirst{false};
    ffprotocol::IndexedSearchResponse firstResponse;
    std::thread firstSearch([&] {
        firstResponse = pipeline.SearchIndexed(firstRequest, nullptr, [&cancelFirst] {
            return cancelFirst.load(std::memory_order_acquire);
        });
    });

    result.searchReachedColdStream = coldStreamBarrier.WaitForArrival(std::chrono::seconds(5));
    if (!result.searchReachedColdStream) {
        cancelFirst.store(true, std::memory_order_release);
        coldStreamBarrier.Release();
        firstSearch.join();
        pipeline.SetSearchTestHooks({});
        result.cancelledSearch = firstResponse.status == ffprotocol::IndexedSearchStatus::Cancelled;
        const auto diagnostics = pipeline.GetReadPlaneDiagnostics();
        result.activeReadersAfterCleanup = diagnostics.activeDurableReaders;
        result.peakReaders = diagnostics.peakDurableReaders;
        return result;
    }

    const auto heldStart = Clock::now();
    std::atomic<bool> ingestionDone{false};
    std::atomic<bool> aggregateDone{false};
    std::atomic<bool> snapshotDone{false};
    std::atomic<bool> secondSearchDone{false};
    double ingestionMs = 0.0;
    double aggregateMs = 0.0;
    double snapshotMs = 0.0;
    double secondSearchMs = 0.0;
    bool ingestionSucceeded = false;
    ffprotocol::IndexedSearchResponse secondResponse;

    std::thread ingestion([&] {
        if (detailedDiagnostics) std::printf("  [diag] ingestion enter\n"); std::fflush(stdout);
        const auto start = Clock::now();
        ingestionSucceeded = pipeline.ApplyMftBatch(volumeId, {
            MakeMftRecord(10'000'000 + static_cast<uint64_t>(scale), 5,
                          L"contention_writer.txt", 0, 7, 777),
        });
        ingestionMs = ElapsedMs(start);
        if (detailedDiagnostics) std::printf("  [diag] ingestion done\n"); std::fflush(stdout);
        ingestionDone.store(true, std::memory_order_release);
    });
    std::thread aggregate([&] {
        if (detailedDiagnostics) std::printf("  [diag] aggregate enter\n"); std::fflush(stdout);
        const auto start = Clock::now();
        (void)pipeline.GetFolderAggregate(volumeId, ffindexstore::FileId{5, 0});
        aggregateMs = ElapsedMs(start);
        if (detailedDiagnostics) std::printf("  [diag] aggregate done %.1f ms\n", aggregateMs); std::fflush(stdout);
        aggregateDone.store(true, std::memory_order_release);
    });
    std::thread snapshot([&] {
        if (detailedDiagnostics) std::printf("  [diag] snapshot enter\n"); std::fflush(stdout);
        const auto start = Clock::now();
        (void)pipeline.ExportDirectorySnapshot(volumeId, L"C:");
        snapshotMs = ElapsedMs(start);
        if (detailedDiagnostics) std::printf("  [diag] snapshot done %.1f ms\n", snapshotMs); std::fflush(stdout);
        snapshotDone.store(true, std::memory_order_release);
    });
    std::thread secondSearch([&] {
        if (detailedDiagnostics) std::printf("  [diag] secondSearch enter\n"); std::fflush(stdout);
        ffprotocol::IndexedSearchRequest request = firstRequest;
        request.requestId += 1;
        request.query = L"report";
        const auto start = Clock::now();
        secondResponse = pipeline.SearchIndexed(request);
        secondSearchMs = ElapsedMs(start);
        if (detailedDiagnostics) std::printf("  [diag] secondSearch done %.1f ms\n", secondSearchMs); std::fflush(stdout);
        secondSearchDone.store(true, std::memory_order_release);
    });

    const auto operationDeadline = Clock::now() + std::chrono::seconds(10);
    while (Clock::now() < operationDeadline &&
           (!ingestionDone.load(std::memory_order_acquire) ||
            !aggregateDone.load(std::memory_order_acquire) ||
            !snapshotDone.load(std::memory_order_acquire) ||
            !secondSearchDone.load(std::memory_order_acquire))) {
        std::this_thread::yield();
    }
    result.heldSearchMs = ElapsedMs(heldStart);
    const bool ingestionFinishedWhileHeld = ingestionDone.load(std::memory_order_acquire);
    const bool aggregateFinishedWhileHeld = aggregateDone.load(std::memory_order_acquire);
    const bool snapshotFinishedWhileHeld = snapshotDone.load(std::memory_order_acquire);
    const bool secondSearchFinishedWhileHeld = secondSearchDone.load(std::memory_order_acquire);

    if (detailedDiagnostics) std::printf("  [diag-contention] deadline passed; releasing\n"); std::fflush(stdout);
    cancelFirst.store(true, std::memory_order_release);
    const auto cancellationStart = Clock::now();
    coldStreamBarrier.Release();
    firstSearch.join();
    if (detailedDiagnostics) std::printf("  [diag-contention] firstSearch joined\n"); std::fflush(stdout);
    result.cancellationMs = ElapsedMs(cancellationStart);
    result.cancelledSearch = firstResponse.status == ffprotocol::IndexedSearchStatus::Cancelled;
    ingestion.join();
    if (detailedDiagnostics) std::printf("  [diag-contention] ingestion joined\n"); std::fflush(stdout);
    aggregate.join();
    if (detailedDiagnostics) std::printf("  [diag-contention] aggregate joined\n"); std::fflush(stdout);
    snapshot.join();
    if (detailedDiagnostics) std::printf("  [diag-contention] snapshot joined\n"); std::fflush(stdout);
    secondSearch.join();
    if (detailedDiagnostics) std::printf("  [diag-contention] secondSearch joined\n"); std::fflush(stdout);
    pipeline.SetSearchTestHooks({});

    result.ingestionCompletedWhileHeld = ingestionFinishedWhileHeld && ingestionSucceeded;
    result.aggregateCompletedWhileHeld = aggregateFinishedWhileHeld;
    result.snapshotCompletedWhileHeld = snapshotFinishedWhileHeld;
    result.secondSearchCompletedWhileHeld = secondSearchFinishedWhileHeld &&
        secondResponse.status == ffprotocol::IndexedSearchStatus::Completed;
    result.ingestionMs = ingestionMs;
    result.aggregateMs = aggregateMs;
    result.snapshotMs = snapshotMs;
    result.secondSearchMs = secondSearchMs;

    const auto diagnostics = pipeline.GetReadPlaneDiagnostics();
    result.activeReadersAfterCleanup = diagnostics.activeDurableReaders;
    result.peakReaders = diagnostics.peakDurableReaders;
    if (detailedDiagnostics) {
        std::printf("  contention held %.2f ms: ingestion %.2f (%s), aggregate %.2f (%s), "
                    "snapshot %.2f (%s), second query %.2f (%s), cancellation %.2f ms\n",
                    result.heldSearchMs, result.ingestionMs,
                    result.ingestionCompletedWhileHeld ? "progress" : "blocked",
                    result.aggregateMs, result.aggregateCompletedWhileHeld ? "progress" : "blocked",
                    result.snapshotMs, result.snapshotCompletedWhileHeld ? "progress" : "blocked",
                    result.secondSearchMs, result.secondSearchCompletedWhileHeld ? "progress" : "blocked",
                    result.cancellationMs);
        std::fflush(stdout);
    }
    return result;
}

std::map<std::wstring, ffprotocol::SnapshotDirectory> MakeSnapshot(size_t rows) {
    ffprotocol::SnapshotDirectory directory;
    directory.status = ffprotocol::DirectoryEnumerationStatus::Success;
    directory.volumeRowId = 1;
    directory.directoryIdLow = 5;
    directory.parentIdLow = 5;
    directory.entries.reserve(rows);
    for (size_t index = 0; index < rows; ++index) {
        ffprotocol::SnapshotDirectoryEntry entry;
        entry.name = L"snapshot_item_" + std::to_wstring(index) + ((index % 5 == 0) ? L".txt" : L".bin");
        entry.sizeBytes = 4096 + index * 17;
        entry.attributes = 0;
        entry.creationTime = 10 + index;
        entry.lastModifiedTime = 20 + index;
        entry.volumeRowId = 1;
        entry.fileIdLow = 100 + index;
        entry.parentIdLow = 5;
        directory.entries.push_back(std::move(entry));
    }
    std::map<std::wstring, ffprotocol::SnapshotDirectory> result;
    result.emplace(L"C:\\Benchmark", std::move(directory));
    return result;
}

size_t SerializedSizeForRows(size_t rows) {
    constexpr size_t kDirectoryCountBytes = sizeof(uint32_t);
    constexpr size_t kStatusBytes = sizeof(uint32_t);
    constexpr size_t kDirectoryIdentifierBytes = 5 * sizeof(uint64_t);
    constexpr size_t kEntryFixedBytes = sizeof(uint8_t) + 8 * sizeof(uint64_t) + sizeof(uint32_t);
    const std::wstring path = L"C:\\Benchmark";
    size_t total = kDirectoryCountBytes + sizeof(uint32_t) + path.size() * sizeof(wchar_t) +
        kStatusBytes + kDirectoryIdentifierBytes + sizeof(uint32_t);
    for (size_t index = 0; index < rows; ++index) {
        size_t digits = 1;
        for (size_t value = index; value >= 10; value /= 10) ++digits;
        const size_t nameChars = 18 + digits; // snapshot_item_ + decimal index + .txt/.bin
        total += kEntryFixedBytes + sizeof(uint32_t) + nameChars * sizeof(wchar_t);
    }
    return total;
}

size_t FindLargestSnapshotRows() {
    size_t low = 0;
    size_t high = kCapacitySearchUpperBound;
    while (high - low > 512) {
        const size_t middle = low + (high - low) / 2;
        if (SerializedSizeForRows(middle) <= ffprotocol::kSnapshotMaximumPayloadBytes) {
            low = middle;
        } else {
            high = middle;
        }
    }
    while (SerializedSizeForRows(low + 1) <= ffprotocol::kSnapshotMaximumPayloadBytes) ++low;
    return low;
}

struct PublishedRead {
    double elapsedMs = 0.0;
    size_t rows = 0;
    bool parsed = false;
};

PublishedRead ReadPublishedSnapshot(const std::wstring& sectionName) {
    const auto start = Clock::now();
    PublishedRead result;
    const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, sectionName.c_str());
    if (mapping == nullptr) {
        result.elapsedMs = ElapsedMs(start);
        return result;
    }
    const auto* view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (view != nullptr) {
        ffprotocol::SnapshotSharedHeader header{};
        std::memcpy(&header, view, sizeof(header));
        if (ffprotocol::IsSnapshotFormatCompatible(header.formatVersion) && header.generation != 0 &&
            header.payloadSizeBytes <= ffprotocol::kSnapshotMaximumPayloadBytes) {
            const uint8_t* data = view + sizeof(header);
            const auto parsed = ffprotocol::ParseSnapshot(data, static_cast<size_t>(header.payloadSizeBytes));
            if (parsed) {
                result.parsed = true;
                for (const auto& [path, directory] : *parsed) {
                    UNREFERENCED_PARAMETER(path);
                    result.rows += directory.entries.size();
                }
            }
        }
        UnmapViewOfFile(view);
    }
    CloseHandle(mapping);
    result.elapsedMs = ElapsedMs(start);
    return result;
}

SnapshotRun RunSnapshot(size_t rows, ffengine::SnapshotPublisher& publisher, bool detailedDiagnostics) {
    SnapshotRun result;
    result.rows = rows;
    result.generationBefore = publisher.CurrentGeneration();
    result.memoryBefore = ReadMemory();
    const auto modelStart = Clock::now();
    const auto snapshot = MakeSnapshot(rows);
    result.modelBuildMs = ElapsedMs(modelStart);
    result.memoryAfterModel = ReadMemory();
    ffengine::SnapshotPublisher::PublishTimings timings;
    const auto totalStart = Clock::now();
    const uint64_t generation = publisher.Publish(snapshot, &timings);
    result.totalMs = ElapsedMs(totalStart) + result.modelBuildMs;
    result.serializedBytes = timings.serializedBytes;
    result.serializationMs = timings.serializationMs;
    result.publishCopyAndCommitMs = timings.copyAndCommitMs;
    result.publishSucceeded = generation != 0;
    result.oversize = !result.publishSucceeded && timings.serializedBytes > ffprotocol::kSnapshotMaximumPayloadBytes;
    result.generationAfter = publisher.CurrentGeneration();
    result.memoryAfterPublish = ReadMemory();
    if (result.publishSucceeded) {
        const PublishedRead read = ReadPublishedSnapshot(publisher.CurrentSectionName());
        result.uiMappingAndParsingMs = read.elapsedMs;
        result.uiParsedRows = read.rows;
        if (!read.parsed || read.rows != rows) {
            std::fprintf(stderr, "warning: published snapshot did not round-trip all rows\n");
        }
    }
    result.memoryAfterUi = ReadMemory();
    result.memoryAfter = result.memoryAfterUi;
    if (detailedDiagnostics) {
        std::printf("  snapshot rows %zu: model %.2f ms, serialize %.2f ms, publish %.2f ms, "
                    "UI map/parse %.2f ms, bytes %zu, %s\n",
                    rows, result.modelBuildMs, result.serializationMs, result.publishCopyAndCommitMs,
                    result.uiMappingAndParsingMs, result.serializedBytes,
                    result.publishSucceeded ? "published" : (result.oversize ? "rejected-oversize" : "failed"));
        std::fflush(stdout);
    }
    return result;
}

void WriteMemory(std::ostream& output, const MemorySample& sample) {
    output << "{\"working_set_bytes\":" << sample.workingSetBytes
           << ",\"private_bytes\":" << sample.privateBytes << '}';
}

void WriteResource(std::ostream& output, const ResourceSample& sample) {
    output << "{\"memory\":";
    WriteMemory(output, sample.memory);
    output << ",\"handle_count\":" << sample.handleCount
           << ",\"thread_count\":" << sample.threadCount
           << ",\"wal_bytes\":" << sample.walBytes << '}';
}

void WriteMachine(std::ostream& output, const MachineInfo& machine) {
    output << "{\"cpu\":"; WriteJsonString(output, machine.cpu);
    output << ",\"logical_cores\":" << machine.logicalCores << ",\"ram_bytes\":" << machine.ramBytes
           << ",\"windows_version\":"; WriteJsonString(output, machine.windowsVersion);
    output << ",\"compiler\":"; WriteJsonString(output, machine.compiler);
    output << ",\"build_configuration\":"; WriteJsonString(output, machine.buildConfiguration);
    output << ",\"database_storage\":"; WriteJsonString(output, machine.databaseStorage);
    output << '}';
}

void WriteContention(std::ostream& output, const DatasetResult::ContentionResult& contention) {
    output << "{\"executed\":" << (contention.executed ? "true" : "false")
           << ",\"search_reached_cold_stream\":"
           << (contention.searchReachedColdStream ? "true" : "false")
           << ",\"ingestion_completed_while_held\":"
           << (contention.ingestionCompletedWhileHeld ? "true" : "false")
           << ",\"aggregate_completed_while_held\":"
           << (contention.aggregateCompletedWhileHeld ? "true" : "false")
           << ",\"snapshot_completed_while_held\":"
           << (contention.snapshotCompletedWhileHeld ? "true" : "false")
           << ",\"second_search_completed_while_held\":"
           << (contention.secondSearchCompletedWhileHeld ? "true" : "false")
           << ",\"cancelled_search\":"
           << (contention.cancelledSearch ? "true" : "false")
           << ",\"active_readers_after_cleanup\":" << contention.activeReadersAfterCleanup
           << ",\"peak_readers\":" << contention.peakReaders
           << ",\"held_search_ms\":";
    WriteJsonDouble(output, contention.heldSearchMs);
    output << ",\"ingestion_ms\":";
    WriteJsonDouble(output, contention.ingestionMs);
    output << ",\"aggregate_ms\":";
    WriteJsonDouble(output, contention.aggregateMs);
    output << ",\"snapshot_ms\":";
    WriteJsonDouble(output, contention.snapshotMs);
    output << ",\"second_search_ms\":";
    WriteJsonDouble(output, contention.secondSearchMs);
    output << ",\"cancellation_ms\":";
    WriteJsonDouble(output, contention.cancellationMs);
    output << '}';
}

void WriteReadPlane(std::ostream& output, const ffengine::IndexPipeline::ReadPlaneDiagnostics& readPlane) {
    output << "{\"active_searches\":" << readPlane.activeSearches
           << ",\"peak_searches\":" << readPlane.peakSearches
           << ",\"active_durable_readers\":" << readPlane.activeDurableReaders
           << ",\"peak_durable_readers\":" << readPlane.peakDurableReaders
           << ",\"completed_searches\":" << readPlane.completedSearches
           << ",\"cancelled_searches\":" << readPlane.cancelledSearches
           << ",\"unavailable_searches\":" << readPlane.unavailableSearches
           << ",\"reader_failures\":" << readPlane.readerFailures
           << ",\"generation_restarts\":" << readPlane.generationRestarts
           << ",\"query_visible_epoch\":" << readPlane.queryVisibleEpoch
           << ",\"dependency_set_epoch\":" << readPlane.dependencySetEpoch
           << ",\"query_visible_invalidations\":" << readPlane.queryVisibleInvalidations
           << ",\"metadata_only_updates\":" << readPlane.metadataOnlyUpdates
           << ",\"metadata_only_invalidations_avoided\":"
           << readPlane.metadataOnlyInvalidationsAvoided
           << ",\"durable_fallbacks\":" << readPlane.durableFallbacks
           << ",\"successful_durable_fallbacks\":" << readPlane.successfulDurableFallbacks
           << ",\"failed_durable_fallbacks\":" << readPlane.failedDurableFallbacks
           << ",\"durable_fallback_rows_streamed\":" << readPlane.durableFallbackRowsStreamed << '}';
}

void WriteDataset(std::ostream& output, const DatasetResult& dataset) {
    output << "{\"dataset_size\":" << dataset.scale << ",\"database_location\":";
    WriteJsonString(output, ToUtf8(dataset.databaseLocation));
    output << ",\"database_bytes\":" << dataset.databaseBytes << ",\"generation_ms\":";
    WriteJsonDouble(output, dataset.generationMs);
    output << ",\"rebuild_ms\":"; WriteJsonDouble(output, dataset.rebuildMs);
    output << ",\"snapshot_export_ms\":"; WriteJsonDouble(output, dataset.snapshotExportMs);
    output << ",\"actual_snapshot_directory_count\":" << dataset.actualSnapshotDirectoryCount
           << ",\"actual_snapshot_entry_count\":" << dataset.actualSnapshotEntryCount
           << ",\"actual_snapshot_bytes\":" << dataset.actualSnapshotBytes
           << ",\"actual_snapshot_publish_succeeded\":"
           << (dataset.actualSnapshotPublishSucceeded ? "true" : "false");
    output << ",\"actual_snapshot_serialization_ms\":";
    WriteJsonDouble(output, dataset.actualSnapshotSerializationMs);
    output << ",\"actual_snapshot_publish_copy_and_commit_ms\":";
    WriteJsonDouble(output, dataset.actualSnapshotPublishCopyAndCommitMs);
    output << ",\"actual_snapshot_ui_mapping_and_parsing_ms\":";
    WriteJsonDouble(output, dataset.actualSnapshotUiMappingAndParsingMs);
    output << ",\"memory_before_fixture\":"; WriteMemory(output, dataset.beforeFixture);
    output << ",\"memory_after_fixture\":"; WriteMemory(output, dataset.afterFixture);
    output << ",\"memory_after_database_open\":"; WriteMemory(output, dataset.afterDatabaseOpen);
    output << ",\"memory_after_rebuild\":"; WriteMemory(output, dataset.afterRebuild);
    output << ",\"projection_estimated_bytes\":" << dataset.projectionEstimatedBytes
           << ",\"projection_entry_bytes\":" << dataset.projectionEntryBytes;
    output << ",\"memory_after_broad_query\":"; WriteMemory(output, dataset.afterBroadQuery);
    output << ",\"memory_post_query\":"; WriteMemory(output, dataset.postQuery);
    output << ",\"resources_after_rebuild\":"; WriteResource(output, dataset.resourcesAfterRebuild);
    output << ",\"resources_post_query\":"; WriteResource(output, dataset.resourcesPostQuery);
    output << ",\"contention\":"; WriteContention(output, dataset.contention);
    output << ",\"read_plane\":"; WriteReadPlane(output, dataset.readPlane);
    output << '}';
}

void WriteSearchRun(std::ostream& output, const SearchRun& run) {
    output << "{\"dataset_size\":" << run.scale << ",\"query_class\":";
    WriteJsonString(output, run.queryClass);
    output << ",\"query\":"; WriteJsonString(output, run.query);
    output << ",\"temperature\":"; WriteJsonString(output, run.temperature);
    output << ",\"iteration\":" << run.iteration << ",\"result_count\":" << run.resultCount
           << ",\"candidate_count\":" << run.candidateCount << ",\"candidates_visited\":"
           << run.candidatesVisited << ",\"truncated\":" << (run.truncated ? "true" : "false")
           << ",\"cancelled\":" << (run.cancelled ? "true" : "false")
           << ",\"unavailable\":" << (run.unavailable ? "true" : "false")
           << ",\"wire_result_count\":" << run.wireResultCount
           << ",\"wire_truncated\":" << (run.wireTruncated ? "true" : "false")
           << ",\"filename_index_used\":" << (run.filenameIndexUsed ? "true" : "false")
           << ",\"filename_index_candidate_count\":" << run.filenameIndexCandidateCount;
    output << ",\"search_generation\":" << run.searchGeneration
           << ",\"query_visible_epoch\":" << run.queryVisibleEpoch
           << ",\"dependency_set_epoch\":" << run.dependencySetEpoch
           << ",\"generation_restarts\":" << run.generationRestarts
           << ",\"relevant_invalidations\":" << run.relevantInvalidations
           << ",\"durable_fallback_attempts\":" << run.durableFallbackAttempts
           << ",\"durable_fallback_rows_streamed\":" << run.durableFallbackRowsStreamed
           << ",\"durable_fallback_used\":" << (run.durableFallbackUsed ? "true" : "false")
           << ",\"durable_fallback_succeeded\":" << (run.durableFallbackSucceeded ? "true" : "false")
           << ",\"state_lock_wait_ms\":";
    WriteJsonDouble(output, run.stateLockWaitMs);
    output << ",\"max_state_lock_hold_ms\":";
    WriteJsonDouble(output, run.maxStateLockHoldMs);
    output << ",\"peak_searches\":" << run.peakSearches
           << ",\"peak_durable_readers\":" << run.peakDurableReaders;
    output << ",\"sqlite_ms\":"; WriteJsonDouble(output, run.sqliteMs);
    output << ",\"projection_traversal_ms\":"; WriteJsonDouble(output, run.projectionTraversalMs);
    output << ",\"projection_enumeration_ms\":"; WriteJsonDouble(output, run.projectionEnumerationMs);
    output << ",\"name_pool_lookup_ms\":"; WriteJsonDouble(output, run.namePoolLookupMs);
    output << ",\"name_view_construction_ms\":"; WriteJsonDouble(output, run.nameViewConstructionMs);
    output << ",\"materialization_ms\":"; WriteJsonDouble(output, run.materializationMs);
    output << ",\"lightweight_candidate_materialization_ms\":";
    WriteJsonDouble(output, run.lightweightCandidateMaterializationMs);
    output << ",\"lazy_path_materialization_ms\":";
    WriteJsonDouble(output, run.lazyPathMaterializationMs);
    output << ",\"lazy_path_materialization_count\":" << run.lazyPathMaterializationCount;
    output << ",\"query_parse_ms\":"; WriteJsonDouble(output, run.queryParseMs);
    output << ",\"match_filter_sort_ms\":"; WriteJsonDouble(output, run.matchFilterSortMs);
    output << ",\"predicate_dispatch_ms\":"; WriteJsonDouble(output, run.predicateDispatchMs);
    output << ",\"substring_matching_ms\":"; WriteJsonDouble(output, run.substringMatchingMs);
    output << ",\"glob_matching_ms\":"; WriteJsonDouble(output, run.globMatchingMs);
    output << ",\"extension_matching_ms\":"; WriteJsonDouble(output, run.extensionMatchingMs);
    output << ",\"metadata_matching_ms\":"; WriteJsonDouble(output, run.metadataMatchingMs);
    output << ",\"folder_matching_ms\":"; WriteJsonDouble(output, run.folderMatchingMs);
    output << ",\"matched_row_handling_ms\":"; WriteJsonDouble(output, run.matchedRowHandlingMs);
    output << ",\"response_materialization_ms\":"; WriteJsonDouble(output, run.responseMaterializationMs);
    output << ",\"response_serialization_ms\":"; WriteJsonDouble(output, run.responseSerializationMs);
    output << ",\"response_parse_ms\":"; WriteJsonDouble(output, run.responseParseMs);
    output << ",\"ui_result_materialization_ms\":"; WriteJsonDouble(output, run.uiResultMaterializationMs);
    output << ",\"total_ms\":"; WriteJsonDouble(output, run.totalMs);
    output << ",\"engine_and_response_ms\":"; WriteJsonDouble(output, run.engineAndResponseMs);
    output << ",\"projection_entries_visited\":" << run.projectionEntriesVisited;
            output << ",\"projection_rows_examined\":" << run.projectionRowsExamined;
            output << ",\"hot_rows_examined\":" << run.hotRowsExamined;
            output << ",\"cold_rows_examined\":" << run.coldRowsExamined;
            output << ",\"cold_volumes_searched\":" << run.coldVolumesSearched;
            output << ",\"store_stream_ms\":" << run.storeStreamMs;
    output << ",\"name_pool_lookups\":" << run.namePoolLookups;
    output << ",\"empty_names_rejected\":" << run.emptyNamesRejected;
    output << ",\"name_views_constructed\":" << run.nameViewsConstructed;
    output << ",\"query_predicate_evaluations\":" << run.queryPredicateEvaluations;
    output << ",\"query_term_evaluations\":" << run.queryTermEvaluations;
    output << ",\"case_insensitive_comparisons\":" << run.caseInsensitiveComparisons;
    output << ",\"case_fold_operations\":" << run.caseFoldOperations;
    output << ",\"substring_searches\":" << run.substringSearches;
    output << ",\"glob_matches\":" << run.globMatches;
    output << ",\"prefix_checks\":" << run.prefixChecks;
    output << ",\"extension_predicates\":" << run.extensionPredicates;
    output << ",\"metadata_predicates\":" << run.metadataPredicates;
    output << ",\"early_rejects\":" << run.earlyRejects;
    output << ",\"predicate_matches\":" << run.predicateMatches;
    output << ",\"path_needed_decisions\":" << run.pathNeededDecisions;
    output << ",\"path_lookups\":" << run.pathLookups;
    output << ",\"matched_rows\":" << run.matchedRows;
    output << ",\"candidate_objects_materialized\":" << run.candidateObjectsMaterialized;
    output << ",\"candidate_name_copies\":" << run.candidateNameCopies;
    output << ",\"search_result_objects_materialized\":" << run.searchResultObjectsMaterialized;
    output << ",\"wire_bytes\":" << run.wireBytes << ",\"memory\":";
    WriteMemory(output, run.memory);
    output << '}';
}

void WriteSummary(std::ostream& output, const SearchSummary& summary) {
    output << "{\"dataset_size\":" << summary.scale << ",\"query_class\":";
    WriteJsonString(output, summary.queryClass);
    output << ",\"result_count\":" << summary.resultCount << ",\"min_ms\":";
    WriteJsonDouble(output, summary.minMs);
    output << ",\"median_ms\":"; WriteJsonDouble(output, summary.medianMs);
    output << ",\"p90_ms\":"; WriteJsonDouble(output, summary.p90Ms);
    output << ",\"max_ms\":"; WriteJsonDouble(output, summary.maxMs);
    output << '}';
}

void WriteIncremental(std::ostream& output, const IncrementalRow& row) {
    output << "{\"dataset_size\":" << row.scale << ",\"query_step\":";
    WriteJsonString(output, row.step);
    output << ",\"result_count\":" << row.resultCount << ",\"median_ms\":";
    WriteJsonDouble(output, row.medianMs);
    output << '}';
}

void WriteSnapshot(std::ostream& output, const SnapshotRun& run) {
    output << "{\"rows\":" << run.rows << ",\"serialized_bytes\":" << run.serializedBytes
           << ",\"publish_succeeded\":" << (run.publishSucceeded ? "true" : "false")
           << ",\"oversize\":" << (run.oversize ? "true" : "false")
           << ",\"generation_before\":" << run.generationBefore << ",\"generation_after\":"
           << run.generationAfter << ",\"model_build_ms\":";
    WriteJsonDouble(output, run.modelBuildMs);
    output << ",\"serialization_ms\":"; WriteJsonDouble(output, run.serializationMs);
    output << ",\"publish_copy_and_commit_ms\":"; WriteJsonDouble(output, run.publishCopyAndCommitMs);
    output << ",\"total_ms\":"; WriteJsonDouble(output, run.totalMs);
    output << ",\"ui_mapping_and_parsing_ms\":"; WriteJsonDouble(output, run.uiMappingAndParsingMs);
    output << ",\"ui_parsed_rows\":" << run.uiParsedRows;
    output << ",\"memory_before\":"; WriteMemory(output, run.memoryBefore);
    output << ",\"memory_after_model\":"; WriteMemory(output, run.memoryAfterModel);
    output << ",\"memory_after_publish\":"; WriteMemory(output, run.memoryAfterPublish);
    output << ",\"memory_after_ui\":"; WriteMemory(output, run.memoryAfterUi);
    output << ",\"memory_after\":";
    WriteMemory(output, run.memoryAfter);
    output << '}';
}

// ---------------------------------------------------------------------------
// performance-closure-and-multisurface-acceleration 8.1: projection-memory
// decomposition mode.
//
// Method (emitted in the JSON, not only in prose):
//   * process private bytes and working set from GetProcessMemoryInfo;
//   * a working-set page-class split (shareable vs private) from
//     QueryWorkingSet + QueryWorkingSetEx;
//   * requested/usable live and cumulative allocation bytes and counts from
//     the benchmark-scoped operator-new probe (bench_allocator_probe.h);
//   * the projection's own category capacities from
//     Projection::GetMemoryBreakdown -- live capacity measurements, never a
//     sizeof-only estimate;
//   * repeated same-process rebuild cycles plus a close/reopen/rebuild cycle
//     for plateau and return-to-baseline evidence;
//   * a counterbalanced pooled-vs-owning export/publication weighing on the
//     same projection generation and the same bounded directory set.
// ---------------------------------------------------------------------------

struct WorkingSetSplit {
    bool available = false;
    uint64_t totalBytes = 0;
    uint64_t shareableBytes = 0;
    uint64_t privateBytes = 0;
    uint64_t pages = 0;
    uint64_t pageSizeBytes = 0;
};

WorkingSetSplit ReadWorkingSetSplit() {
    WorkingSetSplit split;
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    split.pageSizeBytes = systemInfo.dwPageSize != 0 ? systemInfo.dwPageSize : 4096;

    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters))) {
        return split;
    }
    // QueryWorkingSet fills the caller's array with the resident page base
    // addresses; a zero-initialized array beyond the returned count stays
    // null, which is how the count is recovered. QueryWorkingSetEx then
    // classifies each resident page as shareable (mapped file / section,
    // including the published generation) or private.
    size_t capacity = static_cast<size_t>(counters.WorkingSetSize / split.pageSizeBytes) + 65536;
    std::vector<PVOID> pages;
    std::vector<PSAPI_WORKING_SET_EX_INFORMATION> extended;
    for (int attempt = 0; attempt < 6; ++attempt) {
        pages.assign(capacity, nullptr);
        if (!QueryWorkingSet(GetCurrentProcess(), pages.data(),
                             static_cast<DWORD>(pages.size() * sizeof(PVOID)))) {
            capacity *= 2;
            continue;
        }
        size_t count = 0;
        while (count < pages.size() && pages[count] != nullptr) ++count;
        extended.assign(count, PSAPI_WORKING_SET_EX_INFORMATION{});
        for (size_t index = 0; index < count; ++index) {
            extended[index].VirtualAddress = pages[index];
        }
        if (count != 0
            && !QueryWorkingSetEx(GetCurrentProcess(), extended.data(),
                                  static_cast<DWORD>(extended.size() * sizeof(extended[0])))) {
            break;
        }
        split.available = true;
        for (const auto& entry : extended) {
            if (!entry.VirtualAttributes.Valid) continue;
            ++split.pages;
            split.totalBytes += split.pageSizeBytes;
            if (entry.VirtualAttributes.Shared) {
                split.shareableBytes += split.pageSizeBytes;
            } else {
                split.privateBytes += split.pageSizeBytes;
            }
        }
        return split;
    }
    return split;
}

uint64_t Fnv1a64(const std::vector<uint8_t>& bytes) {
    uint64_t hash = 1469598103934665603ull;
    for (const uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

struct MemoryCapturePoint {
    std::string name;
    MemorySample memory;
    uint64_t peakWorkingSetBytes = 0;
    uint64_t peakPrivateBytes = 0;
    WorkingSetSplit workingSetSplit;
    ffbench::AllocatorSnapshot allocator;
    double elapsedMs = 0.0;
    bool hasProjection = false;
    ffindexstore::Projection::MemoryBreakdown breakdown;
    size_t estimatedBytes = 0;
};

MemoryCapturePoint CaptureMemoryPoint(const std::string& name, const double elapsedMs) {
    MemoryCapturePoint point;
    point.name = name;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        point.memory = {static_cast<uint64_t>(counters.WorkingSetSize),
                        static_cast<uint64_t>(counters.PrivateUsage)};
        point.peakWorkingSetBytes = counters.PeakWorkingSetSize;
        point.peakPrivateBytes = counters.PeakPagefileUsage;
    }
    point.workingSetSplit = ReadWorkingSetSplit();
    point.allocator = ffbench::ReadAllocatorSnapshot();
    point.elapsedMs = elapsedMs;
    return point;
}

MemoryCapturePoint CaptureProjectionPoint(const std::string& name, ffengine::IndexPipeline& pipeline,
                                          const double elapsedMs) {
    MemoryCapturePoint point = CaptureMemoryPoint(name, elapsedMs);
    point.breakdown = pipeline.GetProjectionMemoryBreakdown();
    point.estimatedBytes = pipeline.GetProjectionMemoryDiagnostics().estimatedBytes;
    point.hasProjection = true;
    return point;
}

struct ExportArmPass {
    size_t directories = 0;
    size_t entries = 0;
    size_t serializedBytes = 0;
    size_t publishReportedSerializedBytes = 0;
    uint64_t payloadHash = 0;
    double exportMs = 0.0;
    double explicitSerializeMs = 0.0;
    double publishTotalMs = 0.0;
    double publishCopyAndCommitMs = 0.0;
    double publishSerializationMs = 0.0;
    uint64_t generation = 0;
    bool publishSucceeded = false;
    MemorySample memoryBefore;
    MemorySample memoryAfterExport;
    MemorySample memoryAfterPublish;
    ffbench::AllocatorSnapshot allocatorBefore;
    ffbench::AllocatorSnapshot allocatorAfterExport;
    ffbench::AllocatorSnapshot allocatorAfterPublish;
    long long retainedExportLiveRequestedBytes = 0;
    unsigned long long retainedExportLiveUsableBytes = 0;
    unsigned long long exportAllocationCount = 0;
    unsigned long long armTotalRequestedBytes = 0;
    unsigned long long armAllocationCount = 0;
};

struct ExportArm {
    std::string name;
    bool executed = false;
    std::vector<ExportArmPass> passes;
};

ExportArmPass RunPooledExportPass(ffengine::IndexPipeline& pipeline, const ffindexstore::VolumeRowId volumeId,
                                  const size_t boundDirectories,
                                  ffengine::SnapshotPublisher& publisher) {
    ExportArmPass pass;
    pass.memoryBefore = ReadMemory();
    pass.allocatorBefore = ffbench::ReadAllocatorSnapshot();

    const auto exportStart = Clock::now();
    ffprotocol::PooledSnapshotMap snapshot =
        pipeline.ExportDirectorySnapshotBoundedPooled(volumeId, L"C:", boundDirectories);
    pass.exportMs = ElapsedMs(exportStart);
    pass.memoryAfterExport = ReadMemory();
    pass.allocatorAfterExport = ffbench::ReadAllocatorSnapshot();
    pass.retainedExportLiveRequestedBytes =
        ffbench::LiveRequestedDelta(pass.allocatorBefore, pass.allocatorAfterExport);
    pass.retainedExportLiveUsableBytes =
        ffbench::LiveUsableDelta(pass.allocatorBefore, pass.allocatorAfterExport);
    pass.exportAllocationCount = ffbench::AllocationsDelta(pass.allocatorBefore, pass.allocatorAfterExport);
    pass.directories = snapshot.size();
    for (const auto& [path, directory] : snapshot) {
        UNREFERENCED_PARAMETER(path);
        pass.entries += directory.entries.size();
    }

    const auto serializeStart = Clock::now();
    const std::vector<uint8_t> bytes = ffprotocol::SerializeSnapshot(snapshot);
    pass.explicitSerializeMs = ElapsedMs(serializeStart);
    pass.serializedBytes = bytes.size();
    pass.payloadHash = Fnv1a64(bytes);

    ffengine::SnapshotPublisher::PublishTimings timings;
    const auto publishStart = Clock::now();
    pass.generation = publisher.Publish(snapshot, &timings);
    pass.publishTotalMs = ElapsedMs(publishStart);
    pass.publishSucceeded = pass.generation != 0;
    pass.publishReportedSerializedBytes = timings.serializedBytes;
    pass.publishCopyAndCommitMs = timings.copyAndCommitMs;
    pass.publishSerializationMs = timings.serializationMs;
    pass.memoryAfterPublish = ReadMemory();
    pass.allocatorAfterPublish = ffbench::ReadAllocatorSnapshot();
    pass.armTotalRequestedBytes =
        ffbench::RequestedBytesDelta(pass.allocatorBefore, pass.allocatorAfterPublish);
    pass.armAllocationCount = ffbench::AllocationsDelta(pass.allocatorBefore, pass.allocatorAfterPublish);
    return pass;
}

ExportArmPass RunOwningExportPass(ffengine::IndexPipeline& pipeline, const ffindexstore::VolumeRowId volumeId,
                                  const size_t boundDirectories,
                                  ffengine::SnapshotPublisher& publisher) {
    ExportArmPass pass;
    pass.memoryBefore = ReadMemory();
    pass.allocatorBefore = ffbench::ReadAllocatorSnapshot();

    const auto exportStart = Clock::now();
    std::map<std::wstring, ffprotocol::SnapshotDirectory> snapshot =
        pipeline.ExportDirectorySnapshotBounded(volumeId, L"C:", boundDirectories);
    pass.exportMs = ElapsedMs(exportStart);
    pass.memoryAfterExport = ReadMemory();
    pass.allocatorAfterExport = ffbench::ReadAllocatorSnapshot();
    pass.retainedExportLiveRequestedBytes =
        ffbench::LiveRequestedDelta(pass.allocatorBefore, pass.allocatorAfterExport);
    pass.retainedExportLiveUsableBytes =
        ffbench::LiveUsableDelta(pass.allocatorBefore, pass.allocatorAfterExport);
    pass.exportAllocationCount = ffbench::AllocationsDelta(pass.allocatorBefore, pass.allocatorAfterExport);
    pass.directories = snapshot.size();
    for (const auto& [path, directory] : snapshot) {
        UNREFERENCED_PARAMETER(path);
        pass.entries += directory.entries.size();
    }

    const auto serializeStart = Clock::now();
    const std::vector<uint8_t> bytes = ffprotocol::SerializeSnapshot(snapshot);
    pass.explicitSerializeMs = ElapsedMs(serializeStart);
    pass.serializedBytes = bytes.size();
    pass.payloadHash = Fnv1a64(bytes);

    ffengine::SnapshotPublisher::PublishTimings timings;
    const auto publishStart = Clock::now();
    pass.generation = publisher.Publish(snapshot, &timings);
    pass.publishTotalMs = ElapsedMs(publishStart);
    pass.publishSucceeded = pass.generation != 0;
    pass.publishReportedSerializedBytes = timings.serializedBytes;
    pass.publishCopyAndCommitMs = timings.copyAndCommitMs;
    pass.publishSerializationMs = timings.serializationMs;
    pass.memoryAfterPublish = ReadMemory();
    pass.allocatorAfterPublish = ffbench::ReadAllocatorSnapshot();
    pass.armTotalRequestedBytes =
        ffbench::RequestedBytesDelta(pass.allocatorBefore, pass.allocatorAfterPublish);
    pass.armAllocationCount = ffbench::AllocationsDelta(pass.allocatorBefore, pass.allocatorAfterPublish);
    return pass;
}

struct ScaleMemoryReport {
    size_t scale = 0;
    size_t directoryCount = 0;
    size_t fileCount = 0;
    size_t boundDirectories = 0;
    size_t rebuildCycleTarget = 0;
    uint64_t databaseBytes = 0;
    uint64_t volumeRowId = 0;
    std::wstring databaseLocation;
    double generationMs = 0.0;
    bool executed = false;
    std::string failureReason;
    std::vector<MemoryCapturePoint> captures;
    std::vector<MemoryCapturePoint> rebuildCycles;
    ExportArm pooledExport;
    ExportArm owningExport;
    bool payloadIdentical = false;
};

bool RunScaleMemoryReport(const size_t scale, const Options& options,
                          ffengine::SnapshotPublisher& publisher, ScaleMemoryReport& report) {
    report.scale = scale;
    report.directoryCount = (std::max)(size_t{1}, scale / 64);
    report.fileCount = scale > 1 + report.directoryCount ? scale - 1 - report.directoryCount : 0;
    report.boundDirectories = options.exportBoundDirectories;
    report.rebuildCycleTarget = options.rebuildCycles;

    const std::filesystem::path temporaryDirectory = MakeTemporaryDirectory(scale);
    if (temporaryDirectory.empty()) {
        report.failureReason = "temporary directory creation failed";
        return false;
    }
    const std::filesystem::path database = temporaryDirectory / L"index.db";
    report.databaseLocation = database.wstring();

    bool succeeded = false;
    do {
        report.captures.push_back(CaptureMemoryPoint("process_start", 0.0));
        MemorySample beforeFixture{};
        MemorySample afterFixture{};
        ffindexstore::VolumeRowId volumeId = 0;
        if (!BuildSyntheticDatabase(database, scale, report.generationMs, volumeId,
                                    beforeFixture, afterFixture, options.detailedDiagnostics)) {
            report.failureReason = "synthetic database generation failed";
            break;
        }
        report.databaseBytes = DatabaseBytes(database);
        report.volumeRowId = static_cast<uint64_t>(volumeId);
        report.captures.push_back(CaptureMemoryPoint("after_fixture_generation", 0.0));

        ffprotocol::VolumeSetting setting;
        setting.key = L"C:";
        setting.enabled = true;
        {
            ffengine::IndexPipeline pipeline;
            const auto openStart = Clock::now();
            if (!pipeline.Open(PathToUtf8(database))) {
                report.failureReason = "database open failed";
                break;
            }
            pipeline.SetVolumeRules(volumeId, setting);
            report.captures.push_back(
                CaptureProjectionPoint("after_database_open", pipeline, ElapsedMs(openStart)));

            for (size_t cycle = 1; cycle <= options.rebuildCycles; ++cycle) {
                const auto rebuildStart = Clock::now();
                pipeline.RebuildAll({});
                const double rebuildMs = ElapsedMs(rebuildStart);
                report.rebuildCycles.push_back(CaptureProjectionPoint(
                    "after_rebuild_cycle_" + std::to_string(cycle), pipeline, rebuildMs));
                if (options.detailedDiagnostics) {
                    const auto& point = report.rebuildCycles.back();
                    std::printf("  [%zu] rebuild cycle %zu: %.1f ms, private %.1f MB, live requested %.1f MB, "
                                "live allocations %llu\n",
                                scale, cycle, rebuildMs,
                                static_cast<double>(point.memory.privateBytes) / (1024.0 * 1024.0),
                                static_cast<double>(point.allocator.liveRequestedBytes) / (1024.0 * 1024.0),
                                point.allocator.liveAllocations);
                    std::fflush(stdout);
                }
            }

            // Counterbalanced export weighing: pooled (retained production path),
            // owning (reference path), owning, pooled.
            ExportArm pooledArm;
            pooledArm.name = "pooled-export-publication";
            ExportArm owningArm;
            owningArm.name = "owning-export-publication";
            for (int pass = 0; pass < 4; ++pass) {
                if (pass == 0 || pass == 3) {
                    pooledArm.passes.push_back(RunPooledExportPass(
                        pipeline, volumeId, options.exportBoundDirectories, publisher));
                } else {
                    owningArm.passes.push_back(RunOwningExportPass(
                        pipeline, volumeId, options.exportBoundDirectories, publisher));
                }
            }
            pooledArm.executed = true;
            owningArm.executed = true;
            report.payloadIdentical = !pooledArm.passes.empty() && !owningArm.passes.empty()
                && pooledArm.passes.front().payloadHash == owningArm.passes.front().payloadHash
                && pooledArm.passes.front().serializedBytes == owningArm.passes.front().serializedBytes;
            report.pooledExport = std::move(pooledArm);
            report.owningExport = std::move(owningArm);
            report.captures.push_back(CaptureMemoryPoint("after_export_weighing", 0.0));

            const auto closeStart = Clock::now();
            pipeline.Close();
            report.captures.push_back(CaptureMemoryPoint("after_pipeline_close", ElapsedMs(closeStart)));
        }
        report.captures.push_back(CaptureMemoryPoint("after_pipeline_release", 0.0));

        // Release-and-rebuild cycle: the same durable database is reopened in
        // the same process and rebuilt once more, then closed again. A
        // justified baseline means this repeat does not raise live allocations
        // or private bytes above the first generation's plateau.
        ffengine::IndexPipeline reopened;
        const auto reopenStart = Clock::now();
        if (!reopened.Open(PathToUtf8(database))) {
            report.failureReason = "reopen after close failed";
            break;
        }
        reopened.SetVolumeRules(volumeId, setting);
        reopened.RebuildAll({});
        report.captures.push_back(CaptureProjectionPoint("after_close_reopen_rebuild", reopened,
                                                          ElapsedMs(reopenStart)));
        const auto secondCloseStart = Clock::now();
        reopened.Close();
        report.captures.push_back(CaptureMemoryPoint("after_close_reopen_close", ElapsedMs(secondCloseStart)));
        succeeded = true;
    } while (false);

    std::error_code ignored;
    std::filesystem::remove_all(temporaryDirectory, ignored);
    report.executed = succeeded;
    return succeeded;
}

void WriteAllocator(std::ostream& output, const ffbench::AllocatorSnapshot& snapshot) {
    output << "{\"live_requested_bytes\":" << snapshot.liveRequestedBytes
           << ",\"live_usable_bytes\":" << snapshot.liveUsableBytes
           << ",\"live_rounding_overhead_bytes\":" << snapshot.liveRoundingOverheadBytes()
           << ",\"live_allocations\":" << snapshot.liveAllocations
           << ",\"total_allocations\":" << snapshot.totalAllocations
           << ",\"total_requested_bytes\":" << snapshot.totalRequestedBytes
           << ",\"peak_live_requested_bytes\":" << snapshot.peakLiveRequestedBytes << '}';
}

void WriteBreakdown(std::ostream& output, const ffindexstore::Projection::MemoryBreakdown& b) {
    output << "{\"entry_count\":" << b.entryCount
           << ",\"row_capacity\":" << b.rowCapacity
           << ",\"row_bytes\":" << b.rowBytes
           << ",\"row_live_bytes\":" << b.rowLiveBytes
           << ",\"row_slack_bytes\":" << b.rowSlackBytes
           << ",\"free_list_slots\":" << b.freeListSlots
           << ",\"free_list_bytes\":" << b.freeListBytes
           << ",\"id_map_elements\":" << b.idMapElements
           << ",\"id_map_buckets\":" << b.idMapBuckets
           << ",\"id_map_key_bytes\":" << b.idMapKeyBytes
           << ",\"id_map_value_bytes\":" << b.idMapValueBytes
           << ",\"id_map_state_bytes\":" << b.idMapStateBytes
           << ",\"id_map_bytes\":" << b.idMapBytes
           << ",\"child_map_elements\":" << b.childMapElements
           << ",\"child_map_buckets\":" << b.childMapBuckets
           << ",\"child_map_child_links\":" << b.childMapChildLinks
           << ",\"child_map_key_bytes\":" << b.childMapKeyBytes
           << ",\"child_map_child_bytes\":" << b.childMapChildBytes
           << ",\"child_map_state_bytes\":" << b.childMapStateBytes
           << ",\"child_map_offset_bytes\":" << b.childMapOffsetBytes
           << ",\"child_map_count_bytes\":" << b.childMapCountBytes
           << ",\"child_map_bytes\":" << b.childMapBytes
           << ",\"unique_name_count\":" << b.uniqueNameCount
           << ",\"name_arena_chars\":" << b.nameArenaChars
           << ",\"name_arena_capacity_chars\":" << b.nameArenaCapacityChars
           << ",\"name_arena_bytes\":" << b.nameArenaBytes
           << ",\"name_arena_slack_bytes\":" << b.nameArenaSlackBytes
           << ",\"name_table_slot_count\":" << b.nameTableSlotCount
           << ",\"name_table_bytes\":" << b.nameTableBytes
           << ",\"name_hash_bucket_count\":" << b.nameHashBucketCount
           << ",\"name_hash_bucket_bytes\":" << b.nameHashBucketBytes
           << ",\"name_hash_node_count\":" << b.nameHashNodeCount
           << ",\"name_hash_approx_node_bytes\":" << b.nameHashApproxNodeBytes
           << ",\"name_hash_payload_bytes\":" << b.nameHashPayloadBytes
           << ",\"name_bytes\":" << b.nameBytes
           << ",\"filename_index_valid\":" << (b.filenameIndexValid ? "true" : "false")
           << ",\"filename_index_distinct_trigrams\":" << b.filenameIndexDistinctTrigrams
           << ",\"filename_index_posting_slots\":" << b.filenameIndexPostingSlots
           << ",\"filename_index_posting_payload\":" << b.filenameIndexPostingPayload
           << ",\"filename_index_posting_bytes\":" << b.filenameIndexPostingBytes
           << ",\"filename_index_posting_header_bytes\":" << b.filenameIndexPostingHeaderBytes
           << ",\"filename_index_bucket_count\":" << b.filenameIndexBucketCount
           << ",\"filename_index_bucket_bytes\":" << b.filenameIndexBucketBytes
           << ",\"filename_index_approx_node_bytes\":" << b.filenameIndexApproxNodeBytes
           << ",\"filename_index_exceptional_count\":" << b.filenameIndexExceptionalCount
           << ",\"filename_index_exceptional_slots\":" << b.filenameIndexExceptionalSlots
           << ",\"filename_index_exceptional_bytes\":" << b.filenameIndexExceptionalBytes
           << ",\"filename_index_bytes\":" << b.filenameIndexBytes
           << ",\"lru_slot_capacity\":" << b.lruSlotCapacity
           << ",\"lru_slot_count\":" << b.lruSlotCount
           << ",\"lru_tracked_count\":" << b.lruTrackedCount
           << ",\"lru_bytes\":" << b.lruBytes
           << ",\"evicted_volume_count\":" << b.evictedVolumeCount
           << ",\"evicted_volume_bucket_count\":" << b.evictedVolumeBucketCount
           << ",\"evicted_volume_bucket_bytes\":" << b.evictedVolumeBucketBytes
           << ",\"normalized_name_storage_bytes\":" << b.normalizedNameStorageBytes
           << ",\"stored_path_bytes\":" << b.storedPathBytes
           << ",\"modeled_category_bytes\":" << b.modeledCategoryBytes
           << ",\"reported_total_bytes\":" << b.reportedTotalBytes << '}';
}

void WriteCapturePoint(std::ostream& output, const MemoryCapturePoint& point) {
    output << "{\"name\":";
    WriteJsonString(output, point.name);
    output << ",\"elapsed_ms\":";
    WriteJsonDouble(output, point.elapsedMs);
    output << ",\"working_set_bytes\":" << point.memory.workingSetBytes
           << ",\"private_bytes\":" << point.memory.privateBytes
           << ",\"peak_working_set_bytes\":" << point.peakWorkingSetBytes
           << ",\"peak_private_bytes\":" << point.peakPrivateBytes
           << ",\"allocator\":";
    WriteAllocator(output, point.allocator);
    output << ",\"working_set_split\":{\"available\":"
           << (point.workingSetSplit.available ? "true" : "false")
           << ",\"page_size_bytes\":" << point.workingSetSplit.pageSizeBytes
           << ",\"pages\":" << point.workingSetSplit.pages
           << ",\"total_bytes\":" << point.workingSetSplit.totalBytes
           << ",\"shareable_bytes\":" << point.workingSetSplit.shareableBytes
           << ",\"private_bytes\":" << point.workingSetSplit.privateBytes << '}';
    if (point.hasProjection) {
        output << ",\"estimator_bytes\":" << point.estimatedBytes << ",\"projection\":";
        WriteBreakdown(output, point.breakdown);
    }
    output << '}';
}

void WriteExportArmPass(std::ostream& output, const ExportArmPass& pass) {
    output << "{\"directories\":" << pass.directories << ",\"entries\":" << pass.entries
           << ",\"serialized_bytes\":" << pass.serializedBytes
           << ",\"publish_reported_serialized_bytes\":" << pass.publishReportedSerializedBytes
           << ",\"payload_fnv1a64\":" << pass.payloadHash
           << ",\"export_ms\":";
    WriteJsonDouble(output, pass.exportMs);
    output << ",\"explicit_serialize_ms\":";
    WriteJsonDouble(output, pass.explicitSerializeMs);
    output << ",\"publish_total_ms\":";
    WriteJsonDouble(output, pass.publishTotalMs);
    output << ",\"publish_copy_and_commit_ms\":";
    WriteJsonDouble(output, pass.publishCopyAndCommitMs);
    output << ",\"publish_serialization_ms\":";
    WriteJsonDouble(output, pass.publishSerializationMs);
    output << ",\"publish_succeeded\":" << (pass.publishSucceeded ? "true" : "false")
           << ",\"generation\":" << pass.generation
           << ",\"retained_export_live_requested_bytes\":" << pass.retainedExportLiveRequestedBytes
           << ",\"retained_export_live_usable_bytes\":" << pass.retainedExportLiveUsableBytes
           << ",\"export_allocation_count\":" << pass.exportAllocationCount
           << ",\"arm_total_requested_bytes\":" << pass.armTotalRequestedBytes
           << ",\"arm_allocation_count\":" << pass.armAllocationCount
           << ",\"memory_before\":";
    WriteMemory(output, pass.memoryBefore);
    output << ",\"memory_after_export\":";
    WriteMemory(output, pass.memoryAfterExport);
    output << ",\"memory_after_publish\":";
    WriteMemory(output, pass.memoryAfterPublish);
    output << ",\"allocator_before\":";
    WriteAllocator(output, pass.allocatorBefore);
    output << ",\"allocator_after_export\":";
    WriteAllocator(output, pass.allocatorAfterExport);
    output << ",\"allocator_after_publish\":";
    WriteAllocator(output, pass.allocatorAfterPublish);
    output << '}';
}

void WriteExportArm(std::ostream& output, const ExportArm& arm) {
    output << "{\"name\":";
    WriteJsonString(output, arm.name);
    output << ",\"executed\":" << (arm.executed ? "true" : "false") << ",\"passes\":[";
    for (size_t index = 0; index < arm.passes.size(); ++index) {
        if (index != 0) output << ',';
        WriteExportArmPass(output, arm.passes[index]);
    }
    output << "]}";
}

void WriteScaleMemoryReport(std::ostream& output, const ScaleMemoryReport& report) {
    output << "{\"scale\":" << report.scale
           << ",\"directory_count\":" << report.directoryCount
           << ",\"file_count\":" << report.fileCount
           << ",\"bound_directories\":" << report.boundDirectories
           << ",\"rebuild_cycle_target\":" << report.rebuildCycleTarget
           << ",\"database_bytes\":" << report.databaseBytes
           << ",\"durable_volume_row_id\":" << report.volumeRowId
           << ",\"generation_ms\":";
    WriteJsonDouble(output, report.generationMs);
    output << ",\"executed\":" << (report.executed ? "true" : "false")
           << ",\"failure_reason\":";
    WriteJsonString(output, report.failureReason);
    output << ",\"payload_identical\":" << (report.payloadIdentical ? "true" : "false")
           << ",\"rebuild_cycles\":[";
    for (size_t index = 0; index < report.rebuildCycles.size(); ++index) {
        if (index != 0) output << ',';
        WriteCapturePoint(output, report.rebuildCycles[index]);
    }
    output << "],\"captures\":[";
    for (size_t index = 0; index < report.captures.size(); ++index) {
        if (index != 0) output << ',';
        WriteCapturePoint(output, report.captures[index]);
    }
    output << "],\"pooled_export\":";
    WriteExportArm(output, report.pooledExport);
    output << ",\"owning_export\":";
    WriteExportArm(output, report.owningExport);
    output << '}';
}

int RunMemoryReportMode(const Options& options) {
    ffengine::SnapshotPublisher publisher;
    if (!publisher.Start(GetCurrentProcessId())) {
        std::fprintf(stderr, "snapshot publisher start failed\n");
        return 1;
    }
    std::vector<ScaleMemoryReport> reports;
    bool allSucceeded = true;
    for (const size_t scale : options.scales) {
        std::printf("Projection-memory decomposition: building %zu synthetic entries...\n", scale);
        std::fflush(stdout);
        ScaleMemoryReport report;
        if (!RunScaleMemoryReport(scale, options, publisher, report)) {
            allSucceeded = false;
            std::fprintf(stderr, "memory decomposition failed at %zu: %s\n", scale,
                         report.failureReason.c_str());
        }
        reports.push_back(std::move(report));
    }
    publisher.Stop();

    std::filesystem::path parent = options.memoryOutputPath.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
            std::fprintf(stderr, "unable to create memory report directory\n");
            return 1;
        }
    }
    std::ofstream output(options.memoryOutputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::fprintf(stderr, "unable to open memory report output: %s\n",
                     PathToUtf8(options.memoryOutputPath).c_str());
        return 1;
    }
    const MachineInfo machine = ReadMachineInfo();
    output << "{\n  \"schema\":\"fastfiles-projection-memory-decomposition-v1\"\n"
           << "  ,\"generated_epoch_seconds\":" << static_cast<long long>(std::time(nullptr)) << '\n'
           << "  ,\"build_identity\":\"" << ffsetup::kBuildIdentityAscii << "\"\n"
           << "  ,\"build_configuration\":";
    WriteJsonString(output, machine.buildConfiguration);
    output << "\n  ,\"process_id\":" << GetCurrentProcessId()
           << "\n  ,\"requested_scales\":[";
    for (size_t index = 0; index < options.scales.size(); ++index) {
        if (index != 0) output << ',';
        output << options.scales[index];
    }
    output << "]";
    output << "\n  ,\"command_line\":";
    WriteJsonString(output, options.commandLine);
    output << "\n  ,\"source_state\":";
    WriteJsonString(output, options.sourceState);
    output << "\n  ,\"source_sha\":";
    WriteJsonString(output, options.sourceSha);
    output << "\n  ,\"preset\":";
    WriteJsonString(output, options.preset);
    output << "\n  ,\"units_are_binary\":true"
           << "\n  ,\"projection_memory_layout_revision\":"
           << ffindexstore::kProjectionMemoryLayoutRevision
           << "\n  ,\"allocator_probe_enabled\":true";
    output << "\n  ,\"rebuild_cycles_requested\":" << options.rebuildCycles
           << "\n  ,\"export_bound_directories\":" << options.exportBoundDirectories
           << "\n  ,\"harness_retention\":{\"capture_point_bytes\":" << sizeof(MemoryCapturePoint)
           << ",\"note\":\"each rebuild cycle and lifecycle capture retains one MemoryCapturePoint "
              "(plus its capture-name string) inside this report; that harness retention is part of the "
              "measured live-allocator delta and is reconciled against the projection category sums\"}"
           << "\n  ,\"machine\":";
    WriteMachine(output, machine);
    output << "\n  ,\"instrumentation\":{"
              "\"allocator_probe\":\"replaced global operator new/delete: live+cumulative requested and "
              "usable bytes, allocation counts, live peak; C++ allocations only (SQLite C allocations are "
              "outside the counter)\","
              "\"process_memory\":\"GetProcessMemoryInfo PROCESS_MEMORY_COUNTERS_EX (working set, private "
              "usage, peak working set, peak pagefile)\","
              "\"working_set_split\":\"QueryWorkingSet + QueryWorkingSetEx page classification (shareable "
              "vs private)\","
              "\"projection_categories\":\"Projection::GetMemoryBreakdown live capacity/size/bucket_count "
              "measurements; modeled_category_bytes equals Projection::EstimatedMemoryBytes\","
              "\"units\":\"bytes and milliseconds\"}\n"
           << "  ,\"limitations\":["
              "\"allocator counts cover C++ operator new/delete only; SQLite (compiled as C) allocates "
              "through malloc/free and appears in the private-bytes residual instead\","
              "\"standard-library node links, the probe's 32-byte allocation header, heap block headers and "
              "heap rounding are residual and are not attributed to any projection category\","
              "\"working-set shareable pages identify shareable/pagefile-backed pages; they are not proof "
              "that another process maps the same section\","
              "\"the export weighing uses the real IndexPipeline pooled/owning export and SnapshotPublisher "
              "publish APIs in one process; the UI-side parse here is the in-process published-section read, "
              "not the FastFiles UI process\","
              "\"timings in this mode are single-process measurements and are not the campaign's idle-gated "
              "latency protocol\","
              "\"this executable replaces the global operator new/delete in every mode, so any other mode of this benchmark binary is instrumented and must not be used as uninstrumented latency evidence\","
              "\"all byte quantities are binary (MiB = 1048576 bytes); allocator counts exclude the C-only SQLite allocation plane\"]\n"

           << "  ,\"scales\":[";
    for (size_t index = 0; index < reports.size(); ++index) {
        if (index != 0) output << ',';
        WriteScaleMemoryReport(output, reports[index]);
    }
    output << "]\n}\n";
    output.close();

    std::printf("Wrote %s\n", PathToUtf8(options.memoryOutputPath).c_str());
    return allSucceeded ? 0 : 1;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!ParseOptions(argc, argv, options)) return 2;

    // performance-closure-and-multisurface-acceleration 8.1: the memory mode is
    // a separate, focused path (fixture -> rebuild cycles -> export weighing ->
    // teardown) that writes its own machine-readable report.
    if (options.memoryReport) {
        if (options.memoryOutputPath.empty()) {
            std::fprintf(stderr, "--memory-report requires an output path\n");
            return 2;
        }
        return RunMemoryReportMode(options);
    }

    std::vector<DatasetResult> datasets;
    std::vector<SearchRun> searchRuns;
    std::vector<SearchSummary> searchSummaries;
    std::vector<IncrementalRow> incremental;

    const std::vector<QueryCase> queryCases{
        {"selective", L"unique_component_7391", 0},
        {"moderate", L"report", 0},
        {"broad", L"common", 0},
        {"no_match", L"zzzz_never_present", 0},
        {"prefix", L"photo", 0},
        {"extension", L"ext:txt", 3},
        {"worst_substring", L"component", 3},
        // performance-closure-and-multisurface-acceleration 7.1 additions:
        {"exact_name", L"unique_component_7391.txt", 0},
        {"short_prefix", L"co", 0},
        {"mixed_case", L"CoMmOn_CoMpOnEnT", 0},
        {"unicode", L"\u0441\u043f\u0438\u0441\u043e\u043a", 0},
        {"high_limit_broad", L"common", 0, 50000},
    };

    ffengine::SnapshotPublisher publisher;
    if (!publisher.Start(GetCurrentProcessId())) {
        std::fprintf(stderr, "snapshot publisher start failed\n");
        return 1;
    }
    std::printf("FastFiles large-index scalability benchmark (%zu dataset scales)\n", options.scales.size());
    std::fflush(stdout);
    for (const size_t scale : options.scales) {
        std::printf("Building %zu synthetic entries...\n", scale);
        std::fflush(stdout);
        const std::filesystem::path temporaryDirectory = MakeTemporaryDirectory(scale);
        if (temporaryDirectory.empty()) {
            std::fprintf(stderr, "unable to create temporary directory for %zu\n", scale);
            return 1;
        }
        const std::filesystem::path database = temporaryDirectory / L"index.db";
        DatasetResult dataset;
        dataset.scale = scale;
        dataset.databaseLocation = database.wstring();
        ffindexstore::VolumeRowId volumeId = 0;
        if (!BuildSyntheticDatabase(database, scale, dataset.generationMs, volumeId,
                                     dataset.beforeFixture, dataset.afterFixture,
                                     options.detailedDiagnostics)) {
            std::fprintf(stderr, "synthetic database generation failed at %zu\n", scale);
            std::error_code ignored;
            std::filesystem::remove_all(temporaryDirectory, ignored);
            return 1;
        }
        dataset.databaseBytes = DatabaseBytes(database);

        ffengine::IndexPipeline pipeline;
        if (!LoadPipeline(database, pipeline, volumeId, dataset.rebuildMs,
                           dataset.afterDatabaseOpen, dataset.afterRebuild,
                           dataset.projectionEstimatedBytes,
                           options.detailedDiagnostics, scale)) {
            std::fprintf(stderr, "synthetic database reload failed at %zu\n", scale);
            std::error_code ignored;
            std::filesystem::remove_all(temporaryDirectory, ignored);
            return 1;
        }
        dataset.resourcesAfterRebuild = ReadResources(database);

        {
            if (options.detailedDiagnostics) {
                std::printf("  [%zu] snapshot export/publication start\n", scale);
                std::fflush(stdout);
            }
            const auto exportStart = Clock::now();
            const auto actualSnapshot = pipeline.ExportDirectorySnapshot(volumeId, L"C:");
            dataset.snapshotExportMs = ElapsedMs(exportStart);
            dataset.actualSnapshotDirectoryCount = actualSnapshot.size();
            for (const auto& [path, directory] : actualSnapshot) {
                UNREFERENCED_PARAMETER(path);
                dataset.actualSnapshotEntryCount += directory.entries.size();
            }
            ffengine::SnapshotPublisher::PublishTimings publishTimings;
            dataset.actualSnapshotPublishSucceeded =
                publisher.Publish(actualSnapshot, &publishTimings) != 0;
            dataset.actualSnapshotBytes = publishTimings.serializedBytes;
            dataset.actualSnapshotSerializationMs = publishTimings.serializationMs;
            dataset.actualSnapshotPublishCopyAndCommitMs = publishTimings.copyAndCommitMs;
            if (dataset.actualSnapshotPublishSucceeded) {
                dataset.actualSnapshotUiMappingAndParsingMs =
                    ReadPublishedSnapshot(publisher.CurrentSectionName()).elapsedMs;
            }
            if (options.detailedDiagnostics) {
                std::printf("  [%zu] snapshot export/publication complete: export %.2f ms, "
                            "serialize %.2f ms, publish %.2f ms, UI map/parse %.2f ms, bytes %zu, %s\n",
                            scale, dataset.snapshotExportMs, dataset.actualSnapshotSerializationMs,
                            dataset.actualSnapshotPublishCopyAndCommitMs,
                            dataset.actualSnapshotUiMappingAndParsingMs, dataset.actualSnapshotBytes,
                            dataset.actualSnapshotPublishSucceeded ? "published" : "rejected/failed");
                std::fflush(stdout);
            }
        }

        if (options.detailedDiagnostics) {
            const QueryCase projectionProbe{"projection_probe", L"zzzz_never_present", 0};
            const SearchOutcome probe = RunSearch(pipeline, scale, projectionProbe, 0,
                                                  "diagnostic", false, true);
            searchRuns.push_back(probe.run);
            std::printf("  projection probe   %.2f ms, rows %zu\n", probe.run.totalMs,
                        probe.run.projectionRowsExamined);
            std::fflush(stdout);
        }

        for (size_t caseIndex = 0; caseIndex < queryCases.size(); ++caseIndex) {
            const QueryCase& queryCase = queryCases[caseIndex];
            const bool keyCell = std::string_view(queryCase.name) == "selective" ||
                std::string_view(queryCase.name) == "exact_name" ||
                std::string_view(queryCase.name) == "no_match";
            const size_t warmRuns = options.searchAcceptanceOnly && !keyCell ? 1 :
                (scale >= kLargeSearchThreshold ? kWarmRunsLarge : kWarmRunsNormal);
            if (caseIndex == 0) {
                const SearchOutcome first = RunSearch(pipeline, scale, queryCase, 0,
                                                      "cold-ish-first-after-rebuild", options.detailedDiagnostics);
                searchRuns.push_back(first.run);
            } else if (!options.searchAcceptanceOnly || keyCell) {
                RunSearch(pipeline, scale, queryCase, 0, "warmup", options.detailedDiagnostics);
                RunSearch(pipeline, scale, queryCase, 1, "warmup", options.detailedDiagnostics);
            }
            for (size_t iteration = 0; iteration < warmRuns; ++iteration) {
                const SearchOutcome measured = RunSearch(pipeline, scale, queryCase, iteration, "warm",
                                                         options.detailedDiagnostics);
                searchRuns.push_back(measured.run);
            }
            AddSearchSummary(searchSummaries, scale, queryCase, searchRuns);
            std::printf("  %-18s median %.2f ms, results %zu\n", queryCase.name,
                        searchSummaries.back().medianMs, searchSummaries.back().resultCount);
            std::fflush(stdout);
            if (std::string_view(queryCase.name) == "broad") dataset.afterBroadQuery = ReadMemory();
        }

        // memory-bounded-indexed-scale 7.1: forced-eviction cold stage. The
        // whole volume is unloaded (durable data untouched) so every query
        // class streams from SQLite -- the bounded-memory search path.
        {
            const auto beforeUnload = pipeline.GetProjectionMemoryDiagnostics();
            pipeline.UnloadVolumeFromProjection(volumeId);
            const auto afterUnload = pipeline.GetProjectionMemoryDiagnostics();
            std::printf("  forced eviction: estimated %zu -> %zu bytes\n",
                        beforeUnload.estimatedBytes, afterUnload.estimatedBytes);
            std::fflush(stdout);
            for (const auto& queryCase : queryCases) {
                if (!options.searchAcceptanceOnly) {
                    RunSearch(pipeline, scale, queryCase, 0, "warmup", options.detailedDiagnostics);
                }
                const SearchOutcome measured = RunSearch(pipeline, scale, queryCase, 0, "cold",
                                                         options.detailedDiagnostics);
                searchRuns.push_back(measured.run);
                std::printf("  %-18s COLD %.2f ms, results %zu, coldRows %zu, stream %.2f ms\n",
                            queryCase.name, measured.run.totalMs, measured.run.resultCount,
                            measured.run.coldRowsExamined, measured.run.storeStreamMs);
                std::fflush(stdout);
            }
        }

        // search-availability-under-index-churn 7.1: deterministic sustained
        // relevant writes exceed the hybrid restart budget, then exercise the
        // durable-snapshot broad-search and cancellation paths. Keep this
        // evidence to the first requested scale so the default multi-million
        // row run remains useful without multiplying full-dataset churn.
        if (!options.searchAcceptanceOnly && scale == options.scales.front()) {
            const QueryCase sustainedCase{"sustained_write_broad", L"common", 0};
            std::atomic<size_t> validationCount{0};
            pipeline.SetSearchTestHooks({[&](const ffengine::IndexPipeline::SearchPhase phase) {
                if (phase != ffengine::IndexPipeline::SearchPhase::GenerationValidation) return;
                const size_t sequence = validationCount.fetch_add(1, std::memory_order_acq_rel);
                if (sequence < 4) {
                    pipeline.ApplyMftBatch(volumeId, {
                        MakeMftRecord(10'000'000 + sequence, 5,
                                      L"benchmark_sustained_churn_" + std::to_wstring(sequence), 0, 1, 1)
                    });
                }
            }});
            const SearchOutcome fallback = RunSearch(pipeline, scale, sustainedCase, 0,
                                                      "durable-fallback", options.detailedDiagnostics);
            pipeline.SetSearchTestHooks({});
            searchRuns.push_back(fallback.run);
            std::printf("  sustained-write broad fallback %.2f ms, results %zu, restarts %zu, rows %zu\n",
                        fallback.run.totalMs, fallback.run.resultCount,
                        fallback.run.generationRestarts, fallback.run.durableFallbackRowsStreamed);
            std::fflush(stdout);
            searchRuns.push_back(RunDurableFallbackCancellationProbe(
                pipeline, volumeId, scale, options.detailedDiagnostics));
        }

        if (!options.searchAcceptanceOnly && scale == 1'000'000) {
            const std::vector<std::wstring> steps{L"p", L"ph", L"pho", L"phot", L"photo"};
            for (const auto& step : steps) {
                QueryCase stepCase{"incremental", step.c_str(), 0};
                std::vector<double> values;
                size_t resultCount = 0;
                RunSearch(pipeline, scale, stepCase, 0, "warmup", options.detailedDiagnostics);
                for (size_t iteration = 0; iteration < 10; ++iteration) {
                    const SearchOutcome measured = RunSearch(pipeline, scale, stepCase, iteration, "warm",
                                                             options.detailedDiagnostics);
                    values.push_back(measured.run.totalMs);
                    resultCount = measured.run.resultCount;
                }
                incremental.push_back({scale, ToUtf8(step), resultCount, Percentile(values, 0.50)});
            }
        }
        // nonblocking-indexed-read-plane 7.2: hold one real cold search at
        // the durable-stream boundary, then exercise the writer, aggregate,
        // snapshot, second-search, and cancellation paths while that query
        // is intentionally in flight. This is evidence, not a pass/fail
        // timing gate; the booleans make lock monopolization visible.
        if (!options.searchAcceptanceOnly) {
            dataset.contention = RunContentionProbe(pipeline, volumeId, scale, options.detailedDiagnostics);
        }
        dataset.readPlane = pipeline.GetReadPlaneDiagnostics();
        dataset.postQuery = ReadMemory();
        dataset.resourcesPostQuery = ReadResources(database);
        datasets.push_back(dataset);
        pipeline.Close();
        if (options.detailedDiagnostics) {
            std::printf("  [%zu] cleanup start\n", scale);
            std::fflush(stdout);
        }
        std::error_code ignored;
        std::filesystem::remove_all(temporaryDirectory, ignored);
        if (options.detailedDiagnostics) {
            std::printf("  [%zu] cleanup complete\n", scale);
            std::fflush(stdout);
        }
        std::printf("Completed %zu; database %.1f MB, generation %.1f s, rebuild %.1f s\n",
                    scale, static_cast<double>(dataset.databaseBytes) / (1024.0 * 1024.0),
                    dataset.generationMs / 1000.0, dataset.rebuildMs / 1000.0);
        std::fflush(stdout);
    }

    std::vector<SnapshotRun> snapshotRuns;
    size_t largestRows = 0;
    if (!options.searchAcceptanceOnly) {
        for (const size_t rows : {size_t{100}, size_t{1'000}, size_t{10'000}, size_t{50'000},
                                  size_t{100'000}, size_t{250'000}, size_t{500'000}}) {
            snapshotRuns.push_back(RunSnapshot(rows, publisher, options.detailedDiagnostics));
        }
        largestRows = FindLargestSnapshotRows();
        for (const double fraction : {0.50, 0.75, 0.90, 1.00}) {
            const size_t rows = static_cast<size_t>(static_cast<double>(largestRows) * fraction);
            snapshotRuns.push_back(RunSnapshot(rows, publisher, options.detailedDiagnostics));
        }
        snapshotRuns.push_back(RunSnapshot(largestRows + 1, publisher, options.detailedDiagnostics));
    }
    publisher.Stop();

    std::filesystem::path parent = options.outputPath.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
            std::fprintf(stderr, "unable to create benchmark output directory\n");
            return 1;
        }
    }
    std::ofstream output(options.outputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::fprintf(stderr, "unable to open benchmark output: %s\n", PathToUtf8(options.outputPath).c_str());
        return 1;
    }
    const MachineInfo machine = ReadMachineInfo();
    size_t largestPublishedSnapshotRows = 0;
    for (const SnapshotRun& run : snapshotRuns) {
        if (run.publishSucceeded) {
            largestPublishedSnapshotRows = std::max(largestPublishedSnapshotRows, run.rows);
        }
    }
    output << "{\n  \"schema\":\"fastfiles-large-index-scalability-v2\",\n";
    // durable-cold-search-index-and-hybrid-residency 5.5 (campaign section
    // 14): authoritative source/executable identity is embedded in the
    // artifact itself. source_sha/source_state/preset are caller-asserted
    // and labeled as such; build_identity binds the binary to the source
    // tree (ffsetup identity, refreshed at build time).
    output << "  \"identity\":{";
    output << "\"source_sha\":"; WriteJsonString(output, options.sourceSha);
    output << ",\"source_state\":"; WriteJsonString(output, options.sourceState);
    output << ",\"source_state_asserted_by\":\"caller\"";
    output << ",\"preset\":"; WriteJsonString(output, options.preset);
    output << ",\"build_identity\":\"" << ffsetup::kBuildIdentityAscii << "\"";
    output << ",\"command_line\":"; WriteJsonString(output, options.commandLine);
    output << ",\"generated_epoch_seconds\":" << static_cast<long long>(std::time(nullptr));
    output << ",\"candidate_index_version\":3";
    output << ",\"search_acceptance_only\":" << (options.searchAcceptanceOnly ? "true" : "false");
    output << ",\"sampling_note\":";
    WriteJsonString(output, options.searchAcceptanceOnly ?
        "same fixture/RunSearch timing; key cells retain warmups/five samples at large scale; other classes one ordered sample; forced-cold one sample; no churn/contention/snapshot-capacity probes" :
        "legacy full sampling and ancillary probes");
#ifdef FFBENCH_NO_ALLOCATOR_PROBE
    output << ",\"allocator_probe_enabled\":false";
#else
    output << ",\"allocator_probe_enabled\":true";
#endif
    output << ",\"fixture_generation\":\"deterministic index-derived names (no RNG seed)\"";
    output << ",\"projection_memory_budget_bytes\":" << (500ull * 1024ull * 1024ull);
    output << "},\n  \"machine\":";
    WriteMachine(output, machine);
    output << ",\n  \"snapshot_maximum_payload_bytes\":" << ffprotocol::kSnapshotMaximumPayloadBytes
           << ",\n  \"snapshot_capacity_estimate_rows\":" << largestRows
           << ",\n  \"snapshot_largest_published_probe_rows\":" << largestPublishedSnapshotRows
           << ",\n  \"indexed_search_result_cap\":" << ffprotocol::kMaxIndexedSearchResultCount
           << ",\n  \"database_cleanup\":true,\n  \"datasets\":[\n";
    for (size_t index = 0; index < datasets.size(); ++index) {
        if (index != 0) output << ",\n";
        output << "    "; WriteDataset(output, datasets[index]);
    }
    output << "\n  ],\n  \"search_runs\":[\n";
    for (size_t index = 0; index < searchRuns.size(); ++index) {
        if (index != 0) output << ",\n";
        output << "    "; WriteSearchRun(output, searchRuns[index]);
    }
    output << "\n  ],\n  \"search_summaries\":[\n";
    for (size_t index = 0; index < searchSummaries.size(); ++index) {
        if (index != 0) output << ",\n";
        output << "    "; WriteSummary(output, searchSummaries[index]);
    }
    output << "\n  ],\n  \"incremental_search\":[\n";
    for (size_t index = 0; index < incremental.size(); ++index) {
        if (index != 0) output << ",\n";
        output << "    "; WriteIncremental(output, incremental[index]);
    }
    output << "\n  ],\n  \"snapshot_runs\":[\n";
    for (size_t index = 0; index < snapshotRuns.size(); ++index) {
        if (index != 0) output << ",\n";
        output << "    "; WriteSnapshot(output, snapshotRuns[index]);
    }
    output << "\n  ],\n  \"supersession\":{\"engine_cancellation\":true,\"ui_generation_discards_stale_completion\":true,\"old_work_completes\":false,\"regression_test\":\"ffengine_index_pipeline_tests\"}\n}\n";
    output.close();

    std::printf("Wrote %s\n", PathToUtf8(options.outputPath).c_str());
    std::printf("Largest published snapshot probe: %zu rows (theoretical capacity estimate %zu rows, maximum payload %zu bytes)\n",
                largestPublishedSnapshotRows, largestRows, ffprotocol::kSnapshotMaximumPayloadBytes);
    return 0;
}
