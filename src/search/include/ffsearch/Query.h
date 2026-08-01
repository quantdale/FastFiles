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
};

struct SizeFilter { enum class Op { Equal, Greater, GreaterEqual, Less, LessEqual, Range }; Op op; uint64_t first; uint64_t second = 0; };
struct DateFilter { enum class Op { EqualDay, GreaterThan, Range }; Op op; uint64_t first; uint64_t second = 0; };
using FilterValue = std::variant<std::wstring, SizeFilter, DateFilter>;
using Predicate = std::function<bool(const Candidate&)>;

struct FilterDefinition {
    std::function<std::optional<FilterValue>(std::wstring_view)> parseValue;
    std::function<Predicate(const FilterValue&)> makePredicate;
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
    std::vector<std::wstring> unrecognizedKeys;
    std::vector<std::wstring> invalidFilters;
    std::wstring primaryTerm;
    bool Matches(const Candidate& candidate) const;
};

Query ParseQuery(std::wstring_view text, const FilterRegistry& registry);
bool OrdinalContains(std::wstring_view haystack, std::wstring_view needle);
bool GlobMatches(std::wstring_view text, std::wstring_view pattern);

} // namespace ffsearch
