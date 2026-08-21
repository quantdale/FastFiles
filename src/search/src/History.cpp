#include "ffsearch/History.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "ffprotocol/UiProtocol.h"

namespace ffsearch {

namespace {

constexpr size_t kMaxHistoryLineBytes =
    static_cast<size_t>(ffprotocol::kMaxIndexedSearchQueryLengthChars) * 4 + 64;

// Ordinal case-insensitive equality, matching the case-insensitive matching
// semantics of the search itself and the lexical case-insensitive dedup used
// by the folder history (RecentFolderHistory).
bool EqualsCaseInsensitive(const std::wstring& left, const std::wstring& right) {
    if (left.size() != right.size()) return false;
    return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
                                right.c_str(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

template <typename Container>
auto FindCaseInsensitive(Container& queries, const std::wstring& query) {
    return std::find_if(queries.begin(), queries.end(),
                        [&query](const std::wstring& entry) { return EqualsCaseInsensitive(entry, query); });
}

// UTF-16 <-> UTF-8 conversion for history persistence. The default C locale
// under which std::wofstream/std::wifstream run truncates non-ASCII wchar_t
// to a single byte, so the history file is written as explicit UTF-8 bytes
// instead (workstream E).
std::optional<std::wstring> Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring{};
    }
    if (utf8.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return std::nullopt;
    }
    const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), nullptr, 0);
    if (wideLength <= 0) {
        return std::nullopt;
    }
    std::wstring result(static_cast<size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                            static_cast<int>(utf8.size()), result.data(), wideLength) <= 0) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::string> WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return std::string{};
    }
    if (wide.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return std::nullopt;
    }
    const int utf8Length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
        static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0) {
        return std::nullopt;
    }
    std::string result(static_cast<size_t>(utf8Length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                            static_cast<int>(wide.size()), result.data(), utf8Length,
                            nullptr, nullptr) <= 0) {
        return std::nullopt;
    }
    return result;
}

} // namespace

bool SearchHistory::Load(const std::filesystem::path& path) {
    queries_.clear();
    timestamps_.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) return true;
    for (std::string line; std::getline(input, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back(); // tolerate legacy text-mode files
        if (line.size() > kMaxHistoryLineBytes) continue;
        const auto wideLine = Utf8ToWide(line);
        if (!wideLine || wideLine->empty()) continue;
        uint64_t timestamp = 0;
        std::wstring query = *wideLine;
        const size_t tab = wideLine->find(L'\t');
        if (tab != std::wstring::npos) {
            try { timestamp = std::stoull(wideLine->substr(0, tab)); } catch (const std::exception&) { timestamp = 0; }
            query = wideLine->substr(tab + 1);
        }
        if (query.size() <= ffprotocol::kMaxIndexedSearchQueryLengthChars && !query.empty() &&
            FindCaseInsensitive(queries_, query) == queries_.end()) {
            queries_.push_back(std::move(query));
            timestamps_.push_back(timestamp);
        }
    }
    return !input.bad();
}

bool SearchHistory::Save(const std::filesystem::path& path) const {
    if (path.empty()) return false;
    std::error_code error;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const std::wstring parentW = path.parent_path().wstring();
    // Use a unique temp name so two concurrent saves don't collide on
    // "<file>.tmp" and a crash-left stale .tmp can't be silently reused.
    // When there's no parent (bare filename), fall back to "<file>.tmp".
    std::filesystem::path temporary;
    if (parentW.empty()) {
        temporary = std::filesystem::path(path.wstring() + L".tmp");
    } else {
        wchar_t uniqueTemp[MAX_PATH * 4] = {};
        if (GetTempFileNameW(parentW.c_str(), L"ffh", 0, uniqueTemp) == 0) {
            return false;
        }
        temporary = uniqueTemp;
    }
    std::ofstream output(temporary, std::ios::trunc | std::ios::binary);
    if (!output) return false;
    for (size_t index = 0; index < queries_.size(); ++index) {
        if (index >= timestamps_.size() || queries_[index].empty() ||
            queries_[index].size() > ffprotocol::kMaxIndexedSearchQueryLengthChars) {
            output.close();
            DeleteFileW(temporary.c_str());
            return false;
        }
        const auto encoded = WideToUtf8(queries_[index]);
        if (!encoded) {
            output.close();
            DeleteFileW(temporary.c_str());
            return false;
        }
        output << timestamps_[index] << '\t' << *encoded << '\n';
        if (!output) {
            output.close();
            DeleteFileW(temporary.c_str());
            return false;
        }
    }
    output.close();
    if (!output) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

bool SearchHistory::Clear(const std::filesystem::path& path) {
    queries_.clear();
    timestamps_.clear();
    std::error_code error;
    std::filesystem::remove(path, error);
    return !error;
}

bool SearchHistory::Record(std::wstring query, const std::filesystem::path& path, size_t limit) {
    // A query above the protocol maximum is a hard rejection (false), unlike
    // the benign no-op cases handled inside RecordQuery (recording disabled,
    // empty query), which keep succeeding without touching the file.
    if (query.size() > ffprotocol::kMaxIndexedSearchQueryLengthChars) return false;
    if (!RecordQuery(std::move(query), limit)) return true;
    return Save(path);
}

bool SearchHistory::RecordQuery(std::wstring query, size_t limit) {
    if (!recordingEnabled_ || query.empty()) return false;
    if (query.size() > ffprotocol::kMaxIndexedSearchQueryLengthChars) return false;
    const auto existing = FindCaseInsensitive(queries_, query);
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
    return true;
}

} // namespace ffsearch
