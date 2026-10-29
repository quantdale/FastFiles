// durable-cold-search-index-and-hybrid-residency 4.2/5.7: identical fixture,
// ordering, batches and production pipeline, without allocator replacements
// or per-batch stage timers. Existing instrumented targets stay unchanged.
#define FFBENCH_NO_ALLOCATOR_PROBE 1
#define wmain UnusedLargeIndexBenchmarkMain
#include "bench_large_index_scalability.cpp"
#undef wmain

namespace {
uint64_t CpuTicks() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    const auto ticks = [](const FILETIME value) {
        return (static_cast<uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    return ticks(kernel) + ticks(user);
}
}

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!ParseOptions(argc, argv, options) || options.memoryReport) return 2;
    std::ofstream out(options.outputPath, std::ios::binary | std::ios::trunc);
    if (!out) return 3;
    out << "{\"schema\":\"fastfiles-uninstrumented-ingestion-v1\",\"allocator_probe_enabled\":false,"
           "\"stage_timers_enabled\":false,\"fixture\":\"BuildSyntheticDatabase deterministic v1; no random seed\","
           "\"batch_rows\":8192,\"projection_budget_bytes\":524288000,\"source_sha_asserted\":";
    WriteJsonString(out, options.sourceSha);
    out << ",\"source_state_asserted\":";
    WriteJsonString(out, options.sourceState);
    out << ",\"build_identity\":";
    WriteJsonString(out, ffsetup::kBuildIdentityAscii);
    out << ",\"compiler_msc_full_ver\":" << _MSC_FULL_VER << ",\"preset\":";
    WriteJsonString(out, options.preset);
    out << ",\"command_line\":";
    WriteJsonString(out, options.commandLine);
    out << ",\"scales\":[";
    bool first = true;
    for (const auto scale : options.scales) {
        // Operational safety floor, not a claimed universal WAL bound.
        const auto minimum = scale >= 3'000'000 ? 4ULL * 1024 * 1024 * 1024 : 2ULL * 1024 * 1024 * 1024;
        if (std::filesystem::space(std::filesystem::temp_directory_path()).available < minimum) {
            std::fprintf(stderr, "INSUFFICIENT_HEADROOM rows=%zu required=%llu; incomplete artifact is NOT acceptance\n", scale, minimum);
            return 3;
        }
        const auto dir = MakeTemporaryDirectory(scale);
        if (dir.empty()) return 3;
        const auto database = dir / L"index.db";
        std::printf("OWNED_INGESTION_DATABASE=%s\n", PathToUtf8(database).c_str());
        double generationMs = 0;
        ffindexstore::VolumeRowId volume = 0;
        MemorySample before, after;
        const auto cpuBefore = CpuTicks();
        const auto start = Clock::now();
        const bool ok = BuildSyntheticDatabase(database, scale, generationMs, volume, before, after, true);
        const double wallMs = ElapsedMs(start);
        const double cpuMs = static_cast<double>(CpuTicks() - cpuBefore) / 10000.0;
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        const bool memoryOk = GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != FALSE;
        const auto teardown = ReadMemory();
        if (!first) out << ',';
        first = false;
        out << "{\"rows\":" << scale << ",\"completed\":" << (ok ? "true" : "false")
            << ",\"generation_ms\":" << generationMs << ",\"wall_ms_including_open_close\":" << wallMs
            << ",\"cpu_ms_including_open_close\":" << cpuMs
            << ",\"database_bytes\":" << DatabaseBytes(database)
            << ",\"private_after_ingestion_before_close\":" << after.privateBytes
            << ",\"private_after_teardown\":" << teardown.privateBytes
            << ",\"peak_private_bytes_cumulative_process\":" << (memoryOk ? counters.PeakPagefileUsage : 0)
            << ",\"peak_working_set_bytes_cumulative_process\":" << (memoryOk ? counters.PeakWorkingSetSize : 0)
            << ",\"memory_sample_valid\":" << (memoryOk ? "true" : "false") << '}';
        out.flush();
        std::filesystem::remove_all(dir);
        std::printf("OWNED_INGESTION_CLEANED=1 rows=%zu completed=%d elapsed_ms=%.3f\n", scale, ok ? 1 : 0, generationMs);
        std::fflush(stdout);
        if (!ok || !memoryOk) return 1;
    }
    out << "]}\n";
    out.close();
    return out ? 0 : 1;
}
