#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ffsearch {

struct Candidate {
    std::wstring name;
    std::wstring folder;
    uint64_t sizeBytes = 0;
    uint64_t modifiedTime = 0; // Windows FILETIME ticks
    bool isDirectory = false;
    uint64_t createdTime = 0;
    int64_t volumeId = 0;
    uint64_t id = 0;
    uint64_t parentId = 0;
    // True when the candidate came from the durable/index projection rather
    // than a degraded directory snapshot. Library callers historically build
    // candidates as indexed records, so the default preserves that contract;
    // the UI marks degraded-only records false at its snapshot boundary.
    bool persistentIndexed = true;
};

// A projection-backed candidate. The name is a view into the immutable
// NamePool while a search holds the projection lock; folder is populated only
// when scope or a folder predicate requires path evaluation. Search results
// continue to own ordinary Candidate strings.
struct CandidateView {
    std::u16string_view name;
    std::wstring_view folder;
    uint64_t sizeBytes = 0;
    uint64_t modifiedTime = 0;
    bool isDirectory = false;
    uint64_t createdTime = 0;
    int64_t volumeId = 0;
    uint64_t id = 0;
    uint64_t parentId = 0;
    bool persistentIndexed = true;
    bool nameAsciiLower = false;
};

struct SizeFilter { enum class Op { Equal, Greater, GreaterEqual, Less, LessEqual, Range }; Op op; uint64_t first; uint64_t second = 0; };
struct DateFilter { enum class Op { EqualDay, GreaterThan, Range }; Op op; uint64_t first; uint64_t second = 0; };
using FilterValue = std::variant<std::wstring, SizeFilter, DateFilter>;
using Predicate = std::function<bool(const Candidate&)>;
using ViewPredicate = std::function<bool(const CandidateView&)>;

enum class PredicateKind {
    Unknown,
    LiteralName,
    GlobName,
    FolderName,
    Extension,
    Size,
    Modified,
    Kind,
};

// Optional benchmark-only attribution for the canonical view matcher. A
// null diagnostics pointer keeps ordinary searches on the existing path.
struct MatchDiagnostics {
    size_t predicateEvaluations = 0;
    size_t termEvaluations = 0;
    size_t caseInsensitiveComparisons = 0;
    size_t caseFoldOperations = 0;
    size_t substringSearches = 0;
    size_t globMatches = 0;
    size_t prefixChecks = 0;
    size_t extensionPredicates = 0;
    size_t metadataPredicates = 0;
    size_t earlyRejects = 0;
    size_t predicateMatches = 0;
    double predicateDispatchMs = 0.0;
    double substringMatchingMs = 0.0;
    double globMatchingMs = 0.0;
    double extensionMatchingMs = 0.0;
    double metadataMatchingMs = 0.0;
    double folderMatchingMs = 0.0;
};

struct FilterDefinition {
    std::function<std::optional<FilterValue>(std::wstring_view)> parseValue;
    std::function<Predicate(const FilterValue&)> makePredicate;
    std::function<ViewPredicate(const FilterValue&)> makeViewPredicate = {};
};

class FilterRegistry {
public:
    void Register(std::wstring key, FilterDefinition definition);
    const FilterDefinition* Find(std::wstring_view key) const;
    static FilterRegistry WithDefaults();
private:
    std::unordered_map<std::wstring, FilterDefinition> definitions_;
};

struct Query {
    std::vector<Predicate> predicates;
    // The canonical matcher keeps folder-dependent predicates separate so a
    // caller with a lazy path source can reject name/metadata non-matches
    // before reconstructing a full parent path.
    std::vector<Predicate> predicatesWithoutFolder;
    std::vector<ViewPredicate> viewPredicates;
    std::vector<ViewPredicate> viewPredicatesWithoutFolder;
    std::vector<PredicateKind> viewPredicateKinds;
    std::vector<PredicateKind> viewPredicateWithoutFolderKinds;
    std::vector<std::wstring> unrecognizedKeys;
    std::vector<std::wstring> invalidFilters;
    std::wstring primaryTerm;
    std::wstring simpleLiteralTerm;
    std::u16string simpleLiteralFolded;
    char16_t simpleLiteralAnchor = 0;
    size_t simpleLiteralAnchorIndex = 0;
    std::array<size_t, 128> simpleLiteralShifts{};
    bool simpleLiteralUseBmh = false;
    bool requiresFolder = false;
    bool viewCompatible = true;
    bool simpleLiteralName = false;
    bool Matches(const Candidate& candidate) const;
    bool MatchesWithoutFolder(const Candidate& candidate) const;
    bool MatchesView(const CandidateView& candidate, MatchDiagnostics* diagnostics = nullptr) const;
    bool MatchesViewWithoutFolder(const CandidateView& candidate, MatchDiagnostics* diagnostics = nullptr) const;
};

Query ParseQuery(std::wstring_view text, const FilterRegistry& registry);
bool OrdinalContains(std::wstring_view haystack, std::wstring_view needle);
bool GlobMatches(std::wstring_view text, std::wstring_view pattern);

} // namespace ffsearch
