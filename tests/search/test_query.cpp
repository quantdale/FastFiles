#include <cstdio>

#include <windows.h>

#include "ffsearch/Query.h"
#include "../TestSupport.h"

using namespace fftest;

static int TestDateBoundariesAndDiagnostics(const ffsearch::FilterRegistry& registry);


int main() {
    const auto registry = ffsearch::FilterRegistry::WithDefaults();
    static const ffsearch::Candidate report{L"quarterly report.pdf", L"C:\\Program Files\\Reports", 2 * 1024 * 1024, 0, false};
    static const ffsearch::CandidateView reportView{u"quarterly report.pdf", report.folder, report.sizeBytes,
                                             report.modifiedTime, report.isDirectory, report.createdTime,
                                             report.volumeId, report.id, report.parentId, report.persistentIndexed};

    auto quoted = ffsearch::ParseQuery(L"folder:\"Program Files\" ext:.PDF quarterly", registry);
    Check(quoted.Matches(report), "quoted filter values and extension matching are ANDed");
    Check(quoted.viewCompatible && quoted.MatchesView(reportView),
          "projection-backed view matching agrees with owning candidate matching");
    Check(ffsearch::ParseQuery(L"*.pdf", registry).Matches(report), "asterisk wildcard matches filename");
    Check(ffsearch::ParseQuery(L"quarterly?report.pdf", registry).Matches(report), "question-mark wildcard matches one character");
    Check(ffsearch::ParseQuery(L"quarterly report", registry).Matches(report), "unquoted whitespace creates ANDed terms");
    Check(ffsearch::ParseQuery(L"size:>1MB", registry).Matches(report), "size greater-than supports binary units");
    Check(ffsearch::ParseQuery(L"size:1MB..3MB", registry).Matches(report), "inclusive size range matches");
    Check(ffsearch::ParseQuery(L"size:>=2MB size:<=2MB size:=2097152B", registry).Matches(report),
          "size equality and inclusive comparison operators support explicit units");
    Check(ffsearch::ParseQuery(L"size:<3MB", registry).Matches(report), "size less-than comparison matches");
    Check(!ffsearch::ParseQuery(L"size:wat", registry).invalidFilters.empty(), "invalid size value is reported");
    Check(!ffsearch::ParseQuery(L"size:18446744073709551616GB", registry).invalidFilters.empty(), "overflowing size is rejected");
    Check(ffsearch::ParseQuery(L"modified:today", registry).invalidFilters.empty(), "today is a valid modified-date value");
    Check(ffsearch::ParseQuery(L"modified:>7d", registry).invalidFilters.empty(), "relative modified-date offsets are valid");
    Check(ffsearch::ParseQuery(L"modified:2024-01-01", registry).invalidFilters.empty(), "absolute modified dates are valid");
    Check(ffsearch::ParseQuery(L"modified:2024-01-01..2024-12-31", registry).invalidFilters.empty(), "modified-date ranges are valid");
    Check(!ffsearch::ParseQuery(L"modified:not-a-date", registry).invalidFilters.empty(), "invalid modified value is reported");
    Check(!ffsearch::ParseQuery(L"modified:>d", registry).invalidFilters.empty(), "empty relative modified offset is rejected");
    Check(!ffsearch::ParseQuery(L"kind:spreadsheet", registry).invalidFilters.empty(), "unknown kind category is rejected");
    Check(!ffsearch::ParseQuery(L"ext:", registry).invalidFilters.empty(), "empty extension is rejected");
    Check(!ffsearch::ParseQuery(L"name:", registry).invalidFilters.empty(), "empty name filter is rejected");
    Check(!ffsearch::ParseQuery(L"folder:\"\"", registry).invalidFilters.empty(), "empty quoted folder filter is rejected");
    auto unknown = ffsearch::ParseQuery(L"owner:alice", registry);
    Check(unknown.unrecognizedKeys.size() == 1 && unknown.unrecognizedKeys.front() == L"owner", "unknown filter key is reported");
    Check(!unknown.Matches(report), "unknown filter is retained as literal text");
    Check(ffsearch::ParseQuery(L"kind:document", registry).Matches(report), "minimal document kind category matches");
    for (const std::wstring queryText : {L"*.pdf", L"quarterly?report.pdf", L"quarterly report",
                                          L"size:>1MB", L"modified:>7d", L"kind:document",
                                          L"folder:Reports"}) {
        const auto parsed = ffsearch::ParseQuery(queryText, registry);
        Check(parsed.viewCompatible && parsed.Matches(report) == parsed.MatchesView(reportView),
              "view matcher preserves the owning matcher result for a built-in predicate");
    }
    std::wstring emojiName = L"file_";
    emojiName.push_back(static_cast<wchar_t>(0xD83D));
    emojiName.push_back(static_cast<wchar_t>(0xDE00));
    emojiName += L".txt";
    static const ffsearch::Candidate emoji{emojiName, L"C:\\emoji", 1, 0, false};
    static const ffsearch::CandidateView emojiView{u"file_\U0001F600.txt", emoji.folder, emoji.sizeBytes,
                                            emoji.modifiedTime, emoji.isDirectory, emoji.createdTime,
                                            emoji.volumeId, emoji.id, emoji.parentId, emoji.persistentIndexed};
    const auto emojiQuery = ffsearch::ParseQuery(L"file_?.txt", registry);
    Check(emojiQuery.Matches(emoji) == emojiQuery.MatchesView(emojiView),
          "view glob matching preserves surrogate-pair semantics");
    const auto simpleLiteral = ffsearch::ParseQuery(L"REPORT", registry);
    Check(simpleLiteral.simpleLiteralName && simpleLiteral.MatchesView(reportView),
          "simple literal queries use the prepared projection matcher");
    static const ffsearch::Candidate upperAscii{L"Report.TXT", L"C:\\ascii", 1, 0, false};
    static const ffsearch::CandidateView upperAsciiView{u"Report.TXT", upperAscii.folder, upperAscii.sizeBytes,
                                                 upperAscii.modifiedTime, upperAscii.isDirectory,
                                                 upperAscii.createdTime, upperAscii.volumeId, upperAscii.id,
                                                 upperAscii.parentId, upperAscii.persistentIndexed};
    const auto shortLiteral = ffsearch::ParseQuery(L"ph", registry);
    const auto bmhLiteral = ffsearch::ParseQuery(L"phot", registry);
    Check(shortLiteral.Matches(upperAscii) == bmhLiteral.Matches(upperAscii),
          "short and BMH-range needles agree on the same mixed-case candidate");
    Check(simpleLiteral.Matches(upperAscii) == simpleLiteral.MatchesView(upperAsciiView) &&
              simpleLiteral.MatchesView(upperAsciiView),
          "uppercase ASCII names use the ordinal fallback without false negatives");
    static const ffsearch::Candidate unicode{L"\u00C5ngstr\u00F6m-report.txt", L"C:\\unicode", 1, 0, false};
    static const ffsearch::CandidateView unicodeView{u"\u00C5ngstr\u00F6m-report.txt", unicode.folder, unicode.sizeBytes,
                                              unicode.modifiedTime, unicode.isDirectory, unicode.createdTime,
                                              unicode.volumeId, unicode.id, unicode.parentId, unicode.persistentIndexed};
    const auto unicodeQuery = ffsearch::ParseQuery(L"\u00E5NGSTR\u00D6M", registry);
    Check(unicodeQuery.Matches(unicode) == unicodeQuery.MatchesView(unicodeView) && unicodeQuery.MatchesView(unicodeView),
          "non-ASCII literal matching preserves ordinal case-insensitive fallback semantics");
    auto extended = ffsearch::FilterRegistry::WithDefaults();
    extended.Register(L"owner", {[](std::wstring_view value) -> std::optional<ffsearch::FilterValue> { return std::wstring(value); }, [](const ffsearch::FilterValue&) { return [](const ffsearch::Candidate&) { return true; }; }});
    Check(ffsearch::ParseQuery(L"ext:pdf", extended).Matches(report), "registering a new key leaves existing filters unchanged");
    return TestDateBoundariesAndDiagnostics(registry);
}

// Split from main so neither frame trips /analyze C6262 (16 KiB stack budget).
static int TestDateBoundariesAndDiagnostics(const ffsearch::FilterRegistry& registry) {
    // Date filters use LOCAL day boundaries (matching Explorer timestamps),
    // never UTC-day arithmetic.
    FILETIME nowFt{};
    GetSystemTimeAsFileTime(&nowFt);
    FILETIME localFt{};
    SYSTEMTIME localNow{};
    if (!FileTimeToLocalFileTime(&nowFt, &localFt) || !FileTimeToSystemTime(&localFt, &localNow)) {
        Check(false, "local-time conversion is available for date-boundary tests");
        return fftest::FailureCount() == 0 ? 0 : 1;
    }
    SYSTEMTIME midnight = localNow;
    midnight.wHour = 0;
    midnight.wMinute = 0;
    midnight.wSecond = 0;
    midnight.wMilliseconds = 0;
    FILETIME midnightFt{};
    if (!SystemTimeToFileTime(&midnight, &midnightFt)) {
        Check(false, "local midnight conversion is available for date-boundary tests");
        return fftest::FailureCount() == 0 ? 0 : 1;
    }
    ULARGE_INTEGER midnightTicks{};
    midnightTicks.LowPart = midnightFt.dwLowDateTime;
    midnightTicks.HighPart = midnightFt.dwHighDateTime;
    constexpr uint64_t kDay = 24ULL * 60 * 60 * 10'000'000;
    static const ffsearch::Candidate noonToday{L"today.txt", L"C:\\dates", 1,
                                        midnightTicks.QuadPart + kDay / 2, false};
    static const ffsearch::Candidate lastTickYesterday{L"yesterday.txt", L"C:\\dates", 1,
                                                midnightTicks.QuadPart - 1, false};
    static const ffsearch::Candidate sixDaysAgo{L"six.txt", L"C:\\dates", 1,
                                         midnightTicks.QuadPart - 6 * kDay, false};
    static const ffsearch::Candidate eightDaysAgo{L"eight.txt", L"C:\\dates", 1,
                                           midnightTicks.QuadPart - 8 * kDay, false};
    const auto today = ffsearch::ParseQuery(L"modified:today", registry);
    Check(today.Matches(noonToday), "modified:today matches a file stamped this local afternoon");
    Check(!today.Matches(lastTickYesterday), "modified:today excludes the last tick of yesterday local time");
    const auto lastTwoDays = ffsearch::ParseQuery(L"modified:>2d", registry);
    Check(lastTwoDays.Matches(noonToday) && lastTwoDays.Matches(lastTickYesterday),
          "modified:>2d covers the current and previous local day");
    const auto lastWeek = ffsearch::ParseQuery(L"modified:>7d", registry);
    Check(lastWeek.Matches(sixDaysAgo), "modified:>7d includes six local days ago");
    Check(!lastWeek.Matches(eightDaysAgo), "modified:>7d excludes eight local days ago");

    // Invalid filter tokens are reported (not silently dropped) while the
    // valid remainder of the query still executes.
    auto mixedInvalid = ffsearch::ParseQuery(L"size:abc report", registry);
    Check(mixedInvalid.invalidFilters.size() == 1 && mixedInvalid.invalidFilters.front() == L"size",
          "an invalid size value is surfaced as an invalid filter key");
    static const ffsearch::Candidate plainReport{L"report.pdf", L"C:\\docs", 1, 0, false};
    Check(mixedInvalid.Matches(plainReport), "the valid remainder of a query with an invalid filter still executes");
    auto mixedUnknown = ffsearch::ParseQuery(L"owner:alice budget", registry);
    Check(mixedUnknown.unrecognizedKeys.size() == 1 && mixedUnknown.unrecognizedKeys.front() == L"owner",
          "unknown filter keys are surfaced alongside normal execution");
    return fftest::FailureCount() == 0 ? 0 : 1;
}