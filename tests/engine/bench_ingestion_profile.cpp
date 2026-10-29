// durable-cold-search-index-and-hybrid-residency: reuse EXACT fixture code
// without running the query/catalog matrix. FF_INGESTION_PROFILE enables
// per-batch stage timing only in this diagnostic executable, never shipping.
#define wmain LargeIndexBenchmarkMain
#include "bench_large_index_scalability.cpp"
#undef wmain

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) return 2;
    const bool entriesOnly = argc == 3 && std::wstring(argv[2]) == L"--entries-only";
    if (argc == 3 && !entriesOnly) return 2;
    const size_t rows = static_cast<size_t>(std::wcstoull(argv[1], nullptr, 10));
    if (rows == 0) return 2;
    const auto directory = MakeTemporaryDirectory(rows);
    if (directory.empty()) return 3;
    std::printf("PROFILE_DATABASE %s\n", PathToUtf8(directory / L"index.db").c_str());
    std::fflush(stdout);
    double elapsedMs = 0;
    ffindexstore::VolumeRowId volume = 0;
    MemorySample before;
    MemorySample after;
    ffindexstore::SetTestingCandidateMaintenanceDisabled(entriesOnly);
    const bool ok = BuildSyntheticDatabase(directory / L"index.db", rows, elapsedMs, volume,
                                            before, after, true);
    ffindexstore::SetTestingCandidateMaintenanceDisabled(false);
    std::printf("PROFILE_COMPLETE rows=%zu elapsed_ms=%.3f ok=%d\n", rows, elapsedMs, ok ? 1 : 0);
    std::filesystem::remove_all(directory);
    return ok ? 0 : 1;
}
