#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <windows.h>

#include "ffprotocol/SnapshotFormat.h"
#include "ffsearch/Search.h"

namespace ffui {

inline constexpr size_t kStorageRankingLimit = 100;

struct StorageRankingScope {
    std::wstring matchPath;
    bool indexedSupported = false;
};

// Matching spelling only. Operation paths stay exactly as their source supplied
// them. Reject relative/dot-component paths rather than resolving against CWD or
// claiming that a lexical query traverses reparse targets/namespace aliases.
inline std::optional<StorageRankingScope> NormalizeStorageRankingScope(std::wstring_view path) {
    std::wstring normalized(path);
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    if (normalized.size() >= 8 && CompareStringOrdinal(normalized.c_str(), 8, L"\\\\?\\UNC\\", 8, TRUE) == CSTR_EQUAL) normalized = L"\\\\" + normalized.substr(8);
    else if (normalized.starts_with(L"\\\\?\\")) normalized.erase(0, 4);
    const bool drive = normalized.size() >= 3 &&
        ((normalized[0] >= L'A' && normalized[0] <= L'Z') ||
         (normalized[0] >= L'a' && normalized[0] <= L'z')) &&
        normalized[1] == L':' && normalized[2] == L'\\';
    const bool unc = normalized.starts_with(L"\\\\") && normalized.size() > 4;
    if (!drive && !unc) return std::nullopt;
    const size_t start = drive ? 3 : 2;
    size_t components = 0;
    for (size_t pos = start; pos < normalized.size();) {
        const size_t end = normalized.find(L'\\', pos);
        const auto part = std::wstring_view(normalized).substr(pos,
            end == std::wstring::npos ? normalized.size() - pos : end - pos);
        if (part.empty() || part == L"." || part == L".." || part.find(L':') != std::wstring_view::npos) return std::nullopt;
        ++components;
        if (end == std::wstring::npos) break;
        pos = end + 1;
    }
    if (unc && components < 2) return std::nullopt;
    while (normalized.size() > 3 && normalized.back() == L'\\') normalized.pop_back();
    return StorageRankingScope{std::move(normalized), drive};
}

inline bool StorageRankingContains(const StorageRankingScope& scope, std::wstring_view folder) {
    if (scope.matchPath.empty()) return false;
    const auto normalized = NormalizeStorageRankingScope(folder);
    if (!normalized || normalized->matchPath.size() < scope.matchPath.size()) return false;
    if (CompareStringOrdinal(scope.matchPath.c_str(), static_cast<int>(scope.matchPath.size()),
            normalized->matchPath.c_str(), static_cast<int>(scope.matchPath.size()), TRUE) != CSTR_EQUAL) return false;
    return normalized->matchPath.size() == scope.matchPath.size() || scope.matchPath.back() == L'\\' ||
        normalized->matchPath[scope.matchPath.size()] == L'\\';
}

// Presentation only: keep the scope's repeated absolute prefix out of the
// narrow Location column. UIA HelpText and operation targets retain full paths.
inline std::wstring StorageRankingLocationText(std::wstring_view scopeFolder, std::wstring_view folder) {
    const auto scope = NormalizeStorageRankingScope(scopeFolder);
    const auto normalized = NormalizeStorageRankingScope(folder);
    if (!scope || !normalized || !StorageRankingContains(*scope, folder)) return std::wstring(folder);
    if (scope->matchPath.size() == normalized->matchPath.size()) return L"This folder";
    const size_t offset = scope->matchPath.size() + (scope->matchPath.back() == L'\\' ? 0 : 1);
    return normalized->matchPath.substr(offset);
}

inline bool StorageRankingCompletionMatches(uint64_t generation, uint64_t currentGeneration,
        std::wstring_view currentPath, const StorageRankingScope& completionScope,
        bool visible, bool recursive) {
    if (!visible || !recursive || generation == 0 || generation != currentGeneration) return false;
    const auto scope = NormalizeStorageRankingScope(currentPath);
    return scope && scope->matchPath.size() == completionScope.matchPath.size() &&
        CompareStringOrdinal(scope->matchPath.c_str(), static_cast<int>(scope->matchPath.size()),
            completionScope.matchPath.c_str(), static_cast<int>(completionScope.matchPath.size()), TRUE) == CSTR_EQUAL;
}

inline std::wstring StorageRankingItemPath(std::wstring_view folder, std::wstring_view name) {
    if (!NormalizeStorageRankingScope(folder) || name.empty() || name == L"." || name == L".." ||
        name.find_first_of(L"\\/:") != std::wstring_view::npos) return {};
    std::wstring path(folder);
    if (path.back() != L'\\' && path.back() != L'/') path += L'\\';
    path += name;
    return path;
}

// Streams only successful published listings; no filesystem walk, no inference
// of recursive completeness, and no full candidate-vector copy. File exclusion
// and component-bounded scope filtering precede the existing true top-N heap.
inline ffsearch::SearchResponse RankKnownStorageSnapshot(
        const std::map<std::wstring, ffprotocol::SnapshotDirectory>& snapshot,
        const StorageRankingScope& scope, const std::function<bool()>& cancelled = {}) {
    ffsearch::SearchRequest request;
    request.query = ffsearch::ParseQuery(L"kind:file", ffsearch::FilterRegistry::WithDefaults());
    request.scope = ffsearch::SearchScope::AllIndexedLocations;
    request.sortField = ffsearch::SortField::Size;
    request.descending = true;
    request.maxResults = kStorageRankingLimit;
    request.candidateFilter = [scope](const ffsearch::Candidate& item) {
        return StorageRankingContains(scope, item.folder) && !StorageRankingItemPath(item.folder, item.name).empty();
    };
    ffsearch::SearchAccumulator accumulator(request, cancelled);
    for (const auto& [folder, directory] : snapshot) {
        if (cancelled && cancelled()) return {{}, true, false, 0};
        if (directory.status != ffprotocol::DirectoryEnumerationStatus::Success || !StorageRankingContains(scope, folder)) continue;
        for (const auto& entry : directory.entries) {
            if (entry.isDirectory) continue;
            ffsearch::Candidate item;
            item.name = entry.name;
            item.folder = folder;
            item.sizeBytes = entry.sizeBytes;
            item.modifiedTime = entry.lastModifiedTime;
            item.createdTime = entry.creationTime;
            item.volumeId = entry.volumeRowId;
            item.id = entry.fileIdLow;
            item.parentId = entry.parentIdLow;
            item.persistentIndexed = entry.volumeRowId != 0;
            item.allocatedSizeBytes = entry.allocatedSizeBytes;
            item.allocatedSizeKnown = entry.allocatedSizeKnown;
            if (!accumulator.Add(std::move(item))) return accumulator.Finish();
        }
    }
    return accumulator.Finish();
}

inline std::wstring StorageRankingStatusText(size_t count, bool indexed, bool truncated) {
    return (indexed ? L"Indexed known files: " : L"Browsed/published files: ") + std::to_wstring(count) +
        (indexed ? L" — coverage unverified" : L" — partial coverage") +
        L"; subfolders included; " + (truncated ? L"top 100 logical sizes" : L"up to 100 logical sizes") +
        L"; sorting affects displayed rows";
}

} // namespace ffui
