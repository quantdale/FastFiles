Task: READ-ONLY research lane F (FastFiles repo; work ONLY read-only, no shell, no edits) — Benchmark methodology audit. Repo: C:/Users/Michael Roy/Documents/FastFiles. Investigate: (1) the bench harness ffengine_bench_large_index_scalability (find its source under tests/engine/ or bench/) — CLI flags (--scales, --memory-report, --diagnostics, cold/hot modes, source-sha embedding), fixture generator (name shapes, lengths, non-ASCII ratio), idle gating, warmup policy, repetition counts, and what machine-readable fields it emits (compare to openspec/changes/large-index-lifecycle-and-search-scalability/evidence/search/*.json and evidence/memory-cold-decision.md); (2) known methodology gaps from the predecessor's evidence (n=1 cold samples, host-drift 2.8x on hot paths, the same-block control -9%..+6%, lane-E corrections E-1..E-8 in evidence/adversarial-review-2026-10-21.md) and a corrected protocol for this campaign: repetitions, counterbalancing, noise floor, provenance (--source-sha), and what 'cold' must mean (cold projection + warm SQLite page cache? fully cold?); (3) whether the harness can measure: rows scanned, candidates retrieved/verified, planner choice, CPU, private bytes, disk reads, WAL growth, index bytes, cold-to-hot transitions (which of these exist vs must be added); (4) 5M feasibility: estimated runtime/disk for a 5M fixture on this host from the 2M numbers, and a staged plan that avoids uncontrolled hours-long runs. Deliver: findings with file:line, the corrected measurement protocol, and the harness gaps to fill (listed precisely so a human can implement them).
find: C:/Users/Michael Roy/Documents/FastFiles
find: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes
docs/benchmarks/
docs/benchmarks/indexed-search-allocated-size.md
docs/benchmarks/large-index-scalability-secondary-index.json
docs/benchmarks/large-index-scalability-sql-no-count.json
docs/benchmarks/large-index-scalability.json
docs/benchmarks/large-index-scalability.md
docs/benchmarks/phase8-bench-run-2m.log
docs/benchmarks/phase8-bench-run.log
docs/benchmarks/phase8-large-index-scalability-2m.json
docs/benchmarks/phase8-large-index-scalability.json
docs/benchmarks/phase8-large-index-scalability.md
docs/benchmarks/secondary-filename-index.json
docs/benchmarks/secondary-filename-index.md
openspec/changes/indexed-search-zero-copy-hot-path-and-lock-pressure-convergence/evidence/benchmark-and-validation-plan.md
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/ffengine_bench_large_index_scalability.last-run-json-summary.json
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_first_generation_publish.rep1.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_first_generation_publish.rep2.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_first_generation_publish.rep3.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_first_generation_publish.rep4.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_first_generation_publish.rep5.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_large_index_scalability.rep1.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_large_index_scalability.rep2.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_large_index_scalability.rep3.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_large_index_scalability.rep4.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_large_index_scalability.rep5.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_secondary_filename_index.rep1.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_secondary_filename_index.rep2.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_secondary_filename_index.rep3.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_secondary_filename_index.rep4.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffengine_bench_secondary_filename_index.rep5.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffindexstore_bench_projection_memory.rep1.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffindexstore_bench_projection_memory.rep2.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffindexstore_bench_projection_memory.rep3.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffindexstore_bench_projection_memory.rep4.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffindexstore_bench_projection_memory.rep5.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffsearch_bench_literal_matcher.rep1.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffsearch_bench_literal_matcher.rep2.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffsearch_bench_literal_matcher.rep3.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffsearch_bench_literal_matcher.rep4.log
openspec/changes/overnight-truth-audit-and-measured-performance/evidence/performance/raw/ffsearch_bench_literal_matcher.rep5.log
tests/engine/bench_allocation_isolation.cpp
tests/engine/bench_allocator_probe.h
tests/engine/bench_first_generation_publish.cpp
tests/engine/bench_large_index_scalability.cpp
tests/engine/bench_secondary_filename_index.cpp
tests/indexstore/bench_projection_memory.cpp
tests/indexstore/bench_rebuild_isolation.cpp
tests/search/bench_literal_matcher.cpp
large-index-lifecycle-and-search-scalability/design.md
large-index-lifecycle-and-search-scalability/evidence/
large-index-lifecycle-and-search-scalability/evidence/adversarial-review-2026-10-21.md
large-index-lifecycle-and-search-scalability/evidence/certification-ladder.md
large-index-lifecycle-and-search-scalability/evidence/equivalence-gate-adversarial-matrix.md
large-index-lifecycle-and-search-scalability/evidence/equivalence-product-tree-diff-43fc3c1-2f365a5.txt
large-index-lifecycle-and-search-scalability/evidence/explore-findings.md
large-index-lifecycle-and-search-scalability/evidence/hosted-ci-truth-2026-10-21.md
large-index-lifecycle-and-search-scalability/evidence/lifecycle-reload-resources.md
large-index-lifecycle-and-search-scalability/evidence/memory-cold-decision.md
large-index-lifecycle-and-search-scalability/evidence/rebuild/
large-index-lifecycle-and-search-scalability/evidence/rebuild/memory-report-after-fix.json
large-index-lifecycle-and-search-scalability/evidence/rebuild/memory-report-before.json
large-index-lifecycle-and-search-scalability/evidence/rebuild/rebuild-isolation-after-fix.json
large-index-lifecycle-and-search-scalability/evidence/rebuild/rebuild-isolation-before.json
large-index-lifecycle-and-search-scalability/evidence/review-remediation/
large-index-lifecycle-and-search-scalability/evidence/review-remediation/green-confirmation.txt
large-index-lifecycle-and-search-scalability/evidence/review-remediation/probe-clipboard-late-delivery.txt
large-index-lifecycle-and-search-scalability/evidence/review-remediation/probe-ordinal-fold-verdicts.txt
large-index-lifecycle-and-search-scalability/evidence/review-remediation/red-first-failures.txt
large-index-lifecycle-and-search-scalability/evidence/review-remediation-2026-10-21.md
large-index-lifecycle-and-search-scalability/evidence/search/
large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-after-rebuild-fix-control.json
large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-before.json
large-index-lifecycle-and-search-scalability/evidence/search/search-matrix-planner-fix.json
large-index-lifecycle-and-search-scalability/evidence/uia-explorer-drag-in-investigation.md
large-index-lifecycle-and-search-scalability/proposal.md
large-index-lifecycle-and-search-scalability/specs/
large-index-lifecycle-and-search-scalability/specs/index-engine/
large-index-lifecycle-and-search-scalability/specs/index-engine/spec.md
large-index-lifecycle-and-search-scalability/tasks.md
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
ls: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
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
#define FFBENCH_ALLOCATOR_PROBE_DEFINE
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
[2219 more lines in file. Use offset=201 to continue.]
search-matrix-after-rebuild-fix-control.json
search-matrix-before.json
search-matrix-planner-fix.json
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
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
[1919 more lines in file. Use offset=501 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
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
[1569 more lines in file. Use offset=851 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
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
        const auto start = Clock::now();
        ingestionSucceeded = pipeline.ApplyMftBatch(volumeId, {
            MakeMftRecord(10'000'000 + static_cast<uint64_t>(scale), 5,
                          L"contention_writer.txt", 0, 7, 777),
        });
        ingestionMs = ElapsedMs(start);
        ingestionDone.store(true, std::memory_order_release);
    });
    std::thread aggregate([&] {
        const auto start = Clock::now();
        (void)pipeline.GetFolderAggregate(volumeId, ffindexstore::FileId{5, 0});
        aggregateMs = ElapsedMs(start);
        aggregateDone.store(true, std::memory_order_release);
    });
    std::thread snapshot([&] {
        const auto start = Clock::now();
        (void)pipeline.ExportDirectorySnapshot(volumeId, L"C:");
        snapshotMs = ElapsedMs(start);
        snapshotDone.store(true, std::memory_order_release);
    });
    std::thread secondSearch([&] {
        ffprotocol::IndexedSearchRequest request = firstRequest;
        request.requestId += 1;
        request.query = L"report";
        const auto start = Clock::now();
        secondResponse = pipeline.SearchIndexed(request);
        secondSearchMs = ElapsedMs(start);
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
    cancelFirst.store(true, std::memory_order_release);
    const auto cancellationStart = Clock::now();
    coldStreamBarrier.Release();
    firstSearch.join();
    result.cancellationMs = ElapsedMs(cancellationStart);
    result.cancelledSearch = firstResponse.status == ffprotocol::IndexedSearchStatus::Cancelled;
    ingestion.join();
    aggregate.join();
    snapshot.join();
    secondSearch.join();
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
[1219 more lines in file. Use offset=1201 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
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
[869 more lines in file. Use offset=1551 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
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
[519 more lines in file. Use offset=1901 to continue.]
read: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
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
                RunSearch(pipeline, scale, queryCase, 0, "warmup", options.detailedDiagnostics);
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
        if (scale == options.scales.front()) {
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
        // nonblocking-indexed-read-plane 7.2: hold one real cold search at
        // the durable-stream boundary, then exercise the writer, aggregate,
        // snapshot, second-search, and cancellation paths while that query
        // is intentionally in flight. This is evidence, not a pass/fail
        // timing gate; the booleans make lock monopolization visible.
        dataset.contention = RunContentionProbe(pipeline, volumeId, scale, options.detailedDiagnostics);
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
    for (const size_t rows : {size_t{100}, size_t{1'000}, size_t{10'000}, size_t{50'000},
                              size_t{100'000}, size_t{250'000}, size_t{500'000}}) {
        snapshotRuns.push_back(RunSnapshot(rows, publisher, options.detailedDiagnostics));
    }
    const size_t largestRows = FindLargestSnapshotRows();
    for (const double fraction : {0.50, 0.75, 0.90, 1.00}) {
        const size_t rows = static_cast<size_t>(static_cast<double>(largestRows) * fraction);
        snapshotRuns.push_back(RunSnapshot(rows, publisher, options.detailedDiagnostics));
    }
    snapshotRuns.push_back(RunSnapshot(largestRows + 1, publisher, options.detailedDiagnostics));
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
    output << "{\n  \"schema\":\"fastfiles-large-index-scalability-v2\",\n  \"machine\":";
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
Now let me read the key evidence files for the methodology gaps and lane-E corrections.
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
# Independent adversarial review (9.1) — six lanes, 2026-10-21
Six parallel read-only review lanes attacked the committed seams at `b6df9ef`
(Unicode/search completeness, clipboard/test integrity, rebuild correctness,
memory/cold integrity, performance/statistics, certification truth). Full
lane reports are preserved in the session run artifacts
(`build/campaign/review-task-{1..6}.md` mirrors). All confirmed material
findings were fixed in `43fc3c1` and re-tested; the lane-suggested validation
commands were run by the primary agent.
## Confirmed findings and remediation (fix commit `43fc3c1`)
| Lane | Finding | Severity | Remediation | Regression test |
|---|---|---|---|---|
| D P1-1 / C F1 | Memory-pressure enforcement evicts+compacts across volumes with no ProjectionResidency epoch bump; in-flight searches serve silently incomplete results as Completed and walk repacked layouts | P1 (results integrity) | `EnforceMemoryLimitNow` returns whether eviction ran; every enforcement/reload site bumps residency epochs via the existing restart machinery (scoped unrelated-mutation semantics preserved) | `TestEnforcementEvictionAdvancesQueryVisibleEpochs` |
| D P1-2 | Over-watermark reload streams the whole volume under the pipeline mutex (unbounded transient; at 2M ~839 MB) then re-evicts -> empty export, repeatedly | P1 (availability + memory bound) | fail-fast fit estimate vs the early-enforcement watermark before streaming; reload of an oversized volume returns false immediately and stays cold | `TestOversizedVolumeReloadFailsFastAndStaysCold` |
| D P2-2 | Allocation failure mid-reload-stream left a volume silently partial-but-"hot" | P2 | stream wrapped in catch-all -> existing failed-read cleanup (remove partials, re-mark cold) | covered by the reload test + existing failure-injection tests |
| C F2 | `ProjectionEntryCursor` holds raw map/array positions; growth/compaction scrambles them -> silent truncation | P2 | layout markers + `Stale()`; hot loops treat staleness as a stale generation (restart + re-capture), never "exhausted" | `TestCursorDetectsDisturbedLayout` |
| C F3 | Accelerator dead after every bulk scan (nothing rebuilt it) | P2 (silent perf loss) | `SetBulkScanActive(false)` rebuilds the accelerator once | `TestBulkScanCycleRevivesFilenameAccelerator` |
| C F4 | Store INSERT arm ignored the field mask: partial upsert of a missing row persisted wire values while the projection zeroed them (hot/cold divergence) | P2 | INSERT binds zero/NULL for absent fields (matches `UpsertPartial` contract) | `TestPartialUpsertOfMissingRowZeroesAbsentFields` |
| C F5 | `RemoveBatch(keep=false)` left small batches' accelerator valid (contract violation) | P3 | keep=false invalidates regardless of batch size; contract comment corrected | existing removal tests + comment |
| C F6 | Cold reload re-enabled the accelerator mid-bulk-scan | P3 | rebuild gated on `!bulkScanActive_` | covered by bulk-scan test |
| C F7 | Missing-trigram branch returned the wrong (too large) candidate set via dead intent | P3 (safe over-approximation) | returns the exceptional merge of an empty intersection | existing superset tests |
| D P2-1 | Single-pass enforcement could free O(1) rows per batch boundary (never converges in flat corpora) | P2 | bounded multi-pass loop (8 passes, no-progress guard) | `TestProjectionMemoryLimitRunsAtBatchBoundary` (semantics preserved) |
| B P2-1 | Interception SKIP evaluated before the sequence-change guard (external change + all-null probes could skip) | P2 (skip integrity) | sequence-change evaluated first; counterexample is case 7b | `ffclipboard_classifier_tests` case 7b |
| B P2-2 | `writePublished=false -> SKIP` conflated session refusal with product write regression | P2 | readable evidence + failed write = FAIL(undetermined) | case 8b |
| B P2-3 | Interception gate required a CF_UNICODETEXT witness the product never publishes (skip unreachable; comment overclaimed evidence) | P2 | gate over the product's actual write set + no readable handle anywhere; comment cites the real late-delivery evidence | case 7 (updated) |
| B P3-4/5/6 | Probe bookkeeping invariant; observation-identical controls; window diagnostics | P3 | bookkeeping fixed; direction-distinct controls; attempts-before-exact recorded | controls 3-6 updated |
| E E-1/E-2 | Evidence noise-floor claim off ~an order of magnitude; cold "medians" were n=1 | P1 (evidence truth) | `memory-cold-decision.md` corrected (same-block control -9%..+6%; n=1 samples; hot medians 10/5 runs) | n/a (documentation) |
| E E-3..E-8 | Scan-floor number, rebuild band phrasing, 189x scope conditions, attribution tautology, 1M-cold context, artifact provenance | P2/P3 | all corrected in the evidence docs | n/a |
## Honest open items (not fixed here, no material risk claimed)
- Lane D P3-3: `Store::ForEachEntry` and `ReadEntryRecord` duplicate the
  column decode (currently identical); consolidation deferred.
- Lane C F6 second half: `RebuildAll` pays O(V*N) full-projection
  compaction/rebuild across volumes; batching deferred (correctness fine).
- Lane E E-8: future cold-profile artifacts should embed `--source-sha`
  (current pairing is asserted provenance, now disclosed).
- Lane D P3-2: a direct same-visitor A/B prefilter test would close the last
  equivalence-test indirection (semantics currently covered transitively by
  the store exact-set tests + hot/cold signature equality).
## Validation after remediation (`43fc3c1`)
- `ffindexstore_projection_tests`, `ffindexstore_store_tests`,
  `ffindexstore_read_session_tests`, `ffengine_index_pipeline_tests`,
  `ffengine_index_pipeline_concurrency_tests`, `ffclipboard_classifier_tests`,
  `ffquick_actions_tests`: all PASS (Debug, MSVC `/W4 /WX`).
- Full CTest suites at the certified tip: see `certification-ladder.md`.
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
# Memory budget & cold-query characterization (tasks 8.2/19) — 2026-10-21
Measurement host: same campaign host, idle, release-test binaries at
`b6df9ef` (after) vs `420792d` (before). Same harness/command both sides:
`ffengine_bench_large_index_scalability --scales 200000,1000000 --diagnostics`
(`build/campaign/cold-profile-*.json` + console logs).
## 1. Policy map (verified against source; full map in the Agent-C audit)
- `Projection::kDefaultMaximumMemoryBytes` = **500 MiB**; the early-enforcement
  watermark (`EnforceMemoryLimit` at batch boundaries, `IsMemoryPressureHigh`)
  is `max - max/5` = **400 MiB**. Only `SetMaximumMemoryBytes` enforces the
  strict 500 MiB ceiling synchronously.
- `IndexPipeline::SetProjectionMemoryBudget` has **no production caller**
  (repo-wide grep: tests only). The production budget is therefore the fixed
  500 MiB default.
- Eviction: subtree victims preferred (oldest directory subtree), and
  `EvictSubtree` marks the **whole volume** cold. Reload
  (`ReloadEvictedVolume` -> `RebuildVolumeFromStore`) is a full-volume
  re-stream under the pipeline mutex (snapshot export path only; search never
  reloads). A volume larger than the low watermark can never reload (tested
  thrash behavior).
- Cold search streams `SELECT ... FROM entries WHERE volume_id=?` with no
  name predicate and no LIMIT; every cold row was decoded and matched in C++.
## 2. Cold-path profiling (the bottleneck, proven before changing anything)
At 1M rows (forced eviction; `coldRows` = rows reaching the C++ matcher):
| class | before: COLD ms / rows | after: COLD ms / rows | delta |
|---|---:|---:|---:|
| selective | 1470 / 1,000,000 | 1141 / 98,438 | 1.29x |
| no_match | 1413 / 1,000,000 | 1101 / 98,437 | 1.28x |
| exact_name | 1601 / 1,000,000 | 1115 / 98,438 | 1.44x |
| moderate | 6261 / 1,000,000 | 5817 / 196,875 | 1.08x |
| prefix | 6087 / 1,000,000 | 6449 / 196,875 | ~1.0 (noise) |
| broad | 17,680 / 1,000,000 | 14,885 / 393,749 | 1.19x |
| worst_substring | 10,921 / 1,000,000 | 10,498 / 295,313 | 1.04x |
| mixed_case | 11,891 / 1,000,000 | 11,476 / 295,312 | 1.04x |
| high_limit_broad | 16,337 / 1,000,000 | 16,120 / 393,749 | 1.01x |
| short_prefix | 16,354 / 1,000,000 | 15,357 / 1,000,000 | prefilter ineligible (<3) |
| extension | 19,971 / 1,000,000 | 21,758 / 1,000,000 | prefilter ineligible (predicate) |
| unicode | 6437 / 1,000,000 | 6237 / 1,000,000 | prefilter ineligible (non-ASCII needle) |
Before the change `stream` (the SQLite stream + decode + matcher) was >99.8%
of every cold class (e.g. selective 1468.5 of 1469.7 ms); hot selective stayed
2.5 ms (accelerated). Review-lane E E-6 correction: that percentage is a
timer-coverage tautology (the `storeStreamMs` timer wraps the entire
stream+match loop), not a scan/decode/match localization -- no such split was
instrumented. The prefilter case rests on the row-count mechanism
(decode/materialize 1M -> 98k rows), the before/after delta, and the
below-subtraction floor, not on the attribution percentage.
Measurement-discipline corrections (review-lane E E-1/E-2/E-3):
- Every cold cell is a **single measured sample** per class per scale (the
  bench runs one measured cold query after one warmup; hot figures are
  medians over 10 (200k) / 5 (1M) warm runs). "COLD" here means
  cold-projection with a warm SQLite page cache.
- The two runs were **not host-comparable on CPU-bound paths**: untouched hot
  medians moved 1.2-2.8x at 1M and up to 1.4x slower at 200k. The defensible
  noise control is the prefilter-ineligible cold classes measured in the SAME
  block (extension 19,971->21,758 ms; short_prefix 16,354->15,357; unicode
  6,437->6,237: -9%..+6%). Against that control the 1.28-1.44x literal-class
  deltas exceed observed drift and the row-count mechanism supports the
  direction, but each cell is one sample and the effect carries ~±10%
  uncertainty.
- Two-point subtraction on the no_match pair gives a survivor cost of ~0.35
  us/row and a **scan floor of ~1.05-1.10 us/scanned-row** (consistent at both
  scales) -- not the ~0.8-0.9 us/row previously written. Even a zero-survivor
  literal is therefore ~1.05-1.1 s at 1M; on a 1-2% non-ASCII corpus (an
  unmeasured assumption; CJK-heavy corpora fall back toward the measured
  9.8%-survivor case) cold selective at 1M is ~1.1 s vs 1.47 s before
  (~1.3-1.4x).
- Provenance note (lane E E-8): the two cold-profile artifacts do not embed
  source identity; the `420792d`/`b6df9ef` pairing is asserted provenance
  (consistent with observable data such as the durable-fallback row drop),
  and future runs should embed `--source-sha` like the memory-report does.
The survivors at 1M are exactly the fixture's non-ASCII name population
(98,438 = 9.8%): the conservative GLOB disjunct admits every name carrying
non-ASCII bytes (host-fold safety, see review-remediation P2-A). The hard
floor is the SQLite name-column scan itself (~1.05-1.10 us/row, see the
corrections above), so expected real-corpus cold selective latency at 1M is
~1.1 s vs 1.47 s before -- about 1.3-1.4x. Run-to-run noise between the two
runs reached 2.8x on untouched hot paths (host state differed); the
same-block ineligible-class control bounds the drift at -9%..+6% for the
cold table.
## 3. The 2M regime (task 8.2 finding, characterized)
The 2M-row projection models ~839.7 MB > the 500 MiB default, so every 2M
query serves from the cold stream under the default budget (historical
search-matrix p50 at 2M: selective ~2.5 s, broad ~27.1 s, extension ~37.6 s;
the prefilter does not change non-literal classes and removes only the
survivor decode share for literals). Serving 2M rows hot would require a
budget near 1 GiB -- not raised here (see decision).
## 4. Product decision (mission 19): **Option A + E, with the cold prefilter
## shipped as the evidence-backed improvement**
- **Option A (retained):** keep the 500 MiB default; accelerate the cold path
  within existing semantics. Shipped: `ForEachEntryWithAsciiNameUntil`
  conservative SQL name prefilter (commit `b6df9ef`), superset-proven
  (LIKE-ASCII-fold + non-ASCII-bytes disjunct; red-first on the LIKE-escape
  under-match class) and measured above. No schema change, no new memory.
- **Option B (adaptive budget) — rejected for this change:** an adaptive
  policy needs floors/ceilings and behavior proofs under changing available
  RAM; none of the measured evidence requires it yet, and the mission forbids
  raising default consumption without safety proof.
- **Option C (hybrid hot/cold residency) — deferred as separately scoped
  architecture work:** eviction currently marks a whole volume cold and the
  reload path re-streams under the mutex; a compact searchable subset would
  need strict consistency + memory-bound proofs (a new OpenSpec change).
- **Option D (explicit setting) — rejected:** `SetProjectionMemoryBudget` has
  no production caller and no settings surface/persistence semantics exist;
  a half-wired setting would be a worse product than none.
- **Option E (document the limitation) — adopted:** the 2M-entry cold regime
  is documented precisely above; the remaining candidate improvement (a
  durable trigram/FTS candidate table, schema v3 + migration + superset proof)
  is deliberately NOT taken here because the mission requires separate schema
  justification and the measured 1.3-1.7x cold-literal gain does not justify
  that risk inside this campaign.
## 5. Retention classification (mission 23) for the one performance change
- Candidate: cold SQL name prefilter (`b6df9ef`), baseline `420792d`.
- Fixture: same synthetic 200k/1M corpora (10% non-ASCII names), same bench.
  Hot figures are medians over the harness's warm runs; every cold figure is
  a single measured sample per class per scale (lane E E-2), on runs whose
  CPU-bound paths were not host-comparable (E-1); see the corrections in §2.
- Correctness: hot-vs-cold identity+ordering signatures (extended
  `TestForcedEvictionSearchEquivalence`), store-level superset + escaping
  red-first (`build/campaign/prefilter-red-run.log`), full suites green.
- Decision: **RETAIN** (correct; 1.28-1.44x on cold literal classes at 10%
  non-ASCII vs a -9%..+6% same-block control, ~1.3-1.4x expected on real
  corpora; zero schema/memory risk). Scope conditions on the prior hot
  acceleration (lane E E-5): the 56-189x figure is hot/projection-resident,
  planner-eligible literal needles only (best case 189x = 1-result ASCII
  literal at 1M; rows examined 1M->1), it repairs a planner bug that had
  disabled the accelerator entirely, and it does NOT apply to cold queries
  or the >=2M regime, where all queries serve from the durable stream and
  latency is governed by the scan floor above. Lane E E-7: the 1M cold table
  characterizes the production cold-stream cost model under a state that at
  1M rows requires multi-volume memory pressure to reach (the 1M projection
  models ~318 MiB, below the 400 MiB watermark); the common production cold
  regime is >=2M rows.
grep: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine
search-matrix-before.json:2:   "schema":"fastfiles-large-index-scalability-v2",
search-matrix-after-rebuild-fix-control.json:2:   "schema":"fastfiles-large-index-scalability-v2",
search-matrix-planner-fix.json:2:   "schema":"fastfiles-large-index-scalability-v2",
test_index_pipeline.cpp:31: bool HasSqlitePlannerStats(const std::string& dbPath) {
test_index_pipeline.cpp:169:     Check(!HasSqlitePlannerStats(dbPath), "ANALYZE is not triggered before 10,000 inserted records");
test_index_pipeline.cpp:176:     Check(HasSqlitePlannerStats(dbPath), "ANALYZE creates planner statistics at 10,000 inserted records");
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine
src/IndexPipeline.h:457:     // address-engineering-audit-findings 5.1: keep planner statistics fresh
src/IndexPipeline.cpp:365: // cold-stream prefilter eligibility. An ASCII-folded simple literal needle of
src/IndexPipeline.cpp:366: // >= 3 units can use the conservative SQL name prefilter
src/IndexPipeline.cpp:371: std::optional<std::string> ColdNamePrefilterNeedle(const ffsearch::Query& query) {
src/IndexPipeline.cpp:1887:                 const auto coldNamePrefilter = ColdNamePrefilterNeedle(searchRequest.query);
src/IndexPipeline.cpp:1904:                 const bool streamed = coldNamePrefilter.has_value()
src/IndexPipeline.cpp:1905:                     ? context.reader->ForEachEntryWithAsciiNameUntil(coldVolumeId, *coldNamePrefilter,
src/IndexPipeline.cpp:2017:                 const auto durableNamePrefilter = ColdNamePrefilterNeedle(searchRequest.query);
src/IndexPipeline.cpp:2034:                 const bool streamed = durableNamePrefilter.has_value()
src/IndexPipeline.cpp:2035:                     ? context.reader->ForEachEntryWithAsciiNameUntil(volumeId, *durableNamePrefilter,
src/IndexPipeline.h:56:     struct SearchTimings {
read: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
    // work has released the mutation/lifecycle critical section.
    enum class SearchPhase : uint8_t {
        WorkStarted,
        Matching,
        ColdStreaming,
        DurableFallback,
        GenerationValidation,
    };
    struct SearchTestHooks {
        std::function<void(SearchPhase)> onPhase;
    };
    // Optional attribution seam for the explicit scalability benchmark. The
    // normal product path passes no timing object and remains behaviorally
    // unchanged; the fields describe the real SearchIndexed stages rather
    // than adding production logging.
    struct SearchTimings {
        bool detailedDiagnostics = false;
        bool projectionOnly = false;
        double sqliteMetadataMs = 0.0;
        double projectionTraversalMs = 0.0;
        double projectionEnumerationMs = 0.0;
        double namePoolLookupMs = 0.0;
        double nameViewConstructionMs = 0.0;
        double candidateMaterializationMs = 0.0;
        double lightweightCandidateMaterializationMs = 0.0;
        double lazyPathMaterializationMs = 0.0;
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
        double totalMs = 0.0;
        size_t candidateCount = 0;
        size_t projectionEntriesVisited = 0;
        size_t projectionRowsExamined = 0;
        size_t namePoolLookups = 0;
        size_t emptyNamesRejected = 0;
        size_t nameViewsConstructed = 0;
        size_t candidateObjectsMaterialized = 0;
        size_t candidateNameCopies = 0;
        size_t candidatesVisited = 0;
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
        size_t resultCount = 0;
        size_t searchResultObjectsMaterialized = 0;
        size_t lazyPathMaterializationCount = 0;
        size_t filenameIndexCandidateCount = 0;
        bool filenameIndexUsed = false;
        // memory-bounded-indexed-scale 3.2: hot/cold attribution for the
        // bounded-projection search path. Cold rows stream from the durable
        // store; they never pass through a projection reload.
        size_t hotRowsExamined = 0;
        size_t coldRowsExamined = 0;
        size_t coldVolumesSearched = 0;
        double storeStreamMs = 0.0;
        bool coldSearchUsed = false;
        bool truncated = false;
        bool cancelled = false;
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
        // indexed-search-zero-copy-hot-path 7.x: view-first lightweight capture.
        // Timing attribution is a non-overlapping partition:
        //  - lightweightCaptureMs = bounded name-buffer capture under mutex
        //  - viewMatchingMs = pure view predicate (MatchesView*) time outside lock, excluding survivor work
        //  - survivorMaterializationMs = survivor Upgrade (MakeProjectionCandidate + accumulator Add) outside lock
        size_t hotLightweightRowsCaptured = 0;
        size_t hotRowsRejectedBeforeMaterialization = 0;
        size_t viewPathFallbackRows = 0;
        double lightweightCaptureMs = 0.0;
        double viewMatchingMs = 0.0;
        double survivorMaterializationMs = 0.0;
        bool viewPathUsed = false;
    };
    struct ReadPlaneDiagnostics {
        size_t activeSearches = 0;
        size_t peakSearches = 0;
        size_t activeDurableReaders = 0;
        size_t peakDurableReaders = 0;
        uint64_t completedSearches = 0;
        uint64_t cancelledSearches = 0;
        uint64_t unavailableSearches = 0;
        uint64_t readerFailures = 0;
        uint64_t generationRestarts = 0;
        uint64_t queryVisibleEpoch = 0;
        uint64_t dependencySetEpoch = 0;
        uint64_t queryVisibleInvalidations = 0;
        uint64_t metadataOnlyUpdates = 0;
        uint64_t metadataOnlyInvalidationsAvoided = 0;
        uint64_t durableFallbacks = 0;
        uint64_t successfulDurableFallbacks = 0;
        uint64_t failedDurableFallbacks = 0;
        uint64_t durableFallbackRowsStreamed = 0;
    };
    // outIntegrityFailed (optional, task 1.7): set to true when the open
    // failed specifically because the existing file failed its integrity
    // check, so the caller can apply the rebuild-from-fresh-scan fallback
    // (delete the file and reopen) rather than treating it as a generic
    // I/O failure.
    bool Open(const std::string& dbPathUtf8, bool* outIntegrityFailed = nullptr);
    void Close();
    // nonblocking-indexed-read-plane 2.1: test-only phase injection. The
    // callback is never invoked when no hook is installed.
    void SetSearchTestHooks(SearchTestHooks hooks);
    // nonblocking-indexed-read-plane 3.4: portable fault injection for the
    // per-query durable reader. Production callers leave this empty; a
    // forced reader failure must surface as Unavailable rather than a partial
    // completed response.
    void SetReadSessionFaultInjector(ffindexstore::ReadSessionFaultInjector injector);
#ifdef FF_INDEX_PIPELINE_TESTING
[297 more lines in file. Use offset=180 to continue.]
Now let me check for idle gating, CPU/IO counters in the harness, and the repetition practice in the overnight audit evidence.
grep: C:/Users/Michael Roy/Documents/FastFiles
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine
tests/engine/bench_large_index_scalability.cpp:2087:               "\"timings in this mode are single-process measurements and are not the campaign's idle-gated "
openspec/changes/durable-cold-search-index-and-hybrid-residency/tasks.md:9: - [ ] 1.1 Reproduce the 2M-5M cold-regime baseline on the landed PR #14 tree (same harness discipline: idle gating, embedded source identity, machine-readable artifacts) at 200k / 1M / 2M / 5M, all 12 query classes, hot and cold.
openspec/changes/performance-closure-and-multisurface-acceleration/tasks.md:11: - [x] 2.1 Identify and stop only campaign-owned or safely disposable processes that distort measurement. Do not kill arbitrary user work. Wait for the established benchmark idle gate.
openspec/changes/performance-closure-and-multisurface-acceleration/tasks.md:29: - [x] 4.2 Measure the first-generation path with the production-faithful move, idle gate, single-core pin, high priority, and enough process-level samples. Record source SHA, tree state, preset, fixture, warm-up, repetitions, and stage medians. Do not reuse 88.403 ms or 129.329 ms as the before row.
openspec/changes/performance-closure-and-multisurface-acceleration/specs/performance-closure-and-multisurface-acceleration/spec.md:19: - **AND** a load-sensitive lifecycle test has been rerun on a host that meets the established idle gate, or its idle-host failure is recorded as a real defect under investigation
openspec/changes/performance-closure-and-multisurface-acceleration/specs/performance-closure-and-multisurface-acceleration/spec.md:81: Both arms SHALL be compared with the campaign's idle-gated, core-pinned, high-priority, interleaved protocol. A gain inside the freshly measured same-binary noise floor MUST NOT be treated as an allocation speedup.
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/retention-ab-pooled-name-path.md:5: measured reps per process) under the established protocol: idle-gated
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/projection-memory-decomposition.md:64: Remaining (queued behind the campaign's idle-gate wait for wall-clock work; a
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/projection-memory/projection-memory-200k.json:20:   ,"limitations":["allocator counts cover C++ operator new/delete only; SQLite (compiled as C) allocates through malloc/free and appears in the private-bytes residual instead","standard-library node links, the probe's 32-byte allocation header, heap block headers and heap rounding are residual and are not attributed to any projection category","working-set shareable pages identify shareable/pagefile-backed pages; they are not proof that another process maps the same section","the export weighing... [truncated]
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/projection-memory/projection-memory-1m.json:20:   ,"limitations":["allocator counts cover C++ operator new/delete only; SQLite (compiled as C) allocates through malloc/free and appears in the private-bytes residual instead","standard-library node links, the probe's 32-byte allocation header, heap block headers and heap rounding are residual and are not attributed to any projection category","working-set shareable pages identify shareable/pagefile-backed pages; they are not proof that another process maps the same section","the export weighing... [truncated]
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/projection-memory/projection-memory-1m-comparison-baseline.json:13:   ,"limitations":["allocator counts cover C++ operator new/delete only; SQLite (compiled as C) allocates through malloc/free and appears in the private-bytes residual instead","standard-library node links, the probe's 32-byte allocation header, heap block headers and heap rounding are residual and are not attributed to any projection category","working-set shareable pages identify shareable/pagefile-backed pages; they are not proof that another process maps the same section","the export weighing... [truncated]
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/projection-memory/projection-memory-1m-comparison-after-slack-release.json:13:   ,"limitations":["allocator counts cover C++ operator new/delete only; SQLite (compiled as C) allocates through malloc/free and appears in the private-bytes residual instead","standard-library node links, the probe's 32-byte allocation header, heap block headers and heap rounding are residual and are not attributed to any projection category","working-set shareable pages identify shareable/pagefile-backed pages; they are not proof that another process maps the same section","the export weighing... [truncated]
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/first-generation-baseline-and-noise-session1.json:12:  "method": "interleaved randomized order, idle-gated, each repetition pinned to one logical core at high priority, 5 warm-up + 5 measured reps",
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/final-report.md:114: D5 matrix (30 idle-gated, core-pinned, high-priority interleaved processes,
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/certification-debt-phase-a.md:6: after the established idle gate opened (median CPU load over 3 samples <= 25;
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/certification-debt-phase-a.md:10: ## Task 2.1 — idle gate and process hygiene
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/broad-search-profile-200k.md:145:   (the campaign's <=25 idle gate was not enforced for this bench), and no
openspec/changes/measured-performance-acceleration-and-exact-certification/tasks.md:53:   priority with an idle gate. Same-binary control spread fell to ~5–7%.
openspec/changes/measured-performance-acceleration-and-exact-certification/proposal.md:27: - Add an idle-gated, core-pinned A/B measurement protocol (idle host gate,
openspec/changes/large-index-lifecycle-and-search-scalability/tasks.md:30: - [x] 5.1 Benchmark the 12 query classes (selective, moderate, broad, no_match, prefix, extension, worst_substring, exact_name, short_prefix, mixed_case, unicode, high_limit_broad) at 200k/1M/2M on the current tree with machine-readable output and idle gating. Evidence: `evidence/search/search-matrix-before.json` (in collection).
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/allocation-probe-interpretation-v2.json:48:     "method": "interleaved randomized order, idle-gated, each repetition pinned to one logical core at high priority, 5 warm-up + 5 measured reps",
openspec/changes/performance-closure-and-multisurface-acceleration/evidence/allocation-isolation-result.json:5:  "protocol": "interleaved randomized order, idle-gated, each repetition pinned to one logical core at high priority, 5 warm-up + 5 measured reps",
openspec/changes/performance-closure-and-multisurface-acceleration/design.md:62: **Trade-off:** Apply waits for the established idle gate and may stop optimization work if the idle run still exceeds 30 seconds. A small controlled set, not more than three idle runs, distinguishes a stable pass from a stable failure. Any idle exceedance starts a product/test investigation. The historical loaded failure stays in the record.
openspec/changes/performance-closure-and-multisurface-acceleration/design.md:112: Protocol: idle gate, one logical core, high priority, interleaved randomized order, enough process-level samples to compare with a same-binary control measured in the same session. The historical 5–7% spread is a reference, not the threshold.
openspec/changes/performance-closure-and-multisurface-acceleration/APPLY_PROMPT.md:95: - Use the production-faithful move, established idle gate, one logical core, high priority, randomized interleaved order, warm-up, and sufficient process-level samples. Measure a same-binary control in that same session.
openspec/changes/measured-performance-acceleration-and-exact-certification/evidence/retention-summary.json:5:  "reason": "Measured inside the noise floor; the unchanged baseline binary itself differed by 5.7% between two idle-gated sessions, so no attribution was possible.",
openspec/changes/measured-performance-acceleration-and-exact-certification/evidence/retention-emplaceinplace-reverted.json:8:  "method": "Interleaved randomized idle-gated A/B in one session, 6 processes per arm, 5 warm-up + 5 measured repetitions, release-test preset.",
openspec/changes/measured-performance-acceleration-and-exact-certification/evidence/retention-emplaceinplace-reverted.json:26:  "noise_spread": "Decisive evidence that the effect is below the noise floor: the SAME baseline binary was re-measured in two different idle-gated sessions and produced TOTAL median-of-run-medians of 91.799 ms and 96.992 ms -- a 5.7% difference with no code change whatsoever. The candidate's apparent -4.31% is inside that baseline-only run-to-run variation. Export moved -0.08% (no signal at all) while publish moved +3.5% against the change's intent.",
openspec/changes/measured-performance-acceleration-and-exact-certification/evidence/name-representation-ceiling.json:3:   "method": "Interleaved randomized A/B, each repetition pinned to one logical core at high priority, idle-gated (<=25% load), 5 warm-up + 5 measured repetitions, 5 processes per arm, release-test preset, 200,000 entries / 3,126 directories / 199,999 files.",
openspec/changes/large-index-lifecycle-and-search-scalability/evidence/rebuild/memory-report-after-fix.json:20:   ,"limitations":["allocator counts cover C++ operator new/delete only; SQLite (compiled as C) allocates through malloc/free and appears in the private-bytes residual instead","standard-library node links, the probe's 32-byte allocation header, heap block headers and heap rounding are residual and are not attributed to any projection category","working-set shareable pages identify shareable/pagefile-backed pages; they are not proof that another process maps the same section","the export weighing... [truncated]
[30 matches limit reached. Use limit=60 for more, or refine pattern. Some lines truncated to 500 chars. Use read tool to see full lines]
No matches found
ls: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
design.md
proposal.md
specs/
tasks.md
{
  "schema": "fastfiles-large-index-scalability-v2",
  "machine": {
    "cpu": "13th Gen Intel(R) Core(TM) i5-13500HX",
    "logical_cores": 20,
    "ram_bytes": 34065997824,
    "windows_version": "10.0.26200",
    "compiler": "MSVC 1950.195035725",
    "build_configuration": "Release",
    "database_storage": "temporary directory (drive/model not collected)"
  },
  "snapshot_maximum_payload_bytes": 268435456,
  "snapshot_capacity_estimate_rows": 2200468,
  "snapshot_largest_published_probe_rows": 1980421,
  "indexed_search_result_cap": 4096
}
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
# Tasks
Proposal-stage ledger. **Nothing here is started.** The first action of the
future campaign is task 1.1; no production implementation task may begin
before the measurement phase (group 1) has produced its decision record.
## 1. Measurement phase (mandatory before any implementation)
- [ ] 1.1 Reproduce the 2M-5M cold-regime baseline on the landed PR #14 tree (same harness discipline: idle gating, embedded source identity, machine-readable artifacts) at 200k / 1M / 2M / 5M, all 12 query classes, hot and cold.
- [ ] 1.2 Measure alternatives A-E from `design.md` as prototypes behind test-only seams (never production): candidate-generation latency, completeness status, ingestion overhead, index size, memory bounds.
- [ ] 1.3 Prove or disprove candidate completeness under Windows ordinal folding for each alternative (fold-exceptional + surrogate classes included); record red-first differential evidence.
## 2. Decision
- [ ] 2.1 Select or reject each alternative with measured rationale in the `design.md` decision log; state the schema/migration consequence explicitly.
- [ ] 2.2 If no alternative meets the targets with acceptable risk, record the measured floor and stop — an honest documented floor is an acceptable outcome.
## 3. Implementation (only after 2.x selection and owner authorization)
- [ ] 3.1 Implement the selected candidate generator incrementally (additive schema, versioned), with crash-consistent updates and rebuild-on-open fallback.
- [ ] 3.2 Wire cost-based planner selection (candidate cap + fallback to canonical scan preserved).
- [ ] 3.3 Migration + rollback tooling with measured v2->v3 cost at 2M-5M rows and crash-recovery tests.
## 4. Acceptance
- [ ] 4.1 Cold/hot result identity + ordering equivalence suites green for all query classes at 200k/1M/2M/5M.
- [ ] 4.2 Resource bounds proven (planner caches, disk bytes per 1M rows, default 500 MiB projection budget unchanged).
- [ ] 4.3 Full certification ladder at the final candidate (Debug/release-test CTest, Analyze, ASan slice, strict OpenSpec, local CI parity, E2E, UIA, visual), with the same honest classification discipline as the predecessor campaign.
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
# Design / decision record: durable-cold-search-index-and-hybrid-residency
Status: **decision record seeded during the PR #14 closure campaign; every
alternative is UNDECIDED pending the measurement phase (task group 1).** No
production implementation exists or is authorized by this document.
## 1. Bounding facts (inherited, measured)
These constrain the design space and come from the completed
`large-index-lifecycle-and-search-scalability` campaign's evidence
(`evidence/memory-cold-decision.md`, `evidence/lifecycle-reload-resources.md`):
- Projection budget: 500 MiB default, 400 MiB early-enforcement watermark;
  `SetProjectionMemoryBudget` has no production caller (tests only). A 1M-row
  projection models ~414 MB (straddles the watermark); 2M models ~839.7 MB
  (always cold under default policy).
- Cold stream floor: ~1.05-1.10 us per scanned row (name-column scan,
  decode, match). The shipped conservative SQL name prefilter
  (`ForEachEntryWithAsciiNameUntil`) removes the survivor-decode share for
  ASCII literals (1.28-1.44x at 1M, 9.8% non-ASCII names) but cannot beat
  the scan floor; non-literal classes are ineligible by design.
- Hot path: filename-trigram accelerator (folded postings + host-derived
  fold-exceptional set + surrogate-conservative set) gives 56-189x on
  eligible literal classes at 1M; candidate cap
  `kMaximumFilenameIndexCandidates = 16,384` is the existing cost-based
  fallback to canonical scan.
- Correctness contract: hot/cold identity+ordering equivalence is pinned by
  differential suites; candidate sets must be conservative supersets under
  `CompareStringOrdinal(..., TRUE)` host semantics (predecessor P2-A repair:
  fold-exceptional classification via a memoized host probe, fail-closed on
  API error).
## 2. Alternatives to measure (all currently UNDECIDED)
| # | Alternative | Sketch | Principal risk |
|---|---|---|---|
| A | SQLite FTS5 candidate table | FTS5 (trigram or unicode61) over names, queried for rowid candidates, verified by the canonical matcher | Tokenizer case-folding is NOT Windows ordinal folding; must prove superset incl. fold-exceptional + surrogate classes, or maintain an exception side-set |
| B | Durable trigram posting table | Persist the in-memory accelerator's structure (folded trigram -> sorted rowid postings + exception set) in SQLite; incremental maintenance per batch | Write amplification on ingestion; crash-consistency of postings vs entries; disk size |
| C | Hybrid hot/cold residency | Keep a compact per-volume candidate subset resident (e.g. folded-name keys or a minimal automaton) while rows stay cold | Memory bound must be strict and provable; consistency with evicted rows; predecessor marked this "separately scoped architecture work" |
| D | Covering ordinal-key index | Sorted index on the matcher's folded name key; range scans for prefix/substring classes | Substring/wildcard classes get little benefit; index size |
| E | Budget policy (adaptive/default raise) | Control arm: serve 2M hot by raising/deriving the budget | Competes with system memory; forbidden without safety proof; does not help 5M |
## 3. Mandatory acceptance dimensions (per alternative)
1. **Completeness:** candidate superset proof under host ordinal semantics,
   with red-first differential tests (fold-exceptional, surrogate, dotted,
   mixed-case, short-name shapes) — the predecessor's
   `TestFilenameCandidatesAreConservativeSuperset` family extended to the
   durable generator.
2. **Equivalence:** cold-with-index results byte-identical to hot results for
   all 12 query classes at 200k/1M/2M (and 5M for the selected candidate).
3. **Migration:** v2 -> v3 cost at 2M-5M; rollback path; crash-consistency
   (kill-during-update recovery; index never disagrees with entries);
   rebuild-on-open as the fallback recovery.
4. **Resources:** planner/index cache memory bounds; disk bytes per 1M rows;
   no change to the default 500 MiB projection budget.
5. **Cost:** ingestion/USN-batch maintenance overhead; reopen/recovery cost;
   measured planner decision quality (when index is consulted vs scan).
6. **Scale:** p50/p90/p95 at 200k/1M/2M/5M per query class, machine-readable
   artifacts with embedded source identity (predecessor lane E E-8 lesson:
   artifacts must embed `--source-sha`).
## 4. Decision log
| Date | Decision | Status |
|---|---|---|
| 2026-10-21 | Proposal + decision record created at PR #14 closure; alternatives A-E recorded; measurement-first mandated; no alternative selected | RECORDED |
| (future) | Task group 1 measurements complete | PENDING |
| (future) | Alternative selected/rejected with measured rationale | PENDING |
| (future) | Schema/migration/rollback design for the selected alternative | PENDING |
## 5. Explicit non-decisions
- FTS5 is **not** presumed correct: its tokenizer folding must be proven a
  superset of Windows ordinal ignore-case on this host (see dimension 1) or
  supplemented with an exception side-set maintained like the in-memory
  accelerator's.
- Schema v3 is **not** presumed necessary: alternative C (hybrid residency)
  may achieve the latency targets without a schema change, and alternative E
  is retained only as a control arm.
- The 500 MiB default budget is **not** raised by this proposal.
read: C:/Users/Michael Roy/Documents/FastFiles/docs/benchmarks/...
# FastFiles large-index scalability baseline
> **Superseded for architecture claims (2026-08-24):** indexed search now has a
> durable cold path under projection eviction and publication is bounded by a
> working set. Current-architecture measurements live in
> [phase8-large-index-scalability.md](phase8-large-index-scalability.md). This
> document is retained as the pre-Phase-8 provenance baseline; its hot-path
> numbers remain representative for fully-resident indexes.
Reference run: 2026-08-10, Release, MSVC 19.44.35228.0, CMake 3.31.6, Windows 10.0.26200, AMD Ryzen 5 7535HS (12 logical cores), 23.3 GiB RAM, Kingston OM8SEP4512Q-AA NVMe SSD, NTFS C:. The machine-readable run is [large-index-scalability.json](large-index-scalability.json).
No formal latency target was found in the repository. The interpretation below uses engineering bands only: `<50 ms` immediate, `50–100 ms` excellent, `100–250 ms` responsive, `250–500 ms` noticeable, `>500 ms` poor, and `>1 s` not instant.
## Reproduction
```powershell
# Run from a Visual Studio developer environment. The verification harness
# discovers the installed VC toolchain and exposes resolved CMake/Ninja/CTest
# front ends from PATH or the selected installation.
cmake --preset release
cmake --build --preset release --target ffengine_bench_large_index_scalability
build\release\tests\engine\ffengine_bench_large_index_scalability.exe --output docs/benchmarks/large-index-scalability.json
```
The benchmark is not registered with CTest. It creates a temporary SQLite database per scale, opens it through `IndexPipeline`, rebuilds the real projection, runs the real indexed-search path, measures the real snapshot export/publication path, and removes the temporary directory afterward. Default scales are 10K, 100K, 500K, and 1M; larger scales are explicit (`--scales 2000000,5000000`) because the measured lower-scale boundary is already clear.
## Actual pipelines
Indexed search:
```text
SearchPanel::Dispatch
→ EngineClient::RequestIndexedSearch
→ UiServer::HandleRequestIndexedSearch
→ IndexPipeline::SearchIndexed
→ SQLite volume metadata lookup only
→ projection candidate/path materialization
→ ParseQuery
→ ExecuteSearch linear scope/filter/match scan + SortResults
→ response materialization
→ indexed-search response serialization / pipe
→ UI response parse + result materialization
```
The indexed search specification intentionally forbids a per-search SQLite entry query. The measured SQLite stage is therefore the small `GetAllVolumes` metadata query; entry matching is entirely projection-based. The request is clamped to 4,096 engine results, and the 1 MiB frame budget can reduce the wire result count further for long paths.
Snapshot publication:
```text
Projection::ForEachEntry / IndexPipeline::ExportDirectorySnapshot
→ SerializeSnapshot
→ SnapshotPublisher inactive-slot copy + generation commit
→ shared-section mapping + ParseSnapshot (UI-side proxy)
```
## Search medians
Warm-run medians; p90 and max are in the JSON. The query classes were selective (`unique_component_7391`), moderate (`report`), broad (`common`), no-match, prefix (`photo`), extension (`ext:txt`), and worst plausible substring (`component`).
| Index | Selective (1 result) | Broad | No match | Extension |
| ---: | ---: | ---: | ---: | ---: |
| 10K | 9.93 ms | 12.49 ms | 9.91 ms | 12.47 ms |
| 100K | 145.07 ms | 145.41 ms | 145.19 ms | 143.10 ms |
| 500K | 923.44 ms | 895.94 ms | 932.91 ms | 894.29 ms |
| 1M | 2,037.15 ms | 1,983.53 ms | 2,098.81 ms | 1,986.99 ms |
The nearly identical selective, broad, and no-match latencies show that total index size—not returned result count—is dominant in the current indexed path. At 1M, broad/extension queries requested 4,096 engine results but the wire parser received 3,955/4,001 because the response approached the 1 MiB frame budget; this is a secondary bounded-result issue.
## Representative 1M attribution
Warm selective query, one result, 2,037.15 ms total engine time:
| Stage | Time | Share |
| --- | ---: | ---: |
| Projection candidate/path materialization | 1,970.48 ms | 96.7% |
| SQLite metadata query | 33.81 ms | 1.7% |
| Match/filter/sort | 32.85 ms | 1.6% |
| Query parse | 0.005 ms | <0.1% |
| Response materialization | 0.002 ms | <0.1% |
| Response serialization | 0.008 ms | <0.1% |
The broad 1M case had the same shape: 1,952.96 ms materialization, 34.79 ms metadata SQL, 4.10 ms match/filter/sort, 0.73 ms response materialization, and 1.05 ms response serialization. Headless UI response parsing/materialization was 0.54/0.49 ms for the wire-truncated 3,955-row result.
## Actual snapshot export and publication
| Index | Export | Entries | Serialized bytes | Serialize | Publish copy/commit | UI map/parse | Outcome |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 10K | 1.84 ms | 9,999 | 1.10 MB | 1.64 ms | 0.42 ms | 1.56 ms | success |
| 100K | 32.03 ms | 99,999 | 11.25 MB | 17.72 ms | 3.95 ms | 18.67 ms | success |
| 500K | 194.11 ms | 499,999 | 57.37 MB | 98.66 ms | 19.57 ms | 100.65 ms | success |
| 1M | 378.76 ms | 999,999 | 115.16 MB | 178.15 ms | 0 ms | 0 ms | rejected oversize |
The isolated snapshot fixture measured approximately 120.6 serialized bytes per row at 500K. Its 500K run took 54.88 ms to build the model, 77.75 ms to serialize, 16.96 ms to publish, and 79.22 ms to map/parse.
## Capacity boundary
Configured slot capacity is 67,108,864 bytes (64 MiB). In the isolated fixture, 556,454 rows serialized to 67,108,794 bytes and published successfully. 556,455 rows serialized to 67,108,915 bytes, returned failure, and left the shared generation unchanged (`14 → 14`). The existing publication safety behavior is preserved; capacity was not increased.
## Memory and interactive typing
| Index | Private after rebuild | Private after broad query | Private retained after queries |
| ---: | ---: | ---: | ---: |
| 10K | 8.7 MB | 8.3 MB | 8.8 MB |
| 100K | 51.2 MB | 53.7 MB | 53.8 MB |
| 500K | 242.2 MB | 320.0 MB | 336.0 MB |
| 1M | 478.4 MB | 678.6 MB | 722.5 MB |
The 1M fixture database was 71.1 MB. No unbounded leak was proven by this short run, but per-query candidate copies add substantial retained allocator/process memory at scale.
At 1M, incremental `p → ph → pho → phot → photo` searches returned 4,096 rows at every step and measured 1,979.76–1,996.67 ms median per request. Every keystroke therefore performs the same full expensive indexed work.
Supersession is generation-safe but not cancellable on the indexed path: UI generation checks discard stale completions, while the engine completes old work and sends its response. A broad old request can therefore consume CPU/memory while a newer selective request waits or competes; no result-order correctness defect was found in the inspected path.
## Path-materialization optimization — historical stage
The 2026-08-10 follow-up keeps the historical baseline above and adds the optimized Release rerun. The resulting path is:
```text
query parse
→ SQLite volume metadata lookup
→ lightweight projection candidate/name/metadata copy
→ cheap filename/metadata predicates
→ lazy full-folder reconstruction only for folder-aware scope/filter survivors or matched results
→ canonical scope/filter/match/sort with the existing 4,096 cap
→ response materialization / serialization
```
The `folder:` predicate and non-all scopes still receive full canonical paths. `AllIndexedLocations` filename/metadata queries do not reconstruct paths for non-matches. Sorting and the cap remain in the existing matcher; unresolved paths are discarded before consuming the cap.
### Required before/after medians
| Scale | Query | Before Median | After Median | Speedup |
| ----: | ----- | ------------: | -----------: | ------: |
| 100K | Selective | 145.07 ms | 46.00 ms | 3.15x |
| 500K | Selective | 923.44 ms | 245.44 ms | 3.76x |
| 1M | Selective | 2,037.15 ms | 470.61 ms | 4.33x |
| 1M | No match | 2,098.81 ms | 472.99 ms | 4.44x |
| 1M | Broad | 1,983.53 ms | 441.56 ms | 4.49x |
Full query-class medians are ordered `selective / moderate / broad / no-match / prefix / extension / worst-substring`:
| Scale | Before (ms) | After (ms) |
| ----: | -----------: | ----------: |
| 10K | 9.93 / 9.84 / 12.49 / 9.91 / 11.16 / 12.47 / 11.54 | 1.68 / 3.74 / 7.41 / 1.73 / 4.01 / 7.23 / 6.63 |
| 100K | 145.07 / 148.77 / 145.41 / 145.19 / 147.24 / 143.10 / 143.46 | 46.00 / 53.47 / 43.93 / 40.64 / 48.49 / 52.04 / 46.68 |
| 500K | 923.44 / 925.13 / 895.94 / 932.91 / 896.63 / 894.29 / 889.99 | 245.44 / 228.69 / 224.66 / 227.59 / 220.23 / 213.83 / 221.02 |
| 1M | 2,037.15 / 1,998.47 / 1,983.53 / 2,098.81 / 1,988.56 / 1,986.99 / 1,993.35 | 470.61 / 446.99 / 441.56 / 472.99 / 449.72 / 446.91 / 447.06 |
### Stage attribution — 1M selective
| Stage | Before | After | % After |
| ----- | -----: | ----: | ------: |
| Full projection parent-path reconstruction | 1,970.48 ms* | 0.014 ms | <0.1% |
| Lightweight name/metadata candidate materialization | included above* | 411.42 ms | 86.6% |
| SQLite metadata query | 33.81 ms | 33.10 ms | 7.0% |
| Match/filter/sort | 32.85 ms | 30.87 ms | 6.5% |
| Query parse | 0.0055 ms | 0.01 ms | <0.1% |
| Response materialization | 0.0021 ms | 0 ms | <0.1% |
| Total | 2,037.15 ms | 475.22 ms | 100% |
`*` The historical implementation measured candidate construction and full parent paths as one stage. The instrumented optimized rerun measured 411.42 ms for lightweight name/metadata candidates, 0.014 ms for one surviving lazy path, and 475.22 ms total. The new bottleneck is therefore candidate name conversion/copying and the linear filename predicate scan, not full parent-path reconstruction.
### Incremental search — 1M
| Query | Before | After | Speedup |
| ----- | -----: | ----: | ------: |
| `p` | 1,996.67 ms | 445.45 ms | 4.48x |
| `ph` | 1,990.67 ms | 448.34 ms | 4.44x |
| `pho` | 1,986.35 ms | 446.03 ms | 4.45x |
| `phot` | 1,990.34 ms | 444.62 ms | 4.48x |
| `photo` | 1,979.76 ms | 446.84 ms | 4.43x |
### Memory — 1M private bytes
| Measurement | Before | After |
| ----------- | -----: | ----: |
| Database/index open | 6.0 MB | 5.5 MB |
| Selective peak | 601.3 MB | 540.4 MB |
| Broad peak | 647.1 MB | 537.0 MB |
| Post-query retained | 689.1 MB | 530.5 MB |
The optimized run preserved the snapshot capacity boundary and supersession behavior; snapshot serialization/publication and cancellation were out of scope for this historical slice. The later audit-remediation work measures and implements both concerns separately.
## Assessment and decision
The first clear unacceptable boundary is between 100K and 500K: 100K is noticeable/responsive only at the upper edge, 500K is poor, and 1M is not instant. 2M/5M were not run because the measured 500K/1M failure boundary already determines the next engineering decision and larger runs would add little attribution value before optimization.
`PRIMARY BOTTLENECK BEFORE: projection candidate/path materialization in IndexPipeline::SearchIndexed`
`PRIMARY BOTTLENECK AFTER: lightweight candidate name/metadata materialization and the linear filename predicate scan`
`OPTIMIZATION REQUIRED: COMPLETED FOR THIS SLICE`
`NEXT SINGLE OPTIMIZATION SLICE: reduce per-entry name conversion/copying with a projection-backed candidate view or exact filename prefilter, guided by the same benchmark; cancellation was deferred at the time of this historical slice and is now covered by the audit-remediation cancellation work.`
This historical path-materialization slice intentionally did not implement the next candidate/name optimization.
## Candidate/name materialization optimization — final 2026-08-10 result
This slice preserves the original baseline and the completed path-materialization optimization above, then removes the remaining all-row owning candidate/name construction. The final path is:
```text
query parse
→ SQLite volume metadata lookup
→ streaming projection row/view traversal
→ direct UTF-16 filename/metadata predicates
→ lazy full-folder reconstruction only for scope/folder survivors or accepted results
→ owning SearchResult materialization only for accepted rows
→ existing ordering, cap-before-sort, response materialization, and serialization
```
The projection lock protects `NamePool` view lifetimes. Built-in predicates have view equivalents; custom predicates without a view factory retain the existing owning fallback. No query syntax, ordering, identity, scope, or result-cap behavior changed.
### Before/after medians for this slice
| Index | Query | Previous Median | New Median | Speedup |
| ----: | ----- | --------------: | ---------: | ------: |
| 100K | Selective | 46.00 ms | 17.88 ms | 2.57x |
| 500K | Selective | 245.44 ms | 132.88 ms | 1.85x |
| 1M | Selective | 470.61 ms | 293.18 ms | 1.61x |
| 1M | No match | 472.99 ms | 300.96 ms | 1.57x |
| 1M | Broad | 441.56 ms | 58.41 ms | 7.56x |
Full query-class medians are ordered `selective / moderate / broad / no-match / prefix / extension / worst-substring`:
| Scale | Previous (ms) | New (ms) |
| ----: | ------------: | --------: |
| 10K | 1.68 / 3.74 / 7.41 / 1.73 / 4.01 / 7.23 / 6.63 | 0.68 / 2.75 / 6.42 / 0.73 / 2.88 / 6.07 / 4.81 |
| 100K | 46.00 / 53.47 / 43.93 / 40.64 / 48.49 / 52.04 / 46.68 | 17.88 / 21.87 / 14.55 / 18.67 / 21.79 / 13.37 / 16.07 |
| 500K | 245.44 / 228.69 / 224.66 / 227.59 / 220.23 / 213.83 / 221.02 | 132.88 / 46.48 / 36.36 / 135.76 / 46.82 / 35.30 / 38.89 |
| 1M | 470.61 / 446.99 / 441.56 / 472.99 / 449.72 / 446.91 / 447.06 | 293.18 / 69.74 / 58.41 / 300.96 / 69.91 / 58.60 / 61.17 |
### Stage attribution — 1M selective
| Stage | Previous | New | % New |
| ----- | -------: | ---: | -----: |
| Full parent-path reconstruction | 0.014 ms | 0.008 ms | <0.1% |
| Lightweight owning candidate/name work | 411.42 ms | 0.003 ms | <0.1% |
| Projection traversal + direct matching/filter/sort | 30.87 ms | 271.79 ms | 89.1% |
| SQLite metadata query | 33.10 ms | 33.24 ms | 10.9% |
| Query parse | 0.010 ms | 0.007 ms | <0.1% |
| Response materialization | 0.002 ms | 0.001 ms | <0.1% |
| Total sampled run | 475.22 ms | 305.05 ms | 100% |
The sampled new total is higher than the 293.18 ms warm median because the table uses one instrumented run for attribution. The 411 ms all-row candidate/name stage is removed; the new dominant work is raw projection traversal plus the linear direct filename predicate scan.
### Allocation/materialization counts — 1M selective
| Item | Previous | New |
| ---- | -------: | ---: |
| Projection rows examined | 1,000,000 | 1,000,000 |
| Owning candidate objects | 1,000,000 | 1 |
| Owning name copies | 1,000,000 | 1 |
| Path materializations | 1 | 1 |
| SearchResult objects | 1 | 1 |
Broad queries retain only the accepted capped set: the final run examined 13,779 rows before the exact 4,096 cap, materialized 4,096 owning candidates/names/results, and preserved the historical wire-truncated 3,955-row response.
### Incremental search — 1M
| Query | Previous | New | Speedup |
| ----- | -------: | ---: | ------: |
| `p` | 445.45 ms | 55.44 ms | 8.03x |
| `ph` | 448.34 ms | 70.53 ms | 6.36x |
| `pho` | 446.03 ms | 70.85 ms | 6.29x |
| `phot` | 444.62 ms | 71.04 ms | 6.26x |
| `photo` | 446.84 ms | 70.41 ms | 6.35x |
### Memory — 1M private bytes
| Measurement | Previous | New |
| ----------- | -------: | ---: |
| Database/index open | 5.5 MB | 6.0 MB |
| Selective peak | 540.4 MB | 466.3 MB |
| Broad peak | 537.0 MB | 473.9 MB |
| Post-query retained | 530.5 MB | 475.3 MB |
### Semantic equivalence
Focused deterministic engine/search coverage remains passing for selective, no-match, broad/capped, filename, parent-path, extension, prefix/glob, case-insensitive, multi-term, nested duplicate names, path ordering, folder predicates, current-folder scope, Unicode/locale-independent matching, surrogate-pair wildcard behavior, and disabled/unavailable-volume boundaries. The view matcher is directly checked against the owning matcher for built-in predicates.
`PRIMARY BOTTLENECK AFTER CANDIDATE OPTIMIZATION: raw projection traversal plus direct UTF-16 filename matching/filtering (271.79 ms, 89.1% of the sampled 1M selective search)`
`NEXT SINGLE OPTIMIZATION SLICE: reduce projection iteration/name-predicate scan cost, guided by dense live-entry traversal or a narrowly measured exact filename prefilter; SQLite schema work and cancellation remain separate.`
The candidate/name materialization optimization is complete; this benchmark does not implement the next slice.
## Projection scan / direct filename matching optimization — final 2026-08-10
This slice preserves the preceding original → path-materialization → candidate/name-materialization history. It optimizes the remaining projection walk without changing the query language, owning-result boundary, lazy paths, ordering, or cap-before-sort behavior.
### Root cause and broad/selective explanation
The old 1M selective stage was approximately `271.79 ms` (`89.1%` of the sampled search). Detailed diagnostics decomposed it into overlapping work: about `102.6 ms` of NamePool lookup timing, `23.2 ms` of name-view construction timing, and `63.7 ms` of direct predicate/substring timing; the combined stage is the authoritative wall-clock measure. No-match traversed the same 1,000,000 rows and performed `2,872,240` ordinal comparison windows. Broad was different because the existing cap-before-sort behavior stopped after exactly 4,096 accepted results, at 13,779 examined rows; it did not scan the full million.
The final implementation makes rebuilt projection entries follow the original live hash-bucket order in a dense vector and remaps the ID and parent-child indices. A valid dense-order flag is used for read-only scans; after mutations, direct live-slot iteration preserves the previous order. The matcher recognizes a single simple literal name term, folds query-side ASCII once, stores only a one-bit-equivalent entry property indicating an ASCII lowercase-compatible name, and uses an allocation-free anchor search in that case. Uppercase/non-ASCII names and complex terms retain the canonical ordinal/Unicode fallback. No lowercase filename copies are stored.
### Final search pipeline
```text
query parse and query-side literal preparation
→ SQLite volume metadata lookup
→ projection lock and dense live-entry/view traversal
→ prepared simple-literal matcher or canonical generic view predicates
→ lazy folder/path materialization only for survivors
→ owning result materialization, existing 4,096 cap, and sort
→ response serialization
```
NamePool access remains a UTF-16 view. Simple literal queries use the prepared query term and an ASCII anchor when the entry property proves the exact fast-path precondition; all other cases use the existing ordinal case-insensitive semantics. Result and path ownership remain limited to accepted rows and path-dependent predicates.
### Before/after medians
| Index | Query | Before | After | Speedup |
| ----: | ----- | -----: | ----: | ------: |
| 100K | Selective | 17.88 ms | 7.40 ms | 2.42x |
| 500K | Selective | 132.88 ms | 57.29 ms | 2.32x |
| 1M | Selective | 293.18 ms | 130.76 ms | 2.24x |
| 1M | No match | 300.96 ms | 172.52 ms | 1.74x |
| 1M | Broad | 58.41 ms | 51.09 ms | 1.14x |
| 1M | Worst substring | 61.17 ms | 52.87 ms | 1.16x |
| 1M | Extension | 58.60 ms | 51.74 ms | 1.13x |
Full query-class medians remain in the machine-readable artifact. Ordered `selective / moderate / broad / no-match / prefix / extension / worst-substring`:
| Scale | Before (ms) | After (ms) |
| ----: | -----------: | ----------: |
| 10K | 0.68 / 2.75 / 6.42 / 0.73 / 2.88 / 6.07 / 4.81 | 0.49 / 2.24 / 5.35 / 0.49 / 2.28 / 4.91 / 3.83 |
| 100K | 17.88 / 21.87 / 14.55 / 18.67 / 21.79 / 13.37 / 16.07 | 7.40 / 15.87 / 12.27 / 7.23 / 16.40 / 12.42 / 13.40 |
| 500K | 132.88 / 46.48 / 36.36 / 135.76 / 46.82 / 35.30 / 38.89 | 57.29 / 37.84 / 32.62 / 73.78 / 39.68 / 32.42 / 32.63 |
| 1M | 293.18 / 69.74 / 58.41 / 300.96 / 69.91 / 58.60 / 61.17 | 130.76 / 59.07 / 51.09 / 172.52 / 60.31 / 51.74 / 52.87 |
### Stage attribution — 1M selective
The normal warm sample below is paired with a separate projection-only probe. Diagnostic per-operation clocks are intentionally non-additive and perturb wall time; they are included to identify operations, not to replace the normal median.
| Stage | Before | After | % After |
| ----- | -----: | ----: | ------: |
| SQLite metadata | 33.24 ms | 33.98 ms | 26.4% |
| Projection-only traversal floor | not isolated | 54.28 ms | 42.1% |
| Direct literal matching/filter residual | included in 271.79 ms | ~40.54 ms | 31.5% |
| Combined projection traversal + matching | 271.79 ms | 94.82 ms | 73.6% |
| Lazy path work | 0.008 ms | 0.012 ms | <0.1% |
| Result handling | 0.003 ms | 0.016 ms | <0.1% |
| Total sampled search | 305.05 ms | 128.81 ms | 100% |
The normal final 1M selective median is `130.76 ms`. The paired sample examined 1,000,000 rows, performed 999,999 NamePool lookups and 1,000,000 view/predicate evaluations, accepted one row, and materialized one candidate/name/result with one lazy path. The no-match warm sample measured `138.01 ms` for the combined projection/matching stage; subtracting the `54.28 ms` projection-only floor leaves an approximately `83.73 ms` failed-literal matching/filter residual. Its diagnostic run counted 1,000,000 failed substring searches, 1,000,000 early rejects, and zero path lookups. This is the remaining dominant operation.
### Incremental search — 1M
| Query | Before | After | Speedup |
| ----- | -----: | ----: | ------: |
| `p` | 55.44 ms | 49.99 ms | 1.11x |
| `ph` | 70.53 ms | 59.55 ms | 1.18x |
| `pho` | 70.85 ms | 58.74 ms | 1.21x |
| `phot` | 71.04 ms | 58.53 ms | 1.21x |
| `photo` | 70.41 ms | 59.18 ms | 1.19x |
### Memory and semantic equivalence
The clean isolated 1M run measured private memory at approximately `4.4 / 454.0 / 470.4 / 471.2 MiB` for open / rebuild / after broad / post-query, versus the prior `6.0 / 466.3 / 473.9 / 475.3 MiB`. The canonical multi-scale artifact records `6.4 / 455.2 / 487.7 / 490.1 MiB`; the higher post-broad values are allocator-history effects from retaining the earlier 10K/100K/500K runs in the same process, not persistent projection storage. The isolated measurement is the comparable footprint check and shows no material regression.
| Scenario | Result |
| -------- | ------ |
| selective | PASS |
| no match | PASS |
| broad / capped | PASS |
| exact 4,096 result set | PASS |
| filename substring | PASS |
| parent path | PASS |
| extension | PASS |
| prefix | PASS |
| glob | PASS |
| ASCII case-insensitive | PASS |
| Unicode/non-ASCII | PASS |
| multi-term | PASS |
| metadata/folder predicate | PASS |
| duplicate names | PASS |
| result ordering | PASS |
Focused deterministic search tests pass, including new uppercase-ASCII fast-path/fallback agreement and non-ASCII Unicode fallback cases. Existing engine tests continue to cover parent-path-only queries, folder scope, glob, extension, metadata, duplicate names, ordering, and the 4,096 cap.
### Larger scale
2M: NOT TESTED
5M: NOT TESTED
### Assessment and next slice
`PRIMARY BOTTLENECK BEFORE: combined raw projection traversal plus direct UTF-16 matching/filtering (271.79 ms sampled)`
`PRIMARY BOTTLENECK AFTER: failed direct literal filename matching/filtering (~83.73 ms residual in the 1M no-match sample)`
`NEXT SINGLE OPTIMIZATION SLICE: evaluate an evidence-backed secondary filename-search index/FTS design or a narrowly targeted failed-literal matcher improvement; do not implement it in this slice.`
At the time of this historical artifact, cancellation, snapshot changes, SQL schema redesign, MFT/USN activation, and security/signing work remained deferred. The current repository contains subsequent audit-remediation work for cancellation and snapshot publication; the historical measurements below are retained for attribution.
## Failed literal filename matcher optimization
This follow-up slice measured and optimized only failed direct literal filename matching. The old simple-literal path pre-folded an ASCII query once, selected a rare anchor, scanned each ASCII-compatible name with `u16string_view::find` plus folded-view equality, and used a per-window `CompareStringOrdinal(..., TRUE)` fallback for uppercase/non-ASCII cases. It already rejected a needle longer than the candidate name before searching.
The isolated 1M matcher benchmark used the real synthetic filename distribution, plus short/long, late-match, common-prefix, and Unicode cases:
| Strategy | Failed | Early | Late | Short 2–4 | Long | Common prefix | Unicode fallback |
| -------- | -----: | ----: | ---: | --------: | ----: | ------------: | ----------------: |
| Current anchor | 19.1 ms | 22.7 ms | 26.6 ms | 25.5 ms | 16.0 ms | 21.7 ms | exact fallback |
| First + last rejection | 16.8 ms | 23.6 ms | 22.7 ms | 22.5–25.0 ms | 15.2 ms | 46.6 ms | exact fallback |
| BMH candidate | 14.5 ms | 21.0 ms | 25.0 ms | 25.5–35.0 ms | 12.6 ms | 16.5 ms | exact fallback |
The selected implementation stores a 128-entry BMH shift table in the prepared `Query`. It is used only for ASCII-compatible names and folded needles of length 4 or more; 1–3 character needles retain the existing anchor path, and all non-ASCII/uppercase cases retain the exact ordinal fallback. The no-diagnostics path also avoids the benchmark diagnostics scope. No persistent lowercase filename copy or secondary index was added.
### Final search evidence
| Scale | Query | Before | After | Speedup |
| ----: | ----- | -----: | ----: | ------: |
| 100K | Selective | 7.23 ms | 6.59 ms | 1.10x |
| 500K | Selective | 72.46 ms | 58.33 ms | 1.24x |
| 1M | Selective | 131.06 ms | 126.99 ms | 1.03x |
| 1M | No match | 166.33 ms | 147.90 ms | 1.12x |
| 1M | Broad | 48.83 ms | 48.82 ms | 1.00x |
| 1M | Worst substring | 50.92 ms | 49.95 ms | 1.02x |
| 1M | Extension | 50.27 ms | 49.30 ms | 1.02x |
Final 1M p90/max were selective `129.34/130.71 ms`, no-match `149.38/152.35 ms`, broad `49.99/50.73 ms`, and worst-substring `50.13/51.20 ms`. Incremental `p/ph/pho/phot/photo` was `46.97/56.26/56.38/55.62/55.70 ms` median; cancellation was intentionally a separate future concern for this historical slice and is now addressed by the audit-remediation work.
### Attribution and decision
Using the established projection-only floor (`54.28 ms`) and SQLite metadata floor (`33.98 ms`), the 1M no-match failed-literal residual is approximately `59.64 ms` after this slice, versus approximately `83.73 ms` before it. The combined lower bound is already about `88 ms` before meaningful result work. The full scan remains linear in projection size and still costs `147.90 ms` for a guaranteed no-match query; a secondary filename-search design must therefore be evaluated before further matcher micro-optimization.
`ARCHITECTURAL DECISION: SECONDARY FILENAME INDEX EVALUATION REQUIRED`
`NEW PRIMARY BOTTLENECK: full O(N) projection traversal plus residual literal rejection`
## Secondary filename index integration — 2026-08-11
The measured UTF-16 trigram posting design is now integrated into the production indexed-search path. The new historical artifact is [large-index-scalability-secondary-index.json](large-index-scalability-secondary-index.json); the original `large-index-scalability.json` remains unchanged so the optimization history is preserved.
The current pipeline is:
```text
ParseQuery
→ GetAllVolumes metadata query
→ projection lock
→ eligible simple-literal UTF-16 trigram candidate lookup
→ canonical filename/path/predicate verification
→ existing result cap, ordering, and response
```
Only simple literal filename terms with at least three ASCII-foldable UTF-16 units use the accelerator. Short terms, Unicode terms, globs, multi-term queries, path predicates, extension predicates, metadata predicates, and any unavailable/invalid index use the existing canonical scan. The index is a candidate generator, never the semantic authority; exact result sets and ordering remain owned by the canonical matcher.
### Integrated Release medians
| Scale | Selective | No match | Broad |
| ----: | --------: | -------: | ----: |
| 100K | 1.93 ms | 2.02 ms | 10.99 ms |
| 500K | 17.71 ms | 18.06 ms | 29.15 ms |
| 1M | 34.34 ms | 36.25 ms | 47.83 ms |
| 2M | 64.91 ms | 67.84 ms | 81.36 ms |
At 2M, the selective query verified one candidate and the no-match query verified zero candidates; both still measured approximately 65–68 ms because the `GetAllVolumes` SQLite metadata query is now the dominant stage. Broad search used 590,624 candidates and retained the existing 4,096-result cap. The query summaries include p90/max values and all seven benchmark classes.
The integrated result improves the prior same-harness 1M medians of approximately `126.99/147.90/48.82 ms` (selective/no-match/broad) to `34.34/36.25/47.83 ms`. Selective and no-match no longer scale with the full projection row count; broad remains governed by candidate verification and result materialization.
### Lifecycle and resource behavior
`Projection` owns the postings beside its generation-owned dense slots. Rebuilds bulk-load and sort/deduplicate postings after compaction; insert, delete, and rename update postings incrementally. Allocation or update failure invalidates only the accelerator, and search falls back to the canonical linear path. Focused tests cover duplicate/uppercase/non-ASCII recall, empty no-match, short and Unicode fallback, delete, and rename behavior; engine tests cover eligible, no-match, short-term, and folder-query routing.
The final artifact reports private bytes after rebuild of approximately `59.3/280.7/551.9/1120.6 MiB` at `100K/500K/1M/2M`, with post-query values of `61.3/292.7/565.4/1294.2 MiB`. The isolated architecture benchmark estimated 168.6 MiB of trigram postings at 2M; the integrated process also retains the existing SQLite/projection/snapshot working set. The 2M rebuild took approximately 5.0 seconds and the temporary database occupied 150,564,864 bytes. No schema migration or persistent secondary-index file was introduced.
This historical slice did not optimize SQLite or add search cancellation. Those were separate decisions requiring fresh measurements rather than attribution from the pre-index path; the current audit-remediation work adds cancellation with dedicated regression and latency coverage.
No 2M run was performed because the 1M selective result remained above the optional scale-up gate. An isolated 1M memory check measured approximately `4.9 / 453.8 / 468.8 / 470.1 MiB` for open / rebuild / after broad / post-query, with no material regression against the prior isolated `4.4 / 454.0 / 470.4 / 471.2 MiB` check. Focused and full semantic tests passed for selective/no-match, early/late matching, short and overlong needles, ASCII case-insensitivity, uppercase fallback, Unicode/mixed Unicode, prefix, glob, extension, parent path, multi-term, duplicate names, capped 4,096 results, and ordering.
## SQL metadata no-count path — 2026-08-11
The secondary-index artifact showed that `GetAllVolumes()` had become the dominant stage because it counted every entry for every volume even though indexed search only needs volume identity, availability, and the resolved root key. The focused change adds `GetAllVolumesWithoutEntryCounts()` and routes both indexed-search branches through it. Rebuild, public metadata, and unavailable-volume reporting retain the counted API and its existing semantics; no schema migration or entry-count cache was added.
The final historical artifact is [large-index-scalability-sql-no-count.json](large-index-scalability-sql-no-count.json). It preserves the previous secondary-index artifact and records the same Release harness at 100K/500K/1M/2M:
| Scale | Selective | No match | Broad |
| ----: | --------: | -------: | ----: |
| 100K | 0.03 ms | 0.06 ms | 8.82 ms |
| 500K | 0.50 ms | 0.62 ms | 11.89 ms |
| 1M | 0.64 ms | 0.63 ms | 14.29 ms |
| 2M | 0.55 ms | 2.24 ms | 17.55 ms |
At 2M, the eligible selective query still used one trigram candidate and the no-match query used zero; SQLite metadata timing was approximately `0.10 ms`, down from approximately `65–68 ms`. Broad search retained the existing 4,096-result cap and used 590,624 candidates. The result is now below the low-tens-of-milliseconds engineering target for eligible selective/no-match queries through 2M; no further SQL redesign is justified by this measurement.
The benchmark process exited after writing the complete artifact when the external 120-second command deadline expired; post-run checks found no live benchmark process, no leftover disposable benchmark directories, and valid JSON with 4 datasets, 179 runs, and 28 summaries. Focused Store/engine tests passed before the benchmark. Full Debug/Analyze/CTest validation and the bounded product commit remain the next gate for this slice.
## Phase 9 non-blocking read-plane evidence — 2026-08-24
The benchmark now records `search_generation`, generation restarts, state-lock
wait/maximum hold time, peak searches/readers, cumulative read-plane counters,
and a deterministic contention probe. The probe uses a real cold query parked
at the durable-stream boundary. While it is parked, the existing pipeline
executes an MFT ingestion batch, a durable folder aggregate, a directory
snapshot export, and a second indexed search; the first query is then
cancelled and its reader is released. These are evidence fields, not brittle
latency gates.
The first Phase 9 run used the established 10K fixture on the same host and
compared the current refactor with the immediately preceding Phase 8 binary:
| 10K case | Phase 8 | Phase 9 | Interpretation |
| --- | ---: | ---: | --- |
| warm selective | 0.64 ms | 0.48 ms | same-harness reference; no fast-path regression observed |
| cold broad | 806.99 ms | 742.34 ms | cold streaming remains expensive but is isolated from writers |
The Phase 9 contention probe recorded: ingestion `2.07 ms`, aggregate
`81.55 ms`, snapshot export `311.74 ms`, second query `365.62 ms`, all
completed while the first search was held for `365.98 ms`; cancellation took
`0.24 ms`. The probe reached the cold stream, reported two peak durable
readers, ended with zero active readers, and published no partial result.
The cumulative read-plane counters ended at two peak searches, 99 completed
searches, one cancellation, zero unavailable searches, and zero reader
failures. The 10K post-query private bytes were approximately 13.6 MiB, with
99 handles and four process threads; the WAL measured 20,632 bytes at the
post-query sample. The forced-eviction projection estimate remained
`4,297,568 → 196,072` bytes.
The harness still retains the 100K/500K/1M/2M default scales. A complete
multi-scale Phase 9 rerun is evidence work rather than a CTest gate; the
mandatory architectural result is already structural: a deterministic
long-running cold read no longer holds the mutation/lifecycle critical
section while ingestion, aggregate, snapshot, another search, and
cancellation make progress.
### Final rerun and regression investigation
Two final 10K runs used the same temporary-database harness and machine. The
earlier Phase 9 run remains the clean comparison point; the two final runs
were collected while the host also had several high-CPU agent/VM processes, so
their wall-clock values are recorded as noisy evidence rather than acceptance
thresholds:
| Case | Phase 9 reference | Final run A | Final run B |
| --- | ---: | ---: | ---: |
| warm selective median | 0.53 ms | 0.95 ms | 0.75 ms |
| warm broad average | 183.42 ms | 232.69 ms | 229.99 ms |
| cold selective | 124.78 ms | 150.43 ms | 190.56 ms |
| cold broad | 742.34 ms | 981.03 ms | 996.03 ms |
The stage counters show the variance is dominated by projection traversal and
durable stream wall time, not an unbounded synchronization hold: the final
warm-broad runs reported maximum state-lock holds of `17.12 ms` and
`15.88 ms`, with zero generation restarts; the cold-broad runs reported
`0.50 ms` and `0.56 ms` maximum state-lock holds. A process snapshot during
the rerun confirmed substantial unrelated host CPU contention. No semantic or
algorithmic regression was identified, and no brittle timing threshold was
added. The structural contention probe, bounded memory/readers, and exact
search assertions remain the Phase 9 acceptance evidence.
## Phase 10 sustained-write availability evidence — 2026-08-24
Phase 10 extends this same benchmark rather than creating a separate harness.
The final local Debug probe used `--scales 1000 --diagnostics` and the real
`IndexPipeline`/SQLite/WAL/search path. Its machine-readable output was
`build/debug/phase10-large-index-scalability-final.json`; the generated file
is execution evidence and is intentionally not source-controlled.
The representative fast-path and cold-path measurements were:
| 1K case | Median/elapsed | Results | Interpretation |
| --- | ---: | ---: | --- |
| hot selective | 1.49 ms | 0 | bounded projection/trigram path |
| hot broad | 18.06 ms | 296 | bounded top-N broad query |
| cold selective | 15.51 ms | 0 | durable stream after eviction |
| cold broad | 67.48 ms | 296 | durable stream after eviction |
The sustained-write broad query intentionally committed four searchable MFT
mutations during the old hybrid validation budget. It completed with 296
results after four restarts and four relevant invalidations, then made one
durable-fallback attempt that streamed 1,004 rows from one coherent SQLite
snapshot and succeeded. It did not return `Unavailable`. The matching
durable-fallback cancellation probe also crossed the four-restart boundary,
entered fallback, was cancelled, published zero results, and recorded one
fallback attempt with no successful completion. These are structural outcomes;
the elapsed values are host measurements, not acceptance thresholds.
The contention probe held a real cold query while ingestion, a folder
aggregate, snapshot publication, a second query, and cancellation progressed.
All five operations completed; measured samples were ingestion `0.55 ms`,
aggregate `26.99 ms`, snapshot `27.32 ms`, second query `46.75 ms`, and
cancellation `0.31 ms`. Peak searches were `2`, peak durable readers were `2`,
and cleanup ended with zero active searches/readers. The final read-plane
diagnostics reported zero unavailable searches and zero reader failures, two
fallbacks (one successful, one cancelled), and 1,004 fallback rows streamed.
The run also retained the existing snapshot capacity probe: 1,980,421 rows
published successfully below the 256 MiB payload ceiling; the estimated
2,200,468-row boundary and the next row were rejected without advancing the
published generation. The benchmark continues to exercise memory samples,
hot/cold selective and broad queries, sustained fallback, cancellation, and
contention without adding noisy wall-clock CTest thresholds. Larger historical
Phase 8/9 scales remain preserved above and in their machine-readable artifacts.
Now let me look at the 2M run evidence for feasibility numbers.
grep: C:/Users/Michael Roy/Documents/FastFiles/docs/benchmarks/...
grep: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
phase8-bench-run-2m.log:2: Building 2000000 synthetic entries...
phase8-bench-run-2m.log:3:   selective          median 2305.27 ms, results 1
phase8-bench-run-2m.log:4:   moderate           median 3157.36 ms, results 4096
phase8-bench-run-2m.log:5:   broad              median 4672.65 ms, results 4096
phase8-bench-run-2m.log:6:   no_match           median 2322.41 ms, results 0
phase8-bench-run-2m.log:7:   prefix             median 3185.00 ms, results 4096
phase8-bench-run-2m.log:8:   extension          median 4389.07 ms, results 4096
phase8-bench-run-2m.log:9:   worst_substring    median 3802.04 ms, results 4096
phase8-bench-run-2m.log:10:   forced eviction: estimated 903299344 -> 26242480 bytes
phase8-bench-run-2m.log:11:   selective          COLD 2374.19 ms, results 1, coldRows 2000000, stream 2374.11 ms
phase8-bench-run-2m.log:12:   moderate           COLD 12737.83 ms, results 4096, coldRows 2000000, stream 12733.78 ms
phase8-bench-run-2m.log:13:   broad              COLD 29827.55 ms, results 4096, coldRows 2000000, stream 29822.81 ms
phase8-bench-run-2m.log:14:   no_match           COLD 2374.54 ms, results 0, coldRows 2000000, stream 2374.46 ms
phase8-bench-run-2m.log:15:   prefix             COLD 11551.62 ms, results 4096, coldRows 2000000, stream 11547.02 ms
phase8-bench-run-2m.log:16:   extension          COLD 30912.30 ms, results 4096, coldRows 2000000, stream 30908.87 ms
phase8-bench-run-2m.log:17:   worst_substring    COLD 27026.59 ms, results 4096, coldRows 2000000, stream 27023.95 ms
phase8-bench-run-2m.log:18: Completed 2000000; database 145.9 MB, generation 115.8 s, rebuild 20.9 s
search-matrix-after-rebuild-fix-control.json-133-     {"dataset_size":200000,"query_class":"high_limit_broad","query":"common","temperature":"warm","iteration":7,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0... [truncated]
search-matrix-after-rebuild-fix-control.json-134-     {"dataset_size":200000,"query_class":"high_limit_broad","query":"common","temperature":"warm","iteration":8,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0... [truncated]
search-matrix-after-rebuild-fix-control.json-135-     {"dataset_size":200000,"query_class":"high_limit_broad","query":"common","temperature":"warm","iteration":9,"result_count":4096,"candidate_count":200000,"candidates_visited":19573,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0... [truncated]
search-matrix-after-rebuild-fix-control.json:136:     {"dataset_size":200000,"query_class":"selective","query":"unique_component_7391","temperature":"cold","iteration":0,"result_count":1,"candidate_count":200000,"candidates_visited":200000,"truncated":false,"cancelled":false,"unavailable":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attemp... [truncated]
search-matrix-after-rebuild-fix-control.json-137-     {"dataset_size":200000,"query_class":"moderate","query":"report","temperature":"cold","iteration":0,"result_count":4096,"candidate_count":200000,"candidates_visited":200000,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durab... [truncated]
search-matrix-after-rebuild-fix-control.json-138-     {"dataset_size":200000,"query_class":"broad","query":"common","temperature":"cold","iteration":0,"result_count":4096,"candidate_count":200000,"candidates_visited":200000,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durable_... [truncated]
search-matrix-after-rebuild-fix-control.json-139-     {"dataset_size":200000,"query_class":"no_match","query":"zzzz_never_present","temperature":"cold","iteration":0,"result_count":0,"candidate_count":200000,"candidates_visited":200000,"truncated":false,"cancelled":false,"unavailable":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":... [truncated]
search-matrix-after-rebuild-fix-control.json-209-     {"dataset_size":1000000,"query_class":"high_limit_broad","query":"common","temperature":"warm","iteration":2,"result_count":4096,"candidate_count":1000000,"candidates_visited":52615,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts"... [truncated]
search-matrix-after-rebuild-fix-control.json-210-     {"dataset_size":1000000,"query_class":"high_limit_broad","query":"common","temperature":"warm","iteration":3,"result_count":4096,"candidate_count":1000000,"candidates_visited":52615,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts"... [truncated]
search-matrix-after-rebuild-fix-control.json-211-     {"dataset_size":1000000,"query_class":"high_limit_broad","query":"common","temperature":"warm","iteration":4,"result_count":4096,"candidate_count":1000000,"candidates_visited":52615,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts"... [truncated]
search-matrix-after-rebuild-fix-control.json:212:     {"dataset_size":1000000,"query_class":"selective","query":"unique_component_7391","temperature":"cold","iteration":0,"result_count":1,"candidate_count":1000000,"candidates_visited":1000000,"truncated":false,"cancelled":false,"unavailable":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_att... [truncated]
search-matrix-after-rebuild-fix-control.json-213-     {"dataset_size":1000000,"query_class":"moderate","query":"report","temperature":"cold","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":1000000,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"du... [truncated]
search-matrix-after-rebuild-fix-control.json-214-     {"dataset_size":1000000,"query_class":"broad","query":"common","temperature":"cold","iteration":0,"result_count":4096,"candidate_count":1000000,"candidates_visited":1000000,"truncated":true,"cancelled":false,"unavailable":false,"wire_result_count":4096,"wire_truncated":true,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"durab... [truncated]
search-matrix-after-rebuild-fix-control.json-215-     {"dataset_size":1000000,"query_class":"no_match","query":"zzzz_never_present","temperature":"cold","iteration":0,"result_count":0,"candidate_count":1000000,"candidates_visited":1000000,"truncated":false,"cancelled":false,"unavailable":false,"wire_result_count":0,"wire_truncated":false,"filename_index_used":false,"filename_index_candidate_count":0,"search_generation":5,"query_visible_epoch":5,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempt... [truncated]
[Some lines truncated to 500 chars. Use read tool to see full lines]
Let me verify harness build registration, core-pinning/affinity, and the idle-gate definition.
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/CMa...
grep: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
No matches found
CMakeLists.txt-252- # Informational only: explicit large-index scalability evidence, never a CTest
CMakeLists.txt-253- # wall-clock gate and never run by the normal test suite.
CMakeLists.txt:254: add_executable(ffengine_bench_large_index_scalability
CMakeLists.txt-255-   bench_large_index_scalability.cpp
CMakeLists.txt-256-   ${CMAKE_SOURCE_DIR}/src/engine/src/IndexPipeline.cpp
CMakeLists.txt-253- # wall-clock gate and never run by the normal test suite.
CMakeLists.txt-254- add_executable(ffengine_bench_large_index_scalability
CMakeLists.txt:255:   bench_large_index_scalability.cpp
CMakeLists.txt-256-   ${CMAKE_SOURCE_DIR}/src/engine/src/IndexPipeline.cpp
CMakeLists.txt-257-   ${CMAKE_SOURCE_DIR}/src/engine/src/VolumeIdentity.cpp
CMakeLists.txt-258-   ${CMAKE_SOURCE_DIR}/src/engine/src/SnapshotPublisher.cpp
CMakeLists.txt-259- )
CMakeLists.txt:260: target_include_directories(ffengine_bench_large_index_scalability PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
CMakeLists.txt-261- target_link_libraries(ffengine_bench_large_index_scalability PRIVATE ffindexstore ffprotocol ffsearch ffsetup psapi Rpcrt4 Shell32 Ole32)
CMakeLists.txt-262- # The memory report embeds ffsetup::kBuildIdentityAscii; a targeted build must
CMakeLists.txt-259- )
CMakeLists.txt-260- target_include_directories(ffengine_bench_large_index_scalability PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
CMakeLists.txt:261: target_link_libraries(ffengine_bench_large_index_scalability PRIVATE ffindexstore ffprotocol ffsearch ffsetup psapi Rpcrt4 Shell32 Ole32)
CMakeLists.txt-262- # The memory report embeds ffsetup::kBuildIdentityAscii; a targeted build must
CMakeLists.txt-263- # refresh the generated identity first or the report could claim a stale tree.
CMakeLists.txt-262- # The memory report embeds ffsetup::kBuildIdentityAscii; a targeted build must
CMakeLists.txt-263- # refresh the generated identity first or the report could claim a stale tree.
CMakeLists.txt:264: add_dependencies(ffengine_bench_large_index_scalability fastfiles_build_identity)
CMakeLists.txt-265- 
CMakeLists.txt-266- # Informational only: compares bounded secondary filename-index candidates
adversarial-review-2026-10-21.md-27- | B P2-3 | Interception gate required a CF_UNICODETEXT witness the product never publishes (skip unreachable; comment overclaimed evidence) | P2 | gate over the product's actual write set + no readable handle anywhere; comment cites the real late-delivery evidence | case 7 (updated) |
adversarial-review-2026-10-21.md-28- | B P3-4/5/6 | Probe bookkeeping invariant; observation-identical controls; window diagnostics | P3 | bookkeeping fixed; direction-distinct controls; attempts-before-exact recorded | controls 3-6 updated |
adversarial-review-2026-10-21.md-29- | E E-1/E-2 | Evidence noise-floor claim off ~an order of magnitude; cold "medians" were n=1 | P1 (evidence truth) | `memory-cold-decision.md` corrected (same-block control -9%..+6%; n=1 samples; hot medians 10/5 runs) | n/a (documentation) |
adversarial-review-2026-10-21.md:30: | E E-3..E-8 | Scan-floor number, rebuild band phrasing, 189x scope conditions, attribution tautology, 1M-cold context, artifact provenance | P2/P3 | all corrected in the evidence docs | n/a |
adversarial-review-2026-10-21.md-31- 
adversarial-review-2026-10-21.md-32- ## Honest open items (not fixed here, no material risk claimed)
adversarial-review-2026-10-21.md-33- 
lifecycle-reload-resources.md-48- `rebuild_cycles[1..10]` per scale, each a full replacement rebuild through
lifecycle-reload-resources.md-49- `RebuildVolumeFromStore` (the production mass-removal + re-ingest path):
lifecycle-reload-resources.md-50- 
lifecycle-reload-resources.md:51: - **Rebuild revalidation (lane E E-4 correction):** 1M rebuild cycles measure
lifecycle-reload-resources.md-52-   4.9-9.0 s vs the pre-fix 158-164 s. The honest band is **28-32x typical**
lifecycle-reload-resources.md-53-   (6 of 10 cycles at 4.9-5.7 s), **>=17.7x worst observed** (one unattributed
lifecycle-reload-resources.md-54-   4-cycle transient to 6.5-9.0 s; no host telemetry was collected, so the
memory-cold-decision.md-134- - Decision: **RETAIN** (correct; 1.28-1.44x on cold literal classes at 10%
memory-cold-decision.md-135-   non-ASCII vs a -9%..+6% same-block control, ~1.3-1.4x expected on real
memory-cold-decision.md-136-   corpora; zero schema/memory risk). Scope conditions on the prior hot
memory-cold-decision.md:137:   acceleration (lane E E-5): the 56-189x figure is hot/projection-resident,
memory-cold-decision.md-138-   planner-eligible literal needles only (best case 189x = 1-result ASCII
memory-cold-decision.md-139-   literal at 1M; rows examined 1M->1), it repairs a planner bug that had
memory-cold-decision.md-140-   disabled the accelerator entirely, and it does NOT apply to cold queries
memory-cold-decision.md-139-   literal at 1M; rows examined 1M->1), it repairs a planner bug that had
memory-cold-decision.md-140-   disabled the accelerator entirely, and it does NOT apply to cold queries
memory-cold-decision.md-141-   or the >=2M regime, where all queries serve from the durable stream and
memory-cold-decision.md:142:   latency is governed by the scan floor above. Lane E E-7: the 1M cold table
memory-cold-decision.md-143-   characterizes the production cold-stream cost model under a state that at
memory-cold-decision.md-144-   1M rows requires multi-volume memory pressure to reach (the 1M projection
memory-cold-decision.md-145-   models ~318 MiB, below the 400 MiB watermark); the common production cold
Let me verify the export path behavior for oversized/cold volumes (relevant to 5M feasibility) and the idle-gate definition.
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
read: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
IndexPipeline.cpp-1101-     std::lock_guard<std::mutex> lock(mutex_);
IndexPipeline.cpp-1102-     // large-index-lifecycle-and-search-scalability review-lane D P1-1: the
IndexPipeline.cpp-1103-     // reload (and its trailing enforcement) is a structural change -- bump
IndexPipeline.cpp-1104-     // residency epochs so in-flight searches re-capture instead of reading a
IndexPipeline.cpp-1105-     // replaced layout.
IndexPipeline.cpp-1106-     const bool wasCold = projection_.NeedsReload(id);
IndexPipeline.cpp:1107:     const bool reloaded = projection_.ReloadEvictedVolume(store_, id);
IndexPipeline.cpp-1108-     if (wasCold) {
IndexPipeline.cpp-1109-         AdvanceResidencyEpochsForStructuralChangeLocked();
IndexPipeline.cpp-1110-     }
IndexPipeline.cpp-1111-     if (!reloaded) return {};
IndexPipeline.cpp-1112-     ffprotocol::PooledSnapshotMap result;
IndexPipeline.cpp-1113-     projection_.ForEachEntry([&](const ffindexstore::EntryKey& key, const ffindexstore::ProjectionEntry& entry) {
[pi-lens: read widened to the Markdown section under the heading "Phase A — predecessor certification debt closed on the pre-optimization candidate" (heading boundary): you asked for lines 1-30, this shows lines 1-138. Re-request with limit > 100 for the exact range.]
# Phase A — predecessor certification debt closed on the pre-optimization candidate
Candidate: `feat/performance-closure-and-multisurface-acceleration-20261001` at
`8c8aa0b` (planning repair + reconciliation + evidence/gate repair; no product
source changed vs `origin/main` `bdd7d7b`). All runs 2026-10-02, this session,
after the established idle gate opened (median CPU load over 3 samples <= 25;
load at release-test CTest start = 19). Historical results were not reused.
Every count below is the actual registered total from the run named.
## Task 2.1 — idle gate and process hygiene
- Dominating load at apply start was two live Playwright `chrome-headless-shell`
  processes (user/other-session browser automation, started 19:22/19:46 local,
  swiftshader rendering). Not campaign-owned; NOT killed per policy.
- No campaign-owned bench/test processes were running; the installed
  `FastFilesIndexSvc` service is untouched.
- The gate opened when the Playwright session ended (~20:15). Load samples at
  gate time: 39/17/24 then 17/45/21 (median 24 then 21, threshold 25).
## Task 2.2 — full release-test CTest (idle host)
`ctest --test-dir build/release-test --output-on-failure -C RelWithDebInfo`
at `8c8aa0b`: **124 passed / 0 failed / 0 skipped**, total 232.58 s.
- `ffengine_volume_session_manager_tests` passed in **13.99 s** — the existing
  30-second assertion in `TestStopReturnsWithoutWaitingForTheSchedulerInterval`
  is unchanged and passed with margin. No raise, no skip, no retry-until-green;
  this was run 1 of at most three allowed idle runs and needed no repeat.
- The historical 123/124 loaded result is superseded by this idle-host run.
## Task 2.3 — full Debug CTest
`ctest --test-dir build/debug --output-on-failure` at `8c8aa0b`:
**124 passed / 0 failed / 0 skipped**, total 221.20 s.
## Task 2.4 — Analyze
`cmake --build build\analyze` at `8c8aa0b`: rc=0, **0 warnings** (warning-clean).
Current-session result; no historical Analyze pass inherited.
## Task 2.5 — authoritative ASan slice
`ctest --test-dir build\asan` for the four hosted-parity tests, each run
explicitly (a combined `-R` regex was mangled by cmd pipe quoting and matched
only 2; rerun as four invocations to be exact):
| test | result |
|---|---|
| `ffprotocol_fuzz_tests` | PASS (1/1) |
| `ffipc_framing_tests` | PASS (1/1) |
| `ffipc_listener_lifecycle_tests` | PASS (1/1) |
| `ffvolumescanner_terminal_tests` | PASS (1/1) |
**Total: 4/4 passed, 0 failed.**
## Task 2.6 — user-facing validation (same candidate)
| gate | result | classification |
|---|---|---|
| E2E (`dev.ps1 test-e2e`, all scenarios) | **8 passed / 0 failed / 0 skipped** | `boundToHead=true` |
| UIA core (`UiaScenario=core`) | **16 passed / 0 skipped / 0 failed** | PASS |
| UIA full (`UiaScenario=all`, final aggregate) | **23 passed / 6 skipped / 0 failed** | PASS |
| Visual catalog (`Invoke-VisualCatalog.ps1 -Gallery`) | **20 recorded / 20 passed / 0 failed / 0 skipped** | PASS |
| Focused many-tabs (`VisualScenario=shell-many-tabs-overflow`) | **1 passed / 0 skipped / 0 failed** | PASS |
UIA full aggregate instability during this session (recorded, not hidden):
1. Aggregate run #1: FAIL — `search-deep-result-navigation`, the exact
   historical flake (`search query 'alpha' was not accepted on AutomationId
   7101; observed=''`). See section 9 evidence below.
2. Aggregate runs #2/#3: `HARNESS_ERROR capability-worker-exited-without-result`
   (worker exited non-zero after module import with empty stderr and no
   candidate result). Free RAM at the time was ~0.5 GB (user work: browsers,
   qemu, etc.) — environmental memory pressure, not a product failure. Not
   retried beyond the attempts needed for the flake matrix.
3. Aggregate run #4 (current): PASS 23/6/0, matching the historical full-run
   shape (6 conditional skips: privileged 2, selection 1, drag 3).
4. Per-group aggregates (equivalent coverage, better isolation): core 16/0/0,
   search 7/0/0, privileged 2/2/0, treemap 3/0/0, selection 2/1/0, error 3/0/0,
   drag 2/3/0 — all PASS.
## Search-input flake matrix (tasks 9.2–9.4)
| trial set | runs | outcome |
|---|---|---|
| aggregate `all` run #1 | 1 | **REPRODUCED once** (`alpha` not accepted, observed='') |
| focused isolated `search-deep-result-navigation` | 5 | 5 PASS |
| `search` group aggregate | 1 | PASS (7/0/0) |
| six other group aggregates | 6 | all PASS |
| aggregate `all` run #4 | 1 | PASS (23/6/0) |
| focused trial after instrumentation | 1 | PASS (3 sub-results) |
Investigation (9.3): the harness sets queries by foreground + focus + typed
keystrokes into the query combo (AutomationId 7101) with read-back verification
(native window text, then UIA text) and one re-focus/re-type retry. The failure
mode (observed='' despite retry) is consistent with focus transfer or typed
input landing elsewhere during aggregate runs, but no controlled trial
reproduced it, so no deterministic harness or product defect was established.
Bounded failure-path diagnostics were added to `Set-UiaSearchQuery`
(edit-child discovery, combo hwnd at start/end, native vs UIA read-back split,
retry state) so a future failure self-identifies its mechanism. They do not
alter test behavior.
Classification per the repaired contract: **`UNREPRODUCED / OBSERVED FLAKE`** —
with the fresh aggregate reproduction retained above and the two historical
failures kept visible. No deterministic fix was fabricated.
## Task 2.7 — predecessor closure
Predecessor `measured-performance-acceleration-and-exact-certification` tasks:
- 6.3 (authoritative ASan slice + Analyze for the final candidate): **checked**
  — current results above (ASan 4/4, Analyze 0 warnings).
- 6.4 (E2E, UIA core/full, visual catalog for the final candidate): **checked**
  — current results above (E2E 8/0/0, UIA core 16/0/0, UIA full 23/6/0,
  visual 20/20).
Preceding conditions from the repaired contract are satisfied: stale evidence
was corrected (allocation-probe interpretation v2, equivalence-gate validation
v2 — both in this change's evidence dir, historical files byte-identical);
repository truth was reconciled (`apply-live-reconciliation.md`); current-state
validation is this record. The predecessor is therefore marked `completed` in
`openspec/lifecycle.json` (not by `supersededBy`). Raw-volume tasks 5.4 and 2.3
are NOT touched: they remain unchecked and external (`elevated`/`clean-host`).
## Task 9.1 — user-visible checks after the retained changes
`pwsh ./dev.ps1 test-e2e` on the retained candidate (pooled name path +
search admission + capped-truncation fix), debug-diagnostic rebuilt at the
current HEAD: **8 passed / 0 failed / 0 skipped**, provenance
`boundToHead=true`. The scenario details cover Home, This PC terminal state,
nested browse, Back/Forward/Up, direct path, stale-column and terminal-item
behaviour, plus exact/partial/no-result/superseded-query/selection/result-open
search checks — the practical navigation-to-ready and
search-input-to-visible-results paths. These checks are not new wall-clock
gates; no timing threshold was added. UIA core/full and the visual catalog
are re-run in the final certification ladder (task 11.3).
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
IndexPipeline.cpp-1075- 
IndexPipeline.cpp-1076-     if (searchableMutation) {
IndexPipeline.cpp-1077-         AdvanceQueryVisibleEpochLocked(id, QueryMutationReason::EntryMutation);
IndexPipeline.cpp-1078-     } else {
IndexPipeline.cpp-1079-         RecordMetadataOnlyUpdateLocked();
IndexPipeline.cpp-1080-     }
IndexPipeline.cpp-1081- }
IndexPipeline.cpp-1082- 
IndexPipeline.cpp:1083: ffprotocol::PooledSnapshotMap IndexPipeline::ExportDirectorySnapshotBoundedPooled(
IndexPipeline.cpp-1084-     ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix, size_t maxDirectories) {
IndexPipeline.cpp-1085-     if (maxDirectories == 0) {
IndexPipeline.cpp-1086-         return {};
IndexPipeline.cpp-1087-     }
IndexPipeline.cpp-1088-     // Bounded publication keeps only the lexicographically smallest N
IndexPipeline.cpp-1089-     // directories (shallowest/top-of-tree) deterministically. To avoid
IndexPipeline.cpp-1090-     // building the full N=200k map then discarding the tail, insert with
IndexPipeline.cpp-1091-     // an in-flight bound: each insertion that would exceed maxDirectories
IndexPipeline.cpp-1193-         }
IndexPipeline.cpp-1194-         result.emplace(path, std::move(owned));
IndexPipeline.cpp-1195-     }
IndexPipeline.cpp-1196-     return result;
IndexPipeline.cpp-1197- }
IndexPipeline.cpp-1198- 
IndexPipeline.cpp-1199- }  // namespace
IndexPipeline.cpp-1200- 
IndexPipeline.cpp:1201: std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshot(
IndexPipeline.cpp-1202-     ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix) {
IndexPipeline.cpp-1203-     return ToOwningSnapshotMap(ExportDirectorySnapshotBoundedPooled(
IndexPipeline.cpp-1204-         id, rootPathPrefix, (std::numeric_limits<size_t>::max)()));
IndexPipeline.cpp-1205- }
IndexPipeline.cpp-1206- 
IndexPipeline.cpp-1207- std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshotBounded(
IndexPipeline.cpp-1208-     ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix, size_t maxDirectories) {
IndexPipeline.cpp-1209-     return ToOwningSnapshotMap(ExportDirectorySnapshotBoundedPooled(id, rootPathPrefix, maxDirectories));
IndexPipeline.cpp-1195-     }
IndexPipeline.cpp-1196-     return result;
IndexPipeline.cpp-1197- }
IndexPipeline.cpp-1198- 
IndexPipeline.cpp-1199- }  // namespace
IndexPipeline.cpp-1200- 
IndexPipeline.cpp-1201- std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshot(
IndexPipeline.cpp-1202-     ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix) {
IndexPipeline.cpp:1203:     return ToOwningSnapshotMap(ExportDirectorySnapshotBoundedPooled(
IndexPipeline.cpp-1204-         id, rootPathPrefix, (std::numeric_limits<size_t>::max)()));
IndexPipeline.cpp-1205- }
IndexPipeline.cpp-1206- 
IndexPipeline.cpp-1207- std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshotBounded(
IndexPipeline.cpp-1208-     ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix, size_t maxDirectories) {
IndexPipeline.cpp-1209-     return ToOwningSnapshotMap(ExportDirectorySnapshotBoundedPooled(id, rootPathPrefix, maxDirectories));
IndexPipeline.cpp-1210- }
IndexPipeline.cpp-1211- 
IndexPipeline.cpp-1199- }  // namespace
IndexPipeline.cpp-1200- 
IndexPipeline.cpp-1201- std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshot(
IndexPipeline.cpp-1202-     ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix) {
IndexPipeline.cpp-1203-     return ToOwningSnapshotMap(ExportDirectorySnapshotBoundedPooled(
IndexPipeline.cpp-1204-         id, rootPathPrefix, (std::numeric_limits<size_t>::max)()));
IndexPipeline.cpp-1205- }
IndexPipeline.cpp-1206- 
IndexPipeline.cpp:1207: std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshotBounded(
IndexPipeline.cpp-1208-     ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix, size_t maxDirectories) {
IndexPipeline.cpp-1209-     return ToOwningSnapshotMap(ExportDirectorySnapshotBoundedPooled(id, rootPathPrefix, maxDirectories));
IndexPipeline.cpp-1210- }
IndexPipeline.cpp-1211- 
IndexPipeline.cpp-1212- std::optional<ffindexstore::Projection::FolderAggregate> IndexPipeline::GetFolderAggregate(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn) {
IndexPipeline.cpp-1213-     std::unique_ptr<ffindexstore::StoreReadSession> reader;
IndexPipeline.cpp-1214-     {
IndexPipeline.cpp-1215-         std::lock_guard<std::mutex> lock(mutex_);
IndexPipeline.cpp-1201- std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshot(
IndexPipeline.cpp-1202-     ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix) {
IndexPipeline.cpp-1203-     return ToOwningSnapshotMap(ExportDirectorySnapshotBoundedPooled(
IndexPipeline.cpp-1204-         id, rootPathPrefix, (std::numeric_limits<size_t>::max)()));
IndexPipeline.cpp-1205- }
IndexPipeline.cpp-1206- 
IndexPipeline.cpp-1207- std::map<std::wstring, ffprotocol::SnapshotDirectory> IndexPipeline::ExportDirectorySnapshotBounded(
IndexPipeline.cpp-1208-     ffindexstore::VolumeRowId id, const std::wstring& rootPathPrefix, size_t maxDirectories) {
IndexPipeline.cpp:1209:     return ToOwningSnapshotMap(ExportDirectorySnapshotBoundedPooled(id, rootPathPrefix, maxDirectories));
IndexPipeline.cpp-1210- }
IndexPipeline.cpp-1211- 
IndexPipeline.cpp-1212- std::optional<ffindexstore::Projection::FolderAggregate> IndexPipeline::GetFolderAggregate(ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn) {
IndexPipeline.cpp-1213-     std::unique_ptr<ffindexstore::StoreReadSession> reader;
IndexPipeline.cpp-1214-     {
IndexPipeline.cpp-1215-         std::lock_guard<std::mutex> lock(mutex_);
IndexPipeline.cpp-1216-         if (!projection_.NeedsReload(volumeId)) {
IndexPipeline.cpp-1217-             return projection_.GetFolderAggregate(volumeId, parentFrn);
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore
src/Projection.cpp-528-     const size_t bytesBefore = EstimatedMemoryBytes();
src/Projection.cpp-529-     EvictSubtree(*victim);
src/Projection.cpp-530-     CompactStorage();
src/Projection.cpp-531-     // memory-bounded-indexed-scale 3.1: record the policy's actual effect so
src/Projection.cpp-532-     // tests can distinguish "eviction ran" from "eviction changed nothing".
src/Projection.cpp-533-     lastEvictionBytesBefore_ = bytesBefore;
src/Projection.cpp-534-     lastEvictionBytesAfter_ = EstimatedMemoryBytes();
src/Projection.cpp-535-     return true;
src/Projection.cpp-536- }
src/Projection.cpp-537- 
src/Projection.cpp:538: bool Projection::ReloadEvictedVolume(Store& store, const VolumeRowId volumeRowId) {
src/Projection.cpp-539-     if (!NeedsReload(volumeRowId)) return true;
src/Projection.cpp-540-     ++coldReloadCount_;
src/Projection.cpp-541-     const auto metadata = store.GetVolumeMetadata(volumeRowId);
src/Projection.cpp-542-     if (!metadata.has_value()) {
src/Projection.cpp-543-         ++coldReloadFailedCount_;
src/Projection.cpp-544-         return false;
src/Projection.cpp-545-     }
src/Projection.cpp-546-     const bool rebuilt = RebuildVolumeFromStore(store, volumeRowId, metadata->entryCount);
src/Projection.cpp-547-     const bool reloaded = rebuilt && !NeedsReload(volumeRowId);
src/Projection.cpp-548-     if (!reloaded) ++coldReloadFailedCount_;
grep: C:/Users/Michael Roy/Documents/FastFiles/src/indexstore
src/Projection.cpp-323-     diagnostics.estimatedBytes = EstimatedMemoryBytes();
src/Projection.cpp-324-     diagnostics.lastEvictionBytesBefore = lastEvictionBytesBefore_;
src/Projection.cpp-325-     diagnostics.lastEvictionBytesAfter = lastEvictionBytesAfter_;
src/Projection.cpp-326-     diagnostics.subtreeEvictionCount = subtreeEvictionCount_;
src/Projection.cpp:327:     diagnostics.coldReloadCount = coldReloadCount_;
src/Projection.cpp-328-     diagnostics.coldReloadFailedCount = coldReloadFailedCount_;
src/Projection.cpp-329-     return diagnostics;
src/Projection.cpp-330- }
src/Projection.cpp-331- 
src/Projection.cpp-324-     diagnostics.lastEvictionBytesBefore = lastEvictionBytesBefore_;
src/Projection.cpp-325-     diagnostics.lastEvictionBytesAfter = lastEvictionBytesAfter_;
src/Projection.cpp-326-     diagnostics.subtreeEvictionCount = subtreeEvictionCount_;
src/Projection.cpp-327-     diagnostics.coldReloadCount = coldReloadCount_;
src/Projection.cpp:328:     diagnostics.coldReloadFailedCount = coldReloadFailedCount_;
src/Projection.cpp-329-     return diagnostics;
src/Projection.cpp-330- }
src/Projection.cpp-331- 
src/Projection.cpp-332- void Projection::TouchIndex(const uint32_t index) const noexcept {
src/Projection.cpp-536- }
src/Projection.cpp-537- 
src/Projection.cpp-538- bool Projection::ReloadEvictedVolume(Store& store, const VolumeRowId volumeRowId) {
src/Projection.cpp-539-     if (!NeedsReload(volumeRowId)) return true;
src/Projection.cpp:540:     ++coldReloadCount_;
src/Projection.cpp-541-     const auto metadata = store.GetVolumeMetadata(volumeRowId);
src/Projection.cpp-542-     if (!metadata.has_value()) {
src/Projection.cpp-543-         ++coldReloadFailedCount_;
src/Projection.cpp-544-         return false;
src/Projection.cpp-539-     if (!NeedsReload(volumeRowId)) return true;
src/Projection.cpp-540-     ++coldReloadCount_;
src/Projection.cpp-541-     const auto metadata = store.GetVolumeMetadata(volumeRowId);
src/Projection.cpp-542-     if (!metadata.has_value()) {
src/Projection.cpp:543:         ++coldReloadFailedCount_;
src/Projection.cpp-544-         return false;
src/Projection.cpp-545-     }
src/Projection.cpp-546-     const bool rebuilt = RebuildVolumeFromStore(store, volumeRowId, metadata->entryCount);
src/Projection.cpp-547-     const bool reloaded = rebuilt && !NeedsReload(volumeRowId);
src/Projection.cpp-544-         return false;
src/Projection.cpp-545-     }
src/Projection.cpp-546-     const bool rebuilt = RebuildVolumeFromStore(store, volumeRowId, metadata->entryCount);
src/Projection.cpp-547-     const bool reloaded = rebuilt && !NeedsReload(volumeRowId);
src/Projection.cpp:548:     if (!reloaded) ++coldReloadFailedCount_;
src/Projection.cpp-549-     return reloaded;
src/Projection.cpp-550- }
src/Projection.cpp-551- 
src/Projection.cpp-552- bool Projection::ReloadAllEvictedVolumes(Store& store) {
include/ffindexstore/Projection.h-514-     std::unordered_set<VolumeRowId> evictedVolumes_;
include/ffindexstore/Projection.h-515-     bool enforcingMemoryLimit_ = true;
include/ffindexstore/Projection.h-516-     // memory-bounded-indexed-scale 3.1: monotonic policy observability.
include/ffindexstore/Projection.h-517-     uint64_t subtreeEvictionCount_ = 0;
include/ffindexstore/Projection.h:518:     uint64_t coldReloadCount_ = 0;
include/ffindexstore/Projection.h-519-     uint64_t coldReloadFailedCount_ = 0;
include/ffindexstore/Projection.h-520-     size_t lastEvictionBytesBefore_ = 0;
include/ffindexstore/Projection.h-521-     size_t lastEvictionBytesAfter_ = 0;
include/ffindexstore/Projection.h-522- };
include/ffindexstore/Projection.h-515-     bool enforcingMemoryLimit_ = true;
include/ffindexstore/Projection.h-516-     // memory-bounded-indexed-scale 3.1: monotonic policy observability.
include/ffindexstore/Projection.h-517-     uint64_t subtreeEvictionCount_ = 0;
include/ffindexstore/Projection.h-518-     uint64_t coldReloadCount_ = 0;
include/ffindexstore/Projection.h:519:     uint64_t coldReloadFailedCount_ = 0;
include/ffindexstore/Projection.h-520-     size_t lastEvictionBytesBefore_ = 0;
include/ffindexstore/Projection.h-521-     size_t lastEvictionBytesAfter_ = 0;
include/ffindexstore/Projection.h-522- };
include/ffindexstore/Projection.h-523- 
grep: C:/Users/Michael Roy/Documents/FastFiles/src/engine/src/I...
grep: C:/Users/Michael Roy/Documents/FastFiles/openspec/changes...
IndexPipeline.h-349-     // Projection::SetMaximumMemoryBytes), so tests -- and any future settings
IndexPipeline.h-350-     // surface -- can force deterministic eviction. The diagnostics/volume
IndexPipeline.h-351-     // residency accessors back observability without exposing the projection.
IndexPipeline.h-352-     void SetProjectionMemoryBudget(size_t maximumBytes);
IndexPipeline.h:353:     ffindexstore::Projection::MemoryDiagnostics GetProjectionMemoryDiagnostics() const;
IndexPipeline.h-354-     // performance-closure-and-multisurface-acceleration 8.1: read-only
IndexPipeline.h-355-     // per-category projection memory decomposition (see
IndexPipeline.h-356-     // Projection::MemoryBreakdown). Diagnostic only; no representation or
IndexPipeline.h-357-     // policy change.
IndexPipeline.h-354-     // performance-closure-and-multisurface-acceleration 8.1: read-only
IndexPipeline.h-355-     // per-category projection memory decomposition (see
IndexPipeline.h-356-     // Projection::MemoryBreakdown). Diagnostic only; no representation or
IndexPipeline.h-357-     // policy change.
IndexPipeline.h:358:     ffindexstore::Projection::MemoryBreakdown GetProjectionMemoryBreakdown() const;
IndexPipeline.h-359-     bool IsVolumeEvicted(ffindexstore::VolumeRowId volumeId) const;
IndexPipeline.h-360-     // memory-bounded-indexed-scale 5.x: explicit policy hook dropping one
IndexPipeline.h-361-     // volume's resident rows (durable data untouched). Also the deterministic
IndexPipeline.h-362-     // forced-eviction seam for the bounded-scale test suite.
search-matrix-planner-fix.json:16:     {"dataset_size":200000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":0,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"cancelled":false,"unavailable":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dura... [truncated]
search-matrix-planner-fix.json:152:     {"dataset_size":1000000,"query_class":"selective","query":"unique_component_7391","temperature":"warm","iteration":0,"result_count":1,"candidate_count":1,"candidates_visited":1,"truncated":false,"cancelled":false,"unavailable":false,"wire_result_count":1,"wire_truncated":false,"filename_index_used":true,"filename_index_candidate_count":1,"search_generation":4,"query_visible_epoch":4,"dependency_set_epoch":3,"generation_restarts":0,"relevant_invalidations":0,"durable_fallback_attempts":0,"dur... [truncated]
[Some lines truncated to 500 chars. Use read tool to see full lines]
Let me pin down exact line numbers for the key harness behaviors I'll cite.
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
bench_large_index_scalability.cpp:46: constexpr size_t kWarmRunsNormal = 10;
bench_large_index_scalability.cpp:47: constexpr size_t kWarmRunsLarge = 5;
bench_large_index_scalability.cpp:48: constexpr size_t kLargeSearchThreshold = 500'000;
bench_large_index_scalability.cpp:156:     std::vector<size_t> scales{10'000, 100'000, 500'000, 1'000'000, 2'000'000};
bench_large_index_scalability.cpp:297:     return L"folder_" + std::to_wstring(index % 997) + L"_branch_" + std::to_wstring(index);
bench_large_index_scalability.cpp:302:     if (index == 7391) return L"unique_component_7391.txt";
bench_large_index_scalability.cpp:303:     if (pattern == 0 || pattern == 1) return L"common_component_" + std::to_wstring(index) + L".txt";
bench_large_index_scalability.cpp:579:     const size_t directoryCount = (std::max)(size_t{1}, scale / 64);
bench_large_index_scalability.cpp:806:         if (run.scale == scale && run.queryClass == queryCase.name && run.temperature == "warm") {
bench_large_index_scalability.cpp:1349:     output << ",\"folder_matching_ms\":"; WriteJsonDouble(output, run.folderMatchingMs);
bench_large_index_scalability.cpp:1685:     size_t directoryCount = 0;
bench_large_index_scalability.cpp:1705:     report.directoryCount = (std::max)(size_t{1}, scale / 64);
bench_large_index_scalability.cpp:2034:     output << "{\n  \"schema\":\"fastfiles-projection-memory-decomposition-v1\"\n"
bench_large_index_scalability.cpp:2127:         {"selective", L"unique_component_7391", 0},
bench_large_index_scalability.cpp:2135:         {"exact_name", L"unique_component_7391.txt", 0},
bench_large_index_scalability.cpp:2150:         std::printf("Building %zu synthetic entries...\n", scale);
bench_large_index_scalability.cpp:2233:                                                       "cold-ish-first-after-rebuild", options.detailedDiagnostics);
bench_large_index_scalability.cpp:2236:                 RunSearch(pipeline, scale, queryCase, 0, "warmup", options.detailedDiagnostics);
bench_large_index_scalability.cpp:2237:                 RunSearch(pipeline, scale, queryCase, 1, "warmup", options.detailedDiagnostics);
bench_large_index_scalability.cpp:2256:             pipeline.UnloadVolumeFromProjection(volumeId);
bench_large_index_scalability.cpp:2262:                 RunSearch(pipeline, scale, queryCase, 0, "warmup", options.detailedDiagnostics);
bench_large_index_scalability.cpp:2263:                 const SearchOutcome measured = RunSearch(pipeline, scale, queryCase, 0, "cold",
bench_large_index_scalability.cpp:2309:                 RunSearch(pipeline, scale, stepCase, 0, "warmup", options.detailedDiagnostics);
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
bench_large_index_scalability.cpp:66:     if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
bench_large_index_scalability.cpp:96:             result.walBytes = std::filesystem::file_size(wal, error);
bench_large_index_scalability.cpp:1433: //   * process private bytes and working set from GetProcessMemoryInfo;
bench_large_index_scalability.cpp:1464:     if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
bench_large_index_scalability.cpp:1537:     if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
bench_large_index_scalability.cpp:2036:            << "  ,\"build_identity\":\"" << ffsetup::kBuildIdentityAscii << "\"\n"
bench_large_index_scalability.cpp:2050:     output << "\n  ,\"source_sha\":";
bench_large_index_scalability.cpp:2070:               "\"process_memory\":\"GetProcessMemoryInfo PROCESS_MEMORY_COUNTERS_EX (working set, private "
bench_large_index_scalability.cpp:2077:            << "  ,\"limitations\":["
bench_large_index_scalability.cpp:2087:               "\"timings in this mode are single-process measurements and are not the campaign's idle-gated "
bench_large_index_scalability.cpp:2278:         if (scale == options.scales.front()) {
bench_large_index_scalability.cpp:2411:     output << "\n  ],\n  \"supersession\":{\"engine_cancellation\":true,\"ui_generation_discards_stale_completion\":true,\"old_work_completes\":false,\"regression_test\":\"ffengine_index_pipeline_tests\"}\n}\n";
grep: C:/Users/Michael Roy/Documents/FastFiles/tests/engine/ben...
bench_large_index_scalability.cpp:2239:             for (size_t iteration = 0; iteration < warmRuns; ++iteration) {
bench_large_index_scalability.cpp:2380:     output << "{\n  \"schema\":\"fastfiles-large-index-scalability-v2\",\n  \"machine\":";
## Review
Research lane F (read-only; no files touched). Sources: `tests/engine/bench_large_index_scalability.cpp` (2,419 lines), `tests/engine/CMakeLists.txt`, engine/store seams, and the campaign evidence under `openspec/changes/large-index-lifecycle-and-search-scalability/evidence/` plus `docs/benchmarks/`.
### Correct (verified good)
- The harness is deliberately not a CTest gate (`tests/engine/bench_large_index_scalability.cpp:1-3`; `tests/engine/CMakeLists.txt:252-264`, with a `fastfiles_build_identity` dependency so `--memory-report` can't claim a stale tree).
- Rich machine-readable search attribution already exists: `SearchRun` (bench `:355-433`) maps essentially all of `IndexPipeline::SearchTimings` (`src/engine/src/IndexPipeline.h:56-122`) into `search_runs[]` (`WriteSearchRun`, bench `:1310-1405`), incl. `projection_rows_examined`, `hot_rows_examined`, `cold_rows_examined`, `candidates_visited`, `filename_index_used`, `filename_index_candidate_count`, `store_stream_ms`, lock/epoch/fallback counters.
- The `--memory-report` mode is methodology-aware: counterbalanced pooled-vs-owning export arms (pooled/owning/owning/pooled, bench `:1770-1795`), repeated rebuild cycles + close/reopen/rebuild plateau checks (`:1716-1745`), allocator + working-set-split instrumentation, and an honest `limitations` array (`:2077-2093`) that explicitly disclaims being "the campaign's idle-gated latency protocol" (`:2087`).
- The predecessor's evidence has already been truth-corrected: lane E rows at `evidence/adversarial-review-2026-10-21.md:29-30`; the corrections are materialized in `evidence/memory-cold-decision.md` §2 (n=1 cold disclosure, 2.8x host drift, −9%..+6% same-block control, scan floor 1.05–1.10 µs/row, E-6 tautology, E-8 provenance) and `evidence/lifecycle-reload-resources.md:51-54` (E-4 rebuild band 28–32x typical / ≥17.7x worst).
### Findings
- **P1 — "warm" temperature labels are false at ≥2M rows under the default budget.** The hot loop (bench `:2228-2243`) labels runs `warm`, but the 2M projection models ~839.7 MB > the 500 MiB budget, so "every 2M query serves from the cold stream under the default budget" (`evidence/memory-cold-decision.md` §3). At 5M (~2.1 GB modeled) this is guaranteed. Any `search_summaries`-derived "hot p50 at 5M" would silently be a cold-stream measurement. Fix: label/summarize by measured state (`coldSearchUsed`/`coldRowsExamined`), and/or add a `--projection-budget` flag driving `SetProjectionMemoryBudget` (`src/engine/src/IndexPipeline.h:352`) for an explicit hot arm (design.md alternative-E control).
- **P1 — cold samples are n=1 per class per scale, and there are no cold summaries.** The forced-eviction stage runs one warmup + **one** measured cold query per class (bench `:2256-2266`); `AddSearchSummary` filters `temperature == "warm"` only (`:806`). This is exactly lane E-2; the current harness still cannot produce a cold median/p95, which `durable-cold-search-index-and-hybrid-residency/design.md` §3 dim 6 requires (p50/p90/p95 per class per scale).
- **P1 — provenance is missing from the v2 search artifact (lane E-8 unfixed for search runs).** `--source-state/--source-sha/--preset` are parsed (`:234-245`) but only emitted by `--memory-report` (`:2046-2052`, plus `build_identity` `:2036` and `generated_epoch_seconds` `:2034-2036`). The scalability JSON (`:2380-2413`) embeds no command line, SHA, tree state, preset, build identity, or timestamp — confirmed absent in all three `evidence/search/search-matrix-*.json` (schema line 2 only). `MachineInfo.databaseStorage` is a hardcoded "temporary directory (drive/model not collected)" (`:556`), so the host-drift problem (2.8x) has no disk provenance either.
- **P2 — no CPU, disk-I/O, or process-level counters.** No `GetProcessTimes`/`GetProcessIoCounters`/cycle counters anywhere in `tests/engine/` (grep: zero matches). "CPU" and "disk reads" cannot be answered from current artifacts.
- **P2 — cold→hot transition counters exist in the store but are not emitted.** `Projection::MemoryDiagnostics` carries `subtreeEvictionCount`, `coldReloadCount`, `coldReloadFailedCount`, `lastEvictionBytesBefore/After` (`src/indexstore/src/Projection.cpp:323-329`, counters at `:540-548`), but the bench reads only `estimatedBytes` (`LoadPipeline`, bench `:640-641`). Also relevant at scale: export of an oversized/cold volume fails fast and returns `{}` (`src/engine/src/IndexPipeline.cpp:1106-1111`), so the dataset-level `actual_snapshot_*` fields (`:2160-2180`) are degenerate/empty at 2M/5M and must not be read as export performance there.
- **P2 — planner choice is only half-visible.** `filenameIndexUsed`/`filenameIndexCandidateCount` are emitted (visible in `evidence/search/search-matrix-planner-fix.json:16` `filename_index_used:true,candidate_count:1` vs `search-matrix-after-rebuild-fix-control.json:136` `false` — the dead-accelerator state, review C F3). But the cold SQL prefilter's use is invisible (`ColdNamePrefilterNeedle`, `src/engine/src/IndexPipeline.cpp:365-371`, no `SearchTimings` flag), and seven zero-copy fields already in `SearchTimings` (`viewPathUsed`, `hotLightweightRowsCaptured`, `hotRowsRejectedBeforeMaterialization`, `viewPathFallbackRows`, `lightweightCaptureMs`, `viewMatchingMs`, `survivorMaterializationMs` — `IndexPipeline.h:124-133`) are **not** copied into `SearchRun`.
- **P2 — no repetition/counterbalancing/slicing controls, and probes fire unconditionally.** There is no `--repetitions`, `--classes`, `--temperature`, or `--skip-probes`. The sustained-churn probe keys off `options.scales.front()` (`:2278`) and its fallback "intentionally stream[s] the full dataset" 4 restarts (`:866-878`) — at `--scales 5000000` that alone is tens of minutes. Output JSON is written once at the very end (`:2375-2412`); a killed/timeout run loses everything (a prior campaign hit exactly this: `docs/benchmarks/large-index-scalability.md`, SQL-no-count section — "process exited … when the external 120-second command deadline expired").
- **P2 — fixture shape is fixed and unrealistically narrow.** Names: 10 deterministic shapes (`FileName`, `:300-351`; special case `unique_component_7391.txt` `:302`), lengths ≈10–28 UTF-16 chars (digit-suffix variable), exactly **10% non-ASCII** (Cyrillic `список_*`, `:346-349`), directories `folder_<i%997>_branch_<i>` with `scale/64` dirs (`:297`, `:579`), 16-ary parent fanout (`:596-600`), file sizes 1 KiB–64 MiB (`:608`). No flag varies non-ASCII ratio or name-length profile — yet the prefilter benefit scales directly with the non-ASCII survivor fraction (memory-cold-decision §2: 9.8% survivors here; "1-2% non-ASCII corpus (an unmeasured assumption)"; CJK-heavy falls back to the 9.8% case).
- **P2 — warmup runs are discarded and percentile math is weak.** Warmups execute but are never recorded (`:2235-2238`, `:2262`); `Percentile` uses floor `(n-1)*f` with no interpolation (`:781-786`) — with n=5 large-scale warm runs the emitted "p90" is the 4th of 5 values; p95 does not exist at all.
- **P2 — idle gating/pinning is external discipline only.** The harness neither gates nor pins (no affinity/priority calls in `tests/engine/`). The established gate is "median CPU load over 3 samples ≤ 25" (`performance-closure-and-multisurface-acceleration/evidence/certification-debt-phase-a.md:6`), and the established A/B protocol adds "interleaved randomized order … pinned to one logical core at high priority, 5 warm-up + 5 measured reps" (`evidence/allocation-isolation-result.json:5`). The memory-report's own limitation text admits this (`:2087`). Noise floor context for thresholds: same-binary same-session spread ~5–7% (`measured-performance-acceleration-and-acceleration/tasks.md:53` region; `certification-debt` evidence), same-binary cross-session 5.7% (`retention-emplaceinplace-reverted.json:26`), drifted sessions up to 2.8x on hot paths (E-1).
- **P3 — E-6 attribution tautology still applies to emitted fields.** `store_stream_ms` wraps the entire stream+match loop (memory-cold-decision §2); no scan/decode/match split exists in `SearchTimings`. Any "scan cost" claim must use row-count mechanisms or new instrumentation, not percentage-of-timer.
### What the harness can measure today (question 3 inventory)
| Quantity | Status | Evidence |
|---|---|---|
| Rows scanned | ✅ | `projection_rows_examined`, `hot_rows_examined`, `cold_rows_examined`, `projection_entries_visited`, `candidates_visited` (`:1378-1384`) |
| Candidates retrieved | ✅ | `candidate_count`, `filename_index_candidate_count` (`:1312-1328`) |
| Candidates verified | partial | `matched_rows`, `predicate_matches`, `early_rejects`; no explicit filename-candidate "verified" count |
| Planner choice | partial | `filename_index_used`, `cold_search_used`, `durable_fallback_used`; **missing** prefilter-used flag and `view_path_used` |
| CPU | ❌ | not instrumented (must add `GetProcessTimes`) |
| Private bytes | ✅ | `memory.private_bytes` per run (`ReadMemory` `:60-69`); full decomposition in memory mode |
| Disk reads | ❌ | not instrumented (must add `GetProcessIoCounters`) |
| WAL growth | ✅ sampled | `wal_bytes` at after-rebuild/post-query (`ReadResources` `:73-99`); `database_bytes` includes `-wal`/`-shm` (`:546-551`) |
| Index bytes | ✅ | `database_bytes`, `projection_estimated_bytes`; memory mode: full `Projection::MemoryBreakdown` incl. `filename_index_bytes` (`:1855-1895`) |
| Cold→hot transitions | ❌ emitted (✅ in engine) | `coldReloadCount` etc. exist (`Projection.cpp:327-328`) but are dropped by the bench |
### Corrected measurement protocol for this campaign (task 1.1)
1. **Provenance first (E-8):** every artifact embeds `--source-sha`, `--source-state`, `--preset`, `build_identity`, full command line, timestamp, and machine info *including disk model/free space* (memory-report schema is the template, bench `:2034-2052`). No asserted pairings.
2. **Environment:** release-test preset; established idle gate (median CPU load over 3 samples ≤ 25) before each repetition, with the load samples and gate verdict recorded in the artifact; process hygiene per `certification-debt-phase-a.md` Task 2.1 (stop only campaign-owned processes). If comparing arms, pin each repetition to one logical core at high priority (or record explicitly that runs are unpinned — never mix).
3. **"Cold" definition (the contract):** **cold projection + warm SQLite page cache**, achieved by forced eviction (`UnloadVolumeFromProjection`, bench `:2256`) *or* natural policy eviction, with one unmeasured warmup cold query to stabilize the SQLite/FS cache, and per-run state verified via `cold_search_used`/`cold_rows_examined`. Rationale: this is the production-faithful ≥2M steady state and the state the 1.05–1.10 µs/row scan floor was measured in (memory-cold-decision §2). "Fully cold" (OS cache dropped) requires privileged cache manipulation (repo policy forbids elevation) and is **not reproducible** — instead record first-query-after-process-start as a separate, explicitly n=1, never-median-able telemetry sample. Also distinguish *forced-eviction cold* from *policy-eviction cold* (different mechanisms: `UnloadVolumeFromProjection` vs `EvictSubtree`+compaction, `Projection.cpp:528-535`).
4. **Repetitions & statistics:** hot: ≥5 process-level repetitions with ≥5 measured in-process runs each (the established "5 warm-up + 5 measured" shape, `allocation-isolation-result.json:5`; overnight audit used 5 process reps — `evidence/performance/raw/*.rep1-5.log`); cold: **≥3 measured samples per class per scale per repetition** before any "median" wording (E-2); report p50/p90/p95 + min/max per class per scale (p95 currently impossible — gap G4). n=1 cells are labeled "single sample".
5. **Noise floor & attribution:** interleave arms in randomized order within one session; measure a same-binary control in the same session (expected ~5–7% spread); treat same-block ineligible classes (extension / short_prefix / unicode) as the drift bound (E-1's −9%..+6% control); **no attribution below ~10% same-block spread**. Cross-session comparisons require the control re-measured (5.7% observed with zero code change). Never attribute scan/decode/match via `store_stream_ms` percentages (E-6); use row-count mechanisms or new split timers (gap G7).
6. **Temperature truth:** summarize and label by measured state; at ≥2M run the "hot arm" only with an explicitly recorded raised projection budget (alternative-E control), otherwise report cold only (E-5/E-7 scope discipline).
### Harness gaps to fill (implementation list)
1. **G1 Provenance in v2 JSON** (`:2380-2413`): emit `command_line`, `source_sha`, `source_state`, `preset`, `build_identity` (`ffsetup::kBuildIdentityAscii`, already linked), `generated_epoch_seconds`, `projection_memory_layout_revision`; replace the `databaseStorage` placeholder (`:556`) with real volume model (`GetVolumeInformation`/`GetDiskFreeSpaceEx`).
2. **G2 Repetition/slicing flags:** `--repetitions N`, `--classes <csv>`, `--temperature hot|cold|both`, `--skip-probes` (or `--probes …`), and `--cold-reps N`. Gate the sustained-churn probe away from large `scales.front()` (currently `:2278`).
3. **G3 Cold statistics:** run `--cold-reps` measured cold queries per class (loop at `:2263-2266`) and extend `AddSearchSummary` (`:799-813`) with cold summaries keyed on measured state.
4. **G4 Percentiles:** add p95 and interpolated percentile computation (`:781-786`); emit `n` in every summary row.
5. **G5 Hot arm control:** `--projection-budget <bytes>` → `SetProjectionMemoryBudget` (`IndexPipeline.h:352`), recorded in the artifact.
6. **G6 CPU/IO counters:** `GetProcessTimes` + `GetProcessIoCounters` deltas around each measured run (fields `cpu_user_ms`, `cpu_kernel_ms`, `read_operations`, `read_bytes` in `SearchRun`).
7. **G7 Cold/hot transition + eviction observability:** emit `Projection::MemoryDiagnostics` (`subtreeEvictionCount`, `coldReloadCount`, `coldReloadFailedCount`, `lastEvictionBytesBefore/After`, `Projection.cpp:323-329`) per dataset and per-run deltas; optionally split `storeStreamMs` into sqlite/decode/match timers in `SearchTimings`.
8. **G8 Planner-choice fields:** add `prefilter_used` to `SearchTimings` (set from `ColdNamePrefilterNeedle` at `IndexPipeline.cpp:1887/2017`) and copy the seven zero-copy fields (`IndexPipeline.h:124-133`) into `SearchRun`/`WriteSearchRun`.
9. **G9 Warmup accounting:** emit warmup runs (or at least count + total ms) so artifacts show the policy (`:2235-2238`, `:2262`).
10. **G10 Incremental output:** write per-scale (or per-rep) JSON checkpoints instead of only the end-of-run file (`:2375`), so timeouts don't lose a 2-hour run.
11. **G11 Fixture sensitivity:** `--non-ascii-ratio` and `--name-length-profile` in `FileName`/`BuildSyntheticDatabase` (`:300-351`, `:579`).
12. **G12 Idle gate:** `--require-idle` (3 load samples, median ≤ 25) + record `idle_gate` block; core pin/priority optional flags.
### 5M feasibility (extrapolated from the 2M run, `docs/benchmarks/phase8-bench-run-2m.log:2-18`)
2M facts: fixture generation 115.8 s, rebuild 20.9 s, DB 145.9 MB (~75 B/row); cold queries 2.4 s (selective) → 30.9 s (extension) per query. Projected to 5M (linear in rows; cold classes verified linear by the 1M→2M ratios in memory-cold-decision §2/§3):
- Fixture: ≈ 5–6 min generation, ≈ 50–60 s hot rebuild (or instant fail-fast under the default budget — the volume can never reload: `Projection.cpp:538-550` fail-fast behavior), DB ≈ 365 MB. Disk/RAM (34 GB host, `last-run-json-summary.json`) are non-issues.
- Cold-stream query at 5M: ≥ 5.3 s floor (1.05–1.10 µs/row scan floor × 5M) for zero-survivor literals; per-class spread ≈ 5.5 s (selective) → ~109 s (extension); **one 12-class pass ≈ 9–10 min**.
- Default harness flow at 5M (all queries effectively cold-stream per P1 finding): warm loop 84 queries ≈ 65 min + cold stage 24 queries ≈ 19 min + probes (contention ≈ 1 min; **churn probe up to ~30–40 min if 5M is `scales.front()`**) ≈ **1.5–2.5 h single pass**; 3 reps ≈ 6–8 h. This is the "uncontrolled hours-long run" to avoid.
**Staged plan:**
- **Stage 0:** land G1–G4, G10 at minimum (small diffs in the bench TU + `SearchTimings`).
- **Stage 1 (protocol validation, ~1 h):** `--scales 200000,1000000`, 3 reps, all 12 classes, counterbalanced class order, same-binary control; validates labeling, stats, provenance.
- **Stage 2 (2M baseline, ~2–3 h):** one scale per process, 3 reps, split by class groups via `--classes` (e.g., literals vs prefilter-ineligible) so no invocation exceeds ~30 min; per-rep artifacts with embedded SHA.
- **Stage 3 (5M):** (a) cost probe — 1 rep × 4 representative classes (selective/no_match/broad/extension) to validate the ~90-min estimate (~30 min); (b) full 5M matrix × 3 reps overnight, per-rep invocations, explicit wall-clock budget + abort rule (>2× estimate ⇒ halt and diagnose); (c) hot arm as a separate short run with `--projection-budget` raised (resident queries are fast); (d) churn/contention probes only at 200k, never at 5M.
- **Guardrails:** never run multi-scale in one process for evidence (allocator-history carryover is documented in `docs/benchmarks/large-index-scalability.md`, "isolated measurement is the comparable footprint check"); checkpoint artifacts per rep; keep `--diagnostics` for console progress only.
### Merge verdict
OK with notes — for the campaign as a whole: the measurement phase (task 1.1) is feasible on this host within a staged, budgeted plan, but it must not start against the harness as-is: G1 (provenance), G2/G3 (reps + cold samples), and the P1 temperature-labeling fix are prerequisites, or the resulting artifacts will repeat lanes E-1/E-2/E-8 exactly. Validation command for a human to run after any harness change: `cmake --build --preset release-test --target ffengine_bench_large_index_scalability`, then `build\release-test\tests\engine\ffengine_bench_large_index_scalability.exe --scales 200000 --output build\release-test\bench-smoke.json --source-sha <sha> --source-state <clean|dirty> --preset release-test` (informational benchmark; not registered with CTest).
