#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ffsearch/Query.h"

namespace ffsearch {

enum class SearchScope { CurrentFolder, CurrentFolderAndSubfolders, CurrentDrive, AllIndexedLocations };
enum class MatchTier { Exact, Prefix, Substring, PathOnly };
// indexed-search-allocated-size D6: `AllocatedSize` (display label "Size on
// Disk") is appended so every pre-existing value keeps its exact numeric
// meaning on the engine/UI wire and in the UI sort selector. `Size` remains
// logical size.
enum class SortField { Relevance, Filename, Path, Size, Modified, Created, AllocatedSize };

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
    // Internal bounded-read seam: callers may reject a candidate using a
    // captured configuration snapshot. The filter is evaluated after any
    // required folder materialization and before query/scope acceptance.
    std::function<bool(const Candidate&)> candidateFilter;
    bool candidateFilterRequiresFolder = false;
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

// Optional benchmark-only attribution for the projection-backed search loop.
// The normal product path passes null and keeps the existing behavior.
struct SearchDiagnostics {
    MatchDiagnostics match;
    bool projectionOnly = false;
    size_t pathNeededDecisions = 0;
    size_t pathLookups = 0;
    size_t matchedRows = 0;
    double matchedRowHandlingMs = 0.0;
};

SearchResponse ExecuteSearch(const std::vector<Candidate>& candidates, const SearchRequest& request,
                             const std::function<bool()>& cancelled = {});
using CandidateMaterializer = std::function<bool(Candidate&)>;
SearchResponse ExecuteSearchLazy(std::vector<Candidate>& candidates, const SearchRequest& request,
                                  const CandidateMaterializer& materialize,
                                  const std::function<bool()>& cancelled = {});

// nonblocking-indexed-read-plane 4.2: consumes bounded candidate chunks while
// retaining only the requested top-N results. Matching, path materialization,
// and sorting occur in the caller's read phase; the accumulator owns no
// source-index snapshot and therefore cannot turn a broad query into a second
// full copy of the index.
class SearchAccumulator {
public:
    SearchAccumulator(const SearchRequest& request, const std::function<bool()>& cancelled = {},
                      SearchDiagnostics* diagnostics = nullptr);

    // Returns false only when cancellation was observed. A false return is a
    // cooperative stop signal for the source enumerator, not a search error.
    bool Add(Candidate candidate, const CandidateMaterializer& materialize = {});
    SearchResponse Finish();
    bool Cancelled() const noexcept { return response_.cancelled; }

private:
    SearchRequest request_;
    std::function<bool()> cancelled_;
    SearchDiagnostics* diagnostics_ = nullptr;
    bool folderNeededForEvaluation_ = false;
    SearchResponse response_;
};

class CandidateViewVisitor {
public:
    template <typename Fn>
    explicit CandidateViewVisitor(Fn& fn) noexcept
        : context_(&fn), invoke_([](void* context, const CandidateView& candidate) {
            return (*static_cast<Fn*>(context))(candidate);
        }) {}

    bool operator()(const CandidateView& candidate) const {
        return invoke_(context_, candidate);
    }

private:
    void* context_;
    bool (*invoke_)(void*, const CandidateView&);
};
using CandidateViewEnumerator = std::function<void(const CandidateViewVisitor&)>;
using CandidateViewMaterializer = std::function<std::optional<Candidate>(const CandidateView&)>;
SearchResponse ExecuteSearchViews(const CandidateViewEnumerator& enumerate,
                                  const SearchRequest& request,
                                  const CandidateViewMaterializer& materialize,
                                  const std::function<bool()>& cancelled = {},
                                  SearchDiagnostics* diagnostics = nullptr);
void SortResults(std::vector<SearchResult>& results, SortField field, bool descending);
MatchTier ClassifyMatch(const Candidate& candidate, std::wstring_view term);

struct PathReconstruction {
    std::vector<std::wstring> segments;
    bool complete = false;
    uint64_t unresolvedParentId = 0;
};

PathReconstruction ReconstructPath(const std::vector<Candidate>& entries, size_t entryIndex);

} // namespace ffsearch
