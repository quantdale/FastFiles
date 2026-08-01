#include "ffsearch/Query.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <windows.h>

namespace ffsearch {
namespace {

std::wstring Lower(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return result;
}

std::vector<std::wstring> Tokenize(std::wstring_view text) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    bool quoted = false;
    for (wchar_t c : text) {
        if (c == L'"') { quoted = !quoted; continue; }
        if (!quoted && iswspace(c)) { if (!current.empty()) { tokens.push_back(std::move(current)); current.clear(); } continue; }
        current += c;
    }
    if (!current.empty()) { tokens.push_back(std::move(current)); }
    return tokens;
}

std::optional<uint64_t> ParseSize(std::wstring_view text) {
    size_t digits = 0;
    while (digits < text.size() && iswdigit(text[digits])) ++digits;
    if (digits == 0) return std::nullopt;
    uint64_t value = 0;
    for (size_t i = 0; i < digits; ++i) {
        const uint64_t digit = text[i] - L'0';
        if (value > (UINT64_MAX - digit) / 10) return std::nullopt;
        value = value * 10 + digit;
    }
    const std::wstring unit = Lower(text.substr(digits));
    uint64_t multiplier = 1;
    if (unit == L"kb") multiplier = 1024;
    else if (unit == L"mb") multiplier = 1024 * 1024;
    else if (unit == L"gb") multiplier = 1024ULL * 1024 * 1024;
    else if (!unit.empty() && unit != L"b") return std::nullopt;
    if (value > UINT64_MAX / multiplier) return std::nullopt;
    return value * multiplier;
}

std::optional<FilterValue> ParseSizeFilter(std::wstring_view value) {
    const size_t range = value.find(L"..");
    if (range != std::wstring_view::npos) { auto a = ParseSize(value.substr(0, range)); auto b = ParseSize(value.substr(range + 2)); if (!a || !b || *a > *b) return std::nullopt; return SizeFilter{SizeFilter::Op::Range, *a, *b}; }
    SizeFilter::Op op = SizeFilter::Op::Equal; size_t offset = 0;
    if (value.starts_with(L">=")) { op = SizeFilter::Op::GreaterEqual; offset = 2; }
    else if (value.starts_with(L"<=")) { op = SizeFilter::Op::LessEqual; offset = 2; }
    else if (value.starts_with(L">")) { op = SizeFilter::Op::Greater; offset = 1; }
    else if (value.starts_with(L"<")) { op = SizeFilter::Op::Less; offset = 1; }
    else if (value.starts_with(L"=")) offset = 1;
    auto size = ParseSize(value.substr(offset)); if (!size) return std::nullopt; return SizeFilter{op, *size};
}

Predicate MakeSizePredicate(const FilterValue& value) {
    const auto filter = std::get<SizeFilter>(value);
    return [filter](const Candidate& c) { switch (filter.op) { case SizeFilter::Op::Equal: return c.sizeBytes == filter.first; case SizeFilter::Op::Greater: return c.sizeBytes > filter.first; case SizeFilter::Op::GreaterEqual: return c.sizeBytes >= filter.first; case SizeFilter::Op::Less: return c.sizeBytes < filter.first; case SizeFilter::Op::LessEqual: return c.sizeBytes <= filter.first; case SizeFilter::Op::Range: return c.sizeBytes >= filter.first && c.sizeBytes <= filter.second; } return false; };
}

uint64_t CurrentFileTime() { FILETIME ft{}; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER value{}; value.LowPart = ft.dwLowDateTime; value.HighPart = ft.dwHighDateTime; return value.QuadPart; }
std::optional<uint64_t> ParseDay(std::wstring_view value) {
    if (value.size() != 10 || value[4] != L'-' || value[7] != L'-') return std::nullopt;
    for (size_t index = 0; index < value.size(); ++index) {
        if (index != 4 && index != 7 && !iswdigit(value[index])) return std::nullopt;
    }
    SYSTEMTIME time{};
    time.wYear = static_cast<WORD>(_wtoi(std::wstring(value.substr(0, 4)).c_str()));
    time.wMonth = static_cast<WORD>(_wtoi(std::wstring(value.substr(5, 2)).c_str()));
    time.wDay = static_cast<WORD>(_wtoi(std::wstring(value.substr(8, 2)).c_str()));
    FILETIME fileTime{};
    if (!SystemTimeToFileTime(&time, &fileTime)) return std::nullopt;
    ULARGE_INTEGER result{};
    result.LowPart = fileTime.dwLowDateTime;
    result.HighPart = fileTime.dwHighDateTime;
    return result.QuadPart;
}

std::optional<FilterValue> ParseDateFilter(std::wstring_view value) {
    constexpr uint64_t kDay = 24ULL * 60 * 60 * 10'000'000;
    if (Lower(value) == L"today") {
        const auto now = CurrentFileTime();
        const auto day = now - now % kDay;
        return DateFilter{DateFilter::Op::Range, day, day + kDay - 1};
    }
    const size_t range = value.find(L"..");
    if (range != std::wstring_view::npos) {
        const auto first = ParseDay(value.substr(0, range));
        const auto second = ParseDay(value.substr(range + 2));
        if (!first || !second || *first > *second) return std::nullopt;
        return DateFilter{DateFilter::Op::Range, *first, *second + kDay - 1};
    }
    if (value.starts_with(L">") && value.ends_with(L"d")) {
        const auto digits = value.substr(1, value.size() - 2);
        if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](wchar_t ch) { return iswdigit(ch) != 0; })) {
            return std::nullopt;
        }
        const unsigned long days = std::wcstoul(std::wstring(digits).c_str(), nullptr, 10);
        const auto now = CurrentFileTime();
        if (days > now / kDay) return std::nullopt;
        return DateFilter{DateFilter::Op::GreaterThan, now - static_cast<uint64_t>(days) * kDay};
    }
    const auto day = ParseDay(value);
    if (!day) return std::nullopt;
    return DateFilter{DateFilter::Op::EqualDay, *day, *day + kDay - 1};
}
Predicate MakeDatePredicate(const FilterValue& value) { const auto filter = std::get<DateFilter>(value); return [filter](const Candidate& c) { return filter.op == DateFilter::Op::GreaterThan ? c.modifiedTime > filter.first : c.modifiedTime >= filter.first && c.modifiedTime <= filter.second; }; }

} // namespace

void FilterRegistry::Register(std::wstring key, FilterDefinition definition) { definitions_[Lower(key)] = std::move(definition); }
const FilterDefinition* FilterRegistry::Find(std::wstring_view key) const { const auto it = definitions_.find(Lower(key)); return it == definitions_.end() ? nullptr : &it->second; }

FilterRegistry FilterRegistry::WithDefaults() {
    FilterRegistry registry;
    registry.Register(L"ext", {[](std::wstring_view v) -> std::optional<FilterValue> { std::wstring x(v); if (!x.empty() && x.front() == L'.') x.erase(0, 1); return x.empty() ? std::nullopt : std::optional<FilterValue>(Lower(x)); }, [](const FilterValue& v) { const auto extension = std::get<std::wstring>(v); return [extension](const Candidate& c) { const size_t dot = c.name.find_last_of(L'.'); return dot != std::wstring::npos && Lower(std::wstring_view(c.name).substr(dot + 1)) == extension; }; }});
    registry.Register(L"name", {[](std::wstring_view value) -> std::optional<FilterValue> {
        return value.empty() ? std::nullopt : std::optional<FilterValue>(std::wstring(value));
    }, [](const FilterValue& value) {
        const auto text = std::get<std::wstring>(value);
        return [text](const Candidate& candidate) { return OrdinalContains(candidate.name, text); };
    }});
    registry.Register(L"folder", {[](std::wstring_view value) -> std::optional<FilterValue> {
        return value.empty() ? std::nullopt : std::optional<FilterValue>(std::wstring(value));
    }, [](const FilterValue& value) {
        const auto text = std::get<std::wstring>(value);
        return [text](const Candidate& candidate) { return OrdinalContains(candidate.folder, text); };
    }});
    registry.Register(L"size", {ParseSizeFilter, MakeSizePredicate});
    registry.Register(L"modified", {ParseDateFilter, MakeDatePredicate});
    registry.Register(L"kind", {[](std::wstring_view v) -> std::optional<FilterValue> { const auto x = Lower(v); return (x == L"document" || x == L"image" || x == L"video" || x == L"audio" || x == L"archive" || x == L"executable" || x == L"folder") ? std::optional<FilterValue>(x) : std::nullopt; }, [](const FilterValue& v) { const auto k = std::get<std::wstring>(v); return [k](const Candidate& c) { if (k == L"folder") return c.isDirectory; const size_t dot = c.name.find_last_of(L'.'); const std::wstring ext = dot == std::wstring::npos ? L"" : Lower(std::wstring_view(c.name).substr(dot + 1)); if (k == L"image") return ext == L"png" || ext == L"jpg" || ext == L"jpeg" || ext == L"gif"; if (k == L"document") return ext == L"pdf" || ext == L"doc" || ext == L"docx" || ext == L"txt"; if (k == L"video") return ext == L"mp4" || ext == L"mkv"; if (k == L"audio") return ext == L"mp3" || ext == L"wav"; if (k == L"archive") return ext == L"zip" || ext == L"7z"; return ext == L"exe" || ext == L"bat" || ext == L"cmd"; }; }});
    return registry;
}

bool Query::Matches(const Candidate& candidate) const { return std::all_of(predicates.begin(), predicates.end(), [&](const auto& p) { return p(candidate); }); }
bool OrdinalContains(std::wstring_view h, std::wstring_view n) { if (n.empty()) return true; for (size_t i = 0; i + n.size() <= h.size(); ++i) if (CompareStringOrdinal(h.data() + i, static_cast<int>(n.size()), n.data(), static_cast<int>(n.size()), TRUE) == CSTR_EQUAL) return true; return false; }
bool GlobMatches(std::wstring_view text, std::wstring_view pattern) {
    const auto width = [](std::wstring_view value, size_t offset) -> size_t {
        if (offset + 1 < value.size() && IS_HIGH_SURROGATE(value[offset]) && IS_LOW_SURROGATE(value[offset + 1])) return 2;
        return 1;
    };
    size_t textIndex = 0, patternIndex = 0, star = std::wstring_view::npos, saved = 0;
    while (textIndex < text.size()) {
        if (patternIndex < pattern.size() && pattern[patternIndex] == L'?') {
            textIndex += width(text, textIndex);
            ++patternIndex;
        } else if (patternIndex < pattern.size() && pattern[patternIndex] != L'*') {
            const size_t textWidth = width(text, textIndex);
            const size_t patternWidth = width(pattern, patternIndex);
            if (textWidth == patternWidth && CompareStringOrdinal(text.data() + textIndex, static_cast<int>(textWidth),
                    pattern.data() + patternIndex, static_cast<int>(patternWidth), TRUE) == CSTR_EQUAL) {
                textIndex += textWidth;
                patternIndex += patternWidth;
            } else if (star != std::wstring_view::npos) {
                patternIndex = star + 1;
                saved += width(text, saved);
                textIndex = saved;
            } else return false;
        } else if (patternIndex < pattern.size() && pattern[patternIndex] == L'*') {
            star = patternIndex++;
            saved = textIndex;
        } else if (star != std::wstring_view::npos) {
            patternIndex = star + 1;
            saved += width(text, saved);
            textIndex = saved;
        } else return false;
    }
    while (patternIndex < pattern.size() && pattern[patternIndex] == L'*') ++patternIndex;
    return patternIndex == pattern.size();
}
Query ParseQuery(std::wstring_view text, const FilterRegistry& registry) {
    Query query;
    for (const auto& token : Tokenize(text)) {
        const size_t colon = token.find(L':');
        if (colon != std::wstring::npos) {
            const auto key = std::wstring_view(token).substr(0, colon);
            const auto value = std::wstring_view(token).substr(colon + 1);
            if (const auto* definition = registry.Find(key)) {
                const auto parsed = definition->parseValue(value);
                if (parsed) query.predicates.push_back(definition->makePredicate(*parsed));
                else query.invalidFilters.push_back(std::wstring(key));
                continue;
            }
            query.unrecognizedKeys.push_back(std::wstring(key));
        }
        if (token.find_first_of(L"*?") != std::wstring::npos) {
            query.predicates.push_back([token](const Candidate& candidate) { return GlobMatches(candidate.name, token); });
        } else {
            if (query.primaryTerm.empty()) query.primaryTerm = token;
            query.predicates.push_back([token](const Candidate& candidate) { return OrdinalContains(candidate.name, token); });
        }
    }
    return query;
}

} // namespace ffsearch
