// Seeded indexed-mode acceptance (memory-bounded-indexed-scale 6.x).
//
// Deterministic, test-only proof that the indexed architecture ABOVE the
// privileged acquisition boundary works as an integrated system:
// Store -> IndexPipeline -> Projection/cold index -> bounded export ->
// snapshot serialization/parsing -> indexed-search protocol responses.
// Synthetic MFT/USN-equivalent records populate the real durable store
// through the real ingestion path. No Authenticode verification is touched,
// no unsigned production mode exists, and no production MFT/USN evidence is
// fabricated -- privileged acquisition itself stays externally gated.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <filesystem>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "IndexPipeline.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

using namespace ffengine;
using namespace ffindexstore;

std::u16string U16(const wchar_t* format, ...) {
    wchar_t buffer[64];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, _TRUNCATE, format, args);
    va_end(args);
    std::u16string out;
    for (const wchar_t ch : std::wstring(buffer)) out.push_back(static_cast<char16_t>(ch));
    return out;
}

ffprotocol::MftRecordV1 Mft(uint64_t id, uint64_t parent, std::u16string name,
                            uint32_t attrs, uint64_t size) {
    ffprotocol::MftRecordV1 r{};
    r.fixed.fileReferenceNumber = id;
    r.fixed.parentFileReferenceNumber = parent;
    r.fixed.fileAttributes = attrs;
    r.fixed.sizeBytes = size;
    r.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
    r.fileName = std::move(name);
    return r;
}

std::vector<std::tuple<std::wstring, std::wstring, uint64_t>> Signature(
    const ffprotocol::IndexedSearchResponse& response) {
    std::vector<std::tuple<std::wstring, std::wstring, uint64_t>> out;
    out.reserve(response.candidates.size());
    for (const auto& candidate : response.candidates) {
        out.emplace_back(candidate.name, candidate.folder, candidate.id);
    }
    return out;
}

ffprotocol::IndexedSearchRequest Query(uint64_t requestId, const std::wstring& text,
                                       int sortField = 0, size_t maxResults = 4096) {
    ffprotocol::IndexedSearchRequest request;
    request.requestId = requestId;
    request.scope = 3; // AllIndexedLocations
    request.sortField = static_cast<uint8_t>(sortField);
    request.maxResults = static_cast<uint32_t>(maxResults);
    request.query = text;
    return request;
}

} // namespace

int main() {
    // Seeded fixture layout (FRN -> meaning):
    //   5 root "C:" | 10 docs | 11 media | 12 docs\nested
    //   1000..1299 docs\doc-N.txt        (N = frn - 1000)
    //   5000..5199 media\clip-N.mp4      (N = frn - 5000)
    //   700  docs\nested\<unicode>-unique-report.txt
    // Journal wave: 1000 renames to renamed-doc.txt (size 999); 5000 deletes.
    const std::string dbPath = FreshDbPath("engine_test_seeded_indexed_mode.db");
    IndexPipeline pipeline;
    Check(pipeline.Open(dbPath), "the seeded pipeline opens");
    VolumeKey key;
    key.serialNumber = 1337;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    pipeline.SetVolumeRules(volume, setting);

    std::vector<ffprotocol::MftRecordV1> scan;
    scan.push_back(Mft(5, 5, u"C:", 0x10, 0));
    scan.push_back(Mft(10, 5, u"docs", 0x10, 0));
    scan.push_back(Mft(11, 5, u"media", 0x10, 0));
    scan.push_back(Mft(12, 10, u"nested", 0x10, 0));
    for (uint64_t index = 0; index < 300; ++index) {
        scan.push_back(Mft(1000 + index, 10,
                           U16(L"doc-%llu.txt", static_cast<unsigned long long>(index)),
                           0, 100 + index));
    }
    for (uint64_t index = 0; index < 200; ++index) {
        scan.push_back(Mft(5000 + index, 11,
                           U16(L"clip-%llu.mp4", static_cast<unsigned long long>(index)),
                           0, 10'000 + index));
    }
    scan.push_back(Mft(700, 12, u"\x00e7\x00f6\x03b1-unique-report.txt", 0, 777));
    Check(pipeline.ApplyMftBatch(volume, scan), "seeded scan batch commits durably");

    std::vector<ffprotocol::UsnDeltaV2> journal;
    ffprotocol::UsnDeltaV2 renameDelta{};
    renameDelta.fixed.fileReferenceNumber = 1000;
    renameDelta.fixed.parentFileReferenceNumber = 10;
    renameDelta.fixed.reason = 0x00000100; // rename-new-name upsert
    renameDelta.fixed.fieldMask = ffprotocol::kUsnFieldName | ffprotocol::kUsnFieldSizeBytes;
    renameDelta.fixed.sizeBytes = 999;
    renameDelta.fixed.fileNameLengthChars = 15;
    renameDelta.fileName = u"renamed-doc.txt";
    journal.push_back(renameDelta);
    ffprotocol::UsnDeltaV2 removeDelta{};
    removeDelta.fixed.fileReferenceNumber = 5000;
    removeDelta.fixed.parentFileReferenceNumber = 11;
    removeDelta.fixed.reason = 0x00000200; // USN_REASON_FILE_DELETE
    removeDelta.fixed.fileNameLengthChars = 13;
    removeDelta.fileName = u"clip-0.mp4";
    journal.push_back(removeDelta);
    Check(pipeline.ApplyUsnBatch(volume, journal), "seeded journal wave applies");

    // --- Restart from SQLite exactly like a fresh engine process. ---
    pipeline.Close();
    IndexPipeline restarted;
    Check(restarted.Open(dbPath), "seeded pipeline reopens from SQLite");
    Check(restarted.ResolveVolume(key) == volume, "restart preserves the durable volume id");
    restarted.SetVolumeRules(volume, setting);
    int rebuilt = 0;
    restarted.RebuildAll([&rebuilt](VolumeRowId) { ++rebuilt; });
    Check(rebuilt == 1, "restart rebuilds exactly the persisted volume");

    // --- Indexed navigation data: bounded export -> serialize -> parse. ---
    auto exported = restarted.ExportDirectorySnapshotBounded(volume, L"C:", 2);
    Check(exported.size() == 2 && exported.begin()->first == L"C:",
          "bounded navigation export honors the working-set bound top-of-tree first");
    auto fullExport = restarted.ExportDirectorySnapshot(volume, L"C:");
    Check(fullExport.size() == 4, "complete export covers the whole seeded tree structure");
    auto serialized = ffprotocol::SerializeSnapshot(fullExport);
    Check(!serialized.empty(), "snapshot serialization succeeds");
    auto parsed = ffprotocol::ParseSnapshot(serialized.data(), serialized.size());
    Check(parsed.has_value() && parsed->size() == fullExport.size(),
          "snapshot parsing round-trips the published generation");
    if (parsed.has_value()) {
        bool sawSeededClip = false;
        bool sawDeletedClip = false;
        for (const auto& entry : parsed->at(L"C:\\media").entries) {
            sawSeededClip |= entry.name == L"clip-1.mp4";
            sawDeletedClip |= entry.name == L"clip-0.mp4";
            if (entry.name == L"clip-1.mp4") {
                Check(entry.sizeBytes == 10'001 && !entry.allocatedSizeKnown,
                      "published entries carry durable logical size and honest allocated state");
            }
        }
        Check(sawSeededClip && !sawDeletedClip,
              "the published media listing reflects journal mutations");
    }

    // --- Search over the seeded index (post-restart, fully resident). ---
    auto selective = restarted.SearchIndexed(Query(1, L"unique-report"));
    Check(selective.status == ffprotocol::IndexedSearchStatus::Completed
              && selective.candidates.size() == 1
              && selective.candidates[0].folder == L"C:\\docs\\nested"
              && selective.candidates[0].sizeBytes == 777,
          "a selective query reconstructs the deep seeded folder");
    auto renamed = restarted.SearchIndexed(Query(2, L"renamed-doc"));
    Check(renamed.status == ffprotocol::IndexedSearchStatus::Completed
              && renamed.candidates.size() == 1
              && renamed.candidates[0].sizeBytes == 999,
          "journal rename updates are visible to indexed search");
    auto deletedQuery = restarted.SearchIndexed(Query(3, L"clip-0.mp4"));
    Check(deletedQuery.status == ffprotocol::IndexedSearchStatus::Completed
              && deletedQuery.candidates.empty(),
          "journal deletions disappear from indexed search");

    IndexPipeline::SearchTimings timings;
    auto trigram = restarted.SearchIndexed(Query(4, L"doc-2"), &timings);
    Check(trigram.status == ffprotocol::IndexedSearchStatus::Completed
              && trigram.candidates.size() == 111
              && timings.filenameIndexUsed,
          "an eligible literal uses the trigram accelerator and returns every match");
    auto unicode = restarted.SearchIndexed(Query(5, L"\x03b1-unique"));
    Check(unicode.status == ffprotocol::IndexedSearchStatus::Completed
              && unicode.candidates.size() == 1,
          "unicode filenames match through canonical semantics");
    auto folderPredicate = restarted.SearchIndexed(Query(6, L"FOLDER:MEDIA ext:mp4"));
    Check(folderPredicate.status == ffprotocol::IndexedSearchStatus::Completed
              && folderPredicate.candidates.size() == 199,
          "structured predicates traverse the canonical fallback path");
    auto capped = restarted.SearchIndexed(Query(7, L"ext:txt", 1, 25));
    Check(capped.status == ffprotocol::IndexedSearchStatus::Completed
              && capped.candidates.size() == 25 && capped.truncated,
          "capped queries report honest truncation with sorted top-N");

    // --- Aggregates: resident vs evicted equality from the durable CTE. ---
    // docs subtree after the journal wave: 299 doc files + renamed-doc (999,
    // the renamed former doc-0) + nested dir + unique-report (777).
    const FileId docsFrn{10, 0};
    const uint64_t expectedDocFiles = 300;
    uint64_t expectedDocsSize = 0;
    for (uint64_t index = 1; index < 300; ++index) expectedDocsSize += 100 + index;
    expectedDocsSize += 999 + 777;
    const auto hotAggregate = restarted.GetFolderAggregate(volume, docsFrn);
    Check(hotAggregate.has_value()
              && hotAggregate->itemCount == expectedDocFiles + 2
              && hotAggregate->totalSizeBytes == expectedDocsSize,
          "resident aggregate sums the seeded subtree exactly");

    restarted.UnloadVolumeFromProjection(volume);
    Check(restarted.IsVolumeEvicted(volume), "the seeded volume unloads deterministically");
    const auto coldAggregate = restarted.GetFolderAggregate(volume, docsFrn);
    Check(coldAggregate.has_value() && coldAggregate->itemCount == hotAggregate->itemCount
              && coldAggregate->totalSizeBytes == hotAggregate->totalSizeBytes,
          "cold aggregate equals the resident aggregate without any reload");
    Check(restarted.IsVolumeEvicted(volume),
          "answering aggregates never hydrates the unloaded volume");

    // --- Cold search equivalence against resident references. ---
    auto coldSelective = restarted.SearchIndexed(Query(8, L"unique-report"));
    Check(Signature(coldSelective) == Signature(selective),
          "cold selective search matches the resident result exactly");
    auto coldTrigram = restarted.SearchIndexed(Query(9, L"doc-2"), &timings);
    Check(coldTrigram.status == ffprotocol::IndexedSearchStatus::Completed
              && timings.filenameIndexUsed == false
              && coldTrigram.candidates.size() == trigram.candidates.size(),
          "cold canonical search bypasses the accelerator and finds every row");

    // --- Unavailable volume honesty (spec: queryable while marked). ---
    Check(restarted.SetVolumeAvailable(volume, false, 1), "fixture volume marks unavailable");
    auto offline = restarted.SearchIndexed(Query(10, L"unique-report"));
    Check(offline.status == ffprotocol::IndexedSearchStatus::Completed
              && offline.candidates.size() == 1,
          "an unavailable volume stays queryable (retention-without-corruption)");
    Check(restarted.SetVolumeAvailable(volume, true, 2), "fixture volume returns online");

    // --- Mutations after eviction remain immediately observable. ---
    Check(restarted.ApplyMftBatch(volume, {Mft(8000, 11, u"cold-insert.bin", 0, 42)}),
          "post-eviction insert commits durably");
    auto coldInsert = restarted.SearchIndexed(Query(11, L"cold-insert"));
    Check(coldInsert.status == ffprotocol::IndexedSearchStatus::Completed
              && coldInsert.candidates.size() == 1
              && coldInsert.candidates[0].folder == L"C:\\media",
          "cold search observes post-eviction inserts with reconstructed folders");

    // --- Cancellation during cold work. ---
    std::atomic<size_t> checks{0};
    auto cancelledRun = restarted.SearchIndexed(Query(12, L"doc"), nullptr,
                                                [&checks] { return checks.fetch_add(1) >= 3; });
    Check(cancelledRun.status == ffprotocol::IndexedSearchStatus::Cancelled,
          "cancelling mid-cold-stream reports Cancelled");

    // --- Reconciliation deletion: clip-1 (frn 5001) is never re-observed. ---
    restarted.BeginReconciliationPass(volume);
    std::vector<ffprotocol::MftRecordV1> groundTruth;
    groundTruth.push_back(Mft(5, 5, u"C:", 0x10, 0));
    groundTruth.push_back(Mft(10, 5, u"docs", 0x10, 0));
    groundTruth.push_back(Mft(11, 5, u"media", 0x10, 0));
    groundTruth.push_back(Mft(12, 10, u"nested", 0x10, 0));
    for (uint64_t index = 1; index < 300; ++index) {
        groundTruth.push_back(Mft(1000 + index, 10,
                                  U16(L"doc-%llu.txt", static_cast<unsigned long long>(index)),
                                  0, 100 + index));
    }
    groundTruth.push_back(Mft(1000, 10, u"renamed-doc.txt", 0, 999)); // renamed row survives
    for (uint64_t index = 2; index < 200; ++index) { // clip-1 intentionally unobserved
        groundTruth.push_back(Mft(5000 + index, 11,
                                  U16(L"clip-%llu.mp4", static_cast<unsigned long long>(index)),
                                  0, 10'000 + index));
    }
    groundTruth.push_back(Mft(700, 12, u"\x00e7\x00f6\x03b1-unique-report.txt", 0, 777));
    groundTruth.push_back(Mft(8000, 11, u"cold-insert.bin", 0, 42));
    Check(restarted.ApplyMftBatch(volume, groundTruth),
          "reconciliation ground-truth re-applies surviving rows");
    restarted.FinishReconciliationPass(volume);

    // --- Deterministic terminal state. ---
    auto finalDocs = restarted.SearchIndexed(Query(21, L"ext:txt"));
    Check(finalDocs.status == ffprotocol::IndexedSearchStatus::Completed
              && finalDocs.candidates.size() == 301, // doc-1..299 + renamed-doc + unique-report
          "terminal txt population matches the reconciled model");
    auto finalMedia = restarted.SearchIndexed(Query(22, L"ext:mp4"));
    Check(finalMedia.status == ffprotocol::IndexedSearchStatus::Completed
              && finalMedia.candidates.size() == 198, // clip-2..199
          "terminal mp4 population matches the reconciled model");
    auto finalGone = restarted.SearchIndexed(Query(23, L"clip-1.mp4"));
    Check(finalGone.status == ffprotocol::IndexedSearchStatus::Completed
              && finalGone.candidates.empty(),
          "reconciliation removed the unobserved row from searchable state");
    auto finalRenamed = restarted.SearchIndexed(Query(24, L"renamed-doc"));
    Check(finalRenamed.candidates.size() == 1,
          "reconciliation keeps explicitly re-applied renamed rows searchable");
    auto finalColdInsert = restarted.SearchIndexed(Query(25, L"cold-insert"));
    Check(finalColdInsert.candidates.size() == 1,
          "terminal state retains post-eviction inserts");

    if (FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", FailureCount());
        return 1;
    }
    std::printf("All seeded indexed-mode acceptance tests passed.\n");
    return 0;
}
