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
