#include "ffsearch/Query.h"
#include "ffsearch/Search.h"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <cwctype>
#include <windows.h>

namespace ffsearch {

// performance-closure-and-multisurface-acceleration 7.3: ordinal comparison
// between a NamePool UTF-16 view and a wide-character string. Declared here so
// the anonymous-namespace helpers below and the search accumulator can share
// one implementation.
int CompareOrdinal16(std::u16string_view left, std::wstring_view right);

namespace {

using Clock = std::chrono::steady_clock;
thread_local MatchDiagnostics* gActiveDiagnostics = nullptr;

class DiagnosticsScope {
public:
    explicit DiagnosticsScope(MatchDiagnostics* diagnostics)
        : previous_(gActiveDiagnostics) {
        gActiveDiagnostics = diagnostics;
    }

    ~DiagnosticsScope() {
        gActiveDiagnostics = previous_;
    }

private:
    MatchDiagnostics* previous_;
};

std::wstring Lower(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return result;
}

bool IsAscii(std::wstring_view value) {
    return std::all_of(value.begin(), value.end(), [](wchar_t ch) { return ch <= 0x7f; });
}

std::u16string FoldAscii16(std::wstring_view value) {
    std::u16string result;
    result.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
        result.push_back(static_cast<char16_t>(ch));
    }
    return result;
}

uint8_t AsciiFrequency(char16_t value) {
    switch (value) {
        case u'e': return 255;
        case u't': return 245;
        case u'a': return 235;
        case u'o': return 230;
        case u'i': return 225;
        case u'n': return 220;
        case u's': return 210;
        case u'h': return 205;
        case u'r': return 200;
        case u'd': return 185;
        case u'l': return 180;
        case u'c': return 165;
        case u'u': return 155;
        case u'm': return 150;
        case u'w': return 145;
        case u'f': return 140;
        case u'g': return 135;
        case u'y': return 130;
        case u'p': return 110;
        case u'b': return 100;
        case u'v': return 90;
        case u'k': return 80;
        case u'j': return 60;
        case u'x': return 50;
        case u'q': return 30;
        case u'z': return 20;
        case u'_': return 40;
        default: return 70;
    }
}

size_t ChooseAsciiAnchor(std::u16string_view needle) {
    size_t anchor = 0;
    for (size_t index = 1; index < needle.size(); ++index) {
        if (AsciiFrequency(needle[index]) < AsciiFrequency(needle[anchor])) anchor = index;
    }
    return anchor;
}

std::array<size_t, 128> BuildAsciiShifts(std::u16string_view needle) {
    std::array<size_t, 128> shifts{};
    shifts.fill(needle.size());
    if (needle.size() < 2) return shifts;
    for (size_t index = 0; index + 1 < needle.size(); ++index) {
        const size_t shift = needle.size() - 1 - index;
        shifts[needle[index]] = shift;
        // Mixed-case haystacks index the table with their raw character; map
        // the uppercase spelling of every folded needle character to the same
        // skip so bad-character shifts stay effective. Skip values only ever
        // change speed, never the match decision.
        if (needle[index] >= u'a' && needle[index] <= u'z') {
            shifts[static_cast<size_t>(needle[index]) - (u'a' - u'A')] = shift;
        }
    }
    return shifts;
}

constexpr char16_t FoldAscii16Char(char16_t value) {
    return value >= u'A' && value <= u'Z' ? static_cast<char16_t>(value + (u'a' - u'A')) : value;
}

bool FoldedWindowEquals16(std::u16string_view haystack, size_t start, std::u16string_view folded) {
    for (size_t index = 0; index < folded.size(); ++index) {
        if (FoldAscii16Char(haystack[start + index]) != folded[index]) return false;
    }
    return true;
}

// Anchor scan over any-case ASCII haystacks. The window bound below also
// closes an out-of-bounds read the previous exact-match variant had when an
// anchor occurrence sat within the final anchorIndex+1 code units.
bool AnchorContainsFold(std::u16string_view haystack, std::u16string_view folded,
                        char16_t anchor, size_t anchorIndex) {
    if (folded.empty()) return true;
    if (haystack.size() < folded.size()) return false;
    for (size_t position = anchorIndex; position < haystack.size(); ++position) {
        if (FoldAscii16Char(haystack[position]) != anchor) continue;
        const size_t start = position - anchorIndex;
        if (start + folded.size() > haystack.size()) return false;
        if (FoldedWindowEquals16(haystack, start, folded)) return true;
    }
    return false;
}

bool BoyerMooreHorspoolContains(std::u16string_view haystack, std::u16string_view needle,
                                const std::array<size_t, 128>& shifts) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    const size_t lastIndex = needle.size() - 1;
    size_t position = lastIndex;
    while (position < haystack.size()) {
        const char16_t rawTail = haystack[position];
        if (FoldAscii16Char(rawTail) == needle.back() &&
            FoldedWindowEquals16(haystack, position - lastIndex, needle)) {
            return true;
        }
        position += rawTail < shifts.size() ? shifts[rawTail] : needle.size();
    }
    return false;
}

// Public prepared-needle surface (declared in Query.h); defined at namespace
// scope below together with the other exported matchers.

constexpr wchar_t FoldAsciiWideChar(wchar_t value) {
    return value >= L'A' && value <= L'Z' ? static_cast<wchar_t>(value + (L'a' - L'A')) : value;
}

bool FoldedWindowEqualsWide(std::wstring_view haystack, size_t start, std::u16string_view folded) {
    for (size_t index = 0; index < folded.size(); ++index) {
        if (static_cast<char16_t>(FoldAsciiWideChar(haystack[start + index])) != folded[index]) return false;
    }
    return true;
}

bool AnchorContainsFoldWide(std::wstring_view haystack, const AsciiFoldNeedle& needle) {
    const std::u16string_view folded(needle.folded);
    for (size_t position = needle.anchorIndex; position < haystack.size(); ++position) {
        if (static_cast<char16_t>(FoldAsciiWideChar(haystack[position])) != needle.anchor) continue;
        const size_t start = position - needle.anchorIndex;
        if (start + folded.size() > haystack.size()) return false;
        if (FoldedWindowEqualsWide(haystack, start, folded)) return true;
    }
    return false;
}

bool BoyerMooreHorspoolContainsWide(std::wstring_view haystack, const AsciiFoldNeedle& needle) {
    const std::u16string_view folded(needle.folded);
    const size_t lastIndex = folded.size() - 1;
    size_t position = lastIndex;
    while (position < haystack.size()) {
        const wchar_t rawTail = haystack[position];
        if (static_cast<char16_t>(FoldAsciiWideChar(rawTail)) == folded.back() &&
            FoldedWindowEqualsWide(haystack, position - lastIndex, folded)) {
            return true;
        }
        const size_t raw = static_cast<size_t>(rawTail);
        position += raw < needle.shifts.size() ? needle.shifts[raw] : folded.size();
    }
    return false;
}

// Single dispatch point for prepared literal matching over projection views.
// `shifts` must be built once by the caller via BuildAsciiShifts(foldedNeedle).
bool OrdinalContains16Prepared(std::u16string_view haystack, std::wstring_view needle,
                               std::u16string_view foldedNeedle,
                               const std::array<size_t, 128>& shifts,
                               bool nameIsAscii,
                               char16_t anchor, size_t anchorIndex) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    if (!foldedNeedle.empty() && nameIsAscii) {
        return foldedNeedle.size() >= 4
            ? BoyerMooreHorspoolContains(haystack, foldedNeedle, shifts)
            : AnchorContainsFold(haystack, foldedNeedle, anchor, anchorIndex);
    }
    for (size_t index = 0; index + needle.size() <= haystack.size(); ++index) {
        if (CompareOrdinal16(haystack.substr(index, needle.size()), needle) == 0) return true;
    }
    return false;
}

bool OrdinalContains16(std::u16string_view haystack, std::wstring_view needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    for (size_t index = 0; index + needle.size() <= haystack.size(); ++index) {
        if (CompareOrdinal16(haystack.substr(index, needle.size()), needle) == 0) return true;
    }
    return false;
}

bool GlobMatches16(std::u16string_view text, std::wstring_view pattern) {
    const auto width = [](std::u16string_view value, size_t offset) -> size_t {
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
            const size_t patternWidth = pattern[patternIndex] >= 0xD800 && pattern[patternIndex] <= 0xDBFF &&
                patternIndex + 1 < pattern.size() && pattern[patternIndex + 1] >= 0xDC00 && pattern[patternIndex + 1] <= 0xDFFF ? 2 : 1;
            if (textWidth == patternWidth && CompareOrdinal16(text.substr(textIndex, textWidth),
                                                                pattern.substr(patternIndex, patternWidth)) == 0) {
                textIndex += textWidth;
                patternIndex += patternWidth;
            } else if (star != std::wstring_view::npos) {
                patternIndex = star + 1;
                saved += width(text, saved);
                textIndex = saved;
            } else {
                return false;
            }
        } else if (patternIndex < pattern.size() && pattern[patternIndex] == L'*') {
            star = patternIndex++;
            saved = textIndex;
        } else if (star != std::wstring_view::npos) {
            patternIndex = star + 1;
            saved += width(text, saved);
            textIndex = saved;
        } else {
            return false;
        }
    }
    while (patternIndex < pattern.size() && pattern[patternIndex] == L'*') ++patternIndex;
    return patternIndex == pattern.size();
}

bool LowerEquals16(std::u16string_view value, std::wstring_view lowerValue) {
    if (value.size() != lowerValue.size()) return false;
    for (size_t index = 0; index < value.size(); ++index) {
        if (gActiveDiagnostics != nullptr) ++gActiveDiagnostics->caseFoldOperations;
        if (static_cast<wchar_t>(towlower(static_cast<wchar_t>(value[index]))) != lowerValue[index]) return false;
    }
    return true;
}

bool HasLowerExtension16(std::u16string_view name, std::wstring_view extension) {
    const size_t dot = name.find_last_of(u'.');
    return dot != std::u16string_view::npos && LowerEquals16(name.substr(dot + 1), extension);
}

std::vector<std::wstring> Tokenize(std::wstring_view text) {
    std::vector<std::wstring> tokens;
    tokens.reserve(8);
    std::wstring current;
    current.reserve(text.size());
    bool quoted = false;
    for (wchar_t c : text) {
        if (c == L'"') { quoted = !quoted; continue; }
        if (!quoted && iswspace(c)) { if (!current.empty()) { tokens.push_back(std::move(current)); current.clear(); current.reserve(text.size()); } continue; }
        current.push_back(c);
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

// allocated-size-search-filter D2/D3: ondisk: reuses the SizeFilter value
// grammar (operators, ranges, B/KB/MB/GB) but evaluates allocatedSizeBytes
// only when allocatedSizeKnown==true. UNKNOWN never matches a numeric
// predicate in either direction and never falls back to logical size.
Predicate MakeAllocatedSizePredicate(const FilterValue& value) {
    const auto filter = std::get<SizeFilter>(value);
    return [filter](const Candidate& c) {
        if (!c.allocatedSizeKnown) return false;
        switch (filter.op) {
            case SizeFilter::Op::Equal: return c.allocatedSizeBytes == filter.first;
            case SizeFilter::Op::Greater: return c.allocatedSizeBytes > filter.first;
            case SizeFilter::Op::GreaterEqual: return c.allocatedSizeBytes >= filter.first;
            case SizeFilter::Op::Less: return c.allocatedSizeBytes < filter.first;
            case SizeFilter::Op::LessEqual: return c.allocatedSizeBytes <= filter.first;
            case SizeFilter::Op::Range: return c.allocatedSizeBytes >= filter.first && c.allocatedSizeBytes <= filter.second;
        }
        return false;
    };
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

// All date-filter day boundaries use LOCAL time, matching what Explorer and
// the user's clock display. SYSTEMTIME <-> FILETIME conversions here interpret
// SYSTEMTIME as local time (SystemTimeToFileTime), so "today" and relative
// offsets are anchored to local midnight rather than a UTC day.
uint64_t LocalDayStart(uint64_t now) {
    const FILETIME nowFileTime{static_cast<DWORD>(now & 0xFFFFFFFFu), static_cast<DWORD>(now >> 32)};
    FILETIME localFileTime{};
    SYSTEMTIME local{};
    if (!FileTimeToLocalFileTime(&nowFileTime, &localFileTime) || !FileTimeToSystemTime(&localFileTime, &local)) {
        return now - now % (24ULL * 60 * 60 * 10'000'000); // UTC fallback if conversion fails
    }
    local.wHour = 0;
    local.wMinute = 0;
    local.wSecond = 0;
    local.wMilliseconds = 0;
    FILETIME dayStart{};
    if (!SystemTimeToFileTime(&local, &dayStart)) {
        return now - now % (24ULL * 60 * 60 * 10'000'000);
    }
    ULARGE_INTEGER value{};
    value.LowPart = dayStart.dwLowDateTime;
    value.HighPart = dayStart.dwHighDateTime;
    return value.QuadPart;
}

std::optional<FilterValue> ParseDateFilter(std::wstring_view value) {
    constexpr uint64_t kDay = 24ULL * 60 * 60 * 10'000'000;
    if (Lower(value) == L"today") {
        // Local-day convention: [local midnight, next local midnight).
        const auto day = LocalDayStart(CurrentFileTime());
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
        // Local-day convention: the window covers the last N local days,
        // anchored at today's local midnight (not a rolling UTC window).
        const auto dayStart = LocalDayStart(CurrentFileTime());
        const uint64_t back = days == 0 ? 0 : static_cast<uint64_t>(days - 1);
        if (back > dayStart / kDay + 1) return std::nullopt;
        // Strictly-greater comparison, offset by one tick so the window
        // includes the entire earliest local calendar day.
        return DateFilter{DateFilter::Op::GreaterThan, dayStart - back * kDay - 1};
    }
    const auto day = ParseDay(value);
    if (!day) return std::nullopt;
    return DateFilter{DateFilter::Op::EqualDay, *day, *day + kDay - 1};
}
Predicate MakeDatePredicate(const FilterValue& value) { const auto filter = std::get<DateFilter>(value); return [filter](const Candidate& c) { return filter.op == DateFilter::Op::GreaterThan ? c.modifiedTime > filter.first : c.modifiedTime >= filter.first && c.modifiedTime <= filter.second; }; }

} // namespace

void FilterRegistry::Register(std::wstring key, FilterDefinition definition) { definitions_[Lower(key)] = std::move(definition); }
const FilterDefinition* FilterRegistry::Find(std::wstring_view key) const { const auto it = definitions_.find(Lower(key)); return it == definitions_.end() ? nullptr : &it->second; }

const FilterRegistry& FilterRegistry::WithDefaults() {
    static const FilterRegistry kCachedDefaults = []() {
        FilterRegistry defaults;
    defaults.Register(L"ext", {[](std::wstring_view v) -> std::optional<FilterValue> { std::wstring x(v); if (!x.empty() && x.front() == L'.') x.erase(0, 1); return x.empty() ? std::nullopt : std::optional<FilterValue>(Lower(x)); }, [](const FilterValue& v) { const auto extension = std::get<std::wstring>(v); return [extension](const Candidate& c) { const size_t dot = c.name.find_last_of(L'.'); return dot != std::wstring::npos && Lower(std::wstring_view(c.name).substr(dot + 1)) == extension; }; }, [](const FilterValue& v) { const auto extension = std::get<std::wstring>(v); return [extension](const CandidateView& c) { return HasLowerExtension16(c.name, extension); }; }});
    defaults.Register(L"name", {[](std::wstring_view value) -> std::optional<FilterValue> {
        return value.empty() ? std::nullopt : std::optional<FilterValue>(std::wstring(value));
    }, [](const FilterValue& value) {
        const auto needle = PrepareAsciiFoldNeedle(std::get<std::wstring>(value));
        return [needle](const Candidate& candidate) { return AsciiFoldContains(candidate.name, needle); };
    }, [](const FilterValue& value) {
        const auto text = std::get<std::wstring>(value);
        const auto foldedText = IsAscii(text) ? FoldAscii16(text) : std::u16string{};
        const auto foldedShifts = BuildAsciiShifts(foldedText);
        const size_t foldedAnchorIndex = ChooseAsciiAnchor(foldedText);
        const char16_t foldedAnchor = foldedText.empty() ? 0 : foldedText[foldedAnchorIndex];
        return [text, foldedText, foldedShifts, foldedAnchor, foldedAnchorIndex](const CandidateView& candidate) {
            return OrdinalContains16Prepared(candidate.name, text, foldedText, foldedShifts,
                                             candidate.nameIsAscii, foldedAnchor, foldedAnchorIndex);
        };
    }});
    defaults.Register(L"folder", {[](std::wstring_view value) -> std::optional<FilterValue> {
        return value.empty() ? std::nullopt : std::optional<FilterValue>(std::wstring(value));
    }, [](const FilterValue& value) {
        // Folder paths are long; the prepared fold scan avoids one ordinal
        // API call per alignment on the dominant ASCII path class.
        const auto needle = PrepareAsciiFoldNeedle(std::get<std::wstring>(value));
        return [needle](const Candidate& candidate) { return AsciiFoldContains(candidate.folder, needle); };
    }, [](const FilterValue& value) {
        const auto needle = PrepareAsciiFoldNeedle(std::get<std::wstring>(value));
        return [needle](const CandidateView& candidate) { return AsciiFoldContains(candidate.folder, needle); };
    }});
    defaults.Register(L"size", {ParseSizeFilter, MakeSizePredicate, [](const FilterValue& value) {
        const auto filter = std::get<SizeFilter>(value);
        return [filter](const CandidateView& c) { switch (filter.op) { case SizeFilter::Op::Equal: return c.sizeBytes == filter.first; case SizeFilter::Op::Greater: return c.sizeBytes > filter.first; case SizeFilter::Op::GreaterEqual: return c.sizeBytes >= filter.first; case SizeFilter::Op::Less: return c.sizeBytes < filter.first; case SizeFilter::Op::LessEqual: return c.sizeBytes <= filter.first; case SizeFilter::Op::Range: return c.sizeBytes >= filter.first && c.sizeBytes <= filter.second; } return false; };
    }});
    // allocated-size-search-filter D1/D2/D3: ondisk: is the single canonical
    // allocated-size key, reusing the SizeFilter numeric grammar and gating
    // every numeric predicate on allocatedSizeKnown.
    defaults.Register(L"ondisk", {ParseSizeFilter, MakeAllocatedSizePredicate, [](const FilterValue& value) {
        const auto filter = std::get<SizeFilter>(value);
        return [filter](const CandidateView& c) {
            if (!c.allocatedSizeKnown) return false;
            switch (filter.op) {
                case SizeFilter::Op::Equal: return c.allocatedSizeBytes == filter.first;
                case SizeFilter::Op::Greater: return c.allocatedSizeBytes > filter.first;
                case SizeFilter::Op::GreaterEqual: return c.allocatedSizeBytes >= filter.first;
                case SizeFilter::Op::Less: return c.allocatedSizeBytes < filter.first;
                case SizeFilter::Op::LessEqual: return c.allocatedSizeBytes <= filter.first;
                case SizeFilter::Op::Range: return c.allocatedSizeBytes >= filter.first && c.allocatedSizeBytes <= filter.second;
            }
            return false;
        };
    }});
    defaults.Register(L"modified", {ParseDateFilter, MakeDatePredicate, [](const FilterValue& value) {
        const auto filter = std::get<DateFilter>(value);
        return [filter](const CandidateView& c) { return filter.op == DateFilter::Op::GreaterThan ? c.modifiedTime > filter.first : c.modifiedTime >= filter.first && c.modifiedTime <= filter.second; };
    }});
    defaults.Register(L"kind", {[](std::wstring_view v) -> std::optional<FilterValue> { const auto x = Lower(v); return (x == L"document" || x == L"image" || x == L"video" || x == L"audio" || x == L"archive" || x == L"executable" || x == L"folder") ? std::optional<FilterValue>(x) : std::nullopt; }, [](const FilterValue& v) { const auto k = std::get<std::wstring>(v); return [k](const Candidate& c) { if (k == L"folder") return c.isDirectory; const size_t dot = c.name.find_last_of(L'.'); const std::wstring ext = dot == std::wstring::npos ? L"" : Lower(std::wstring_view(c.name).substr(dot + 1)); if (k == L"image") return ext == L"png" || ext == L"jpg" || ext == L"jpeg" || ext == L"gif"; if (k == L"document") return ext == L"pdf" || ext == L"doc" || ext == L"docx" || ext == L"txt"; if (k == L"video") return ext == L"mp4" || ext == L"mkv"; if (k == L"audio") return ext == L"mp3" || ext == L"wav"; if (k == L"archive") return ext == L"zip" || ext == L"7z"; return ext == L"exe" || ext == L"bat" || ext == L"cmd"; }; }, [](const FilterValue& v) {
        const auto k = std::get<std::wstring>(v);
        return [k](const CandidateView& c) { if (k == L"folder") return c.isDirectory; if (k == L"image") return HasLowerExtension16(c.name, L"png") || HasLowerExtension16(c.name, L"jpg") || HasLowerExtension16(c.name, L"jpeg") || HasLowerExtension16(c.name, L"gif"); if (k == L"document") return HasLowerExtension16(c.name, L"pdf") || HasLowerExtension16(c.name, L"doc") || HasLowerExtension16(c.name, L"docx") || HasLowerExtension16(c.name, L"txt"); if (k == L"video") return HasLowerExtension16(c.name, L"mp4") || HasLowerExtension16(c.name, L"mkv"); if (k == L"audio") return HasLowerExtension16(c.name, L"mp3") || HasLowerExtension16(c.name, L"wav"); if (k == L"archive") return HasLowerExtension16(c.name, L"zip") || HasLowerExtension16(c.name, L"7z"); return HasLowerExtension16(c.name, L"exe") || HasLowerExtension16(c.name, L"bat") || HasLowerExtension16(c.name, L"cmd"); };
    }});
        return defaults;
    }();
    return kCachedDefaults;
}

bool Query::Matches(const Candidate& candidate) const { return std::all_of(predicates.begin(), predicates.end(), [&](const auto& p) { return p(candidate); }); }
bool Query::MatchesWithoutFolder(const Candidate& candidate) const {
    return std::all_of(predicatesWithoutFolder.begin(), predicatesWithoutFolder.end(),
                       [&](const auto& predicate) { return predicate(candidate); });
}
namespace {

bool EvaluateViewPredicates(const std::vector<ViewPredicate>& predicates,
                            const std::vector<PredicateKind>& kinds,
                            const CandidateView& candidate,
                            MatchDiagnostics* diagnostics) {
    DiagnosticsScope scope(diagnostics);
    for (size_t index = 0; index < predicates.size(); ++index) {
        const PredicateKind kind = index < kinds.size() ? kinds[index] : PredicateKind::Unknown;
        if (diagnostics != nullptr) {
            ++diagnostics->predicateEvaluations;
            if (kind == PredicateKind::LiteralName || kind == PredicateKind::GlobName ||
                kind == PredicateKind::FolderName) {
                ++diagnostics->termEvaluations;
            }
        }
        const auto start = diagnostics != nullptr ? Clock::now() : Clock::time_point{};
        const bool matched = predicates[index](candidate);
        if (diagnostics != nullptr) {
            const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            diagnostics->predicateDispatchMs += elapsed;
            switch (kind) {
                case PredicateKind::LiteralName:
                    ++diagnostics->substringSearches;
                    diagnostics->substringMatchingMs += elapsed;
                    break;
                case PredicateKind::GlobName:
                    ++diagnostics->globMatches;
                    diagnostics->globMatchingMs += elapsed;
                    break;
                case PredicateKind::FolderName:
                    ++diagnostics->substringSearches;
                    diagnostics->folderMatchingMs += elapsed;
                    break;
                case PredicateKind::Extension:
                    ++diagnostics->extensionPredicates;
                    diagnostics->extensionMatchingMs += elapsed;
                    break;
                case PredicateKind::Size:
                case PredicateKind::AllocatedSize:
                case PredicateKind::Modified:
                case PredicateKind::Kind:
                    ++diagnostics->metadataPredicates;
                    diagnostics->metadataMatchingMs += elapsed;
                    break;
                case PredicateKind::Unknown:
                    break;
            }
        }
        if (!matched) {
            if (diagnostics != nullptr) ++diagnostics->earlyRejects;
            return false;
        }
        if (diagnostics != nullptr) ++diagnostics->predicateMatches;
    }
    return true;
}

bool MatchSimpleLiteral(const Query& query, const CandidateView& candidate) {
    return OrdinalContains16Prepared(candidate.name, query.simpleLiteralTerm, query.simpleLiteralFolded,
                                     query.simpleLiteralShifts, candidate.nameIsAscii,
                                     query.simpleLiteralAnchor, query.simpleLiteralAnchorIndex);
}

bool EvaluateSimpleLiteral(const Query& query, const CandidateView& candidate,
                           MatchDiagnostics* diagnostics) {
    if (diagnostics == nullptr) return MatchSimpleLiteral(query, candidate);
    DiagnosticsScope scope(diagnostics);
    if (diagnostics != nullptr) {
        ++diagnostics->predicateEvaluations;
        ++diagnostics->termEvaluations;
        ++diagnostics->substringSearches;
    }
    const auto start = diagnostics != nullptr ? Clock::now() : Clock::time_point{};
    const bool matched = MatchSimpleLiteral(query, candidate);
    if (diagnostics != nullptr) {
        const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        diagnostics->predicateDispatchMs += elapsed;
        diagnostics->substringMatchingMs += elapsed;
        if (matched) ++diagnostics->predicateMatches;
        else ++diagnostics->earlyRejects;
    }
    return matched;
}

} // namespace

bool Query::MatchesView(const CandidateView& candidate, MatchDiagnostics* diagnostics) const {
    if (viewCompatible && simpleLiteralName) return EvaluateSimpleLiteral(*this, candidate, diagnostics);
    return viewCompatible && EvaluateViewPredicates(viewPredicates, viewPredicateKinds, candidate, diagnostics);
}
bool Query::MatchesViewWithoutFolder(const CandidateView& candidate, MatchDiagnostics* diagnostics) const {
    if (viewCompatible && simpleLiteralName) return EvaluateSimpleLiteral(*this, candidate, diagnostics);
    return viewCompatible && EvaluateViewPredicates(viewPredicatesWithoutFolder,
                                                    viewPredicateWithoutFolderKinds, candidate, diagnostics);
}
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
AsciiFoldNeedle PrepareAsciiFoldNeedle(std::wstring_view value) {
    AsciiFoldNeedle needle;
    needle.value = std::wstring(value);
    needle.ascii = IsAscii(value);
    if (!needle.ascii) return needle;
    needle.folded = FoldAscii16(value);
    if (!needle.folded.empty()) {
        needle.anchorIndex = ChooseAsciiAnchor(needle.folded);
        needle.anchor = needle.folded[needle.anchorIndex];
        needle.shifts = BuildAsciiShifts(needle.folded);
    }
    return needle;
}

bool AsciiFoldContains(std::wstring_view haystack, const AsciiFoldNeedle& needle) {
    // Non-ASCII and empty needles keep exact ordinal semantics via fallback.
    if (!needle.ascii || needle.folded.empty()) return OrdinalContains(haystack, needle.value);
    if (haystack.size() < needle.folded.size()) return false;
    return needle.folded.size() >= 4 ? BoyerMooreHorspoolContainsWide(haystack, needle)
                                     : AnchorContainsFoldWide(haystack, needle);
}

bool AsciiFoldEquals(std::wstring_view left, std::wstring_view right) {
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < right.size(); ++index) {
        if (static_cast<char16_t>(FoldAsciiWideChar(left[index])) !=
            FoldAscii16Char(static_cast<char16_t>(right[index]))) {
            return false;
        }
    }
    return true;
}

bool AsciiFoldStartsWith(std::wstring_view value, std::wstring_view prefix) {
    if (value.size() < prefix.size()) return false;
    return AsciiFoldEquals(value.substr(0, prefix.size()), prefix);
}

Query ParseQuery(std::wstring_view text, const FilterRegistry& registry) {
    Query query;
    std::wstring simpleLiteralTerm;
    const auto addPredicate = [&query](Predicate predicate, ViewPredicate viewPredicate, bool requiresFolder,
                                       PredicateKind kind) {
        query.predicates.push_back(predicate);
        if (requiresFolder) {
            query.requiresFolder = true;
        } else {
            query.predicatesWithoutFolder.push_back(std::move(predicate));
        }
        if (!viewPredicate) {
            query.viewCompatible = false;
        } else {
            query.viewPredicates.push_back(viewPredicate);
            query.viewPredicateKinds.push_back(kind);
            if (!requiresFolder) query.viewPredicatesWithoutFolder.push_back(std::move(viewPredicate));
            if (!requiresFolder) query.viewPredicateWithoutFolderKinds.push_back(kind);
        }
    };
    for (const auto& token : Tokenize(text)) {
        const size_t colon = token.find(L':');
        if (colon != std::wstring::npos) {
            const auto key = std::wstring_view(token).substr(0, colon);
            const auto value = std::wstring_view(token).substr(colon + 1);
            if (const auto* definition = registry.Find(key)) {
                const auto parsed = definition->parseValue(value);
                if (parsed) {
                    const bool requiresFolder = Lower(key) == L"folder";
                    const auto normalizedKey = Lower(key);
                    const PredicateKind kind = normalizedKey == L"name" ? PredicateKind::LiteralName
                        : normalizedKey == L"folder" ? PredicateKind::FolderName
                        : normalizedKey == L"ext" ? PredicateKind::Extension
                        : normalizedKey == L"size" ? PredicateKind::Size
                        : normalizedKey == L"ondisk" ? PredicateKind::AllocatedSize
                        : normalizedKey == L"modified" ? PredicateKind::Modified
                        : normalizedKey == L"kind" ? PredicateKind::Kind
                        : PredicateKind::Unknown;
                    if (kind == PredicateKind::LiteralName) simpleLiteralTerm = std::get<std::wstring>(*parsed);
                    const ViewPredicate viewPredicate = definition->makeViewPredicate
                        ? definition->makeViewPredicate(*parsed) : ViewPredicate{};
                    addPredicate(definition->makePredicate(*parsed), viewPredicate, requiresFolder, kind);
                }
                else query.invalidFilters.push_back(std::wstring(key));
                continue;
            }
            query.unrecognizedKeys.push_back(std::wstring(key));
        }
        if (token.find_first_of(L"*?") != std::wstring::npos) {
            addPredicate([token](const Candidate& candidate) { return GlobMatches(candidate.name, token); },
                         [token](const CandidateView& candidate) { return GlobMatches16(candidate.name, token); }, false,
                         PredicateKind::GlobName);
        } else {
            if (query.primaryTerm.empty()) query.primaryTerm = token;
            simpleLiteralTerm = token;
            const auto foldedToken = IsAscii(token) ? FoldAscii16(token) : std::u16string{};
            const auto foldedTokenShifts = BuildAsciiShifts(foldedToken);
            const size_t foldedAnchorIndex = ChooseAsciiAnchor(foldedToken);
            const char16_t foldedAnchor = foldedToken.empty() ? 0 : foldedToken[foldedAnchorIndex];
            const auto asciiNeedle = PrepareAsciiFoldNeedle(token);
            addPredicate([asciiNeedle](const Candidate& candidate) { return AsciiFoldContains(candidate.name, asciiNeedle); },
                         [token, foldedToken, foldedTokenShifts, foldedAnchor, foldedAnchorIndex](const CandidateView& candidate) {
                             return OrdinalContains16Prepared(candidate.name, token, foldedToken, foldedTokenShifts,
                                                              candidate.nameIsAscii, foldedAnchor, foldedAnchorIndex);
                         }, false,
                         PredicateKind::LiteralName);
        }
    }
    if (query.viewCompatible && !query.requiresFolder && query.predicates.size() == 1 &&
        query.viewPredicateKinds.size() == 1 && query.viewPredicateKinds.front() == PredicateKind::LiteralName &&
        !simpleLiteralTerm.empty()) {
        query.simpleLiteralName = true;
        query.simpleLiteralTerm = std::move(simpleLiteralTerm);
        query.simpleLiteralFolded = IsAscii(query.simpleLiteralTerm)
            ? FoldAscii16(query.simpleLiteralTerm) : std::u16string{};
        query.simpleLiteralAnchorIndex = ChooseAsciiAnchor(query.simpleLiteralFolded);
        query.simpleLiteralAnchor = query.simpleLiteralFolded.empty()
            ? 0 : query.simpleLiteralFolded[query.simpleLiteralAnchorIndex];
        query.simpleLiteralShifts = BuildAsciiShifts(query.simpleLiteralFolded);
    }
    return query;
}

int CompareOrdinal16(std::u16string_view left, std::wstring_view right) {
    if (gActiveDiagnostics != nullptr) ++gActiveDiagnostics->caseInsensitiveComparisons;
    return CompareStringOrdinal(reinterpret_cast<const wchar_t*>(left.data()), static_cast<int>(left.size()),
                                right.data(), static_cast<int>(right.size()), TRUE) - CSTR_EQUAL;
}

namespace {

bool FoldedEquals16(std::u16string_view name, std::wstring_view term) {
    if (name.size() != term.size()) return false;
    if (IsAscii(term)) {
        for (size_t index = 0; index < name.size(); ++index) {
            if (FoldAscii16Char(name[index]) != static_cast<char16_t>(FoldAsciiWideChar(term[index]))) {
                return false;
            }
        }
        return true;
    }
    return CompareOrdinal16(name, term) == 0;
}

bool FoldedStartsWith16(std::u16string_view name, std::wstring_view term) {
    if (term.size() > name.size()) return false;
    if (IsAscii(term)) {
        for (size_t index = 0; index < term.size(); ++index) {
            if (FoldAscii16Char(name[index]) != static_cast<char16_t>(FoldAsciiWideChar(term[index]))) {
                return false;
            }
        }
        return true;
    }
    return CompareOrdinal16(name.substr(0, term.size()), term) == 0;
}

bool FoldedContains16(std::u16string_view name, std::wstring_view term) {
    if (term.empty()) return true;
    if (term.size() > name.size()) return false;
    if (IsAscii(term)) {
        for (size_t start = 0; start + term.size() <= name.size(); ++start) {
            bool equal = true;
            for (size_t index = 0; index < term.size(); ++index) {
                if (FoldAscii16Char(name[start + index])
                    != static_cast<char16_t>(FoldAsciiWideChar(term[index]))) {
                    equal = false;
                    break;
                }
            }
            if (equal) return true;
        }
        return false;
    }
    for (size_t start = 0; start + term.size() <= name.size(); ++start) {
        if (CompareOrdinal16(name.substr(start, term.size()), term) == 0) return true;
    }
    return false;
}

}  // namespace

MatchTier ClassifyMatchView(std::u16string_view name, std::wstring_view term) {
    std::u16string_view stem = name;
    const size_t dot = stem.find_last_of(u'.');
    if (dot != std::u16string_view::npos) stem = stem.substr(0, dot);
    if (FoldedEquals16(stem, term) || FoldedEquals16(name, term)) return MatchTier::Exact;
    if (FoldedStartsWith16(name, term)) return MatchTier::Prefix;
    if (FoldedContains16(name, term)) return MatchTier::Substring;
    return MatchTier::PathOnly;
}

} // namespace ffsearch
