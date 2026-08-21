#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace ffsearch {

class SearchHistory {
public:
    explicit SearchHistory(bool recordingEnabled = true) : recordingEnabled_(recordingEnabled) {}
    bool Load(const std::filesystem::path& path);
    bool Save(const std::filesystem::path& path) const;
    bool Clear(const std::filesystem::path& path);
    bool Record(std::wstring query, const std::filesystem::path& path, size_t limit = 100);
    // In-memory record only (dedupe-to-front, cap); the caller owns when the
    // store is flushed to disk so a UI thread can coalesce saves.
    bool RecordQuery(std::wstring query, size_t limit = 100);
    void SetRecordingEnabled(bool enabled) { recordingEnabled_ = enabled; }
    bool RecordingEnabled() const { return recordingEnabled_; }
    const std::vector<std::wstring>& Queries() const { return queries_; }
    const std::vector<uint64_t>& Timestamps() const { return timestamps_; }

private:
    bool recordingEnabled_ = true;
    std::vector<std::wstring> queries_;
    std::vector<uint64_t> timestamps_;
};

} // namespace ffsearch
