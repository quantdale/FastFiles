// semantic-treemap-uia 33.J: headless tests for the treemap semantic
// metadata helper (BuildTreemapItemMetadata, TreemapFormatSize,
// TreemapNodeIdentityHash). Pure header-only: no COM, no window, no renderer.

#include "ffui/TreemapSemantics.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../TestSupport.h"

using namespace fftest;

namespace {

void TestFormatSize() {
    Check(ffui::TreemapFormatSize(0) == L"0 B", "0 bytes formats as '0 B'");
    Check(ffui::TreemapFormatSize(512) == L"512 B", "512 B formats correctly");
    Check(ffui::TreemapFormatSize(1023) == L"1023 B", "1023 B formats correctly");
    Check(ffui::TreemapFormatSize(1024) == L"1.0 KB", "1 KB formats correctly");
    Check(ffui::TreemapFormatSize(1536) == L"1.5 KB", "1.5 KB formats correctly");
    Check(ffui::TreemapFormatSize(1024ULL * 1024) == L"1.0 MB", "1 MB formats correctly");
    Check(ffui::TreemapFormatSize(1024ULL * 1024 * 1024) == L"1.0 GB", "1 GB formats correctly");
    Check(ffui::TreemapFormatSize(2048ULL * 1024 * 1024) == L"2.0 GB", "2 GB formats correctly");
}

void TestNodeIdentityHash() {
    // Indexed identity: nonzero volumeRowId + fileIdLow -> FRN-based hash.
    ffui::TreemapNode indexed;
    indexed.volumeRowId = 1;
    indexed.fileIdLow = 42;
    indexed.fileIdHigh = 0;
    indexed.path = L"C:\\test\\file.txt";
    const uint64_t h1 = ffui::TreemapNodeIdentityHash(indexed, L"C:\\test");
    const uint64_t h2 = ffui::TreemapNodeIdentityHash(indexed, L"C:\\other");
    Check(h1 != 0, "indexed identity is non-zero");
    Check(h1 == h2, "indexed identity is path-independent (FRN-based)");
    (void)h2;

    // Degraded identity: zero volumeRowId -> path-based hash.
    ffui::TreemapNode degraded;
    degraded.path = L"C:\\test\\file.txt";
    const uint64_t d1 = ffui::TreemapNodeIdentityHash(degraded, L"C:\\test");
    const uint64_t d2 = ffui::TreemapNodeIdentityHash(degraded, L"C:\\other");
    Check(d1 != 0, "degraded identity is non-zero");
    Check(d1 == d2, "degraded identity is path-based (scope-independent)");
    Check(h1 != d1, "indexed and degraded identities differ");

    // Synthetic catch-all: deterministic per scope.
    ffui::TreemapNode synthetic;
    synthetic.synthetic = true;
    synthetic.name = ffui::kOtherSmallItemsName;
    const uint64_t s1 = ffui::TreemapNodeIdentityHash(synthetic, L"C:\\test");
    const uint64_t s2 = ffui::TreemapNodeIdentityHash(synthetic, L"C:\\test");
    const uint64_t s3 = ffui::TreemapNodeIdentityHash(synthetic, L"C:\\other");
    Check(s1 == s2, "synthetic identity is deterministic");
    Check(s1 != s3, "synthetic identity is scope-dependent");
    Check(s1 != h1, "synthetic identity differs from indexed");

    // Two different paths -> different degraded identities.
    ffui::TreemapNode nodeA;
    nodeA.path = L"C:\\a.txt";
    ffui::TreemapNode nodeB;
    nodeB.path = L"C:\\b.txt";
    Check(ffui::TreemapNodeIdentityHash(nodeA, L"C:") != ffui::TreemapNodeIdentityHash(nodeB, L"C:"),
          "different paths yield different identities");
}

void TestBuildMetadataNormalFile() {
    ffui::TreemapNode node;
    node.name = L"test.txt";
    node.path = L"C:\\dir\\test.txt";
    node.isDirectory = false;
    node.sizeBytes = 4096;
    node.totalSizeBytes = 4096;
    node.allocatedSizeBytes = 8192;
    node.allocatedSizeKnown = true;
    node.volumeRowId = 1;
    node.fileIdLow = 100;

    ffui::TreemapVolumeInfo vol;
    vol.usedBytes = 100000;
    vol.known = true;

    auto info = ffui::BuildTreemapItemMetadata(node, 8192, true, vol, L"C:\\dir",
                                               1, ffui::UiaHashWide(L"C:\\dir"),
                                               0.0f, 0.0f, 50.0f, 100.0f, false);
    Check(info.name == L"test.txt", "file name preserved");
    Check(info.fullPath == L"C:\\dir\\test.txt", "full path preserved");
    Check(!info.isDirectory, "file is not directory");
    Check(!info.synthetic, "file is not synthetic");
    Check(info.invokable, "file is invokable");
    Check(info.logicalSizeBytes == 4096, "logical size correct");
    Check(info.logicalSizeKnown, "logical size known for file");
    Check(info.allocatedSizeBytes == 8192, "allocated size correct");
    Check(info.allocatedSizeKnown, "allocated size known for file");
    Check(!info.allocatedPartial, "file is not partial aggregate");
    Check(info.itemType == L"File", "item type is File");
    Check(info.itemStatus.empty(), "no status for normal file");
    Check(info.percentOfParent > 0.0, "percent of parent is computed");
    Check(info.percentOfVolume > 0.0, "percent of volume is computed");
    Check(!info.calculating, "not calculating");
    Check(!info.aggregateFailed, "not failed");

    // FullDescription contains key facts.
    Check(info.fullDescription.find(L"test.txt") != std::wstring::npos, "FullDescription contains name");
    Check(info.fullDescription.find(L"C:\\dir\\test.txt") != std::wstring::npos, "FullDescription contains path");
    Check(info.fullDescription.find(L"File") != std::wstring::npos, "FullDescription contains type");
    Check(info.fullDescription.find(L"% of parent") != std::wstring::npos, "FullDescription has pct parent");
    Check(info.fullDescription.find(L"% of volume") != std::wstring::npos, "FullDescription has pct volume");

    // HoverLines: [truncated path, sizeLine, diskLine, pctLine]
    Check(info.hoverLines.size() == 4, "hover has 4 lines");
    Check(info.hoverLines[1].find(L"Size:") != std::wstring::npos, "hover line 1 has size");
    Check(info.hoverLines[2].find(L"Size on disk:") != std::wstring::npos, "hover line 2 has disk size");
    Check(info.hoverLines[3].find(L"% of parent") != std::wstring::npos, "hover line 3 has pct parent");
}

void TestBuildMetadataNormalFolder() {
    ffui::TreemapNode node;
    node.name = L"FolderA";
    node.path = L"C:\\dir\\FolderA";
    node.isDirectory = true;
    node.totalSizeBytes = 10000;
    node.totalAllocatedSizeBytes = 20000;
    node.allocatedKnownCount = 5;
    node.allocatedTotalCount = 5;

    ffui::TreemapVolumeInfo vol;
    vol.usedBytes = 200000;
    vol.known = true;

    auto info = ffui::BuildTreemapItemMetadata(node, 100000, true, vol, L"C:\\dir",
                                               1, ffui::UiaHashWide(L"C:\\dir"),
                                               0.0f, 0.0f, 30.0f, 60.0f, false);
    Check(info.itemType == L"Folder", "item type is Folder");
    Check(info.isDirectory, "is directory");
    Check(info.logicalSizeBytes == 10000, "folder logical size");
    Check(info.logicalSizeKnown, "folder logical size known (complete)");
    Check(info.allocatedSizeBytes == 20000, "folder allocated aggregate");
    Check(info.allocatedSizeKnown, "folder allocated known (5/5 complete)");
    Check(!info.allocatedPartial, "folder is not partial (5/5)");
    Check(info.invokable, "folder is invokable");
    Check(info.hoverLines.size() == 4, "hover has 4 lines");
}

void TestBuildMetadataCalculating() {
    ffui::TreemapNode node;
    node.name = L"FolderB";
    node.path = L"C:\\dir\\FolderB";
    node.isDirectory = true;
    node.calculating = true;
    node.totalSizeBytes = 5000; // partial known so far
    node.totalAllocatedSizeBytes = 0;
    node.allocatedKnownCount = 0;
    node.allocatedTotalCount = 2;

    ffui::TreemapVolumeInfo vol;
    vol.usedBytes = 200000;
    vol.known = true;

    auto info = ffui::BuildTreemapItemMetadata(node, 100000, true, vol, L"C:\\dir",
                                               2, ffui::UiaHashWide(L"C:\\dir"),
                                               0.0f, 0.0f, 30.0f, 60.0f, false);
    Check(info.calculating, "node is calculating");
    Check(!info.logicalSizeKnown, "logical size unknown while calculating");
    Check(info.itemStatus == L"Calculating\u2026", "item status is Calculating");
    Check(info.fullDescription.find(L"Calculating") != std::wstring::npos,
          "FullDescription mentions calculating");
    // Size line should include the partial lower bound or just "Calculating..."
    Check(info.hoverLines[1].find(L"Size:") != std::wstring::npos, "hover size line present");
    // Disk line: Size on disk: Unknown (allocatedTotalCount>0, allocatedKnownCount==0)
    Check(info.hoverLines[2].find(L"Unknown") != std::wstring::npos,
          "hover disk line says Unknown for calculating with no known allocations");
    // Percentages unknown: display dash
    Check(info.hoverLines[3].find(L"\u2014") != std::wstring::npos,
          "percentages show dash when unknown");
}

void TestBuildMetadataAllocatedUnknown() {
    ffui::TreemapNode node;
    node.name = L"secret.dat";
    node.path = L"C:\\dir\\secret.dat";
    node.isDirectory = false;
    node.sizeBytes = 1024;
    node.totalSizeBytes = 1024;
    node.allocatedSizeKnown = false;

    ffui::TreemapVolumeInfo vol;
    vol.usedBytes = 200000;
    vol.known = true;

    auto info = ffui::BuildTreemapItemMetadata(node, 100000, true, vol, L"C:\\dir",
                                               1, ffui::UiaHashWide(L"C:\\dir"),
                                               0.0f, 0.0f, 10.0f, 20.0f, false);
    Check(info.logicalSizeKnown, "logical size known for file");
    Check(!info.allocatedSizeKnown, "allocated size NOT known");
    // diskLine should say "Size on disk: Unknown", never "0 B"
    Check(info.hoverLines[2].find(L"Unknown") != std::wstring::npos,
          "hover disk line says Unknown for unknown allocated");
    Check(info.hoverLines[2].find(L"0 B") == std::wstring::npos,
          "hover disk line does not say '0 B' for unknown allocated");
}

void TestBuildMetadataPartialAllocated() {
    ffui::TreemapNode node;
    node.name = L"PartialDir";
    node.path = L"C:\\dir\\PartialDir";
    node.isDirectory = true;
    node.totalSizeBytes = 50000;
    node.totalAllocatedSizeBytes = 2000;
    node.allocatedKnownCount = 1;
    node.allocatedTotalCount = 3;

    ffui::TreemapVolumeInfo vol;
    vol.usedBytes = 200000;
    vol.known = true;

    auto info = ffui::BuildTreemapItemMetadata(node, 100000, true, vol, L"C:\\dir",
                                               1, ffui::UiaHashWide(L"C:\\dir"),
                                               0.0f, 0.0f, 30.0f, 60.0f, false);
    Check(info.allocatedPartial, "aggregate is partial (1/3)");
    Check(!info.allocatedSizeKnown, "allocated NOT fully known");
    Check(info.allocatedKnownCount == 1, "allocated known count correct");
    Check(info.allocatedTotalCount == 3, "allocated total count correct");
    // diskLine: >= X (1/3)
    Check(info.hoverLines[2].find(L"\u2265") != std::wstring::npos,
          "hover disk line has >= lower bound");
    Check(info.hoverLines[2].find(L"1/3") != std::wstring::npos,
          "hover disk line has completeness pair");
}

void TestBuildMetadataDivergence() {
    ffui::TreemapNode node;
    node.name = L"diverge.txt";
    node.path = L"C:\\dir\\diverge.txt";
    node.isDirectory = false;
    node.sizeBytes = 4096;
    node.totalSizeBytes = 4096;
    node.allocatedSizeBytes = 10240; // 2.5x larger
    node.allocatedSizeKnown = true;

    ffui::TreemapVolumeInfo vol;
    vol.usedBytes = 200000;
    vol.known = true;

    auto info = ffui::BuildTreemapItemMetadata(node, 100000, true, vol, L"C:\\dir",
                                               1, ffui::UiaHashWide(L"C:\\dir"),
                                               0.0f, 0.0f, 10.0f, 20.0f, false);
    // divergence: (10240-4096)=6144 >= 4096/2=2048 and >= 4096 -> callout present
    Check(info.hoverLines[2].find(L"differs") != std::wstring::npos,
          "disk line mentions divergence for 2.5x difference");
    Check(info.fullDescription.find(L"differs") != std::wstring::npos,
          "FullDescription mentions divergence");
}

void TestBuildMetadataNoDivergenceSmall() {
    ffui::TreemapNode node;
    node.name = L"tiny.txt";
    node.path = L"C:\\dir\\tiny.txt";
    node.isDirectory = false;
    node.sizeBytes = 100;
    node.totalSizeBytes = 100;
    node.allocatedSizeBytes = 150;
    node.allocatedSizeKnown = true;

    ffui::TreemapVolumeInfo vol;
    vol.usedBytes = 200000;
    vol.known = true;

    auto info = ffui::BuildTreemapItemMetadata(node, 100000, true, vol, L"C:\\dir",
                                               1, ffui::UiaHashWide(L"C:\\dir"),
                                               0.0f, 0.0f, 10.0f, 20.0f, false);
    // 50 < 4096 absolute floor: no divergence callout
    Check(info.hoverLines[2].find(L"differs") == std::wstring::npos,
          "no divergence callout below absolute floor");
}

void TestBuildMetadataZeroDenominators() {
    ffui::TreemapNode node;
    node.name = L"file.txt";
    node.path = L"C:\\file.txt";
    node.sizeBytes = 100;
    node.totalSizeBytes = 100;

    ffui::TreemapVolumeInfo vol;
    vol.usedBytes = 0;
    vol.known = false;

    auto info = ffui::BuildTreemapItemMetadata(node, 0, false, vol, L"C:\\",
                                               1, ffui::UiaHashWide(L"C:\\"),
                                               0.0f, 0.0f, 10.0f, 20.0f, false);
    Check(info.percentOfParent < 0.0, "percent of parent is unknown with zero denominator");
    Check(info.percentOfVolume < 0.0, "percent of volume is unknown with zero volume");
    Check(info.hoverLines[3].find(L"\u2014") != std::wstring::npos,
          "percent line shows dash for unknown percentages");
}

void TestBuildMetadataSyntheticCatchAll() {
    ffui::TreemapNode node;
    node.name = ffui::kOtherSmallItemsName;
    node.synthetic = true;
    node.aggregateItemCount = 42;
    node.totalSizeBytes = 200;

    ffui::TreemapVolumeInfo vol;
    vol.usedBytes = 100000;
    vol.known = true;

    auto info = ffui::BuildTreemapItemMetadata(node, 50000, true, vol, L"C:\\data",
                                               1, ffui::UiaHashWide(L"C:\\data"),
                                               0.0f, 0.0f, 5.0f, 5.0f, false);
    Check(info.synthetic, "tile is synthetic");
    Check(!info.invokable, "synthetic tile is NOT invokable");
    Check(info.name == ffui::kOtherSmallItemsName, "name is 'Other small items'");
    Check(info.fullPath.empty(), "synthetic has no filesystem path");
    Check(info.itemType == ffui::kOtherSmallItemsName, "item type matches");
    Check(info.aggregateItemCount == 42, "aggregate item count correct");

    // Identity is scope-dependent.
    const uint64_t id1 = info.identityHash;
    auto info2 = ffui::BuildTreemapItemMetadata(node, 50000, true, vol, L"C:\\other",
                                                1, ffui::UiaHashWide(L"C:\\other"),
                                                0.0f, 0.0f, 5.0f, 5.0f, false);
    Check(id1 != info2.identityHash, "synthetic identity differs per scope");

    // FullDescription has Aggregate line.
    Check(info.fullDescription.find(L"Aggregate: 42 items") != std::wstring::npos,
          "FullDescription contains aggregate count");
    Check(info.hoverLines[0].find(L"42") != std::wstring::npos,
          "hover line 0 mentions item count");
}

void TestBuildMetadataAggregateFailed() {
    ffui::TreemapNode node;
    node.name = L"FailedDir";
    node.path = L"C:\\FailedDir";
    node.isDirectory = true;
    node.aggregateFailed = true;
    node.totalSizeBytes = 0;

    ffui::TreemapVolumeInfo vol;
    vol.usedBytes = 100000;
    vol.known = true;

    auto info = ffui::BuildTreemapItemMetadata(node, 50000, true, vol, L"C:\\",
                                               1, ffui::UiaHashWide(L"C:\\"),
                                               0.0f, 0.0f, 30.0f, 60.0f, false);
    Check(info.aggregateFailed, "node aggregate failed");
    Check(!info.calculating, "not calculating (just failed)");
    Check(info.itemStatus == L"Partial", "item status is Partial");
    Check(!info.logicalSizeKnown, "logical size not known (failed)");
    Check(info.hoverLines[1].find(L"Partial") != std::wstring::npos,
          "hover size line says Partial");
}

void TestBuildMetadataResolvedEmptyFolder() {
    // A resolved empty folder has a KNOWN exact 0% share: zero is zero.
    ffui::TreemapNode node;
    node.name = L"EmptyDir";
    node.path = L"C:\\EmptyDir";
    node.isDirectory = true;
    node.totalSizeBytes = 0;
    node.totalAllocatedSizeBytes = 0;

    ffui::TreemapVolumeInfo vol;
    vol.usedBytes = 100000;
    vol.known = true;

    auto info = ffui::BuildTreemapItemMetadata(node, 50000, true, vol, L"C:\\",
                                               1, ffui::UiaHashWide(L"C:\\"),
                                               0.0f, 0.0f, 30.0f, 60.0f, false);
    Check(!info.calculating && !info.aggregateFailed, "resolved empty folder is neither pending nor failed");
    Check(info.percentOfParent == 0.0, "resolved empty folder percent of parent is exactly 0");
    Check(info.hoverLines[3].find(L"0.0%") != std::wstring::npos,
          "percent line shows 0.0% for a known-zero folder");
}

void TestBuildMetadataFailedWhileSiblingPending() {
    // Pending and failed are mutually exclusive states in the projection.
    ffui::TreemapNode pending;
    pending.name = L"PendingDir";
    pending.path = L"C:\\PendingDir";
    pending.isDirectory = true;
    pending.calculating = true;
    Check(ffui::BuildTreemapItemMetadata(pending, 100, true, {}, L"C:", 1, 1,
                                         0.0f, 0.0f, 10.0f, 10.0f, false)
                  .itemStatus == L"Calculating\u2026",
          "pending node projects Calculating status");

    ffui::TreemapNode failed;
    failed.name = L"FailedDir";
    failed.path = L"C:\\FailedDir";
    failed.isDirectory = true;
    failed.aggregateFailed = true;
    const auto failedInfo = ffui::BuildTreemapItemMetadata(failed, 100, true, {}, L"C:", 1, 1,
                                                           0.0f, 0.0f, 10.0f, 10.0f, false);
    Check(failedInfo.itemStatus == L"Partial", "failed node projects Partial status");
    Check(!failedInfo.calculating, "failed node is never projected as Calculating");
}

// visual-matrix EX-009: degraded fallback resolution (ResolveDegradedTreemapTotals).
void TestResolveDegradedTreemapTotals() {
    // Indexed directories and files are untouched (engine path owns them).
    ffui::TreemapNode indexed;
    indexed.name = L"Indexed";
    indexed.isDirectory = true;
    indexed.calculating = true;
    indexed.volumeRowId = 7;
    indexed.fileIdLow = 9;
    ffui::SnapshotAggregate any{1, 100, 50, 1, 1, true, true};
    ffui::ResolveDegradedTreemapTotals(indexed, any);
    Check(indexed.calculating, "indexed dir keeps pending state (engine resolves it)");
    Check(indexed.totalSizeBytes == 0, "indexed dir totals untouched");

    ffui::TreemapNode file;
    file.name = L"f.bin";
    file.isDirectory = false;
    file.sizeBytes = 11;
    ffui::ResolveDegradedTreemapTotals(file, any);
    Check(file.totalSizeBytes == 0, "file totals untouched by dir fallback");

    // Degraded dir + complete snapshot subtree -> exact, resolved.
    ffui::TreemapNode degraded;
    degraded.name = L"Sub";
    degraded.isDirectory = true;
    degraded.calculating = true;
    degraded.volumeRowId = 0;
    ffui::SnapshotAggregate full{1, 400, 0, 0, 1, true, false};
    ffui::ResolveDegradedTreemapTotals(degraded, full);
    Check(!degraded.calculating, "complete degraded dir leaves calculating");
    Check(degraded.totalSizeBytes == 400, "complete degraded dir takes snapshot total");
    Check(degraded.aggregateResolved, "complete degraded dir marked resolved");
    Check(!degraded.aggregateFailed, "complete degraded dir not failed");

    // Degraded dir + missing descendants -> honest partial, never calculating.
    ffui::TreemapNode partial;
    partial.name = L"Partial";
    partial.isDirectory = true;
    partial.calculating = true;
    partial.volumeRowId = 0;
    ffui::SnapshotAggregate missing{0, 0, 0, 0, 0, false, false};
    ffui::ResolveDegradedTreemapTotals(partial, missing);
    Check(!partial.calculating, "partial degraded dir is never calculating");
    Check(!partial.aggregateResolved, "partial degraded dir not resolved");
    Check(partial.aggregateFailed, "partial degraded dir marked failed (Partial, not exact)");
    Check(partial.totalSizeBytes == 0, "partial degraded dir contributes zero, not a guess");
}

} // namespace

int main() {
    TestFormatSize();
    TestNodeIdentityHash();
    TestBuildMetadataNormalFile();
    TestBuildMetadataNormalFolder();
    TestBuildMetadataCalculating();
    TestBuildMetadataAllocatedUnknown();
    TestBuildMetadataPartialAllocated();
    TestBuildMetadataDivergence();
    TestBuildMetadataNoDivergenceSmall();
    TestBuildMetadataZeroDenominators();
    TestBuildMetadataSyntheticCatchAll();
    TestBuildMetadataAggregateFailed();
    TestBuildMetadataResolvedEmptyFolder();
    TestBuildMetadataFailedWhileSiblingPending();
    TestResolveDegradedTreemapTotals();

    if (FailureCount() != 0) {
        fprintf(stderr, "fftreemap_metadata_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("fftreemap_metadata_tests: all checks passed\n");
    return 0;
}
