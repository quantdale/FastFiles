#pragma once

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
};

struct SizeFilter { enum class Op { Equal, Greater, GreaterEqual, Less, LessEqual, Range }; Op op; uint64_t first; uint64_t second = 0; };
struct DateFilter { enum class Op { EqualDay, GreaterThan, Range }; Op op; uint64_t first; uint64_t second = 0; };
using FilterValue = std::variant<std::wstring, SizeFilter, DateFilter>;
using Predicate = std::function<bool(const Candidate&)>;
using ViewPredicate = std::function<bool(const CandidateView&)>;

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
    std::vector<std::wstring> unrecognizedKeys;
    std::vector<std::wstring> invalidFilters;
    std::wstring primaryTerm;
    bool requiresFolder = false;
    bool viewCompatible = true;
    bool Matches(const Candidate& candidate) const;
    bool MatchesWithoutFolder(const Candidate& candidate) const;
    bool MatchesView(const CandidateView& candidate) const;
    bool MatchesViewWithoutFolder(const CandidateView& candidate) const;
};

Query ParseQuery(std::wstring_view text, const FilterRegistry& registry);
bool OrdinalContains(std::wstring_view haystack, std::wstring_view needle);
bool GlobMatches(std::wstring_view text, std::wstring_view pattern);

} // namespace ffsearch
