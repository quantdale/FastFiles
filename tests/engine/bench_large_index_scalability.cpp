// Informational benchmark only. This executable is intentionally not
// registered with CTest: large synthetic indexes are repeatable evidence, not
// a wall-clock pass/fail gate.

#include <windows.h>
#include <psapi.h>
#include <intrin.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "IndexPipeline.h"
#include "SnapshotPublisher.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t kBatchSize = 8192;
constexpr size_t kWarmRunsNormal = 10;
constexpr size_t kWarmRunsLarge = 5;
constexpr size_t kLargeSearchThreshold = 500'000;
constexpr size_t kCapacitySearchUpperBound = 1'000'000;

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
    std::vector<size_t> scales{10'000, 100'000, 500'000, 1'000'000};
    std::filesystem::path outputPath = std::filesystem::path(L"docs/benchmarks/large-index-scalability.json");
    bool detailedDiagnostics = false;
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
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--help") {
            std::wprintf(L"Usage: %s [--scales 10000,100000] [--output path] [--diagnostics]\n", argv[0]);
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
    if (pattern == 8) return L"dataset_" + std::to_wstring(index) + L".bin";
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
                            MemorySample& after) {
    before = ReadMemory();
    ffengine::IndexPipeline pipeline;
    if (!pipeline.Open(PathToUtf8(database))) return false;

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
    auto flush = [&] {
        if (batch.empty()) return true;
        const bool applied = pipeline.ApplyMftBatch(volumeId, batch);
        batch.clear();
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
    pipeline.Close();
    return true;
}

bool LoadPipeline(const std::filesystem::path& database, ffengine::IndexPipeline& pipeline,
                  ffindexstore::VolumeRowId volumeId, double& rebuildMs, MemorySample& afterOpen,
                  MemorySample& afterRebuild) {
    if (!pipeline.Open(PathToUtf8(database))) return false;
    afterOpen = ReadMemory();
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    pipeline.SetVolumeRules(volumeId, setting);
    const auto rebuildStart = Clock::now();
    pipeline.RebuildAll({});
    rebuildMs = ElapsedMs(rebuildStart);
    afterRebuild = ReadMemory();
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
    request.maxResults = 10'000;
    request.query = queryCase.query;

    SearchOutcome outcome;
    outcome.timings.detailedDiagnostics = detailedDiagnostics;
    outcome.timings.projectionOnly = projectionOnly;
    const auto response = pipeline.SearchIndexed(request, &outcome.timings);
    const auto serializationStart = Clock::now();
    const std::vector<uint8_t> payload = ffprotocol::SerializeIndexedSearchResponse(response);
    const double serializationMs = ElapsedMs(serializationStart);

    double parseMs = 0.0;
    double materializationMs = 0.0;
    size_t parsedCount = 0;
    if (!payload.empty()) {
        const auto parseStart = Clock::now();
        const auto parsed = ffprotocol::ParseIndexedSearchResponse(payload.data(), payload.size());
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
    outcome.run.candidateCount = outcome.timings.candidateCount;
    outcome.run.candidatesVisited = outcome.timings.candidatesVisited;
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
    const auto snapshot = MakeSnapshot(rows);
    return ffprotocol::SerializeSnapshot(snapshot).size();
}

size_t FindLargestSnapshotRows() {
    size_t low = 0;
    size_t high = kCapacitySearchUpperBound;
    while (high - low > 512) {
        const size_t middle = low + (high - low) / 2;
        if (SerializedSizeForRows(middle) <= ffprotocol::kSnapshotSlotCapacityBytes) {
            low = middle;
        } else {
            high = middle;
        }
    }
    while (SerializedSizeForRows(low + 1) <= ffprotocol::kSnapshotSlotCapacityBytes) ++low;
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
    const size_t totalSize = sizeof(ffprotocol::SnapshotSharedHeader) +
                             2 * ffprotocol::kSnapshotSlotCapacityBytes;
    const auto* view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, totalSize));
    if (view != nullptr) {
        ffprotocol::SnapshotSharedHeader header{};
        std::memcpy(&header, view, sizeof(header));
        if (header.activeSlot <= 1 && header.slotDataSize[header.activeSlot] <= ffprotocol::kSnapshotSlotCapacityBytes) {
            const uint8_t* data = view + sizeof(header) +
                                  static_cast<size_t>(header.activeSlot) * ffprotocol::kSnapshotSlotCapacityBytes;
            const auto parsed = ffprotocol::ParseSnapshot(data, header.slotDataSize[header.activeSlot]);
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

SnapshotRun RunSnapshot(size_t rows, ffengine::SnapshotPublisher& publisher) {
    SnapshotRun result;
    result.rows = rows;
    result.generationBefore = publisher.CurrentGeneration();
    const auto modelStart = Clock::now();
    const auto snapshot = MakeSnapshot(rows);
    result.modelBuildMs = ElapsedMs(modelStart);
    ffengine::SnapshotPublisher::PublishTimings timings;
    const auto totalStart = Clock::now();
    const uint64_t generation = publisher.Publish(snapshot, &timings);
    result.totalMs = ElapsedMs(totalStart) + result.modelBuildMs;
    result.serializedBytes = timings.serializedBytes;
    result.serializationMs = timings.serializationMs;
    result.publishCopyAndCommitMs = timings.copyAndCommitMs;
    result.publishSucceeded = generation != 0;
    result.oversize = !result.publishSucceeded && timings.serializedBytes > ffprotocol::kSnapshotSlotCapacityBytes;
    result.generationAfter = publisher.CurrentGeneration();
    if (result.publishSucceeded) {
        const PublishedRead read = ReadPublishedSnapshot(publisher.SectionName());
        result.uiMappingAndParsingMs = read.elapsedMs;
        result.uiParsedRows = read.rows;
        if (!read.parsed || read.rows != rows) {
            std::fprintf(stderr, "warning: published snapshot did not round-trip all rows\n");
        }
    }
    result.memoryAfter = ReadMemory();
    return result;
}

void WriteMemory(std::ostream& output, const MemorySample& sample) {
    output << "{\"working_set_bytes\":" << sample.workingSetBytes
           << ",\"private_bytes\":" << sample.privateBytes << '}';
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
    output << ",\"memory_after_broad_query\":"; WriteMemory(output, dataset.afterBroadQuery);
    output << ",\"memory_post_query\":"; WriteMemory(output, dataset.postQuery);
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
           << ",\"wire_result_count\":" << run.wireResultCount
           << ",\"wire_truncated\":" << (run.wireTruncated ? "true" : "false");
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
    output << ",\"ui_parsed_rows\":" << run.uiParsedRows << ",\"memory_after\":";
    WriteMemory(output, run.memoryAfter);
    output << '}';
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!ParseOptions(argc, argv, options)) return 2;

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
    };

    ffengine::SnapshotPublisher publisher;
    if (!publisher.Start(GetCurrentProcessId())) {
        std::fprintf(stderr, "snapshot publisher start failed\n");
        return 1;
    }
    std::printf("FastFiles large-index scalability benchmark (%zu dataset scales)\n", options.scales.size());
    for (const size_t scale : options.scales) {
        std::printf("Building %zu synthetic entries...\n", scale);
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
                                     dataset.beforeFixture, dataset.afterFixture)) {
            std::fprintf(stderr, "synthetic database generation failed at %zu\n", scale);
            std::error_code ignored;
            std::filesystem::remove_all(temporaryDirectory, ignored);
            return 1;
        }
        dataset.databaseBytes = DatabaseBytes(database);

        ffengine::IndexPipeline pipeline;
        if (!LoadPipeline(database, pipeline, volumeId, dataset.rebuildMs,
                           dataset.afterDatabaseOpen, dataset.afterRebuild)) {
            std::fprintf(stderr, "synthetic database reload failed at %zu\n", scale);
            std::error_code ignored;
            std::filesystem::remove_all(temporaryDirectory, ignored);
            return 1;
        }

        {
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
                    ReadPublishedSnapshot(publisher.SectionName()).elapsedMs;
            }
        }

        if (options.detailedDiagnostics) {
            const QueryCase projectionProbe{"projection_probe", L"zzzz_never_present", 0};
            const SearchOutcome probe = RunSearch(pipeline, scale, projectionProbe, 0,
                                                  "diagnostic", false, true);
            searchRuns.push_back(probe.run);
            std::printf("  projection probe   %.2f ms, rows %zu\n", probe.run.totalMs,
                        probe.run.projectionRowsExamined);
        }

        for (size_t caseIndex = 0; caseIndex < queryCases.size(); ++caseIndex) {
            const QueryCase& queryCase = queryCases[caseIndex];
            const size_t warmRuns = scale >= kLargeSearchThreshold ? kWarmRunsLarge : kWarmRunsNormal;
            if (caseIndex == 0) {
                const SearchOutcome first = RunSearch(pipeline, scale, queryCase, 0,
                                                      "cold-ish-first-after-rebuild", options.detailedDiagnostics);
                searchRuns.push_back(first.run);
            } else {
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
            if (std::string_view(queryCase.name) == "broad") dataset.afterBroadQuery = ReadMemory();
        }

        if (scale == 1'000'000) {
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
        dataset.postQuery = ReadMemory();
        datasets.push_back(dataset);
        pipeline.Close();
        std::error_code ignored;
        std::filesystem::remove_all(temporaryDirectory, ignored);
        std::printf("Completed %zu; database %.1f MB, generation %.1f s, rebuild %.1f s\n",
                    scale, static_cast<double>(dataset.databaseBytes) / (1024.0 * 1024.0),
                    dataset.generationMs / 1000.0, dataset.rebuildMs / 1000.0);
    }

    std::vector<SnapshotRun> snapshotRuns;
    for (const size_t rows : {size_t{100}, size_t{1'000}, size_t{10'000}, size_t{50'000},
                              size_t{100'000}, size_t{250'000}, size_t{500'000}}) {
        snapshotRuns.push_back(RunSnapshot(rows, publisher));
    }
    const size_t largestRows = FindLargestSnapshotRows();
    for (const double fraction : {0.50, 0.75, 0.90, 1.00}) {
        const size_t rows = static_cast<size_t>(static_cast<double>(largestRows) * fraction);
        snapshotRuns.push_back(RunSnapshot(rows, publisher));
    }
    snapshotRuns.push_back(RunSnapshot(largestRows + 1, publisher));
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
    output << "{\n  \"schema\":\"fastfiles-large-index-scalability-v1\",\n  \"machine\":";
    WriteMachine(output, machine);
    output << ",\n  \"snapshot_capacity_bytes\":" << ffprotocol::kSnapshotSlotCapacityBytes
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
    output << "\n  ],\n  \"supersession\":{\"engine_cancellation\":false,\"ui_generation_discards_stale_completion\":true,\"old_work_completes\":true}\n}\n";
    output.close();

    std::printf("Wrote %s\n", PathToUtf8(options.outputPath).c_str());
    std::printf("Largest successful snapshot probe: %zu rows (configured slot %zu bytes)\n",
                largestRows, ffprotocol::kSnapshotSlotCapacityBytes);
    return 0;
}
