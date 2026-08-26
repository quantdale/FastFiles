#include "ffsearch/Query.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

namespace {

using Clock = std::chrono::steady_clock;

struct Name {
    std::u16string value;
    bool asciiLower = false;
    bool ascii = false;
};

struct PreparedLiteral {
    std::wstring original;
    ffsearch::Query parsed;
    std::u16string folded;
    char16_t last = 0;
    char16_t anchor = 0;
    size_t anchorIndex = 0;
    std::array<size_t, 128> shifts{};
};

bool IsAscii(std::u16string_view value) {
    for (const char16_t ch : value) {
        if (ch > 0x7f) return false;
    }
    return true;
}

bool IsAsciiLower(std::u16string_view value) {
    for (const char16_t ch : value) {
        if (ch > 0x7f || (ch >= u'A' && ch <= u'Z')) return false;
    }
    return true;
}

bool IsAscii(std::wstring_view value) {
    return std::all_of(value.begin(), value.end(), [](const wchar_t ch) { return ch <= 0x7f; });
}

std::u16string FoldAscii(std::wstring_view value) {
    std::u16string result;
    result.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
        result.push_back(static_cast<char16_t>(ch));
    }
    return result;
}

uint8_t AsciiFrequency(const char16_t value) {
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

size_t ChooseAnchor(std::u16string_view needle) {
    size_t result = 0;
    for (size_t index = 1; index < needle.size(); ++index) {
        if (AsciiFrequency(needle[index]) < AsciiFrequency(needle[result])) result = index;
    }
    return result;
}

PreparedLiteral Prepare(std::wstring query) {
    PreparedLiteral result;
    result.original = std::move(query);
    result.parsed = ffsearch::ParseQuery(result.original, ffsearch::FilterRegistry::WithDefaults());
    if (!IsAscii(result.original)) return result;
    result.folded = FoldAscii(result.original);
    if (result.folded.empty()) return result;
    result.last = result.folded.back();
    result.anchorIndex = ChooseAnchor(result.folded);
    result.anchor = result.folded[result.anchorIndex];
    result.shifts.fill(result.folded.size());
    for (size_t index = 0; index + 1 < result.folded.size(); ++index) {
        result.shifts[result.folded[index]] = result.folded.size() - 1 - index;
    }
    return result;
}

bool CompareOrdinal(std::u16string_view left, std::wstring_view right) {
    return CompareStringOrdinal(reinterpret_cast<const wchar_t*>(left.data()), static_cast<int>(left.size()),
                                right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

bool CurrentPrepared(const Name& name, const PreparedLiteral& query) {
    if (query.original.empty()) return true;
    if (name.value.size() < query.original.size()) return false;
    if (name.asciiLower && !query.folded.empty()) {
        size_t anchorPosition = query.anchorIndex;
        while (anchorPosition < name.value.size()) {
            anchorPosition = name.value.find(query.anchor, anchorPosition);
            if (anchorPosition == std::u16string_view::npos) return false;
            const size_t start = anchorPosition - query.anchorIndex;
            if (std::u16string_view(name.value.data() + start, query.folded.size()) == query.folded) return true;
            ++anchorPosition;
        }
        return false;
    }
    for (size_t index = 0; index + query.original.size() <= name.value.size(); ++index) {
        if (CompareOrdinal(name.value.substr(index, query.original.size()), query.original)) return true;
    }
    return false;
}

bool FirstLast(const Name& name, const PreparedLiteral& query) {
    if (query.original.empty()) return true;
    if (name.value.size() < query.original.size()) return false;
    if (!name.asciiLower || query.folded.empty()) return CurrentPrepared(name, query);
    size_t position = 0;
    while (position + query.folded.size() <= name.value.size()) {
        position = name.value.find(query.folded.front(), position);
        if (position == std::u16string_view::npos || position + query.folded.size() > name.value.size()) return false;
        if (name.value[position + query.folded.size() - 1] == query.last &&
            std::u16string_view(name.value.data() + position, query.folded.size()) == query.folded) {
            return true;
        }
        ++position;
    }
    return false;
}

bool BoyerMooreHorspool(const Name& name, const PreparedLiteral& query) {
    if (query.original.empty()) return true;
    if (name.value.size() < query.original.size()) return false;
    if (!name.asciiLower || query.folded.empty()) return CurrentPrepared(name, query);
    const size_t lastIndex = query.folded.size() - 1;
    size_t position = lastIndex;
    while (position < name.value.size()) {
        const char16_t tail = name.value[position];
        if (tail == query.last &&
            std::u16string_view(name.value.data() + position - lastIndex, query.folded.size()) == query.folded) {
            return true;
        }
        position += tail < query.shifts.size() ? query.shifts[tail] : query.folded.size();
    }
    return false;
}

std::wstring DirectoryName(size_t index) {
    return L"folder_" + std::to_wstring(index % 997) + L"_branch_" + std::to_wstring(index);
}

std::wstring FileName(size_t index) {
    const size_t pattern = index % 10;
    if (index == 7391) return L"unique_component_7391.txt";
    if (pattern == 0 || pattern == 1) return L"common_component_" + std::to_wstring(index) + L".txt";
    if (pattern == 2) return L"report_" + std::to_wstring(index) + L".txt";
    if (pattern == 3) return L"photo_" + std::to_wstring(index) + L".jpg";
    if (pattern == 4) return L"archive_" + std::to_wstring(index) + L".zip";
    if (pattern == 5) return L"project_" + std::to_wstring(index) + L".cpp";
    if (pattern == 6) return L"notes_" + std::to_wstring(index) + L".md";
    if (pattern == 7) return L"common_media_" + std::to_wstring(index) + L".mp4";
    if (pattern == 8) return L"dataset_" + std::to_wstring(index) + L".bin";
    return L"backup_" + std::to_wstring(index) + L".7z";
}

Name MakeName(std::wstring value) {
    Name result;
    result.value.assign(value.begin(), value.end());
    result.asciiLower = IsAsciiLower(result.value);
    result.ascii = IsAscii(result.value);
    return result;
}

std::vector<Name> MakeSyntheticNames(size_t count) {
    std::vector<Name> result;
    result.reserve(count);
    result.push_back(MakeName(L"C:\\"));
    const size_t directoryCount = (std::max)(size_t{1}, count / 64);
    for (size_t index = 0; result.size() < count && index < directoryCount; ++index) {
        result.push_back(MakeName(DirectoryName(index)));
    }
    for (size_t index = 0; result.size() < count; ++index) {
        result.push_back(MakeName(FileName(index)));
    }
    return result;
}

std::vector<Name> MakeCommonPrefixNames(size_t count) {
    std::vector<Name> result;
    result.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        result.push_back(MakeName(L"aaaaaaaaaaaaaaaaaaaa" + std::to_wstring(index % 10) + L"_tail"));
    }
    return result;
}

std::vector<Name> MakeMixedCaseNames(size_t count) {
    // Real-world volumes are dominated by ASCII names with mixed case
    // ("Documents", "README.md"). These must exercise the same matcher as the
    // lowercase dataset while taking the non-all-lowercase classification.
    const std::vector<std::wstring> patterns{L"Documents", L"README", L"Program Files",
                                             L"Quarterly-Report", L"MyPhotoAlbum"};
    std::vector<Name> result;
    result.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        std::wstring value = patterns[index % patterns.size()];
        value += L"_";
        value += std::to_wstring(index);
        if ((index % 3) == 0) value += L".txt";
        result.push_back(MakeName(std::move(value)));
    }
    return result;
}

std::vector<Name> MakeUnicodeNames(size_t count) {
    const std::vector<std::wstring> patterns{L"Résumé_файл.txt", L"Report_файл.txt", L"東京_データ.bin", L"café_notes.md"};
    std::vector<Name> result;
    result.reserve(count);
    for (size_t index = 0; index < count; ++index) result.push_back(MakeName(patterns[index % patterns.size()]));
    return result;
}

using Matcher = bool (*)(const Name&, const PreparedLiteral&);

struct Timing {
    double median = 0.0;
    double p90 = 0.0;
    double maximum = 0.0;
    size_t matches = 0;
};

Timing Measure(const std::vector<Name>& names, const PreparedLiteral& query, Matcher matcher) {
    std::vector<double> samples;
    size_t matches = 0;
    for (size_t repetition = 0; repetition < 5; ++repetition) {
        const auto start = Clock::now();
        size_t count = 0;
        for (const Name& name : names) {
            if (matcher(name, query)) ++count;
        }
        samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        matches = count;
    }
    std::sort(samples.begin(), samples.end());
    return {samples[samples.size() / 2], samples[(samples.size() * 9) / 10], samples.back(), matches};
}

bool QueryMatcher(const Name& name, const PreparedLiteral& query) {
    const ffsearch::CandidateView candidate{name.value, {}, 0, 0, false, 0, 1, 1, 0, true,
                                            name.asciiLower, 0, false, name.ascii};
    return query.parsed.MatchesView(candidate);
}

struct MatcherSpec {
    const char* name;
    Matcher matcher;
};

const std::array<MatcherSpec, 4> kMatchers{{
    {"current_api", QueryMatcher},
    {"current_direct", CurrentPrepared},
    {"first_last", FirstLast},
    {"bmh", BoyerMooreHorspool},
}};

bool CheckEquivalent(const std::vector<Name>& names, const PreparedLiteral& query, const Timing& expected,
                     Matcher matcher) {
    const Timing actual = Measure(names, query, matcher);
    return actual.matches == expected.matches;
}

void RunCase(const char* caseName, const std::vector<Name>& names, std::wstring query) {
    const PreparedLiteral prepared = Prepare(std::move(query));
    const Timing expected = Measure(names, prepared, QueryMatcher);
    std::printf("CASE %s (%zu names)\n", caseName, names.size());
    for (const MatcherSpec& spec : kMatchers) {
        const Timing timing = spec.matcher == QueryMatcher ? expected : Measure(names, prepared, spec.matcher);
        const bool equivalent = spec.matcher == QueryMatcher || CheckEquivalent(names, prepared, expected, spec.matcher);
        std::printf("  %-14s median %.3f ms p90 %.3f ms max %.3f ms matches %zu %s\n", spec.name,
                    timing.median, timing.p90, timing.maximum, timing.matches, equivalent ? "PASS" : "FAIL");
    }
}

} // namespace

int wmain() {
    constexpr size_t kCount = 1'000'000;
    const std::vector<Name> synthetic = MakeSyntheticNames(kCount);
    const std::vector<Name> commonPrefix = MakeCommonPrefixNames(kCount);
    const std::vector<Name> unicode = MakeUnicodeNames(kCount);

    RunCase("failed", synthetic, L"zzzz_never_present");
    RunCase("early", synthetic, L"common");
    RunCase("late", synthetic, L"7391");
    RunCase("single", synthetic, L"p");
    RunCase("short_2", synthetic, L"ph");
    RunCase("short_3", synthetic, L"txt");
    RunCase("short_4", synthetic, L"phot");
    RunCase("short", synthetic, L"photo");
    RunCase("medium", synthetic, L"component");
    RunCase("long", synthetic, L"unique_component_7391");
    RunCase("too_long", synthetic, L"this_needle_is_longer_than_any_synthetic_filename_0123456789");
    RunCase("common_prefix", commonPrefix, L"aaaaaaaaaaaaaaaaaaaab");
    const std::vector<Name> mixedCase = MakeMixedCaseNames(kCount);
    RunCase("mixed_case_hit", mixedCase, L"Report");
    RunCase("mixed_case_miss", mixedCase, L"zzzz_never_present");
    RunCase("unicode", unicode, L"Résumé");
    RunCase("unicode_mixed", unicode, L"REPORT_ФАЙЛ");
    return 0;
}
