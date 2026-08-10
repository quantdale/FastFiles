#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "ffsearch/Query.h"

namespace ffsearch {

enum class SearchScope { CurrentFolder, CurrentFolderAndSubfolders, CurrentDrive, AllIndexedLocations };
enum class MatchTier { Exact, Prefix, Substring, PathOnly };
enum class SortField { Relevance, Filename, Path, Size, Modified, Created };

struct SearchRequest {
    Query query;
    SearchScope scope = SearchScope::AllIndexedLocations;
    std::wstring scopePath;
    SortField sortField = SortField::Relevance;
    bool descending = false;
    size_t chunkSize = 1024;
    // Bound UI-visible work and memory for broad searches. Zero means no
    // explicit limit for headless callers; the desktop panel sets a finite
    // limit and reports the bounded result count honestly.
    size_t maxResults = 0;
    // Broad scopes can require durable index coverage. Current-folder scopes
    // intentionally remain useful against degraded snapshots; the UI sets
    // this only when the selected broad scope is advertised as indexed.
    bool persistentOnly = false;
};

struct SearchResult {
    Candidate candidate;
    MatchTier tier = MatchTier::Substring;
};

struct SearchResponse {
    std::vector<SearchResult> results;
    bool cancelled = false;
    bool truncated = false;
    size_t candidatesVisited = 0;
};

SearchResponse ExecuteSearch(const std::vector<Candidate>& candidates, const SearchRequest& request,
                             const std::function<bool()>& cancelled = {});
using CandidateMaterializer = std::function<bool(Candidate&)>;
SearchResponse ExecuteSearchLazy(std::vector<Candidate>& candidates, const SearchRequest& request,
                                  const CandidateMaterializer& materialize,
                                  const std::function<bool()>& cancelled = {});
void SortResults(std::vector<SearchResult>& results, SortField field, bool descending);
MatchTier ClassifyMatch(const Candidate& candidate, std::wstring_view term);

struct PathReconstruction {
    std::vector<std::wstring> segments;
    bool complete = false;
    uint64_t unresolvedParentId = 0;
};

PathReconstruction ReconstructPath(const std::vector<Candidate>& entries, size_t entryIndex);

} // namespace ffsearch
