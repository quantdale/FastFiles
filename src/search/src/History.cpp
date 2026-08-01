#include "ffsearch/History.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>

namespace ffsearch {

bool SearchHistory::Load(const std::filesystem::path& path) {
    queries_.clear();
    timestamps_.clear();
    std::wifstream input(path);
    if (!input) return true;
    for (std::wstring line; std::getline(input, line);) {
        if (line.empty()) continue;
        uint64_t timestamp = 0;
        std::wstring query = line;
        const size_t tab = line.find(L'\t');
        if (tab != std::wstring::npos) {
            try { timestamp = std::stoull(line.substr(0, tab)); } catch (const std::exception&) { timestamp = 0; }
            query = line.substr(tab + 1);
        }
        if (!query.empty() && std::find(queries_.begin(), queries_.end(), query) == queries_.end()) {
            queries_.push_back(std::move(query));
            timestamps_.push_back(timestamp);
        }
    }
    return !input.bad();
}

bool SearchHistory::Save(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::wofstream output(path, std::ios::trunc);
    if (!output) return false;
    for (size_t index = 0; index < queries_.size(); ++index) output << timestamps_[index] << L'\t' << queries_[index] << L'\n';
    return static_cast<bool>(output);
}

bool SearchHistory::Clear(const std::filesystem::path& path) {
    queries_.clear();
    timestamps_.clear();
    std::error_code error;
    std::filesystem::remove(path, error);
    return !error;
}

bool SearchHistory::Record(std::wstring query, const std::filesystem::path& path, size_t limit) {
    if (!recordingEnabled_ || query.empty()) return true;
    const auto existing = std::find(queries_.begin(), queries_.end(), query);
    if (existing != queries_.end()) {
        const size_t index = static_cast<size_t>(existing - queries_.begin());
        queries_.erase(existing);
        timestamps_.erase(timestamps_.begin() + static_cast<std::ptrdiff_t>(index));
    }
    queries_.insert(queries_.begin(), std::move(query));
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    timestamps_.insert(timestamps_.begin(), static_cast<uint64_t>(now));
    if (queries_.size() > limit) {
        queries_.resize(limit);
        timestamps_.resize(limit);
    }
    return Save(path);
}

} // namespace ffsearch
