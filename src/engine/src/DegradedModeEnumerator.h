#pragma once
#include <string>
#include <vector>

namespace ffengine {

struct DirectoryEntry {
    std::wstring name;
    bool isDirectory = false;
    bool accessible = true; // false if per-entry metadata could not be read
    uint64_t sizeBytes = 0;
    uint64_t creationTime = 0;
    uint64_t lastModifiedTime = 0;
    uint32_t attributes = 0;
};

enum class EnumerationStatus {
    Success,
    AccessDenied,
    NotFound,
};

struct EnumerationResult {
    EnumerationStatus status = EnumerationStatus::NotFound;
    std::vector<DirectoryEntry> entries;
};

// Unprivileged directory enumeration via FindFirstFileEx (task 4.5),
// respecting the calling user's own filesystem permissions -- this is the
// engine's permanently-supported fallback path (design.md D5), not an
// error path only used when the privileged service is unavailable.
//
// A subfolder this process's token cannot fully stat is included with
// accessible = false rather than aborting the whole listing (spec
// "Inaccessible subfolder is skipped, not fatal").
EnumerationResult EnumerateDirectoryDegraded(const std::wstring& directoryPath);

} // namespace ffengine
