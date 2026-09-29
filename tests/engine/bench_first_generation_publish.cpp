// overnight-truth-audit-and-measured-performance task 4.3: informational
// benchmark only. This executable is intentionally not registered with CTest.
//
// It measures the one user-visible path the 4.1 inventory showed the existing
// benches do not cover: the wait between the engine having a large index and a
// real UI client holding a parsed, usable first generation of that index. The
// path runs the bounded indexed-directory export, UiServer::MergeIndexDirectories
// (which republishes a shared-memory snapshot and broadcasts the generation),
// the real named-pipe write and client read, and the client-side snapshot
// parse -- exactly what the desktop shell does when a volume first appears.
//
// A wall-clock gate would be a flake factory, so the numbers below are
// repeatable evidence, not a pass/fail criterion.

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "UiServer.h"
#include "IndexPipeline.h"
#include "ffipc/PipeFraming.h"
#include "ffprotocol/UiProtocol.h"
#include "ffsetup/Identifiers.h"
#include "ffsetup/BuildIdentity.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t kDefaultEntries = 200'000;
constexpr size_t kBatchSize = 8192;
constexpr std::chrono::milliseconds kWaitDeadline{15000};

struct Options {
    size_t entries = kDefaultEntries;
    size_t warmup = 5;
    size_t measured = 5;
    std::wstring outputPath = L"first-generation-publish.json";
};

double ElapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::u16string ToUtf16(const std::wstring& value) {
    return std::u16string(value.begin(), value.end());
}

bool ParseOptions(int argc, wchar_t** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        auto count = [&](size_t& target) {
            if (index + 1 >= argc) return false;
            try {
                target = static_cast<size_t>(std::stoull(argv[++index]));
            } catch (...) {
                return false;
            }
            return true;
        };
        if (argument == L"--entries") {
            if (!count(options.entries)) return false;
        } else if (argument == L"--warmup") {
            if (!count(options.warmup)) return false;
        } else if (argument == L"--measured") {
            if (!count(options.measured)) return false;
        } else if (argument == L"--output") {
            if (index + 1 >= argc) return false;
            options.outputPath = argv[++index];
        } else {
            std::fwprintf(stderr,
                          L"Usage: %s [--entries N] [--warmup N] [--measured N] [--output path]\n",
                          argv[0]);
            return false;
        }
    }
    return options.measured > 0 && options.measured <= 4096;
}

std::filesystem::path MakeTemporaryDirectory() {
    wchar_t tempPath[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempPath) == 0) return {};
    const auto root = std::filesystem::path(tempPath) /
        (L"FastFiles-bench-first-generation-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (!std::filesystem::create_directories(root, error)) return {};
    return root;
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
    const wchar_t* extension = pattern == 2 ? L".txt" : (pattern == 3 ? L".jpg" : L".dat");
    return L"entry_" + std::to_wstring(index) + extension;
}

bool BuildSyntheticDatabase(const std::filesystem::path& database, size_t scale,
                            double& generationMs, ffindexstore::VolumeRowId& volumeId) {
    ffengine::IndexPipeline pipeline;
    if (!pipeline.Open(ToUtf8(database.wstring()))) return false;
    volumeId = pipeline.ResolveVolume(ffindexstore::VolumeKey{});
    if (volumeId == 0) {
        pipeline.Close();
        return false;
    }
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    setting.enabled = true;
    pipeline.SetVolumeRules(volumeId, setting);

    const auto generationStart = Clock::now();
    std::vector<ffprotocol::MftRecordV1> batch;
    batch.reserve(kBatchSize);
    const size_t directoryCount = (std::max)(size_t{1}, scale / 64);
    batch.push_back(MakeMftRecord(5, 5, L"", 0x10, 0, 1));
    for (size_t index = 0; index < directoryCount && 1 + index < scale; ++index) {
        const uint64_t id = 100 + static_cast<uint64_t>(index);
        const uint64_t parent = index < 16 ? 5 : 100 + static_cast<uint64_t>((index - 1) / 16);
        batch.push_back(MakeMftRecord(id, parent, DirectoryName(index), 0x10, 0, 10 + index));
        if (batch.size() == kBatchSize) {
            if (!pipeline.ApplyMftBatch(volumeId, batch)) return false;
            batch.clear();
        }
    }
    const size_t filesToCreate = scale > 1 + directoryCount ? scale - 1 - directoryCount : 0;
    const uint64_t fileIdBase = 100 + static_cast<uint64_t>(directoryCount);
    for (size_t index = 0; index < filesToCreate; ++index) {
        const uint64_t parent = 100 + static_cast<uint64_t>(index % directoryCount);
        const uint64_t id = fileIdBase + static_cast<uint64_t>(index);
        batch.push_back(MakeMftRecord(id, parent, FileName(index), 0, 1024 + index, 1000 + index));
        if (batch.size() == kBatchSize) {
            if (!pipeline.ApplyMftBatch(volumeId, batch)) return false;
            batch.clear();
        }
    }
    if (!batch.empty() && !pipeline.ApplyMftBatch(volumeId, batch)) return false;
    generationMs = ElapsedMs(generationStart);
    pipeline.Close();
    return true;
}

std::wstring PipeName(DWORD sessionId) {
    wchar_t buffer[128]{};
    swprintf(buffer, std::size(buffer), ffsetup::kUiCtrlPipeNameFormat, sessionId);
    return buffer;
}

HANDLE ConnectToServer(const std::wstring& pipeName) {
    const auto deadline = GetTickCount64() + static_cast<ULONGLONG>(kWaitDeadline.count());
    while (GetTickCount64() < deadline) {
        if (WaitNamedPipeW(pipeName.c_str(), 100)) {
            HANDLE pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                      OPEN_EXISTING, 0, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) {
                DWORD mode = PIPE_READMODE_BYTE;
                if (SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) return pipe;
                CloseHandle(pipe);
            }
        }
    }
    return INVALID_HANDLE_VALUE;
}

std::optional<ffipc::ReceivedFrame> ReadFrameUntilType(HANDLE pipe, uint16_t messageType,
                                                       std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
        if (WaitForSingleObject(pipe, 200) == WAIT_OBJECT_0) {
            auto frame = ffipc::ReadFrame(pipe);
            if (frame && frame->header.messageType == messageType) return frame;
            // A non-matching frame was drained; keep polling for the wanted one.
        }
    }
    return std::nullopt;
}

// Mirrors EngineClient::IsSnapshotGenerationUsable exactly as the lifecycle
// regression suite does, so the measured parse is the real client parse.
std::optional<std::map<std::wstring, ffprotocol::SnapshotDirectory>> ValidateAndParseSnapshotSection(
    const std::wstring& sectionName, uint64_t expectedGeneration) {
    if (sectionName.empty()) return std::nullopt;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, sectionName.c_str());
    if (mapping == nullptr) return std::nullopt;
    const auto* view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (view == nullptr) {
        CloseHandle(mapping);
        return std::nullopt;
    }
    const auto* header = reinterpret_cast<const ffprotocol::SnapshotSharedHeader*>(view);
    const bool valid = ffprotocol::IsSnapshotFormatCompatible(header->formatVersion) &&
        expectedGeneration != 0 && header->generation == expectedGeneration &&
        header->payloadSizeBytes <= ffprotocol::kSnapshotMaximumPayloadBytes &&
        header->payloadSizeBytes <=
            static_cast<uint64_t>(std::numeric_limits<size_t>::max() - sizeof(*header));
    std::optional<std::map<std::wstring, ffprotocol::SnapshotDirectory>> parsed;
    if (valid) {
        parsed = ffprotocol::ParseSnapshot(view + sizeof(*header),
                                           static_cast<size_t>(header->payloadSizeBytes));
    }
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return parsed;
}

double Median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    return values.size() % 2 == 0 ? (values[middle - 1] + values[middle]) / 2.0 : values[middle];
}

double Percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double raw = fraction * static_cast<double>(values.size() - 1);
    const size_t lower = static_cast<size_t>(raw);
    const size_t upper = (std::min)(lower + 1, values.size() - 1);
    const double weight = raw - static_cast<double>(lower);
    return values[lower] * (1.0 - weight) + values[upper] * weight;
}

double Max(std::vector<double> values) {
    if (values.empty()) return 0.0;
    return *std::max_element(values.begin(), values.end());
}

struct Run {
    double exportMs = 0.0;
    double publicationMs = 0.0;
    double clientReceiveMs = 0.0;
    double clientParseMs = 0.0;
    double totalMs = 0.0;
    size_t directoriesPublished = 0;
    size_t filesPublished = 0;
    uint64_t generationId = 0;
    bool usable = false;
};

struct Bench {
    ffengine::IndexPipeline pipeline;
    ffindexstore::VolumeRowId volumeId = 0;
    std::unique_ptr<ffengine::UiServer> server;
    HANDLE client = INVALID_HANDLE_VALUE;
    DWORD sessionId = 0;
    std::wstring sectionPrefix;

    ~Bench() {
        if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
        if (server) server->Stop();
        pipeline.Close();
    }
};

} // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!ParseOptions(argc, argv, options)) return 2;

    const auto root = MakeTemporaryDirectory();
    if (root.empty()) {
        std::fprintf(stderr, "unable to create a disposable fixture directory\n");
        return 1;
    }
    const auto database = root / L"index.db";

    double generationMs = 0.0;
    ffindexstore::VolumeRowId volumeId = 0;
    if (!BuildSyntheticDatabase(database, options.entries, generationMs, volumeId)) {
        std::fprintf(stderr, "synthetic database generation failed\n");
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    }

    Bench bench;
    if (!bench.pipeline.Open(ToUtf8(database.wstring()))) {
        std::fprintf(stderr, "fixture pipeline open failed\n");
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
    bench.volumeId = volumeId;
    // A durable rebuild is required before the projection can export the
    // volume's directories, exactly as LoadPipeline does for the scalability
    // bench: Open alone leaves the projection empty.
    bench.pipeline.RebuildAll({});
    if (!bench.pipeline.MarkScanComplete(volumeId)) {
        std::fprintf(stderr, "fixture scan-complete marking failed\n");
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    }

    // The engine's real frame writer and pipe canceller, so the measurement
    // crosses the actual control pipe rather than an in-process stand-in.
    bench.sessionId = GetCurrentProcessId();
    bench.server = std::make_unique<ffengine::UiServer>();
    if (!bench.server->Start(bench.sessionId)) {
        std::fprintf(stderr, "UI server start failed\n");
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
    bench.client = ConnectToServer(PipeName(bench.sessionId));
    if (bench.client == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "benchmark client could not connect\n");
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
    if (!ffipc::WriteFrame(bench.client, static_cast<uint16_t>(ffprotocol::UiMessageType::Subscribe))) {
        std::fprintf(stderr, "benchmark client could not subscribe\n");
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
    {
        // The ack advertises the committed generation and the shared snapshot
        // section prefix this reader must open. Start() commits a canonical
        // empty generation without broadcasting, so nothing arrives before the
        // first indexed publication.
        const auto ack = ReadFrameUntilType(
            bench.client, static_cast<uint16_t>(ffprotocol::UiMessageType::SubscribeAck),
            kWaitDeadline);
        if (!ack || ack->payload.size() < sizeof(ffprotocol::SubscribeAckPayload)) {
            std::fprintf(stderr, "subscribe acknowledgement never reached the client\n");
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
            return 1;
        }
        ffprotocol::SubscribeAckPayload header{};
        std::memcpy(&header, ack->payload.data(), sizeof(header));
        bench.sectionPrefix.assign(
            reinterpret_cast<const wchar_t*>(ack->payload.data() + sizeof(header)),
            header.sectionNameLengthChars);
        if (header.generationId == 0 || bench.sectionPrefix.empty()) {
            std::fprintf(stderr, "subscribe acknowledgement advertised an unusable snapshot\n");
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
            return 1;
        }
    }

    std::printf("FastFiles first-generation publish benchmark (%zu synthetic entries, %s)\n",
                options.entries, ffsetup::kBuildIdentityAscii);
    std::printf("fixture SQLite generation %.2f ms; %zu warm-up and %zu measured repetitions\n",
                generationMs, options.warmup, options.measured);
    std::fflush(stdout);

    std::vector<double> exportSamples;
    std::vector<double> publicationSamples;
    std::vector<double> receiveSamples;
    std::vector<double> parseSamples;
    std::vector<double> totalSamples;
    size_t lastDirectories = 0;
    size_t lastFiles = 0;
    uint64_t lastGeneration = 0;

    const size_t totalRepetitions = options.warmup + options.measured;
    for (size_t repetition = 0; repetition < totalRepetitions; ++repetition) {
        const bool measured = repetition >= options.warmup;
        Run run;

        const auto exportStart = Clock::now();
        auto directories = bench.pipeline.ExportDirectorySnapshotBounded(
            bench.volumeId, L"C:", ffengine::IndexPipeline::kInitialIndexedExportDirectories);
        run.exportMs = ElapsedMs(exportStart);
        run.directoriesPublished = directories.size();
        for (const auto& entry : directories) {
            lastFiles += entry.second.entries.size();
        }

        const auto publishStart = Clock::now();
        bench.server->MergeIndexDirectories(directories);
        run.publicationMs = ElapsedMs(publishStart);

        const auto receiveStart = Clock::now();
        const auto frame = ReadFrameUntilType(
            bench.client, static_cast<uint16_t>(ffprotocol::UiMessageType::NewGeneration),
            kWaitDeadline);
        if (!frame) {
            std::fprintf(stderr, "no NewGeneration frame reached the client\n");
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
            return 1;
        }
        ffprotocol::NewGenerationPayload payload{};
        std::memcpy(&payload, frame->payload.data(), sizeof(payload));
        run.generationId = payload.generationId;
        run.clientReceiveMs = ElapsedMs(receiveStart);

        const auto sectionName = bench.sectionPrefix + L"." + std::to_wstring(payload.generationId);
        const auto parseStart = Clock::now();
        auto parsed = ValidateAndParseSnapshotSection(sectionName, payload.generationId);
        run.clientParseMs = ElapsedMs(parseStart);
        run.usable = parsed.has_value();
        if (parsed) {
            run.filesPublished = 0;
            for (const auto& entry : *parsed) {
                run.filesPublished += entry.second.entries.size();
            }
        }

        run.totalMs = run.exportMs + run.publicationMs + run.clientReceiveMs + run.clientParseMs;
        lastDirectories = run.directoriesPublished;
        lastFiles = run.filesPublished;
        lastGeneration = run.generationId;

        if (!run.usable) {
            std::fprintf(stderr, "published snapshot failed client-side validation\n");
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
            return 1;
        }
        if (measured) {
            exportSamples.push_back(run.exportMs);
            publicationSamples.push_back(run.publicationMs);
            receiveSamples.push_back(run.clientReceiveMs);
            parseSamples.push_back(run.clientParseMs);
            totalSamples.push_back(run.totalMs);
        }
    }

    std::printf("directories published %zu; files published %zu; last generation %llu\n",
                lastDirectories, lastFiles, static_cast<unsigned long long>(lastGeneration));
    std::printf("  export            median %.3f ms  p90 %.3f ms  max %.3f ms\n",
                Median(exportSamples), Percentile(exportSamples, 0.90), Max(exportSamples));
    std::printf("  publish+broadcast median %.3f ms  p90 %.3f ms  max %.3f ms\n",
                Median(publicationSamples), Percentile(publicationSamples, 0.90),
                Max(publicationSamples));
    std::printf("  pipe receive      median %.3f ms  p90 %.3f ms  max %.3f ms\n",
                Median(receiveSamples), Percentile(receiveSamples, 0.90), Max(receiveSamples));
    std::printf("  client parse      median %.3f ms  p90 %.3f ms  max %.3f ms\n",
                Median(parseSamples), Percentile(parseSamples, 0.90), Max(parseSamples));
    std::printf("  TOTAL             median %.3f ms  p90 %.3f ms  max %.3f ms\n",
                Median(totalSamples), Percentile(totalSamples, 0.90), Max(totalSamples));
    std::fflush(stdout);

    std::filesystem::path parent = std::filesystem::path(options.outputPath).parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
    }
    std::ofstream output(options.outputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::fprintf(stderr, "unable to open benchmark output\n");
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
    auto writeStats = [&output](const char* name, const std::vector<double>& samples) {
        output << "  \"" << name << "\":{\"median\":" << Median(samples)
               << ",\"p90\":" << Percentile(samples, 0.90) << ",\"max\":" << Max(samples)
               << ",\"samples\":[";
        for (size_t index = 0; index < samples.size(); ++index) {
            if (index != 0) output << ',';
            output << samples[index];
        }
        output << "]}";
    };
    output << "{\n  \"schema\":\"fastfiles-first-generation-publish-v1\",\n"
           << "  \"purpose\":\"user-visible first-generation delivery; informational, not a CTest gate\",\n"
           << "  \"build_identity\":\"" << ffsetup::kBuildIdentityAscii << "\",\n"
           << "  \"build_worktree_state\":\"" << ffsetup::kBuildWorktreeStateAscii << "\",\n"
           << "  \"entries\":" << options.entries << ",\n"
           << "  \"directory_fixture_build_ms\":" << generationMs << ",\n"
           << "  \"warmup\":" << options.warmup << ",\n"
           << "  \"measured\":" << options.measured << ",\n"
           << "  \"directories_published\":" << lastDirectories << ",\n"
           << "  \"files_published_last_repetition\":" << lastFiles << ",\n"
           << "  \"last_generation_id\":" << lastGeneration << ",\n";
    writeStats("export_ms", exportSamples);
    output << ",\n";
    writeStats("publication_ms", publicationSamples);
    output << ",\n";
    writeStats("client_receive_ms", receiveSamples);
    output << ",\n";
    writeStats("client_parse_ms", parseSamples);
    output << ",\n";
    writeStats("total_ms", totalSamples);
    output << "\n}\n";
    output.close();

    std::printf("Wrote %s\\n", ToUtf8(options.outputPath).c_str());
    std::fflush(stdout);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    return 0;
}
