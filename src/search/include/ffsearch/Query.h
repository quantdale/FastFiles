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
    // indexed-search-allocated-size D1: on-disk allocation as a dimension
    // separate from logical `sizeBytes`, with explicit knownness. Zero is a
    // valid KNOWN value, never the absence sentinel, so the default is
    // unknown-and-zero and unknown is never synthesized from `sizeBytes`.
    // Appended after `persistentIndexed` on purpose: existing positional
    // aggregate initializers keep initializing exactly the members they
    // always did, and a new source of allocated truth is never introduced.
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
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
    // indexed-search-allocated-size D3: copied straight from the projection
    // entry while the existing traversal already holds the projection lock --
    // two scalars travelling with the lightweight record, no extra I/O and no
    // eager path reconstruction.
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
    // Appended last on purpose: existing positional aggregate initializers
    // keep initializing exactly the members they always did. True when every
    // name code unit is ASCII regardless of letter case. Matching an ASCII
    // haystack with a pure-ASCII needle is equivalence-preserving under
    // ordinal ignore-case comparison (exactly ASCII case folding -- proven
    // exhaustively over all 128x128 code-unit pairs in
    // ffsearch_ascii_fold_tests), which lets the matcher avoid one Win32
    // CompareStringOrdinal call per alignment for the dominant real-world
    // name class ("Documents", "README.md").
    bool nameIsAscii = false;
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
    AllocatedSize,
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
    static const FilterRegistry& WithDefaults();
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
// performance-closure-and-multisurface-acceleration 7.3: ordinal comparison
// between a NamePool UTF-16 view and a wide-character string (Windows wchar_t
// is UTF-16; the comparison is unit-wise and case-insensitive).
int CompareOrdinal16(std::u16string_view left, std::wstring_view right);
bool GlobMatches(std::wstring_view text, std::wstring_view pattern);

// A literal needle prepared once for repeated case-insensitive ordinal
// substring searches over wide strings. When `ascii` is true the needle is
// pure ASCII and `AsciiFoldContains` is semantically identical to
// `OrdinalContains` while avoiding per-alignment CompareStringOrdinal calls;
// callers must fall back to `OrdinalContains` when `ascii` is false.
struct AsciiFoldNeedle {
    bool ascii = false;
    std::wstring value;
    std::u16string folded;
    char16_t anchor = 0;
    size_t anchorIndex = 0;
    std::array<size_t, 128> shifts{};
};

AsciiFoldNeedle PrepareAsciiFoldNeedle(std::wstring_view value);
bool AsciiFoldContains(std::wstring_view haystack, const AsciiFoldNeedle& needle);
bool AsciiFoldEquals(std::wstring_view left, std::wstring_view right);
bool AsciiFoldStartsWith(std::wstring_view value, std::wstring_view prefix);

} // namespace ffsearch
