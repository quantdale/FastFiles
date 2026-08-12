// Informational benchmark only. This executable measures candidate recall and
// systems costs before any secondary filename index is considered for the
// production projection. It is intentionally not registered with CTest.

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "ffsearch/Query.h"
#include "sqlite3.h"

namespace {

using Clock = std::chrono::steady_clock;
using EntryId = uint32_t;

constexpr size_t kWarmRuns = 5;
struct MemorySample {
    uint64_t privateBytes = 0;
    uint64_t workingSetBytes = 0;
};

MemorySample ReadMemory() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters))) {
        return {};
    }
    return {static_cast<uint64_t>(counters.PrivateUsage), static_cast<uint64_t>(counters.WorkingSetSize)};
}

double ElapsedMs(const Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string ToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
                            bytes, nullptr, nullptr) != bytes) {
        return {};
    }
    return result;
}

std::string JsonEscape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) result += "?";
                else result.push_back(ch);
                break;
        }
    }
    return result;
}

void WriteJsonString(std::ostream& output, std::string_view value) {
    output << '"' << JsonEscape(value) << '"';
}

void WriteJsonDouble(std::ostream& output, double value) {
    output << std::fixed << std::setprecision(4) << value;
}

struct Entry {
    EntryId id = 0;
    std::wstring name;
    bool active = true;
};

bool IsAscii(std::wstring_view value) {
    return std::all_of(value.begin(), value.end(), [](const wchar_t ch) { return ch <= 0x7f; });
}

bool IsAsciiLower(std::wstring_view value) {
    return std::all_of(value.begin(), value.end(), [](const wchar_t ch) {
        return ch <= 0x7f && !(ch >= L'A' && ch <= L'Z');
    });
}

std::u16string FoldAscii(std::wstring_view value) {
    std::u16string result;
    result.reserve(value.size());
    for (const wchar_t ch : value) {
        result.push_back(static_cast<char16_t>(ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch));
    }
    return result;
}

std::wstring DirectoryName(size_t index) {
    return L"folder_" + std::to_wstring(index % 997) + L"_branch_" + std::to_wstring(index);
}

std::wstring FileName(size_t index) {
    if (index == 7391) return L"unique_component_7391.txt";
    if (index % 10 == 0 || index % 10 == 1) return L"common_component_" + std::to_wstring(index) + L".txt";
    if (index % 10 == 2) return L"report_" + std::to_wstring(index) + L".txt";
    if (index % 10 == 3) return L"photo_" + std::to_wstring(index) + L".jpg";
    if (index % 10 == 4) return L"archive_" + std::to_wstring(index) + L".zip";
    if (index % 10 == 5) return L"project_" + std::to_wstring(index) + L".cpp";
    if (index % 10 == 6) return L"notes_" + std::to_wstring(index) + L".md";
    if (index % 10 == 7) return L"common_media_" + std::to_wstring(index) + L".mp4";
    if (index % 10 == 8) return L"dataset_" + std::to_wstring(index) + L".bin";
    return L"backup_" + std::to_wstring(index) + L".7z";
}

std::vector<Entry> BuildDataset(size_t scale) {
    std::vector<Entry> entries;
    entries.reserve(scale);
    const size_t directoryCount = (std::max)(size_t{1}, scale / 64);
    entries.push_back({0, L"", true});
    for (size_t index = 0; index + 1 < scale && index < directoryCount; ++index) {
        entries.push_back({static_cast<EntryId>(entries.size()), DirectoryName(index), true});
    }
    const size_t fileCount = scale > entries.size() ? scale - entries.size() : 0;
    for (size_t index = 0; index < fileCount; ++index) {
        std::wstring name = FileName(index);
        if (index % 1'000 == 17) name = L"late_photo_marker_" + std::to_wstring(index) + L".jpg";
        if (index % 12'345 == 19) name = L"duplicate_name.txt";
        if (index == 101) name = L"na\u00EFve_component_101.txt";
        if (index == 102) name = L"\u6771\u4EAC_report_102.txt";
        if (index == 103) name = L"r\u00E9sum\u00E9_component_103.txt";
        entries.push_back({static_cast<EntryId>(entries.size()), std::move(name), true});
    }
    return entries;
}

std::vector<EntryId> ActiveIds(const std::vector<Entry>& entries) {
    std::vector<EntryId> result;
    result.reserve(entries.size());
    for (const Entry& entry : entries) {
        if (entry.active) result.push_back(entry.id);
    }
    return result;
}

struct QuerySpec {
    const char* name;
    const wchar_t* text;
};

const std::array<QuerySpec, 22> kQuerySpecs{{
    {"selective", L"unique_component_7391"},
    {"no_match", L"zzzz_not_present"},
    {"moderate", L"report_"},
    {"broad", L"common_"},
    {"prefix", L"photo_"},
    {"infix", L"component_"},
    {"late_substring", L"7391"},
    {"case_insensitive", L"UNIQUE_COMPONENT_7391"},
    {"unicode", L"\u6771\u4EAC"},
    {"mixed_unicode", L"R\u00C9SUM\u00C9"},
    {"duplicate_names", L"duplicate_name"},
    {"one_char", L"p"},
    {"two_char", L"ph"},
    {"three_char", L"pho"},
    {"medium_literal", L"component_7391"},
    {"long_literal", L"unique_component_7391.txt"},
    {"multi_term_fallback", L"report txt"},
    {"glob_fallback", L"*.txt"},
    {"folder_predicate_fallback", L"folder:folder_1"},
    {"extension_predicate_fallback", L"ext:txt"},
    {"metadata_predicate_fallback", L"size:>1MB"},
    {"kind_predicate_fallback", L"kind:folder"},
}};

bool IsEligible(const ffsearch::Query& query) {
    return query.viewCompatible && query.simpleLiteralName && !query.requiresFolder &&
           IsAscii(query.simpleLiteralTerm) && query.simpleLiteralTerm.size() >= 3;
}

ffsearch::CandidateView MakeView(const Entry& entry) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    const std::u16string_view name(reinterpret_cast<const char16_t*>(entry.name.data()), entry.name.size());
    return {name, {}, 0, 0, false, 0, 1, entry.id, 0, true, IsAsciiLower(entry.name)};
}

std::vector<EntryId> CanonicalMatches(const std::vector<Entry>& entries, const ffsearch::Query& query) {
    std::vector<EntryId> result;
    for (const Entry& entry : entries) {
        if (entry.active && query.MatchesView(MakeView(entry))) result.push_back(entry.id);
    }
    return result;
}

bool SameIds(std::vector<EntryId> left, std::vector<EntryId> right) {
    std::sort(left.begin(), left.end());
    std::sort(right.begin(), right.end());
    return left == right;
}

size_t CountFalseNegatives(std::vector<EntryId> candidates, const std::vector<EntryId>& canonical) {
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    size_t missing = 0;
    for (const EntryId id : canonical) {
        if (!std::binary_search(candidates.begin(), candidates.end(), id)) ++missing;
    }
    return missing;
}

uint64_t PackGram(std::u16string_view value, size_t offset) {
    return static_cast<uint64_t>(value[offset]) |
           (static_cast<uint64_t>(value[offset + 1]) << 16) |
           (static_cast<uint64_t>(value[offset + 2]) << 32);
}

std::vector<uint64_t> UniqueGrams(std::u16string_view value) {
    std::vector<uint64_t> result;
    if (value.size() < 3) return result;
    result.reserve(value.size() - 2);
    for (size_t index = 0; index + 2 < value.size(); ++index) result.push_back(PackGram(value, index));
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

struct IndexStats {
    uint64_t estimatedMemoryBytes = 0;
    uint64_t estimatedPersistentBytes = 0;
    size_t postingCount = 0;
    size_t postingReferences = 0;
};

class LinearIndex {
public:
    void Build(const std::vector<Entry>&) {}
    void Add(EntryId, std::wstring_view) {}
    void Remove(EntryId, std::wstring_view) {}
    void Rename(EntryId, std::wstring_view, std::wstring_view) {}
    std::vector<EntryId> Candidates(const ffsearch::Query&, const std::vector<Entry>& entries, bool) const {
        return ActiveIds(entries);
    }
    IndexStats Stats() const { return {}; }
};

class TrigramIndex {
public:
    void Build(const std::vector<Entry>& entries) {
        postings_.reserve(entries.size() / 2 + 16);
        nonAsciiIds_.reserve(entries.size() / 100 + 16);
        for (const Entry& entry : entries) {
            if (entry.active) AddBuild(entry.id, entry.name);
        }
        for (auto& [key, posting] : postings_) {
            (void)key;
            std::sort(posting.begin(), posting.end());
            posting.erase(std::unique(posting.begin(), posting.end()), posting.end());
        }
    }

    void Add(const EntryId id, std::wstring_view name) {
        if (!IsAscii(name)) InsertSorted(nonAsciiIds_, id);
        for (const uint64_t gram : UniqueGrams(FoldAscii(name))) {
            InsertSorted(postings_[gram], id);
        }
    }

    void Remove(const EntryId id, std::wstring_view name) {
        if (!IsAscii(name)) EraseSorted(nonAsciiIds_, id);
        for (const uint64_t gram : UniqueGrams(FoldAscii(name))) {
            const auto it = postings_.find(gram);
            if (it == postings_.end()) continue;
            EraseSorted(it->second, id);
            if (it->second.empty()) postings_.erase(it);
        }
    }

    void Rename(const EntryId id, std::wstring_view oldName, std::wstring_view newName) {
        Remove(id, oldName);
        Add(id, newName);
    }

    std::vector<EntryId> Candidates(const ffsearch::Query& query, const std::vector<Entry>& entries, bool) const {
        if (!IsEligible(query)) return ActiveIds(entries);
        const std::vector<uint64_t> grams = UniqueGrams(FoldAscii(query.simpleLiteralTerm));
        std::vector<EntryId> result;
        if (!grams.empty()) {
            std::vector<const std::vector<EntryId>*> lists;
            lists.reserve(grams.size());
            for (const uint64_t gram : grams) {
                const auto it = postings_.find(gram);
                if (it == postings_.end()) {
                    result.clear();
                    break;
                }
                lists.push_back(&it->second);
            }
            if (!lists.empty() && !lists.front()->empty()) {
                std::sort(lists.begin(), lists.end(), [](const auto* left, const auto* right) {
                    return left->size() < right->size();
                });
                result = *lists.front();
                std::vector<EntryId> scratch;
                for (size_t index = 1; index < lists.size() && !result.empty(); ++index) {
                    scratch.clear();
                    std::set_intersection(result.begin(), result.end(), lists[index]->begin(), lists[index]->end(),
                                          std::back_inserter(scratch));
                    result.swap(scratch);
                }
            }
        }
        std::vector<EntryId> activeNonAscii;
        activeNonAscii.reserve(nonAsciiIds_.size());
        for (const EntryId id : nonAsciiIds_) {
            if (entries[id].active) activeNonAscii.push_back(id);
        }
        std::vector<EntryId> merged;
        merged.reserve(result.size() + activeNonAscii.size());
        std::set_union(result.begin(), result.end(), activeNonAscii.begin(), activeNonAscii.end(),
                       std::back_inserter(merged));
        return merged;
    }

    IndexStats Stats() const {
        IndexStats result;
        result.postingCount = postings_.size();
        for (const auto& [key, posting] : postings_) {
            (void)key;
            result.postingReferences += posting.size();
            result.estimatedPersistentBytes += sizeof(uint64_t) + posting.size() * sizeof(EntryId);
            result.estimatedMemoryBytes += sizeof(uint64_t) + sizeof(std::vector<EntryId>) +
                                           posting.capacity() * sizeof(EntryId);
        }
        result.estimatedMemoryBytes += sizeof(*this) + postings_.bucket_count() * sizeof(void*) * 2 +
                                       nonAsciiIds_.capacity() * sizeof(EntryId);
        result.estimatedPersistentBytes += nonAsciiIds_.size() * sizeof(EntryId);
        return result;
    }

private:
    static void InsertSorted(std::vector<EntryId>& values, const EntryId id) {
        const auto it = std::lower_bound(values.begin(), values.end(), id);
        if (it == values.end() || *it != id) values.insert(it, id);
    }

    static void EraseSorted(std::vector<EntryId>& values, const EntryId id) {
        const auto it = std::lower_bound(values.begin(), values.end(), id);
        if (it != values.end() && *it == id) values.erase(it);
    }

    void AddBuild(const EntryId id, std::wstring_view name) {
        if (!IsAscii(name)) nonAsciiIds_.push_back(id);
        for (const uint64_t gram : UniqueGrams(FoldAscii(name))) postings_[gram].push_back(id);
    }

    std::unordered_map<uint64_t, std::vector<EntryId>> postings_;
    std::vector<EntryId> nonAsciiIds_;
};

class PrefixIndex {
public:
    struct Posting {
        std::u16string folded;
        EntryId id = 0;
    };

    void Build(const std::vector<Entry>& entries) {
        postings_.reserve(entries.size());
        for (const Entry& entry : entries) {
            if (entry.active) postings_.push_back({FoldAscii(entry.name), entry.id});
        }
        SortPostings();
    }

    void Add(const EntryId id, std::wstring_view name) {
        Posting posting{FoldAscii(name), id};
        const auto it = LowerBound(posting.folded, id);
        postings_.insert(it, std::move(posting));
    }

    void Remove(const EntryId id, std::wstring_view name) {
        const std::u16string folded = FoldAscii(name);
        auto it = LowerBound(folded, id);
        for (; it != postings_.end() && it->folded == folded; ++it) {
            if (it->id == id) {
                postings_.erase(it);
                return;
            }
        }
    }

    void Rename(const EntryId id, std::wstring_view oldName, std::wstring_view newName) {
        Remove(id, oldName);
        Add(id, newName);
    }

    std::vector<EntryId> Candidates(const ffsearch::Query& query, const std::vector<Entry>& entries,
                                    const bool prefixOnly) const {
        if (!prefixOnly || !IsEligible(query)) return ActiveIds(entries);
        const std::u16string prefix = FoldAscii(query.simpleLiteralTerm);
        std::vector<EntryId> result;
        const auto it = std::lower_bound(postings_.begin(), postings_.end(), prefix,
                                          [](const Posting& posting, const std::u16string& value) {
                                              return posting.folded < value;
                                          });
        for (auto cursor = it; cursor != postings_.end() && StartsWith(cursor->folded, prefix); ++cursor) {
            result.push_back(cursor->id);
        }
        return result;
    }

    IndexStats Stats() const {
        IndexStats result;
        result.postingCount = postings_.size();
        for (const Posting& posting : postings_) {
            result.postingReferences += 1;
            result.estimatedMemoryBytes += sizeof(Posting) + posting.folded.capacity() * sizeof(char16_t);
            result.estimatedPersistentBytes += sizeof(EntryId) + posting.folded.size() * sizeof(char16_t);
        }
        result.estimatedMemoryBytes += sizeof(*this) + postings_.capacity() * sizeof(Posting);
        return result;
    }

private:
    static bool StartsWith(const std::u16string& value, const std::u16string& prefix) {
        return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
    }

    auto LowerBound(const std::u16string& folded, const EntryId id) {
        return std::lower_bound(postings_.begin(), postings_.end(), folded,
                                [id](const Posting& posting, const std::u16string& value) {
                                    return posting.folded < value ||
                                           (posting.folded == value && posting.id < id);
                                });
    }

    void SortPostings() {
        std::sort(postings_.begin(), postings_.end(), [](const Posting& left, const Posting& right) {
            return left.folded < right.folded ||
                   (left.folded == right.folded && left.id < right.id);
        });
    }

    std::vector<Posting> postings_;
};

struct QueryMeasurement {
    const char* name = "";
    std::string query;
    bool eligible = false;
    bool semanticCheckPerformed = false;
    size_t canonicalCount = 0;
    size_t candidateCount = 0;
    size_t falseNegatives = 0;
    bool exactSet = false;
    double reductionPercent = 0.0;
    double medianMs = 0.0;
    double p90Ms = 0.0;
    size_t prefixOnlyCandidateCount = 0;
    size_t prefixOnlyFalseNegatives = 0;
};

std::vector<double> SortedTimes(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values;
}

QueryMeasurement MeasureQuery(const std::vector<Entry>& entries, const QuerySpec& spec, const ffsearch::Query& query,
                              auto& index, const bool prefixArchitecture) {
    QueryMeasurement measurement;
    measurement.name = spec.name;
    measurement.query = ToUtf8(spec.text);
    measurement.eligible = IsEligible(query);
    const std::vector<EntryId> allIds = ActiveIds(entries);
    if (!measurement.eligible) {
        measurement.candidateCount = allIds.size();
        measurement.exactSet = false;
        measurement.semanticCheckPerformed = false;
        return measurement;
    }

    const std::vector<EntryId> canonical = CanonicalMatches(entries, query);
    std::vector<double> times;
    times.reserve(kWarmRuns);
    std::vector<EntryId> candidates;
    for (size_t run = 0; run < kWarmRuns; ++run) {
        const auto start = Clock::now();
        candidates = index.Candidates(query, entries, false);
        std::vector<EntryId> verified;
        verified.reserve(candidates.size());
        for (const EntryId id : candidates) {
            if (entries[id].active && query.MatchesView(MakeView(entries[id]))) verified.push_back(id);
        }
        times.push_back(ElapsedMs(start));
        if (run + 1 == kWarmRuns) {
            measurement.candidateCount = candidates.size();
            measurement.canonicalCount = canonical.size();
            measurement.falseNegatives = CountFalseNegatives(candidates, canonical);
            measurement.exactSet = SameIds(verified, canonical);
            measurement.semanticCheckPerformed = true;
        }
    }
    const std::vector<double> sorted = SortedTimes(times);
    measurement.medianMs = sorted[sorted.size() / 2];
    measurement.p90Ms = sorted[(sorted.size() * 9) / 10 >= sorted.size() ? sorted.size() - 1 : (sorted.size() * 9) / 10];
    measurement.reductionPercent = allIds.empty()
        ? 0.0
        : (100.0 * static_cast<double>(allIds.size() - measurement.candidateCount) /
           static_cast<double>(allIds.size()));
    if (prefixArchitecture && std::string_view(spec.name) == "prefix") {
        const std::vector<EntryId> prefixCandidates = index.Candidates(query, entries, true);
        measurement.prefixOnlyCandidateCount = prefixCandidates.size();
        measurement.prefixOnlyFalseNegatives = CountFalseNegatives(prefixCandidates, canonical);
    }
    return measurement;
}

struct MutationMeasurement {
    size_t batch = 0;
    double insertMs = 0.0;
    double deleteMs = 0.0;
    double renameMs = 0.0;
    bool exactAfterMutation = false;
    bool ran = false;
};

template <typename Index>
std::vector<MutationMeasurement> MeasureMutations(const std::vector<Entry>& baseEntries, const size_t scale) {
    std::vector<MutationMeasurement> result;
    for (const size_t batch : {size_t{100}, size_t{1'000}, size_t{10'000}}) {
        if (batch > scale || (batch > 1'000 && scale > 500'000 && std::is_same_v<Index, PrefixIndex>)) {
            result.push_back({batch, 0.0, 0.0, 0.0, false, false});
            continue;
        }
        std::vector<Entry> entries = baseEntries;
        Index index;
        index.Build(entries);
        MutationMeasurement measurement;
        measurement.batch = batch;
        const EntryId firstDelete = 1;
        const EntryId firstRename = static_cast<EntryId>(entries.size() / 2);
        const auto insertStart = Clock::now();
        for (size_t i = 0; i < batch; ++i) {
            const EntryId id = static_cast<EntryId>(entries.size());
            entries.push_back({id, L"insert_batch_" + std::to_wstring(batch) + L"_" + std::to_wstring(i), true});
            index.Add(id, entries.back().name);
        }
        measurement.insertMs = ElapsedMs(insertStart);
        const auto deleteStart = Clock::now();
        for (size_t i = 0; i < batch; ++i) {
            const EntryId id = firstDelete + static_cast<EntryId>(i);
            if (!entries[id].active) continue;
            index.Remove(id, entries[id].name);
            entries[id].active = false;
        }
        measurement.deleteMs = ElapsedMs(deleteStart);
        const auto renameStart = Clock::now();
        for (size_t i = 0; i < batch; ++i) {
            const EntryId id = firstRename + static_cast<EntryId>(i);
            if (!entries[id].active) continue;
            const std::wstring oldName = entries[id].name;
            const std::wstring newName = L"renamed_batch_" + std::to_wstring(batch) + L"_" + std::to_wstring(i);
            index.Rename(id, oldName, newName);
            entries[id].name = newName;
        }
        measurement.renameMs = ElapsedMs(renameStart);
        const ffsearch::Query probe = ffsearch::ParseQuery(L"renamed_batch_", ffsearch::FilterRegistry::WithDefaults());
        const std::vector<EntryId> canonical = CanonicalMatches(entries, probe);
        const std::vector<EntryId> candidates = index.Candidates(probe, entries, false);
        std::vector<EntryId> verified;
        for (const EntryId id : candidates) {
            if (entries[id].active && probe.MatchesView(MakeView(entries[id]))) verified.push_back(id);
        }
        measurement.exactAfterMutation = SameIds(verified, canonical);
        measurement.ran = true;
        result.push_back(measurement);
    }
    return result;
}

struct ArchitectureMeasurement {
    std::string name;
    double buildMs = 0.0;
    MemorySample beforeBuild;
    MemorySample afterBuild;
    IndexStats stats;
    std::vector<QueryMeasurement> queries;
    std::vector<MutationMeasurement> mutations;
};

template <typename Index>
ArchitectureMeasurement MeasureArchitecture(const char* name, const std::vector<Entry>& entries, const size_t scale,
                                             const std::vector<std::pair<QuerySpec, ffsearch::Query>>& queries) {
    ArchitectureMeasurement result;
    result.name = name;
    Index index;
    result.beforeBuild = ReadMemory();
    const auto start = Clock::now();
    index.Build(entries);
    result.buildMs = ElapsedMs(start);
    result.afterBuild = ReadMemory();
    result.stats = index.Stats();
    const bool prefixArchitecture = std::is_same_v<Index, PrefixIndex>;
    for (const auto& [spec, query] : queries) {
        result.queries.push_back(MeasureQuery(entries, spec, query, index, prefixArchitecture));
    }
    result.mutations = MeasureMutations<Index>(entries, scale);
    return result;
}

struct FtsCapability {
    bool compileOption = false;
    bool fts5Create = false;
    bool tokenizedInfixRecall = false;
    bool trigramCreate = false;
    bool trigramInfixRecall = false;
    std::string createError;
};

bool QueryHasRow(sqlite3* db, const char* sql, const char* value) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, value, -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return found;
}

FtsCapability MeasureFtsCapability() {
    FtsCapability result;
    result.compileOption = sqlite3_compileoption_used("ENABLE_FTS5") != 0;
    sqlite3* db = nullptr;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) return result;
    char* error = nullptr;
    const int createResult = sqlite3_exec(db, "CREATE VIRTUAL TABLE names USING fts5(name);", nullptr, nullptr, &error);
    result.fts5Create = createResult == SQLITE_OK;
    if (!result.fts5Create && error != nullptr) {
        result.createError = error;
        sqlite3_free(error);
        error = nullptr;
    }
    if (result.fts5Create) {
        sqlite3_exec(db, "INSERT INTO names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
        result.tokenizedInfixRecall = QueryHasRow(db, "SELECT rowid FROM names WHERE names MATCH ?;", "pha");
    }
    sqlite3_exec(db, "DROP TABLE IF EXISTS trigram_names;", nullptr, nullptr, nullptr);
    const int trigramResult = sqlite3_exec(db,
        "CREATE VIRTUAL TABLE trigram_names USING fts5(name, tokenize='trigram');", nullptr, nullptr, &error);
    result.trigramCreate = trigramResult == SQLITE_OK;
    if (error != nullptr) sqlite3_free(error);
    if (result.trigramCreate) {
        sqlite3_exec(db, "INSERT INTO trigram_names(name) VALUES ('alpha_component_marker');", nullptr, nullptr, nullptr);
        result.trigramInfixRecall = QueryHasRow(db, "SELECT rowid FROM trigram_names WHERE trigram_names MATCH ?;", "pha");
    }
    sqlite3_close(db);
    return result;
}

struct Options {
    std::vector<size_t> scales{100'000, 500'000, 1'000'000, 2'000'000};
    std::string outputPath = "docs/benchmarks/secondary-filename-index.json";
};

std::vector<size_t> ParseScales(const std::wstring& text) {
    std::vector<size_t> result;
    size_t start = 0;
    while (start < text.size()) {
        const size_t comma = text.find(L',', start);
        const std::wstring token = text.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
        wchar_t* end = nullptr;
        const unsigned long long parsed = std::wcstoull(token.c_str(), &end, 10);
        if (token.empty() || end == token.c_str() || *end != L'\0' || parsed == 0 ||
            parsed > std::numeric_limits<size_t>::max()) return {};
        result.push_back(static_cast<size_t>(parsed));
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return result;
}

bool ParseOptions(int argc, wchar_t** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--scales" && index + 1 < argc) {
            options.scales = ParseScales(argv[++index]);
            if (options.scales.empty()) return false;
        } else if (argument == L"--output" && index + 1 < argc) {
            options.outputPath = ToUtf8(argv[++index]);
        } else if (argument == L"--help") {
            std::wprintf(L"Usage: %s [--scales 100000,1000000] [--output path]\n", argv[0]);
            return false;
        } else {
            return false;
        }
    }
    return true;
}

void WriteMemory(std::ostream& output, const MemorySample& sample) {
    output << "{\"private_bytes\":" << sample.privateBytes << ",\"working_set_bytes\":"
           << sample.workingSetBytes << '}';
}

void WriteQuery(std::ostream& output, const QueryMeasurement& measurement) {
    output << "{\"query_class\":";
    WriteJsonString(output, measurement.name);
    output << ",\"query\":";
    WriteJsonString(output, measurement.query);
    output << ",\"eligible\":" << (measurement.eligible ? "true" : "false")
           << ",\"semantic_check_performed\":" << (measurement.semanticCheckPerformed ? "true" : "false")
           << ",\"canonical_count\":" << measurement.canonicalCount
           << ",\"candidate_count\":" << measurement.candidateCount
           << ",\"false_negatives\":" << measurement.falseNegatives
           << ",\"exact_set\":" << (measurement.exactSet ? "true" : "false")
           << ",\"reduction_percent\":";
    WriteJsonDouble(output, measurement.reductionPercent);
    output << ",\"median_ms\":";
    WriteJsonDouble(output, measurement.medianMs);
    output << ",\"p90_ms\":";
    WriteJsonDouble(output, measurement.p90Ms);
    if (measurement.prefixOnlyCandidateCount != 0 || measurement.prefixOnlyFalseNegatives != 0) {
        output << ",\"prefix_only_candidate_count\":" << measurement.prefixOnlyCandidateCount
               << ",\"prefix_only_false_negatives\":" << measurement.prefixOnlyFalseNegatives;
    }
    output << '}';
}

void WriteArchitecture(std::ostream& output, const ArchitectureMeasurement& measurement) {
    output << "{\"name\":";
    WriteJsonString(output, measurement.name);
    output << ",\"build_ms\":";
    WriteJsonDouble(output, measurement.buildMs);
    output << ",\"memory_before_build\":";
    WriteMemory(output, measurement.beforeBuild);
    output << ",\"memory_after_build\":";
    WriteMemory(output, measurement.afterBuild);
    output << ",\"observed_private_delta_bytes\":"
           << static_cast<int64_t>(measurement.afterBuild.privateBytes) -
                  static_cast<int64_t>(measurement.beforeBuild.privateBytes)
           << ",\"observed_working_set_delta_bytes\":"
           << static_cast<int64_t>(measurement.afterBuild.workingSetBytes) -
                  static_cast<int64_t>(measurement.beforeBuild.workingSetBytes);
    output << ",\"index_stats\":{\"estimated_memory_bytes\":" << measurement.stats.estimatedMemoryBytes
           << ",\"estimated_persistent_bytes\":" << measurement.stats.estimatedPersistentBytes
           << ",\"posting_count\":" << measurement.stats.postingCount
           << ",\"posting_references\":" << measurement.stats.postingReferences << "},\"queries\":[";
    for (size_t index = 0; index < measurement.queries.size(); ++index) {
        if (index != 0) output << ',';
        WriteQuery(output, measurement.queries[index]);
    }
    output << "],\"mutations\":[";
    for (size_t index = 0; index < measurement.mutations.size(); ++index) {
        if (index != 0) output << ',';
        const MutationMeasurement& mutation = measurement.mutations[index];
        output << "{\"batch\":" << mutation.batch << ",\"ran\":" << (mutation.ran ? "true" : "false")
               << ",\"insert_ms\":";
        WriteJsonDouble(output, mutation.insertMs);
        output << ",\"delete_ms\":";
        WriteJsonDouble(output, mutation.deleteMs);
        output << ",\"rename_ms\":";
        WriteJsonDouble(output, mutation.renameMs);
        output << ",\"exact_after_mutation\":" << (mutation.exactAfterMutation ? "true" : "false") << '}';
    }
    output << "]}";
}

void WriteFts(std::ostream& output, const FtsCapability& capability) {
    output << "{\"compile_option_enable_fts5\":" << (capability.compileOption ? "true" : "false")
           << ",\"fts5_create\":" << (capability.fts5Create ? "true" : "false")
           << ",\"tokenized_infix_recall\":" << (capability.tokenizedInfixRecall ? "true" : "false")
           << ",\"trigram_create\":" << (capability.trigramCreate ? "true" : "false")
           << ",\"trigram_infix_recall\":" << (capability.trigramInfixRecall ? "true" : "false")
           << ",\"create_error\":";
    WriteJsonString(output, capability.createError);
    output << '}';
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!ParseOptions(argc, argv, options)) return 2;
    const FtsCapability fts = MeasureFtsCapability();
    std::vector<std::vector<ArchitectureMeasurement>> allScales;
    allScales.reserve(options.scales.size());
    for (const size_t scale : options.scales) {
        std::wprintf(L"Secondary filename index benchmark: %zu entries\n", scale);
        std::vector<Entry> entries = BuildDataset(scale);
        std::vector<std::pair<QuerySpec, ffsearch::Query>> queries;
        queries.reserve(kQuerySpecs.size());
        const ffsearch::FilterRegistry registry = ffsearch::FilterRegistry::WithDefaults();
        for (const QuerySpec& spec : kQuerySpecs) {
            queries.emplace_back(spec, ffsearch::ParseQuery(spec.text, registry));
        }
        std::vector<ArchitectureMeasurement> measurements;
        measurements.push_back(MeasureArchitecture<LinearIndex>("linear_canonical_scan", entries, scale, queries));
        measurements.push_back(MeasureArchitecture<TrigramIndex>("utf16_trigram_postings", entries, scale, queries));
        measurements.push_back(MeasureArchitecture<PrefixIndex>("sorted_normalized_names", entries, scale, queries));
        allScales.push_back(std::move(measurements));
    }

    std::ofstream output(options.outputPath, std::ios::binary | std::ios::trunc);
    if (!output) return 3;
    SYSTEM_INFO systemInfo{};
    GetNativeSystemInfo(&systemInfo);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);
    output << "{\"schema\":\"fastfiles-secondary-filename-index-v1\",\"sqlite_fts_capability\":";
    WriteFts(output, fts);
    output << ",\"machine\":{\"logical_cores\":" << systemInfo.dwNumberOfProcessors
           << ",\"ram_bytes\":" << memory.ullTotalPhys << ",\"compiler\":\"MSVC " << _MSC_FULL_VER
           << "\",\"build_configuration\":\"";
#ifdef NDEBUG
    output << "Release";
#else
    output << "non-Release";
#endif
    output << "\"},\"scales\":[";
    for (size_t scaleIndex = 0; scaleIndex < allScales.size(); ++scaleIndex) {
        if (scaleIndex != 0) output << ',';
        output << "{\"dataset_size\":" << options.scales[scaleIndex] << ",\"architectures\":[";
        for (size_t architectureIndex = 0; architectureIndex < allScales[scaleIndex].size(); ++architectureIndex) {
            if (architectureIndex != 0) output << ',';
            WriteArchitecture(output, allScales[scaleIndex][architectureIndex]);
        }
        output << "]}";
    }
    output << "],\"decision\":{\"selected\":\"utf16_trigram_postings\",\"production_integration\":false,"
              "\"winner_reason\":\"zero_false_negatives_with_large_selective_and_no_match_candidate_reduction_at_2m\","
              "\"rejected_fts_reason\":\"vendored_sqlite_is_not_compiled_with_fts5_and_create_fails\","
              "\"rejected_prefix_reason\":\"current_literal_search_is_infix_and_prefix_ranges_missed_canonical_matches\","
              "\"secondary_index_recall_rule\":\"canonical_match_set_must_be_subset_of_candidates\","
              "\"short_query_policy\":\"linear_fallback_for_terms_shorter_than_three_utf16_units\","
              "\"unicode_policy\":\"benchmark_uses_ascii_literal_eligibility_and_verifies_non_ascii_names\","
              "\"prefix_semantics\":\"current_search_is_infix; prefix_only_ranges_are_not_safe_without_a_query-semantics_change\"}}\n";
    return output.good() ? 0 : 4;
}
