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

// visual-matrix EX-009: the certification fixture shape (files plus one
// unbrowsed subdir) aggregates honestly -- files exact, subdir partial -- so
// the treemap fallback can resolve files while flagging the subdir Partial.
void TestFixtureShapeWithUnbrowsedSubdir() {
    std::map<std::wstring, ffprotocol::SnapshotDirectory> snapshot;
    snapshot[L"C:\\M"] = Success(
        {File(L"fileA.bin", 100), File(L"fileB.bin", 200), File(L"fileC.bin", 300), Folder(L"Sub")});

    const auto scope = ffui::AggregateSnapshotDirectory(snapshot, L"C:\\M");
    Check(!scope.complete, "unbrowsed subdir keeps the scope aggregate partial");
    Check(scope.totalSizeBytes == 600, "scope total covers only enumerated files");

    const auto sub = ffui::AggregateSnapshotDirectory(snapshot, L"C:\\M\\Sub");
    Check(!sub.complete && sub.totalSizeBytes == 0,
          "unbrowsed subdir aggregates to unknown zero, never a guess");

    // After the subdir is browsed, the same scope resolves exactly.
    snapshot[L"C:\\M\\Sub"] = Success({File(L"fileD.bin", 400)});
    const auto rescoped = ffui::AggregateSnapshotDirectory(snapshot, L"C:\\M");
    Check(rescoped.complete && rescoped.totalSizeBytes == 1000,
          "browsed subdir completes the scope aggregate exactly");
    const auto resub = ffui::AggregateSnapshotDirectory(snapshot, L"C:\\M\\Sub");
    Check(resub.complete && resub.totalSizeBytes == 400,
          "browsed subdir aggregates to its exact total");
}

// allocated-size-storage-accuracy 8.2/D8: degraded aggregation carries the
// allocated subtotal over known descendants plus the completeness pair;
// complete only when every descendant is known; unknown never becomes zero.
void TestAllocatedAggregateCompletenessAndUnknown() {
    ffprotocol::SnapshotDirectoryEntry known = File(L"known.bin", 10);
    known.allocatedSizeBytes = 8;
    known.allocatedSizeKnown = true;
    ffprotocol::SnapshotDirectoryEntry zero = File(L"empty.bin", 0);
    zero.allocatedSizeBytes = 0;
    zero.allocatedSizeKnown = true; // genuine zero-byte file
    ffprotocol::SnapshotDirectoryEntry unknown = File(L"unknown.bin", 20);
    // allocatedSizeKnown stays false -- degraded entries are unknown

    std::map<std::wstring, ffprotocol::SnapshotDirectory> snapshot;
    snapshot[L"C:\\"] = Success({Folder(L"sub"), known, zero, unknown});
    snapshot[L"C:\\sub"] = Success({File(L"nested.bin", 4)});

    const auto result = ffui::AggregateSnapshotDirectory(snapshot, L"C:\\");
    Check(result.complete, "fully browsed snapshot subtree is logically complete");
    Check(result.totalSizeBytes == 34, "logical total is unaffected by allocated knowledge");
    Check(result.allocatedBytesTotalCount == 5, "every descendant counts toward the allocated total");
    Check(result.allocatedBytesKnownCount == 2, "only known descendants contribute to the known count");
    Check(result.totalAllocatedSizeBytes == 8, "allocated subtotal covers only known descendants");
    Check(!result.allocatedComplete,
          "an aggregate with an unknown descendant is never allocated-complete");
    Check(known.allocatedSizeKnown && known.allocatedSizeBytes == 8,
          "a known zero-byte file stays distinct from an unknown one");

    // All-known subtree: exact allocated total, allocated-complete.
    std::map<std::wstring, ffprotocol::SnapshotDirectory> allKnown;
    allKnown[L"D:\\"] = Success({File(L"a.bin", 1), File(L"b.bin", 2)});
    auto& a = allKnown[L"D:\\"].entries[0];
    a.allocatedSizeBytes = 1;
    a.allocatedSizeKnown = true;
    auto& b = allKnown[L"D:\\"].entries[1];
    b.allocatedSizeBytes = 3;
    b.allocatedSizeKnown = true;
    const auto knownResult = ffui::AggregateSnapshotDirectory(allKnown, L"D:\\");
    Check(knownResult.allocatedComplete && knownResult.totalAllocatedSizeBytes == 4 &&
              knownResult.allocatedBytesKnownCount == 2 && knownResult.allocatedBytesTotalCount == 2,
          "an all-known subtree reports an exact, complete allocated total");
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
    TestFixtureShapeWithUnbrowsedSubdir();
    TestAllocatedAggregateCompletenessAndUnknown();
    TestCaseVariantDescendantIsTheSameWindowsDirectory();
    TestUnavailableRoot();
    TestPendingSelectionPathIdentity();
    return FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
