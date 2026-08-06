// Shared test-support helpers for the FastFiles test executables.
//
// Every test target is a single translation unit, so the failure counter
// lives in a function-local static inside an inline function: all Check()
// calls in one test executable share it, and main() reports it via
// fftest::FailureCount(). The Check() call signature -- Check(bool,
// const char*) -- is deliberately identical to the copy-pasted helpers it
// replaces, so existing call sites are untouched.
//
// This header is for tests only; product code must not include it.

#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

namespace fftest {

inline int& FailureCounter() {
    static int failures = 0;
    return failures;
}

inline void Check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++FailureCounter();
    } else {
        std::printf("ok: %s\n", description);
    }
}

inline int FailureCount() { return FailureCounter(); }

// Removes any stale SQLite database plus its -wal/-shm sidecars at the
// given temp path so each test opens a genuinely fresh database.
inline std::string FreshDbPath(const char* name) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
    return path.string();
}

}  // namespace fftest
