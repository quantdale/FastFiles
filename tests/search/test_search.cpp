#include "ffsearch/Search.h"
#include "ffsearch/History.h"

#include <cstdio>
#include <chrono>
#include <filesystem>
#include <windows.h>
#include "../TestSupport.h"

using namespace fftest;


int main() {
    const auto registry = ffsearch::FilterRegistry::WithDefaults();
    std::vector<ffsearch::Candidate> entries{
        {L"report", L"C:\\work", 10, 30, false, 20, 1, 2, 1},
        {L"report_final.docx", L"C:\\work", 20, 20, false, 10, 1, 3, 1},
        {L"annual_report.docx", L"C:\\work\\nested", 30, 10, false, 30, 1, 4, 2},
        {L"report_other.txt", L"D:\\elsewhere", 40, 40, false, 40, 2, 5, 5},
    };
    ffsearch::SearchRequest request{ffsearch::ParseQuery(L"report", registry),
                                    ffsearch::SearchScope::CurrentFolder, L"C:\\work"};
    auto response = ffsearch::ExecuteSearch(entries, request);
    Check(response.results.size() == 2, "current-folder scope excludes nested and other drives");
    Check(response.results[0].tier == ffsearch::MatchTier::Exact && response.results[0].candidate.name == L"report",
          "exact match ranks before prefix");

    request.scope = ffsearch::SearchScope::CurrentFolderAndSubfolders;
    response = ffsearch::ExecuteSearch(entries, request);
    Check(response.results.size() == 3, "subfolder scope includes nested entries");
    Check(response.results[1].candidate.name == L"report_final.docx", "prefix ranks before mid-name substring");

    request.scope = ffsearch::SearchScope::CurrentDrive;
    response = ffsearch::ExecuteSearch(entries, request);
    Check(response.results.size() == 3, "current-drive scope excludes other volumes");
    request.scope = ffsearch::SearchScope::AllIndexedLocations;
    response = ffsearch::ExecuteSearch(entries, request);
    Check(response.results.size() == 4, "all-indexed scope spans volumes");

    request.sortField = ffsearch::SortField::Size;
    request.descending = true;
    response = ffsearch::ExecuteSearch(entries, request);
    Check(response.results.front().candidate.sizeBytes == 40, "explicit descending size sort works");
    request.sortField = ffsearch::SortField::Modified;
    response = ffsearch::ExecuteSearch(entries, request);
    Check(response.results.front().candidate.modifiedTime == 40, "explicit descending modified sort works");
    request.sortField = ffsearch::SortField::Created;
    request.descending = false;
    response = ffsearch::ExecuteSearch(entries, request);
    Check(response.results.front().candidate.createdTime == 10, "explicit ascending created sort works");

    request.chunkSize = 1;
    int checks = 0;
    response = ffsearch::ExecuteSearch(entries, request, [&] { return ++checks >= 2; });
    Check(response.cancelled && response.results.empty() && response.candidatesVisited == 1,
          "chunk cancellation discards stale partial results");

    Check(ffsearch::OrdinalContains(L"xxInvoiceyy", L"invoice"), "ordinal substring matches middle case-insensitively");
    std::wstring surrogateName = L"file_";
    surrogateName.push_back(static_cast<wchar_t>(0xD83D));
    surrogateName.push_back(static_cast<wchar_t>(0xDE00));
    surrogateName += L".txt";
    Check(ffsearch::GlobMatches(surrogateName, L"file_?.txt"), "question wildcard consumes one surrogate pair");
    Check(!ffsearch::GlobMatches(surrogateName, L"file_??.txt"), "wildcard never splits surrogate pairs");
    const std::vector<ffsearch::Candidate> unicodeEntries{
        {L"client's report (v2).docx", L"C:\\資料", 1, 0, false},
        {L"résumé final.txt", L"C:\\café", 1, 0, false},
        {L"данные_日本語.txt", L"C:\\mixed scripts", 1, 0, false},
        {surrogateName, L"C:\\emoji", 1, 0, false},
    };
    ffsearch::SearchRequest unicodeRequest{ffsearch::ParseQuery(L"name:\"client's report (v2).docx\"", registry)};
    Check(ffsearch::ExecuteSearch(unicodeEntries, unicodeRequest).results.size() == 1,
          "quoted punctuation and spaces match literally");
    const LCID originalLocale = GetThreadLocale();
    unicodeRequest.query = ffsearch::ParseQuery(L"FINAL.TXT", registry);
    SetThreadLocale(MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT));
    const size_t englishCount = ffsearch::ExecuteSearch(unicodeEntries, unicodeRequest).results.size();
    SetThreadLocale(MAKELCID(MAKELANGID(LANG_TURKISH, SUBLANG_DEFAULT), SORT_DEFAULT));
    const size_t turkishCount = ffsearch::ExecuteSearch(unicodeEntries, unicodeRequest).results.size();
    SetThreadLocale(originalLocale);
    Check(englishCount == 1 && turkishCount == englishCount, "ordinal matching is locale independent");

    std::vector<ffsearch::Candidate> largeIndex;
    largeIndex.reserve(300000);
    for (size_t index = 0; index < 300000; ++index) {
        std::wstring name = L"item_" + std::to_wstring(index) + L".txt";
        if (index % 10000 == 0) name += L"_targetneedle";
        largeIndex.push_back({std::move(name), L"C:\\synthetic", index, index, false});
    }
    ffsearch::SearchRequest largeRequest{ffsearch::ParseQuery(L"targetneedle", registry)};
    const auto started = std::chrono::steady_clock::now();
    const auto largeResponse = ffsearch::ExecuteSearch(largeIndex, largeRequest);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    Check(largeResponse.results.size() == 30, "large synthetic index returns complete results");
    Check(elapsed < std::chrono::seconds(15), "hundreds-of-thousands scan remains responsive");
    largeRequest.chunkSize = 512;
    int largeCancellationChecks = 0;
    const auto cancelledLarge = ffsearch::ExecuteSearch(largeIndex, largeRequest, [&] { return ++largeCancellationChecks >= 2; });
    Check(cancelledLarge.cancelled && cancelledLarge.candidatesVisited == 512,
          "large in-flight scan cancels at the next chunk and discards results");

    std::vector<ffsearch::Candidate> hierarchy{
        {L"C:\\", L"", 0, 0, true, 0, 1, 10, 10},
        {L"work", L"C:\\", 0, 0, true, 0, 1, 11, 10},
        {L"nested", L"C:\\work", 0, 0, true, 0, 1, 12, 11},
        {L"file.txt", L"C:\\work\\nested", 1, 0, false, 0, 1, 13, 12},
    };
    const auto path = ffsearch::ReconstructPath(hierarchy, 3);
    Check(path.complete && path.segments.size() == 4 && path.segments.back() == L"file.txt",
          "deep parent-id chain reconstructs root-to-entry");
    hierarchy.erase(hierarchy.begin() + 2);
    const auto partial = ffsearch::ReconstructPath(hierarchy, 2);
    Check(!partial.complete && partial.unresolvedParentId == 12 && partial.segments.back() == L"file.txt",
          "broken parent chain returns partial resolution and stop id");

    const auto historyPath = std::filesystem::temp_directory_path() / L"fastfiles-search-tests" / L"history.txt";
    ffsearch::SearchHistory history;
    Check(history.Record(L"first query", historyPath) && history.Record(L"second query", historyPath),
          "search history records locally");
    ffsearch::SearchHistory reloaded;
    Check(reloaded.Load(historyPath) && reloaded.Queries().size() == 2 && reloaded.Queries().front() == L"second query",
          "search history persists and recalls newest first");
    Check(reloaded.Timestamps().size() == 2 && reloaded.Timestamps().front() != 0,
          "search history persists execution timestamps");
    reloaded.SetRecordingEnabled(false);
    Check(reloaded.Record(L"disabled query", historyPath) && reloaded.Queries().size() == 2,
          "disabled recording preserves existing entries without adding new ones");
    Check(reloaded.Clear(historyPath) && reloaded.Queries().empty() && !std::filesystem::exists(historyPath),
          "clear history removes entries and persisted file");

    // Non-ASCII round-trip (workstream E): the history file must persist
    // wide characters as UTF-8, not truncate them to one byte via the
    // default-C-locale wofstream path.
    const auto unicodeHistoryPath = std::filesystem::temp_directory_path() / L"fastfiles-search-tests" / L"history-unicode.txt";
    ffsearch::SearchHistory unicodeHistory;
    Check(unicodeHistory.Record(L"résumé データ", unicodeHistoryPath)
              && unicodeHistory.Record(L"café", unicodeHistoryPath),
          "unicode history records locally");
    ffsearch::SearchHistory unicodeReloaded;
    Check(unicodeReloaded.Load(unicodeHistoryPath) && unicodeReloaded.Queries().size() == 2
              && unicodeReloaded.Queries().front() == L"café"
              && unicodeReloaded.Queries().back() == L"résumé データ",
          "non-ASCII history round-trips through UTF-8 without truncation");

    std::error_code cleanupError;
    std::filesystem::remove(historyPath.parent_path(), cleanupError);
    return fftest::FailureCount() == 0 ? 0 : 1;
}
