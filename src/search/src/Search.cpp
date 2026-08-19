#include "ffsearch/Search.h"
#include "ffsearch/Path.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>

namespace ffsearch {
namespace {

bool OrdinalEqual(std::wstring_view left, std::wstring_view right) {
    return left.size() == right.size() && CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
        right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

bool OrdinalStartsWith(std::wstring_view value, std::wstring_view prefix) {
    return value.size() >= prefix.size() && CompareStringOrdinal(value.data(), static_cast<int>(prefix.size()),
        prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

bool InScope(const Candidate& candidate, SearchScope scope, const std::wstring& root, bool persistentOnly) {
    if (persistentOnly && !candidate.persistentIndexed) return false;
    if (scope == SearchScope::AllIndexedLocations) return true;
    if (scope == SearchScope::CurrentFolder) return OrdinalEqual(candidate.folder, root);
    if (scope == SearchScope::CurrentDrive) {
        return SameVolume(root, candidate.folder);
    }
    if (OrdinalEqual(candidate.folder, root)) return true;
    if (!OrdinalStartsWith(candidate.folder, root)) return false;
    return root.empty() || root.back() == L'\\' ||
           (candidate.folder.size() > root.size() && candidate.folder[root.size()] == L'\\');
}

int CompareOrdinal(std::wstring_view left, std::wstring_view right) {
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()), TRUE) - CSTR_EQUAL;
}

struct EntryKey {
    int64_t volume;
    uint64_t id;
    bool operator==(const EntryKey&) const = default;
};
struct EntryKeyHash {
    size_t operator()(const EntryKey& key) const noexcept {
        return std::hash<int64_t>{}(key.volume) ^ (std::hash<uint64_t>{}(key.id) << 1);
    }
};
}

MatchTier ClassifyMatch(const Candidate& candidate, std::wstring_view term) {
    if (term.empty()) return MatchTier::Substring;
    std::wstring_view stem = candidate.name;
    const size_t dot = stem.find_last_of(L'.');
    if (dot != std::wstring_view::npos) stem = stem.substr(0, dot);
    if (OrdinalEqual(stem, term) || OrdinalEqual(candidate.name, term)) return MatchTier::Exact;
    if (OrdinalStartsWith(candidate.name, term)) return MatchTier::Prefix;
    if (OrdinalContains(candidate.name, term)) return MatchTier::Substring;
    return MatchTier::PathOnly;
}

void SortResults(std::vector<SearchResult>& results, SortField field, bool descending) {
    const auto compare = [field, descending](const SearchResult& left, const SearchResult& right) {
        int primary = 0;
        switch (field) {
            case SortField::Relevance:
                primary = static_cast<int>(left.tier) - static_cast<int>(right.tier);
                if (primary == 0) primary = static_cast<int>(left.candidate.name.size()) - static_cast<int>(right.candidate.name.size());
                if (primary == 0) primary = static_cast<int>(left.candidate.folder.size()) - static_cast<int>(right.candidate.folder.size());
                break;
            case SortField::Filename: primary = CompareOrdinal(left.candidate.name, right.candidate.name); break;
            case SortField::Path: primary = CompareOrdinal(left.candidate.folder, right.candidate.folder); break;
            case SortField::Size: primary = left.candidate.sizeBytes < right.candidate.sizeBytes ? -1 : left.candidate.sizeBytes > right.candidate.sizeBytes ? 1 : 0; break;
            case SortField::Modified: primary = left.candidate.modifiedTime < right.candidate.modifiedTime ? -1 : left.candidate.modifiedTime > right.candidate.modifiedTime ? 1 : 0; break;
            case SortField::Created: primary = left.candidate.createdTime < right.candidate.createdTime ? -1 : left.candidate.createdTime > right.candidate.createdTime ? 1 : 0; break;
            case SortField::AllocatedSize: {
                // indexed-search-allocated-size D6: known values sort numerically;
                // an UNKNOWN allocated size always sorts AFTER every known value in
                // BOTH directions (never behaves like zero or UINT64_MAX). When the
                // two rows differ only in knownness, the direction is decided here
                // and must not be flipped by `descending`.
                const bool leftKnown = left.candidate.allocatedSizeKnown;
                const bool rightKnown = right.candidate.allocatedSizeKnown;
                if (leftKnown != rightKnown) return leftKnown;
                if (leftKnown) {
                    primary = left.candidate.allocatedSizeBytes < right.candidate.allocatedSizeBytes ? -1
                             : left.candidate.allocatedSizeBytes > right.candidate.allocatedSizeBytes ? 1 : 0;
                }
                // Both unknown: no primary ordering from allocated size; the
                // alphabetical tie-break below applies (deterministic unknowns).
                break;
            }
        }
        if (primary != 0) return descending ? primary > 0 : primary < 0;
        return CompareOrdinal(left.candidate.name, right.candidate.name) < 0;
    };
    std::stable_sort(results.begin(), results.end(), compare);
}

SearchResponse ExecuteSearchCore(std::vector<Candidate>* mutableCandidates,
                                 const std::vector<Candidate>& candidates, const SearchRequest& request,
                                 const CandidateMaterializer& materialize,
                                 const std::function<bool()>& cancelled) {
    SearchResponse response;
    const size_t chunkSize = (std::max)(size_t{1}, request.chunkSize);
    const bool folderNeededForEvaluation = request.scope != SearchScope::AllIndexedLocations ||
                                           request.query.requiresFolder;
    for (size_t index = 0; index < candidates.size(); ++index) {
        if (index % chunkSize == 0 && cancelled && cancelled()) {
            response.cancelled = true;
            response.results.clear();
            return response;
        }
        ++response.candidatesVisited;
        Candidate* mutableCandidate = mutableCandidates == nullptr ? nullptr : &(*mutableCandidates)[index];
        const Candidate& candidate = mutableCandidate == nullptr ? candidates[index] : *mutableCandidate;
        if (!request.query.MatchesWithoutFolder(candidate)) continue;
        if (folderNeededForEvaluation && materialize &&
            (mutableCandidate == nullptr || !materialize(*mutableCandidate))) continue;
        if (!InScope(candidate, request.scope, request.scopePath, request.persistentOnly)) continue;
        if (!request.query.Matches(candidate)) continue;
        if (!folderNeededForEvaluation && materialize &&
            (mutableCandidate == nullptr || !materialize(*mutableCandidate))) continue;
        response.results.push_back({candidate, ClassifyMatch(candidate, request.query.primaryTerm)});
        if (request.maxResults != 0 && response.results.size() >= request.maxResults) {
            response.truncated = true;
            break;
        }
    }
    SortResults(response.results, request.sortField, request.descending);
    return response;
}

SearchResponse ExecuteSearch(const std::vector<Candidate>& candidates, const SearchRequest& request,
                             const std::function<bool()>& cancelled) {
    return ExecuteSearchCore(nullptr, candidates, request, {}, cancelled);
}

SearchResponse ExecuteSearchLazy(std::vector<Candidate>& candidates, const SearchRequest& request,
                                  const CandidateMaterializer& materialize,
                                  const std::function<bool()>& cancelled) {
    return ExecuteSearchCore(&candidates, candidates, request, materialize, cancelled);
}

SearchResponse ExecuteSearchViews(const CandidateViewEnumerator& enumerate,
                                  const SearchRequest& request,
                                  const CandidateViewMaterializer& materialize,
                                  const std::function<bool()>& cancelled,
                                  SearchDiagnostics* diagnostics) {
    SearchResponse response;
    if (!request.query.viewCompatible) return response;

    const size_t chunkSize = (std::max)(size_t{1}, request.chunkSize);
    const bool folderNeededForEvaluation = request.scope != SearchScope::AllIndexedLocations ||
                                           request.query.requiresFolder;
    bool stopped = false;
    auto visitLambda = [&](const CandidateView& candidate) {
        if (stopped) return false;
        if (response.candidatesVisited % chunkSize == 0 && cancelled && cancelled()) {
            response.cancelled = true;
            response.results.clear();
            stopped = true;
            return false;
        }
        ++response.candidatesVisited;
        if (diagnostics != nullptr && diagnostics->projectionOnly) return true;

        std::optional<Candidate> ownedCandidate;
        if (diagnostics != nullptr) ++diagnostics->pathNeededDecisions;
        if (folderNeededForEvaluation) {
            if (!request.query.MatchesViewWithoutFolder(candidate,
                                                        diagnostics != nullptr ? &diagnostics->match : nullptr)) {
                return true;
            }
            if (diagnostics != nullptr) ++diagnostics->pathLookups;
            ownedCandidate = materialize(candidate);
            if (!ownedCandidate || !InScope(*ownedCandidate, request.scope, request.scopePath,
                                            request.persistentOnly) || !request.query.Matches(*ownedCandidate)) {
                return true;
            }
        } else {
            if (!request.query.MatchesView(candidate,
                                           diagnostics != nullptr ? &diagnostics->match : nullptr)) return true;
            if (diagnostics != nullptr) ++diagnostics->pathLookups;
            ownedCandidate = materialize(candidate);
            if (!ownedCandidate) return true;
        }

        const auto matchedRowStart = diagnostics != nullptr ? std::chrono::steady_clock::now()
                                                             : std::chrono::steady_clock::time_point{};
        Candidate owned = std::move(*ownedCandidate);
        const MatchTier tier = ClassifyMatch(owned, request.query.primaryTerm);
        response.results.push_back({std::move(owned), tier});
        if (diagnostics != nullptr) {
            ++diagnostics->matchedRows;
            diagnostics->matchedRowHandlingMs +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - matchedRowStart).count();
        }
        if (request.maxResults != 0 && response.results.size() >= request.maxResults) {
            response.truncated = true;
            stopped = true;
            return false;
        }
        return true;
    };
    const CandidateViewVisitor visit(visitLambda);
    enumerate(visit);
    if (response.cancelled) return response;
    SortResults(response.results, request.sortField, request.descending);
    return response;
}

PathReconstruction ReconstructPath(const std::vector<Candidate>& entries, size_t entryIndex) {
    PathReconstruction result;
    if (entryIndex >= entries.size()) return result;
    std::unordered_map<EntryKey, const Candidate*, EntryKeyHash> byId;
    for (const auto& entry : entries) byId.emplace(EntryKey{entry.volumeId, entry.id}, &entry);
    const Candidate* current = &entries[entryIndex];
    std::unordered_set<EntryKey, EntryKeyHash> seen;
    while (current != nullptr) {
        if (!seen.insert(EntryKey{current->volumeId, current->id}).second) {
            result.unresolvedParentId = current->id;
            break;
        }
        result.segments.push_back(current->name);
        if (current->parentId == 0 || current->parentId == current->id) {
            result.complete = true;
            break;
        }
        const auto parent = byId.find({current->volumeId, current->parentId});
        if (parent == byId.end()) {
            result.unresolvedParentId = current->parentId;
            break;
        }
        current = parent->second;
    }
    std::reverse(result.segments.begin(), result.segments.end());
    return result;
}

} // namespace ffsearch
