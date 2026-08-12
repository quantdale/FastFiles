#include "ffui/SnapshotAggregate.h"
#include "ffui/SnapshotPathLookup.h"

#include <cstdlib>
#include <initializer_list>
#include <map>

#include "../TestSupport.h"

using namespace fftest;

namespace {

ffprotocol::SnapshotDirectory Success(std::initializer_list<ffprotocol::SnapshotDirectoryEntry> entries) {
    ffprotocol::SnapshotDirectory result;
    result.status = ffprotocol::DirectoryEnumerationStatus::Success;
    result.entries.assign(entries);
    return result;
}

ffprotocol::SnapshotDirectoryEntry File(const wchar_t* name, uint64_t size) {
    ffprotocol::SnapshotDirectoryEntry entry;
    entry.name = name;
    entry.sizeBytes = size;
    return entry;
}

ffprotocol::SnapshotDirectoryEntry Folder(const wchar_t* name) {
    ffprotocol::SnapshotDirectoryEntry entry;
    entry.name = name;
    entry.isDirectory = true;
    return entry;
}

void TestCompleteKnownSubtree() {
    std::map<std::wstring, ffprotocol::SnapshotDirectory> snapshot;
    snapshot[L"C:\\"] = Success({Folder(L"A"), File(L"root.bin", 10)});
    snapshot[L"C:\\A"] = Success({Folder(L"B"), File(L"a.bin", 5)});
    snapshot[L"C:\\A\\B"] = Success({File(L"b.bin", 7)});

    const auto result = ffui::AggregateSnapshotDirectory(snapshot, L"C:\\");
    Check(result.complete, "fully browsed snapshot subtree is complete");
    Check(result.itemCount == 5 && result.totalSizeBytes == 22,
          "complete snapshot aggregate includes nested entries and file bytes");
}

void TestMissingDescendantIsPartial() {
    std::map<std::wstring, ffprotocol::SnapshotDirectory> snapshot;
    snapshot[L"C:\\"] = Success({Folder(L"A"), File(L"root.bin", 10)});

    const auto result = ffui::AggregateSnapshotDirectory(snapshot, L"C:\\");
    Check(!result.complete, "missing descendant is never reported as complete");
    Check(result.totalSizeBytes == 10,
          "partial aggregate retains known file bytes as a lower bound");
}

void TestCaseVariantDescendantIsTheSameWindowsDirectory() {
    std::map<std::wstring, ffprotocol::SnapshotDirectory> snapshot;
    snapshot[L"C:\\"] = Success({Folder(L"Fixture")});
    snapshot[L"c:\\fixture"] = Success({File(L"live.txt", 19)});

    const auto result = ffui::AggregateSnapshotDirectory(snapshot, L"C:\\");
    Check(result.complete && result.itemCount == 2 && result.totalSizeBytes == 19,
          "snapshot aggregation resolves case-variant Windows directory keys");
}

void TestUnavailableRoot() {
    std::map<std::wstring, ffprotocol::SnapshotDirectory> snapshot;
    snapshot[L"C:\\"].status = ffprotocol::DirectoryEnumerationStatus::AccessDenied;
    const auto result = ffui::AggregateSnapshotDirectory(snapshot, L"C:\\");
    Check(!result.complete && result.itemCount == 0 && result.totalSizeBytes == 0,
          "unavailable directory produces an unknown zero aggregate");
}

void TestPendingSelectionPathIdentity() {
    Check(ffui::MatchesPendingSnapshotPath(L"C:\\Fixture", L"c:\\fixture"),
          "pending selection accepts case-variant Windows parent paths");
    Check(ffui::MatchesPendingSnapshotPath(L"C:\\Fixture", L""),
          "pending selection without a parent constraint remains applicable");
    Check(!ffui::MatchesPendingSnapshotPath(L"C:\\Fixture", L"C:\\Other"),
          "pending selection rejects a different Windows parent path");
}

} // namespace

int main() {
    TestCompleteKnownSubtree();
    TestMissingDescendantIsPartial();
    TestCaseVariantDescendantIsTheSameWindowsDirectory();
    TestUnavailableRoot();
    TestPendingSelectionPathIdentity();
    return FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
