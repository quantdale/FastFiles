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
    const auto scope = ffui::NormalizeStorageRankingScope(L"\\\\?\\c:/Root/");
    Check(scope && scope->indexedSupported && scope->matchPath == L"c:\\Root", "extended drive and trailing separator normalize for matching only");
    Check(ffui::NormalizeStorageRankingScope(L"C:\\")->matchPath == L"C:\\", "absolute volume root keeps its separator");
    const auto unc = ffui::NormalizeStorageRankingScope(L"\\\\?\\unc\\server\\share\\folder\\");
    Check(unc && !unc->indexedSupported && unc->matchPath == L"\\\\server\\share\\folder", "UNC supports published fallback without claiming durable UNC indexing");
    Check(!ffui::NormalizeStorageRankingScope(L"C:relative") && !ffui::NormalizeStorageRankingScope(L"C:\\Root\\..\\Other") &&
          !ffui::NormalizeStorageRankingScope(L"\\\\server"), "relative, dot-component and incomplete UNC scopes are rejected");
    Check(ffui::StorageRankingContains(*scope, L"C:\\ROOT\\Nested") && !ffui::StorageRankingContains(*scope, L"C:\\Root-other") &&
          !ffui::StorageRankingContains(*scope, L"D:\\Root"), "scope is ordinal case-insensitive and component/volume bounded");
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
    Check(ffui::StorageRankingStatusText(2, false, false).find(L"partial coverage") != std::wstring::npos &&
          ffui::StorageRankingStatusText(2, true, false).find(L"coverage unverified") != std::wstring::npos,
          "both data routes describe their actual coverage limits");
    return FailureCount() == 0 ? 0 : 1;
}
