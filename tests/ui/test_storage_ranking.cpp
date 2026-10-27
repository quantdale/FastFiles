#include "ffui/StorageRanking.h"
#include "../TestSupport.h"

#include <limits>

using namespace fftest;

namespace {
ffprotocol::SnapshotDirectoryEntry File(std::wstring name, uint64_t size) {
    ffprotocol::SnapshotDirectoryEntry file;
    file.name = std::move(name);
    file.sizeBytes = size;
    return file;
}
}

int main() {
    for (const float width : {224.0f, 240.0f, 276.0f, 586.0f, 1059.0f}) {
        const auto columns = ffui::ComputeStorageRankingColumnWidths(width);
        Check(columns.name >= 96.0f && columns.size >= 64.0f && columns.location >= 64.0f &&
              std::abs(columns.name + columns.size + columns.location - width) < 0.01f,
              "name, size and Location fit together at narrow, normal and wide widths");
        for (const float scale : {1.0f, 1.25f, 2.0f}) {
            const int physicalWidth = static_cast<int>(std::lround(width * scale));
            const int name = static_cast<int>(std::lround(columns.name * scale));
            const int size = static_cast<int>(std::lround(columns.size * scale));
            Check(physicalWidth - name - size >= static_cast<int>(std::lround(64.0f * scale)),
                  "physical Location remains readable after96/120/192DPI rounding");
        }
    }
    const auto invalid = ffui::ComputeStorageRankingColumnWidths(std::numeric_limits<float>::quiet_NaN());
    Check(invalid.name == 96.0f && invalid.size == 64.0f && invalid.location == 64.0f,
          "unavailable or invalid geometry retains finite readable minima");
    const auto scope = ffui::NormalizeStorageRankingScope(L"\\\\?\\c:/Root/");
    Check(scope && scope->indexedSupported && scope->matchPath == L"c:\\Root", "extended drive and trailing separator normalize for matching only");
    Check(ffui::NormalizeStorageRankingScope(L"C:\\")->matchPath == L"C:\\", "absolute volume root keeps its separator");
    const auto unc = ffui::NormalizeStorageRankingScope(L"\\\\?\\unc\\server\\share\\folder\\");
    Check(unc && !unc->indexedSupported && unc->matchPath == L"\\\\server\\share\\folder", "UNC supports published fallback without claiming durable UNC indexing");
    Check(!ffui::NormalizeStorageRankingScope(L"C:relative") && !ffui::NormalizeStorageRankingScope(L"C:\\Root\\..\\Other") &&
          !ffui::NormalizeStorageRankingScope(L"\\\\server"), "relative, dot-component and incomplete UNC scopes are rejected");
    Check(ffui::StorageRankingContains(*scope, L"C:\\ROOT\\Nested") && !ffui::StorageRankingContains(*scope, L"C:\\Root-other") &&
          !ffui::StorageRankingContains(*scope, L"D:\\Root"), "scope is ordinal case-insensitive and component/volume bounded");
    Check(ffui::StorageRankingLocationText(L"C:\\Root", L"C:\\Root") == L"This folder" &&
          ffui::StorageRankingLocationText(L"c:\\root\\", L"\\\\?\\C:\\ROOT") == L"This folder",
          "same-folder location stays concise across case, trailing separator and extended spelling");
    Check(ffui::StorageRankingLocationText(L"C:\\Root", L"C:\\Root\\Nested") == L"Nested" &&
          ffui::StorageRankingLocationText(L"\\\\?\\c:\\root", L"C:\\Root\\First\\Second\\Third") == L"First\\Second\\Third" &&
          ffui::StorageRankingLocationText(L"C:\\", L"C:\\Nested") == L"Nested",
          "descendant location shows the actual relative folder chain including drive-root scopes");
    Check(ffui::StorageRankingLocationText(L"C:\\Root", L"C:\\Root-other\\Nested") == L"C:\\Root-other\\Nested" &&
          ffui::StorageRankingLocationText(L"C:\\Root", L"\\\\?\\D:\\Other") == L"\\\\?\\D:\\Other" &&
          ffui::StorageRankingLocationText(L"relative", L"original\\relative") == L"original\\relative",
          "same-prefix outside, other-volume and unsupported scopes retain their full original location");
    Check(ffui::StorageRankingLocationText(L"\\\\server\\share\\", L"\\\\?\\UNC\\server\\share\\Nested") == L"Nested",
          "known UNC descendant display uses the same lexical matching normalization");
    Check(ffui::StorageRankingItemPath(L"\\\\?\\C:\\Root\\Nested", L"same.bin") == L"\\\\?\\C:\\Root\\Nested\\same.bin",
          "operation target retains original namespace and containing folder");
    Check(ffui::StorageRankingItemPath(L"C:\\Root", L"..\\outside.bin").empty() &&
          ffui::StorageRankingItemPath(L"C:\\Root", L"stream:ads").empty(), "malformed leaves cannot manufacture an operation path");

    std::map<std::wstring, ffprotocol::SnapshotDirectory> snapshot;
    auto& root = snapshot[L"C:\\Root"];
    root.status = ffprotocol::DirectoryEnumerationStatus::Success;
    for (uint64_t index = 0; index < 125; ++index) root.entries.push_back(File(L"file-" + std::to_wstring(index), index));
    auto directory = File(L"not-a-file", std::numeric_limits<uint64_t>::max());
    directory.isDirectory = true;
    root.entries.push_back(directory);
    auto& nested = snapshot[L"\\\\?\\C:\\Root\\Nested"];
    nested.status = ffprotocol::DirectoryEnumerationStatus::Success;
    auto large = File(L"same.bin", 1'000'000);
    large.allocatedSizeBytes = 0;
    large.allocatedSizeKnown = true;
    nested.entries.push_back(large);
    snapshot[L"C:\\Root-other"].entries.push_back(File(L"same.bin", 9'000'000));
    auto& denied = snapshot[L"C:\\Root\\Denied"];
    denied.status = ffprotocol::DirectoryEnumerationStatus::AccessDenied;
    denied.entries.push_back(File(L"hidden.bin", 8'000'000));
    const auto ranked = ffui::RankKnownStorageSnapshot(snapshot, *scope);
    Check(!ranked.cancelled && ranked.truncated && ranked.results.size() == ffui::kStorageRankingLimit,
          "bounded result records truncation after matching file count exceeds100");
    if (ranked.results.size() != ffui::kStorageRankingLimit) return 1;
    Check(ranked.results.front().candidate.name == L"same.bin" && ranked.results.front().candidate.sizeBytes == 1'000'000 &&
          ranked.results.back().candidate.sizeBytes == 26, "later nested file wins true top100 rather than enumeration prefix");
    const auto& winner = ranked.results.front().candidate;
    Check(winner.folder == L"\\\\?\\C:\\Root\\Nested" && winner.allocatedSizeKnown && winner.allocatedSizeBytes == 0 &&
          ffui::StorageRankingItemPath(winner.folder, winner.name) == L"\\\\?\\C:\\Root\\Nested\\same.bin",
          "recursive row preserves exact target and known-zero allocation independently of logical size");
    Check(std::none_of(ranked.results.begin(), ranked.results.end(), [](const auto& row) {
        return row.candidate.isDirectory || row.candidate.name == L"hidden.bin" || row.candidate.sizeBytes == 9'000'000;
    }), "directories, denied listings and same-prefix sibling files are excluded before admission");

    snapshot.clear();
    snapshot[L"C:\\Root"].status = ffprotocol::DirectoryEnumerationStatus::Success;
    snapshot[L"C:\\Root\\Nested"].status = ffprotocol::DirectoryEnumerationStatus::Success;
    snapshot[L"C:\\Root"].entries.push_back(File(L"same.bin", 0));
    snapshot[L"C:\\Root\\Nested"].entries.push_back(File(L"same.bin", 10));
    const auto small = ffui::RankKnownStorageSnapshot(snapshot, *scope);
    Check(small.results.size() == 2, "two successful directory listings contribute two known files");
    if (small.results.size() != 2) return 1;
    Check(small.results.size() == 2 && !small.truncated && small.results.back().candidate.sizeBytes == 0 &&
          !small.results.back().candidate.allocatedSizeKnown, "zero-byte files survive and unknown allocation stays unknown");
    Check(ffui::StorageRankingItemPath(small.results.front().candidate.folder, L"same.bin") !=
          ffui::StorageRankingItemPath(small.results.back().candidate.folder, L"same.bin"), "duplicate basenames retain distinct containing-folder identities");
    size_t probes = 0;
    const auto cancelled = ffui::RankKnownStorageSnapshot(snapshot, *scope, [&probes] { return ++probes >= 2; });
    Check(cancelled.cancelled && cancelled.results.empty(), "snapshot traversal cancellation publishes no partial result set");
    Check(ffui::StorageRankingCompletionMatches(2, 2, L"C:\\ROOT\\", *scope, true, true) &&
          !ffui::StorageRankingCompletionMatches(1, 2, L"C:\\Root", *scope, true, true) &&
          !ffui::StorageRankingCompletionMatches(2, 2, L"C:\\Other", *scope, true, true) &&
          !ffui::StorageRankingCompletionMatches(2, 2, L"C:\\Root", *scope, false, true) &&
          !ffui::StorageRankingCompletionMatches(2, 2, L"C:\\Root", *scope, true, false),
          "actual completion gate rejects old generation, changed scope, hidden and replaced presentation");
    Check(ffui::StorageRankingStatusText(2, false, false).find(L"Partial coverage") == 0 &&
          ffui::StorageRankingStatusText(2, true, false).find(L"Coverage unverified") == 0,
          "both data routes describe their actual coverage limits");
    const auto completedScope = ffui::NormalizeStorageRankingScope(L"C:\\RankScope");
    Check(completedScope.has_value(), "completed display scope fixture is absolute");
    if (completedScope) {
        const std::optional<ffui::StorageRankingDisplayIdentity> display{{*completedScope, 7, false}};
        Check(ffui::StorageRankingDisplayCanRefresh(display, L"c:/rankscope/", 7, true, true),
              "same normalized completed display survives a replacement request generation");
        Check(!ffui::StorageRankingDisplayCanRefresh(display, L"C:\\RankScope-other", 7, true, true) &&
              !ffui::StorageRankingDisplayCanRefresh(display, L"C:\\RankScope\\Nested", 7, true, true) &&
              !ffui::StorageRankingDisplayCanRefresh(display, L"C:\\RankScope", 8, true, true) &&
              !ffui::StorageRankingDisplayCanRefresh(display, L"C:\\RankScope", 7, false, true) &&
              !ffui::StorageRankingDisplayCanRefresh(display, L"C:\\RankScope", 7, true, false) &&
              !ffui::StorageRankingDisplayCanRefresh(std::nullopt, L"C:\\RankScope", 7, true, true),
              "scope source hidden mode/category and absent-display transitions invalidate retained rows");
        Check(std::wstring(ffui::StorageRankingRefreshingStatusText(false)).starts_with(L"Partial coverage") &&
              std::wstring(ffui::StorageRankingRefreshingStatusText(true)).starts_with(L"Coverage unverified"),
              "previous known rows retain explicit coverage limits during refresh");
    }
    return FailureCount() == 0 ? 0 : 1;
}
