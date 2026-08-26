#include <cstdio>

#include <windows.h>

#include "ffsearch/Query.h"
#include "ffsearch/Search.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

// Deterministic LCG so corpus equivalence never depends on library rand().
struct Lcg {
    uint64_t state = 0x9E3779B97F4A7C15ull;
    uint32_t Next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(state >> 33);
    }
};

std::wstring Widen(std::u16string_view value) {
    return std::wstring(value.begin(), value.end());
}

bool IsAsciiWide(std::wstring_view value) {
    for (const wchar_t ch : value) {
        if (ch > 0x7f) return false;
    }
    return true;
}

bool IsAsciiLowerWide(std::wstring_view value) {
    for (const wchar_t ch : value) {
        if (ch > 0x7f || (ch >= L'A' && ch <= L'Z')) return false;
    }
    return true;
}

int ExhaustiveSingleUnitEquivalence() {
    for (unsigned left = 0; left < 128; ++left) {
        for (unsigned right = 0; right < 128; ++right) {
            const wchar_t wl = static_cast<wchar_t>(left);
            const wchar_t wr = static_cast<wchar_t>(right);
            const bool apiEqual = CompareStringOrdinal(&wl, 1, &wr, 1, TRUE) == CSTR_EQUAL;
            const auto needle = ffsearch::PrepareAsciiFoldNeedle(std::wstring(1, wr));
            const std::wstring haystack(1, wl);
            if (apiEqual != ffsearch::AsciiFoldContains(haystack, needle)) {
                Check(false, "fold matcher agrees with the ordinal API for every ASCII code-unit pair");
                return 1;
            }
            const auto equals = ffsearch::AsciiFoldEquals(haystack, std::wstring(1, wr));
            if (!needle.ascii || apiEqual != equals) {
                Check(false, "fold equality agrees with the ordinal API for every ASCII code-unit pair");
                return 1;
            }
        }
    }
    Check(true, "exhaustive 128x128 fold/ordinal equivalence holds");
    return fftest::FailureCount() == 0 ? 0 : 1;
}

int CorpusEquivalence(const ffsearch::FilterRegistry& registry) {
    // Alphabet mixes letter cases, digits, separators, and non-ASCII units so
    // both matcher paths (prepared fold scan and ordinal fallback) are hit.
    const wchar_t alphabet[] = L"aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ0123456789._-\xE9\xC9";
    Lcg rng;
    std::vector<std::wstring> names;
    names.reserve(512);
    names.emplace_back(L"");
    for (size_t index = 0; index < 511; ++index) {
        std::wstring value;
        const size_t length = 1 + rng.Next() % 24;
        value.reserve(length);
        for (size_t position = 0; position < length; ++position) {
            value.push_back(alphabet[rng.Next() % (std::size(alphabet) - 1)]);
        }
        names.push_back(std::move(value));
    }

    std::vector<std::wstring> terms;
    terms.reserve(160);
    for (const std::wstring_view fixed :
         {L"a", L"A", L"zZ", L"abc", L"AbC", L"photo", L"PHOTO", L"\xC9", L"\xE9\xC9",
          L"this_needle_is_longer_than_any_corpus_name_0123456789"}) {
        terms.emplace_back(fixed);
    }
    for (size_t index = 0; index < 150; ++index) {
        const std::wstring& source = names[rng.Next() % names.size()];
        if (source.empty()) continue;
        const size_t begin = rng.Next() % source.size();
        const size_t count = 1 + rng.Next() % source.size();
        std::wstring term = source.substr(begin, (std::min)(count, source.size() - begin));
        // Randomly flip ASCII letter case inside the sampled term.
        for (wchar_t& ch : term) {
            const uint32_t roll = rng.Next() % 4;
            if (roll == 0 && ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - (L'a' - L'A'));
            else if (roll == 1 && ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
        }
        terms.push_back(std::move(term));
    }

    size_t fallbackChecks = 0;
    for (const std::wstring& term : terms) {
        const auto query = ffsearch::ParseQuery(term, registry);
        Check(query.simpleLiteralName || !IsAsciiWide(term),
              "pure-ASCII single terms parse as simple literal queries");
        const auto needle = ffsearch::PrepareAsciiFoldNeedle(term);
        if (!needle.ascii) ++fallbackChecks;
        for (const std::wstring& name : names) {
            const bool expected = ffsearch::OrdinalContains(name, term);
            const ffsearch::Candidate candidate{name, L"C:\\", 1, 0, false};
            if (query.Matches(candidate) != expected) {
                Check(false, "candidate-path literal matching equals OrdinalContains across the randomized corpus");
                return 1;
            }
            const std::u16string narrowName(name.begin(), name.end());
            const ffsearch::CandidateView view{narrowName, candidate.folder, candidate.sizeBytes,
                                               candidate.modifiedTime, candidate.isDirectory,
                                               candidate.createdTime, candidate.volumeId, candidate.id,
                                               candidate.parentId, candidate.persistentIndexed,
                                               IsAsciiLowerWide(name), IsAsciiWide(name)};
            if (query.MatchesView(view) != expected) {
                Check(false, "view-path literal matching equals OrdinalContains across the randomized corpus");
                return 1;
            }
        }
    }
    Check(fallbackChecks > 0, "the corpus exercised the non-ASCII ordinal fallback path");
    return fftest::FailureCount() == 0 ? 0 : 1;
}

int MixedCaseEndToEnd(const ffsearch::FilterRegistry& registry) {
    const std::vector<ffsearch::Candidate> stock{
        {L"REPORT.DOCX", L"C:\\Users", 10, 20, false},
        {L"quarterly report.pdf", L"C:\\Program Files\\Reports", 20, 30, false},
        {L"MyPhotoAlbum", L"C:\\users\\michael ross", 30, 40, false},
        {L"r\xE9sum\xE9", L"C:\\unicode", 40, 50, false},
    };
    struct Case {
        const wchar_t* queryText;
        size_t expectedMatches;
        const char* description;
    };
    for (const Case& testCase : {Case{L"report", 2, "mixed-case term finds upper and lower case names"},
                                 Case{L"REPORT", 2, "uppercase term finds lower and upper case names"},
                                 Case{L"RePoRt", 2, "alternating-case term stays case-insensitive"},
                                 Case{L"photo", 1, "BMH-range term finds camel-case name"},
                                 Case{L"resume", 0, "ASCII term does not match accented lookalikes"},
                                 Case{L"\xE9", 1, "accented term still uses the ordinal path"}}) {
        const auto query = ffsearch::ParseQuery(testCase.queryText, registry);
        size_t matches = 0;
        for (const auto& candidate : stock) {
            if (query.Matches(candidate)) ++matches;
        }
        Check(matches == testCase.expectedMatches, testCase.description);
    }

    const auto folderFilter = ffsearch::ParseQuery(L"folder:\"Program Files\"", registry);
    const ffsearch::Candidate insideMixedCase{L"tool.exe", L"c:\\program files\\tools", 1, 0, false};
    const ffsearch::Candidate outside{L"tool.exe", L"C:\\tools", 1, 0, false};
    Check(folderFilter.Matches(insideMixedCase), "folder filter matches mixed-case ASCII paths");
    Check(!folderFilter.Matches(outside), "folder filter rejects unrelated paths");

    const ffsearch::Candidate exactTarget{L"Report.TXT", L"C:\\", 1, 0, false};
    Check(ffsearch::ClassifyMatch(exactTarget, L"report.txt") == ffsearch::MatchTier::Exact,
          "tier classification treats folded-equal names as exact");
    const ffsearch::Candidate prefixTarget{L"Reporting.dll", L"C:\\", 1, 0, false};
    Check(ffsearch::ClassifyMatch(prefixTarget, L"report") == ffsearch::MatchTier::Prefix,
          "tier classification recognizes case-insensitive prefixes");
    Check(ffsearch::ClassifyMatch(exactTarget, L"port.T") == ffsearch::MatchTier::Substring,
          "tier classification recognizes mid-name substrings");

    // Anchor-boundary regression: an anchor occurrence inside the final
    // anchorIndex+1 code units previously read past the haystack end while
    // comparing its window; these shapes pin the bounded behavior.
    const std::vector<ffsearch::Candidate> boundary{
        {L"aab", L"C:\\", 1, 0, false},
        {L"ab", L"C:\\", 1, 0, false},
        {L"cb", L"C:\\", 1, 0, false},
        {L"xabc", L"C:\\", 1, 0, false},
        {L"abc", L"C:\\", 1, 0, false},
    };
    const auto abcQuery = ffsearch::ParseQuery(L"abc", registry);
    size_t boundaryMatches = 0;
    for (const auto& candidate : boundary) {
        if (abcQuery.Matches(candidate)) ++boundaryMatches;
    }
    Check(boundaryMatches == 2, "anchor-boundary names classify without overreading or false positives");

    const auto bmhCaseQuery = ffsearch::ParseQuery(L"phot", registry);
    Check(bmhCaseQuery.simpleLiteralUseBmh, "four-character needles select the BMH scan");
    const ffsearch::Candidate shiftedUpper{L"pHoToS", L"C:\\", 1, 0, false};
    Check(bmhCaseQuery.Matches(shiftedUpper), "BMH bad-character shifts handle uppercase spellings");

    return fftest::FailureCount() == 0 ? 0 : 1;
}

} // namespace

int main() {
    const auto registry = ffsearch::FilterRegistry::WithDefaults();
    if (ExhaustiveSingleUnitEquivalence() != 0) return 1;
    if (CorpusEquivalence(registry) != 0) return 1;
    return MixedCaseEndToEnd(registry);
}
