#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ffindexstore/Store.h"
#include "sqlite3.h"
#include "../../src/indexstore/src/FoldClassification.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

ffindexstore::EntryRecord MakeEntry(uint64_t id, uint64_t parent, std::u16string name,
                                    uint64_t size = 0) {
    ffindexstore::EntryRecord entry;
    entry.id = {id, 0};
    entry.parentId = {parent, 0};
    entry.name = std::move(name);
    entry.sizeBytes = size;
    return entry;
}

class Barrier {
public:
    void ArriveAndWait() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            arrived_ = true;
        }
        condition_.notify_all();
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return released_; });
    }

    bool WaitForArrival() {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(2), [this] { return arrived_; });
    }

    void Release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        condition_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool arrived_ = false;
    bool released_ = false;
};

void TestIndependentSnapshotOverlapsWriter() {
    ffindexstore::Store store;
    const std::string path = FreshDbPath("ffindexstore_read_session_overlap.db");
    Check(store.Open(path), "read-session overlap store opens");
    const auto volume = store.GetOrCreateVolume({{}, 404});
    Check(volume.has_value(), "read-session overlap volume is created");
    Check(store.ApplyBatch(*volume, {
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(5, 5, u".")},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(10, 5, u"Folder")},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(11, 10, u"before.txt", 4)},
    }), "read-session overlap seed commits");

    auto reader = store.OpenReadSession();
    Check(reader != nullptr && reader->IsOpen(), "independent read session opens");
    Barrier readerBarrier;
    std::vector<std::u16string> readerNames;
    std::mutex readerNamesMutex;
    std::atomic<bool> readerResult{false};
    std::thread readerThread([session = std::move(reader), &readerBarrier, &readerNames,
                              &readerNamesMutex, &readerResult, volume = *volume]() mutable {
        bool first = true;
        readerResult = session->ForEachEntryUntil(volume, [&](const ffindexstore::EntryRecord& entry) {
            if (first) {
                first = false;
                readerBarrier.ArriveAndWait();
            }
            {
                std::lock_guard<std::mutex> lock(readerNamesMutex);
                readerNames.push_back(entry.name);
            }
            return true;
        });
    });
    Check(readerBarrier.WaitForArrival(), "reader reaches a held WAL snapshot");

    std::atomic<bool> writerResult{false};
    std::mutex writerMutex;
    std::condition_variable writerCondition;
    bool writerFinished = false;
    std::thread writer([&] {
        const bool applied = store.ApplyBatch(*volume, {
            {ffindexstore::EntryChangeKind::Upsert, MakeEntry(12, 10, u"during.txt", 8)},
        });
        writerResult = applied;
        {
            std::lock_guard<std::mutex> lock(writerMutex);
            writerFinished = true;
        }
        writerCondition.notify_all();
    });
    {
        std::unique_lock<std::mutex> lock(writerMutex);
        writerFinished = writerCondition.wait_for(lock, std::chrono::seconds(2), [&] {
            return writerFinished;
        });
    }
    const bool writerFinishedWhileReaderHeld = writerFinished && writerResult.load();
    Check(store.CheckpointPassive(), "passive checkpoint remains safe with a held reader snapshot");
    readerBarrier.Release();
    readerThread.join();
    writer.join();
    Check(writerFinishedWhileReaderHeld, "writer commits while an independent reader is held");
    Check(readerResult.load(), "held reader completes without a SQLite error");

    bool sawDuringRow = false;
    {
        std::lock_guard<std::mutex> lock(readerNamesMutex);
        for (const auto& name : readerNames) {
            if (name == u"during.txt") sawDuringRow = true;
        }
    }
    Check(!sawDuringRow, "the held reader keeps its snapshot generation");

    auto freshReader = store.OpenReadSession();
    bool sawAfterRow = false;
    Check(freshReader != nullptr && freshReader->ForEachEntryUntil(*volume, [&](const auto& entry) {
        if (entry.name == u"during.txt") sawAfterRow = true;
        return true;
    }), "a fresh reader streams the committed row");
    Check(sawAfterRow, "a subsequent reader sees the writer commit");

    auto aggregateReader = store.OpenReadSession();
    const auto aggregate = aggregateReader != nullptr
        ? aggregateReader->GetFolderAggregate(*volume, {10, 0}) : ffindexstore::Store::FolderAggregate{};
    Check(aggregate.itemCount == 2 && aggregate.totalSizeBytes == 12,
          "an independent reader serves a durable folder aggregate");

    for (int iteration = 0; iteration < 3; ++iteration) {
        auto repeatedReader = store.OpenReadSession();
        Check(repeatedReader != nullptr && repeatedReader->IsOpen(),
              "repeated read-session acquisition succeeds");
        if (repeatedReader != nullptr) repeatedReader->Close();
        Check(repeatedReader == nullptr || !repeatedReader->IsOpen(),
              "repeated read-session teardown closes its transaction");
    }
}

void TestReadSessionFaultsFailClosed() {
    ffindexstore::Store store;
    Check(store.Open(FreshDbPath("ffindexstore_read_session_faults.db")),
          "read-session fault store opens");
    const auto volume = store.GetOrCreateVolume({{}, 405});
    Check(volume.has_value() && store.ApplyBatch(*volume, {
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(5, 5, u".")},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(6, 5, u"second.txt")},
    }), "read-session fault fixture commits");

    const auto openFault = store.OpenReadSession({[](const auto point) {
        return point == ffindexstore::ReadSessionFaultPoint::Open;
    }});
    Check(openFault == nullptr, "reader-open failure is rejected before a session escapes");

    const auto beginFault = store.OpenReadSession({[](const auto point) {
        return point == ffindexstore::ReadSessionFaultPoint::BeginSnapshot;
    }});
    Check(beginFault == nullptr, "snapshot-begin failure is rejected before a session escapes");

    const auto busyFault = store.OpenReadSession({[](const auto point) {
        return point == ffindexstore::ReadSessionFaultPoint::Busy;
    }});
    Check(busyFault == nullptr, "busy/locked reader failure is rejected fail-closed");

    auto streamFault = store.OpenReadSession({[](const auto point) {
        return point == ffindexstore::ReadSessionFaultPoint::Stream;
    }});
    Check(streamFault != nullptr && !streamFault->ForEachEntryUntil(*volume, [](const auto&) {
        return true;
    }), "stream failure is reported without a partial-success flag");

    int streamCalls = 0;
    auto midStreamFault = store.OpenReadSession({[&streamCalls](const auto point) {
        return point == ffindexstore::ReadSessionFaultPoint::Stream && ++streamCalls >= 2;
    }});
    Check(midStreamFault != nullptr && !midStreamFault->ForEachEntryUntil(*volume, [](const auto&) {
        return true;
    }), "a stream abort after rows have begun remains an incomplete read");

    auto pointFault = store.OpenReadSession({[](const auto point) {
        return point == ffindexstore::ReadSessionFaultPoint::FindEntry;
    }});
    Check(pointFault != nullptr && !pointFault->FindEntry(*volume, {5, 0}).has_value(),
          "point-lookup failure is reported without fabricating a row");

    auto aggregateFault = store.OpenReadSession({[](const auto point) {
        return point == ffindexstore::ReadSessionFaultPoint::Aggregate;
    }});
    Check(aggregateFault != nullptr && aggregateFault->GetFolderAggregate(*volume, {5, 0}).itemCount == 0,
          "aggregate failure returns no partial aggregate");

    auto visitorStop = store.OpenReadSession();
    int visitorCalls = 0;
    Check(visitorStop != nullptr && visitorStop->ForEachEntryUntil(*volume, [&](const auto&) {
        ++visitorCalls;
        return false;
    }) && visitorCalls == 1 && visitorStop->IsOpen(),
          "visitor cancellation is a normal bounded stop, not a stream failure");

    auto closeFault = store.OpenReadSession({[](const auto point) {
        return point == ffindexstore::ReadSessionFaultPoint::DatabaseClosed;
    }});
    Check(closeFault != nullptr && !closeFault->ForEachEntryUntil(*volume, [](const auto&) {
        return true;
    }) && !closeFault->IsOpen(), "database-close fault releases the read transaction");
    Check(store.CountEntries(*volume) == 2, "the writer remains usable after a reader closes itself");
}

} // namespace

// large-index-lifecycle-and-search-scalability review-remediation 18.x:
// the conservative SQL name prefilter must be a strict superset of the
// canonical ordinal-ignore-case matches for ASCII needles (under-match =
// forbidden false negative) while LIKE metacharacters in the needle stay
// literal (gross over-match = a silent full-stream regression). The
// non-ASCII class is admitted unconditionally by design -- names carrying
// non-ASCII UTF-8 bytes can canonically match an ASCII needle through a
// non-ASCII unit under some host ordinal tables (see the fold-exceptional
// contract in Projection) -- so exact-set assertions below account for it.
void TestAsciiNamePrefilterIsConservativeAndPrecise() {
    ffindexstore::Store store;
    const std::string path = FreshDbPath("ffindexstore_ascii_name_prefilter.db");
    Check(store.Open(path), "prefilter store opens");
    const auto volume = store.GetOrCreateVolume({{}, 505});
    Check(volume.has_value(), "prefilter volume is created");
    std::u16string emoji = u"emoji-";
    emoji.push_back(static_cast<char16_t>(0xD83D)); // U+1F600 pair
    emoji.push_back(static_cast<char16_t>(0xDE00));
    emoji += u"tailfrag.txt";
    Check(store.ApplyBatch(*volume, {
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(5, 5, u".")},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(100, 5, u"alpha-report.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(101, 5, u"ALPHA-REPORT.TXT", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(102, 5, u"\u017Foo.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(103, 5, u"\u6771\u4EAC_target.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(104, 5, emoji, 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(105, 5, u"100%_done.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(106, 5, u"a_b.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(107, 5, u"axb.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(108, 5, u"a\\b.txt", 4)},
        {ffindexstore::EntryChangeKind::Upsert, MakeEntry(109, 5, u"plain.bin", 4)},
    }), "prefilter fixture commits");

    auto reader = store.OpenReadSession();
    Check(reader != nullptr && reader->IsOpen(), "prefilter read session opens");
    if (!reader || !reader->IsOpen()) return;
    // Ids 102/103/104 carry non-ASCII units and are admitted for EVERY
    // needle (the conservative GLOB disjunct); the class is named once here.
    const std::vector<uint64_t> nonAsciiClass{102, 103, 104};
    auto collect = [&](const std::u16string& needleUtf16) {
        std::string needle;
        needle.reserve(needleUtf16.size()); // ASCII only
        for (const char16_t ch : needleUtf16) needle.push_back(static_cast<char>(ch));
        std::vector<uint64_t> ids;
        const bool streamed = reader->ForEachEntryWithAsciiNameUntil(*volume, needle,
            [&](const ffindexstore::EntryRecord& entry) {
                ids.push_back(entry.id.low);
                return true;
            });
        Check(streamed, "prefiltered stream completes");
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    auto contains = [](const std::vector<uint64_t>& ids, const uint64_t id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };

    {
        const auto ids = collect(u"alpha");
        Check(contains(ids, 100) && contains(ids, 101),
              "ASCII case variants both stream (LIKE folds ASCII case exactly)");
    }
    {
        // durable-cold-search 4.x contract: the candidate rung admits the
        // fold-exceptional long-s name only under host-derived
        // fold-exceptional classification (sharp side-set). On the campaign
        // host NO non-ASCII BMP unit folds to ASCII (predecessor's exhaustive
        // probe), so for "soo" the stream is required to be a canonical
        // superset (empty here: the host matcher does not fold U+017F). The
        // injected-host admission is proven red-first in
        // TestDurableCandidateSupersetAcrossAdversarialNames.
        const auto ids = collect(u"soo");
        const bool canonicalHit =
            CompareStringOrdinal(L"ſoo.txt", -1, L"soo", -1, TRUE) == CSTR_EQUAL;
        Check(canonicalHit ? contains(ids, 102) : true,
              "the long-s case streams a canonical superset (host-fold rules govern admission)");
    }
    {
        const auto ids = collect(u"target");
        Check(contains(ids, 103), "a CJK name streams when its ASCII tail matches");
    }
    {
        const auto ids = collect(u"tailfrag");
        Check(contains(ids, 104), "a surrogate-pair name streams when its ASCII tail matches");
    }
    {
        const auto ids = collect(u"100%");
        Check(contains(ids, 105), "a percent needle matches its literal occurrence (escaping)");
    }
    {
        const auto ids = collect(u"a_b");
        Check(contains(ids, 106), "an underscore needle matches its literal occurrence");
        Check(!contains(ids, 107),
              "an underscore needle is NOT a one-char wildcard (escaped LIKE)");
    }
    {
        const auto ids = collect(u"a\\b");
        Check(contains(ids, 108),
              "a backslash needle matches its literal occurrence (escaped LIKE, no under-match)");
    }
    {
        // A no-match ASCII needle streams AT MOST the conservative non-ASCII
        // class: the candidate rung admits only the sharp side-set (host
        // fold-exceptional + surrogate names), which is a subset of the old
        // GLOB-disjunct class. Every canonical match (none here) must stream.
        const auto ids = collect(u"zzz");
        const bool bounded = std::includes(nonAsciiClass.begin(), nonAsciiClass.end(),
                                           ids.begin(), ids.end());
        Check(bounded,
              "a no-match needle streams at most the conservative non-ASCII class");
    }
}


ffindexstore::VolumeKey MakeVolumeKeyForSuperset(uint32_t serial) {
    ffindexstore::VolumeKey key;
    key.volumeGuid = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6};
    key.serialNumber = serial;
    return key;
}


struct PlannerDecision {
    bool routeUsed = false;
    size_t counted = 0;
};

ffindexstore::StoreReadSessionOptions ProbeOptions(
    std::vector<PlannerDecision>& decisions) {
    ffindexstore::StoreReadSessionOptions options;
    options.candidatePlannerProbe = [&decisions](bool routeUsed, size_t counted) {
        decisions.push_back({routeUsed, counted});
    };
    return options;
}


// --- durable-cold-search-index-and-hybrid-residency 4.1 ------------------
// Canonical-superset differential for the durable candidate rung: for every
// needle, every canonical match (per-alignment CompareStringOrdinal, the
// product's authoritative matcher) MUST be visited by
// ForEachEntryWithAsciiNameUntil. Also proves the sharp side-set mechanism:
// with an injected fold-exceptional unit the long-s name is admitted by the
// side-set even though its ASCII-folded content cannot contain the folded
// needle (the exact red condition the side-set exists to close).
bool CanonicalContainsName(const std::u16string& name, const std::u16string& needle) {
    if (needle.empty() || needle.size() > name.size()) return false;
    for (size_t offset = 0; offset + needle.size() <= name.size(); ++offset) {
        if (CompareStringOrdinal(
                reinterpret_cast<const wchar_t*>(name.c_str() + offset), static_cast<int>(needle.size()),
                reinterpret_cast<const wchar_t*>(needle.c_str()), static_cast<int>(needle.size()),
                TRUE) == CSTR_EQUAL) {
            return true;
        }
    }
    return false;
}

std::string Utf8ForTest(const std::u16string& text) {
    std::string out;
    for (const char16_t unit : text) {
        if (unit < 0x80) {
            out.push_back(static_cast<char>(unit));
        } else if (unit < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (unit >> 6)));
            out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        } else if (unit >= 0xD800 && unit <= 0xDBFF) {
            out.push_back('?');  // surrogate pairs are not query-side needles in this test
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            continue;
        } else {
            out.push_back(static_cast<char>(0xE0 | (unit >> 12)));
            out.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        }
    }
    return out;
}

std::string FoldAsciiForTest(const std::u16string& name) {
    std::string folded;
    folded.reserve(name.size());
    for (const char16_t unit : name) {
        const char ch = static_cast<char>(unit & 0x7F);
        if (unit < 0x80 && ch >= 'A' && ch <= 'Z') {
            folded.push_back(static_cast<char>(ch + 0x20));
        } else if (unit < 0x80) {
            folded.push_back(ch);
        } else {
            // Non-ASCII units pass through as '?' for the containment check
            // (only ASCII needle containment is under test here).
            folded.push_back('?');
        }
    }
    return folded;
}

void TestDurableCandidateSupersetAcrossAdversarialNames() {
    using namespace ffindexstore;
    ffindexstore::foldclassification::SetTestingFoldExceptionalUnit(0x017F, true);  // simulated host: long-s folds to 'S'
    const std::string dbPath = FreshDbPath("ffindexstore_candidate_superset.db");
    Store store;
    Check(store.Open(dbPath), "superset fixture: store opens");
    const VolumeRowId vol = *store.GetOrCreateVolume(MakeVolumeKeyForSuperset(1));

    const std::vector<std::pair<uint32_t, std::u16string>> names = {
        {100, u"common_report_alpha.txt"},
        {101, u"REPORT_beta.txt"},
        {102, u"a.b.c.d.txt"},
        {103, u"abcabcabcabc.txt"},
        {104, u"\u0441\u043f\u0438\u0441\u043e\u043a_77.txt"},
        {105, u"\u017Fand_probe.txt"},
        {106, u"photo_\U0001F600_file.txt"},
        {107, u"unique_component_7391.txt"},
        {108, u"co"},
        {109, u"plain_sand_probe.txt"},
        {110, u"upper_XYZ_probe.txt"},
        {111, u"zzz_no_hit.txt"},
        // reviewer-A P2: a fold-exceptional unit NOT at name index 0, so a
        // first-unit-only classification bug cannot pass the mechanism proof.
        {112, u"mid_\u017Fand_tail.txt"},
    };
    std::vector<EntryChange> batch;
    for (const auto& [id, name] : names) {
        batch.push_back({EntryChangeKind::Upsert, MakeEntry(id, 5, name)});
    }
    Check(store.ApplyBatch(vol, batch), "superset fixture: adversarial names commit");

    const std::vector<std::u16string> needles = {
        u"report", u"sand_probe", u"_file", u"component", u"zzz_never",
        u"a.b.c", u"abcabc", u"co", u"XYZ_probe", u"nd_tail", u"\u0441\u043f\u0438\u0441\u043e\u043a",
    };
    auto reader = store.OpenReadSession();
    bool supersetHolds = true;
    bool longsRowVisited = false;
    bool longsRowFoldedLacksNeedle = false;
    for (const auto& needle : needles) {
        std::set<uint64_t> canonical;
        for (const auto& [id, name] : names) {
            if (CanonicalContainsName(name, needle)) canonical.insert(id);
        }
        std::set<uint64_t> streamed;
        // The query side carries the REAL needle bytes with the production
        // caller's ASCII fold applied (the API contract is an ASCII-FOLDED
        // needle): non-ASCII needles must reach the ineligible fallback path
        // exactly as production does (ASCII-only gate).
        std::string needleUtf8 = Utf8ForTest(needle);
        for (char& ch : needleUtf8) {
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 0x20);
        }
        Check(reader->ForEachEntryWithAsciiNameUntil(vol, needleUtf8, [&](const EntryRecord& entry) {
            streamed.insert(entry.id.low);
            return true;
        }), "superset: candidate stream completes");
        for (const uint64_t id : canonical) {
            if (streamed.find(id) == streamed.end()) supersetHolds = false;
        }
        if (needle == u"sand_probe") {
            // reviewer-A P2: EXACT set assertions (not mere membership), so
            // an over- or under-inclusive stream cannot pass.
            const std::set<uint64_t> expectedSand{105, 106, 109, 112};
            longsRowVisited = streamed == expectedSand;
            longsRowFoldedLacksNeedle = FoldAsciiForTest(u"\u017Fand_probe.txt").find("sand_probe")
                == std::string::npos;
        }
        if (needle == u"zzz_never") {
            // Exactly the sharp side-set (fold-exceptional 105/112 + surrogate
            // 106) and nothing else on this host.
            const std::set<uint64_t> expectedNoHit{105, 106, 112};
            if (streamed != expectedNoHit) supersetHolds = false;
        }
    }
    Check(supersetHolds, "CanonicalMatches is a subset of the streamed candidates for every needle");
    Check(longsRowVisited,
          "the fold-exceptional long-s name is admitted by the side-set (injected host folding)");
    Check(longsRowFoldedLacksNeedle,
          "red-first mechanism proof: the name's folded form cannot match the needle via FTS alone");
    ffindexstore::foldclassification::ResetTestingFoldExceptionalUnits();

    // Reopen: the rebuilt index must give identical coverage.
    store.Close();
    Store reopened;
    Check(reopened.Open(dbPath), "superset fixture: reopens");
    auto secondReader = reopened.OpenReadSession();
    std::set<uint64_t> streamedAfterReopen;
    Check(secondReader->ForEachEntryWithAsciiNameUntil(vol, "sand_probe", [&](const EntryRecord& entry) {
        streamedAfterReopen.insert(entry.id.low);
        return true;
    }), "post-reopen candidate stream completes");
    Check(streamedAfterReopen.find(105) != streamedAfterReopen.end() &&
              streamedAfterReopen.find(109) != streamedAfterReopen.end(),
          "reopen coverage includes both side-set and FTS-derived matches");
}


// Task 3.2 (reviewer-C P1-class finding): candidate sets at or above the
// cost cap MUST fall back to the canonical scan (the pre-candidate code
// path), which makes broad-needle regression structurally impossible. The
// seam shrinks the cap so the fixture stays small. The cap now represents
// the REAL workload (campaign 5.3): FTS hits PLUS side-set rows, so with
// cap=2 the two-hit needle plus its side-set row (3 > 2) takes the fallback
// (whose stream is the CONSERVATIVE prefilter class) while the one-hit
// needle plus its side-set row (2 <= 2) stays on the candidate route
// (whose stream is the exact sharp union).
void TestCandidateCapFallsBackToCanonicalScan() {
    using namespace ffindexstore;
    ffindexstore::foldclassification::SetTestingFoldExceptionalUnit(0x017F, true);
    const std::string dbPath = FreshDbPath("ffindexstore_candidate_cap.db");
    Store store;
    Check(store.Open(dbPath), "cap fixture: store opens");
    const VolumeRowId vol = *store.GetOrCreateVolume(MakeVolumeKeyForSuperset(2));
    std::vector<EntryChange> batch;
    batch.push_back({EntryChangeKind::Upsert, MakeEntry(200, 5, u"cap_report_one.txt")});
    batch.push_back({EntryChangeKind::Upsert, MakeEntry(201, 5, u"cap_report_two.txt")});
    batch.push_back({EntryChangeKind::Upsert, MakeEntry(202, 5, u"cap_solo_hit.txt")});
    batch.push_back({EntryChangeKind::Upsert, MakeEntry(203, 5, u"ſcap_odd.txt")});
    Check(store.ApplyBatch(vol, batch), "cap fixture: rows commit");

    std::vector<PlannerDecision> decisions;
    auto reader = store.OpenReadSession(ProbeOptions(decisions));
    SetTestingCandidateCapOverride(2);
    std::set<uint64_t> broadStream;
    Check(reader->ForEachEntryWithAsciiNameUntil(vol, "cap_report", [&](const EntryRecord& entry) {
        broadStream.insert(entry.id.low);
        return true;
    }), "cap: broad needle stream completes");
    Check(!decisions.empty() && !decisions.back().routeUsed && decisions.back().counted == 3,
          "cap: the over-cap workload (2 FTS hits + 1 side-set row) is counted and rejected");
    // Fallback shape: literal hits PLUS the conservative non-ASCII class
    // (the GLOB disjunct admits every non-ASCII name), i.e. a SUPERSET of
    // the candidate route's exact union -- proof the fallback ran.
    const std::set<uint64_t> expectedFallback{200, 201, 203};
    Check(broadStream == expectedFallback,
          "an over-cap needle falls back to the canonical scan stream");

    std::set<uint64_t> narrowStream;
    Check(reader->ForEachEntryWithAsciiNameUntil(vol, "cap_solo_hit", [&](const EntryRecord& entry) {
        narrowStream.insert(entry.id.low);
        return true;
    }), "cap: narrow needle stream completes");
    Check(!decisions.empty() && decisions.back().routeUsed && decisions.back().counted == 2,
          "cap: the under-cap workload (1 FTS hit + 1 side-set row) is admitted");
    const std::set<uint64_t> expectedNarrow{202, 203};
    Check(narrowStream == expectedNarrow,
          "an under-cap needle stays on the candidate route (exact sharp union)");
    SetTestingCandidateCapOverride(0);
    ffindexstore::foldclassification::ResetTestingFoldExceptionalUnits();
}

// --- durable-cold-search-index-and-hybrid-residency 5.3/5.4 ----------------
// PR #15 finalization campaign: multi-volume candidate admission (cases A-F)
// and atomic candidate delivery (partial-stream failure semantics). The
// planner probe reports the admission decision and the counted workload;
// the SetTestingCandidateGlobalAdmission arm reproduces the PRE-FIX global
// pre-count so every admission test is provably discriminating (red-first
// both ways), and SetTestingCandidateStagedDelivery(false) reproduces the
// PRE-FIX deliver-as-you-go stream so the atomicity test is provably
// discriminating under the injected mid-stream fault.

bool IsSideSetName(const std::u16string& name) {
    return ffindexstore::foldclassification::NameRequiresCandidateSideSet(name.c_str(), name.size());
}

// Case A (globally broad, locally selective) + Case D (many small volumes,
// collectively over the cap): a needle whose GLOBAL hit set exceeds the cap
// must still take the candidate route when the REQUESTED volume's workload
// is small. The noise row (non-ASCII, no needle) is admitted only by the
// canonical fallback's GLOB disjunct, so the delivered SET also
// distinguishes route from fallback on hosts without fold-exceptional units.
void TestCandidateAdmissionIsVolumeScoped() {
    using namespace ffindexstore;
    const std::string dbPath = FreshDbPath("ffindexstore_multivolume_admission.db");
    Store store;
    Check(store.Open(dbPath), "multivolume fixture: store opens");
    const VolumeRowId volA = *store.GetOrCreateVolume(MakeVolumeKeyForSuperset(11));
    const VolumeRowId volB = *store.GetOrCreateVolume(MakeVolumeKeyForSuperset(12));
    const VolumeRowId volC = *store.GetOrCreateVolume(MakeVolumeKeyForSuperset(13));
    const std::u16string noiseName = u"\u6771\u4EAC_noise.txt";
    std::vector<EntryChange> batchA;
    batchA.push_back({EntryChangeKind::Upsert, MakeEntry(100, 5, u"mv_hit_alpha.txt")});
    batchA.push_back({EntryChangeKind::Upsert, MakeEntry(101, 5, noiseName)});
    std::vector<EntryChange> batchB;
    for (uint64_t index = 0; index < 3; ++index) {
        batchB.push_back({EntryChangeKind::Upsert,
                          MakeEntry(200 + index, 5,
                                    u"mv_hit_beta_" + std::u16string(1, static_cast<char16_t>(u'a' + index)) + u".txt")});
    }
    std::vector<EntryChange> batchC;
    for (uint64_t index = 0; index < 12; ++index) {
        batchC.push_back({EntryChangeKind::Upsert,
                          MakeEntry(300 + index, 5,
                                    u"mv_hit_gamma_" + std::u16string(1, static_cast<char16_t>(u'a' + index % 26)) + u".txt")});
    }
    Check(store.ApplyBatch(volA, batchA), "multivolume fixture: volume A commits");
    Check(store.ApplyBatch(volB, batchB), "multivolume fixture: volume B commits");
    Check(store.ApplyBatch(volC, batchC), "multivolume fixture: volume C commits");

    SetTestingCandidateCapOverride(8);
    std::vector<PlannerDecision> decisions;
    auto reader = store.OpenReadSession(ProbeOptions(decisions));
    Check(reader != nullptr && reader->IsOpen(), "multivolume read session opens");
    if (!reader) return;

    // Case A: volume A has ONE matching row; volumes B/C contribute 15
    // global hits (16 > cap=8 globally, 1 <= cap locally).
    // Review lane B P3 (correlated-oracle fix): the expectations below are
    // HARD-CODED, and the side-set premise is asserted rather than absorbed:
    // a CJK unit cannot fold-equal an ASCII unit under any plausible host
    // table, so if this ever fires the test must be revised, not auto-tuned.
    Check(!IsSideSetName(noiseName),
          "premise: the CJK noise row is not side-set on this host (fail-loud if a host table folds CJK to ASCII)");
    std::set<uint64_t> streamedA;
    Check(reader->ForEachEntryWithAsciiNameUntil(volA, "mv_hit", [&](const EntryRecord& entry) {
        streamedA.insert(entry.id.low);
        return true;
    }), "multivolume case A: stream completes");
    Check(!decisions.empty() && decisions.back().routeUsed && decisions.back().counted == 1,
          "case A: volume-local workload (exactly 1) is counted and admitted, not the global 16");
    Check(streamedA == std::set<uint64_t>({100}),
          "case A: the candidate route streams exactly the sharp union (noise row excluded: not side-set)");

    // Case D: every volume is locally selective (3 <= cap) even though the
    // global set (16) exceeds the cap.
    std::set<uint64_t> streamedB;
    Check(reader->ForEachEntryWithAsciiNameUntil(volB, "mv_hit", [&](const EntryRecord& entry) {
        streamedB.insert(entry.id.low);
        return true;
    }), "multivolume case D: stream completes");
    Check(!decisions.empty() && decisions.back().routeUsed && decisions.back().counted == 3,
          "case D: per-volume count (3) admits the route despite the global cap");
    Check(streamedB == std::set<uint64_t>({200, 201, 202}),
          "case D: the candidate route streams exactly volume B's three rows");
    SetTestingCandidateCapOverride(0);

    // Red-first mechanism proof: the PRE-FIX global pre-count sends BOTH
    // queries to the canonical scan (global 16 > cap 8) and its stream
    // includes the GLOB-admitted noise row for volume A.
    SetTestingCandidateCapOverride(8);
    std::vector<PlannerDecision> legacyDecisions;
    SetTestingCandidateGlobalAdmission(true);
    auto legacyReader = store.OpenReadSession(ProbeOptions(legacyDecisions));
    std::set<uint64_t> legacyStreamedA;
    Check(legacyReader != nullptr && legacyReader->ForEachEntryWithAsciiNameUntil(
              volA, "mv_hit", [&](const EntryRecord& entry) {
                  legacyStreamedA.insert(entry.id.low);
                  return true;
              }), "legacy admission: stream completes");
    SetTestingCandidateGlobalAdmission(false);
    SetTestingCandidateCapOverride(0);
    Check(!legacyDecisions.empty() && !legacyDecisions.back().routeUsed,
          "red-first proof: the global pre-count rejects the route the volume-aware count admits");
    Check(legacyStreamedA.find(101) != legacyStreamedA.end(),
          "red-first proof: the fallback stream shape differs (GLOB noise row) from the candidate route");
}

// Case C (exception-heavy volume): the admission workload must include the
// volume's exception-side-set rows even when they carry no FTS hit.
void TestCandidateAdmissionCountsExceptionCandidates() {
    using namespace ffindexstore;
    foldclassification::SetTestingFoldExceptionalUnit(0x017F, true);  // simulated host: long-s folds
    const std::string dbPath = FreshDbPath("ffindexstore_exception_admission.db");
    Store store;
    Check(store.Open(dbPath), "exception fixture: store opens");
    const VolumeRowId vol = *store.GetOrCreateVolume(MakeVolumeKeyForSuperset(21));
    std::vector<EntryChange> batch;
    batch.push_back({EntryChangeKind::Upsert, MakeEntry(300, 5, u"ex_hit_one.txt")});
    batch.push_back({EntryChangeKind::Upsert, MakeEntry(301, 5, u"ex_hit_two.txt")});
    for (uint64_t index = 0; index < 10; ++index) {
        batch.push_back({EntryChangeKind::Upsert,
                         MakeEntry(310 + index, 5,
                                   u"\u017Fexcep_" + std::u16string(1, static_cast<char16_t>(u'a' + index)) + u".txt")});
    }
    Check(store.ApplyBatch(vol, batch), "exception fixture: rows commit");

    SetTestingCandidateCapOverride(8);
    std::vector<PlannerDecision> decisions;
    auto reader = store.OpenReadSession(ProbeOptions(decisions));
    Check(reader != nullptr && reader->IsOpen(), "exception read session opens");
    if (!reader) return;
    Check(reader->ForEachEntryWithAsciiNameUntil(vol, "ex_hit", [](const EntryRecord&) {
        return true;
    }), "exception case: stream completes");
    // The bounded probe saturates each part at cap+1 (=9): 2 FTS hits + 9 of
    // the 10 side-set rows = 11 > cap 8. The decision is exact (either part
    // saturating already proves over-cap); the side-set rows are what make
    // the count exceed the cap here.
    Check(!decisions.empty() && !decisions.back().routeUsed && decisions.back().counted == 11,
          "case C: side-set rows are counted (2 FTS + 9 bounded side-set = 11 > cap 8) -> fallback");

    // Red-first mechanism proof: the exception-blind pre-count sees only the
    // 2 FTS hits (<= cap) and wrongly admits the route.
    std::vector<PlannerDecision> legacyDecisions;
    SetTestingCandidateGlobalAdmission(true);
    auto legacyReader = store.OpenReadSession(ProbeOptions(legacyDecisions));
    Check(legacyReader != nullptr && legacyReader->ForEachEntryWithAsciiNameUntil(
              vol, "ex_hit", [](const EntryRecord&) { return true; }),
          "legacy admission: stream completes");
    SetTestingCandidateGlobalAdmission(false);
    SetTestingCandidateCapOverride(0);
    Check(!legacyDecisions.empty() && legacyDecisions.back().routeUsed &&
              legacyDecisions.back().counted == 2,
          "red-first proof: the exception-blind count (2) mis-admits a 12-row workload");
    foldclassification::ResetTestingFoldExceptionalUnits();
}

// Case B/E (locally broad / one enormous volume): the per-volume workload is
// over the cap and the canonical fallback is taken; the count is the
// volume's own, not the global sum.
void TestCandidateAdmissionLocallyBroadFallsBack() {
    using namespace ffindexstore;
    const std::string dbPath = FreshDbPath("ffindexstore_broad_admission.db");
    Store store;
    Check(store.Open(dbPath), "broad fixture: store opens");
    const VolumeRowId volA = *store.GetOrCreateVolume(MakeVolumeKeyForSuperset(31));
    const VolumeRowId volB = *store.GetOrCreateVolume(MakeVolumeKeyForSuperset(32));
    std::vector<EntryChange> batchA;
    for (uint64_t index = 0; index < 20; ++index) {
        batchA.push_back({EntryChangeKind::Upsert,
                          MakeEntry(400 + index, 5,
                                    u"mv_broad_" + std::u16string(1, static_cast<char16_t>(u'a' + index % 26)) + u".txt")});
    }
    std::vector<EntryChange> batchB;
    for (uint64_t index = 0; index < 5; ++index) {
        batchB.push_back({EntryChangeKind::Upsert,
                          MakeEntry(500 + index, 5,
                                    u"mv_broad_other_" + std::u16string(1, static_cast<char16_t>(u'a' + index)) + u".txt")});
    }
    Check(store.ApplyBatch(volA, batchA), "broad fixture: volume A commits");
    Check(store.ApplyBatch(volB, batchB), "broad fixture: volume B commits");

    SetTestingCandidateCapOverride(8);
    std::vector<PlannerDecision> decisions;
    auto reader = store.OpenReadSession(ProbeOptions(decisions));
    Check(reader != nullptr && reader->IsOpen(), "broad read session opens");
    if (!reader) return;
    size_t streamed = 0;
    Check(reader->ForEachEntryWithAsciiNameUntil(volA, "mv_broad", [&](const EntryRecord&) {
        ++streamed;
        return true;
    }), "broad case: stream completes");
    Check(!decisions.empty() && !decisions.back().routeUsed && decisions.back().counted == 9,
          "case B/E: volume-local workload saturates the bounded probe (9 = cap+1 > cap 8) -> fallback");
    Check(streamed == 20, "case B/E: fallback streams exactly the volume's 20 prefiltered rows (per-volume, not 25)");
    SetTestingCandidateCapOverride(0);
}

// Campaign section 12: a candidate query that fails AFTER reading some rows
// must never leak duplicate/partial rows into a completed search. Staged
// delivery buffers before any visitor call, so the fall-back re-derives the
// complete set atomically; the legacy arm (delivery OFF) reproduces the
// pre-fix duplicate hazard under the same deterministic fault.
void TestCandidateStreamFailureIsAtomic() {
    using namespace ffindexstore;
    const std::string dbPath = FreshDbPath("ffindexstore_atomic_stream.db");
    Store store;
    Check(store.Open(dbPath), "atomic fixture: store opens");
    const VolumeRowId vol = *store.GetOrCreateVolume(MakeVolumeKeyForSuperset(41));
    std::vector<EntryChange> batch;
    for (uint64_t index = 0; index < 4; ++index) {
        batch.push_back({EntryChangeKind::Upsert,
                         MakeEntry(600 + index, 5,
                                   u"atomic_hit_" + std::u16string(1, static_cast<char16_t>(u'a' + index)) + u".txt")});
    }
    batch.push_back({EntryChangeKind::Upsert, MakeEntry(610, 5, u"\u6771\u4EAC_atomic_noise.txt")});
    Check(store.ApplyBatch(vol, batch), "atomic fixture: rows commit");

    // The injected fault fires on the 3rd candidate row read (rows 1-2 have
    // already been read from SQLite). The counter is per-run state.
    const auto runWithFault = [&](bool staged, std::vector<uint64_t>& delivered) {
        delivered.clear();
        auto counter = std::make_shared<int>(0);
        StoreReadSessionOptions options;
        options.faultInjector = [counter](ReadSessionFaultPoint point) {
            if (point == ReadSessionFaultPoint::CandidateStreamError) {
                return ++(*counter) >= 3;
            }
            return false;
        };
        options.candidatePlannerProbe = [](bool, size_t) {};
        auto reader = store.OpenReadSession(options);
        SetTestingCandidateStagedDelivery(staged);
        const bool streamed = reader != nullptr && reader->ForEachEntryWithAsciiNameUntil(
            vol, "atomic_hit", [&](const EntryRecord& entry) {
                delivered.push_back(entry.id.low);
                return true;
            });
        SetTestingCandidateStagedDelivery(true);
        return streamed;
    };

    std::vector<uint64_t> delivered;
    Check(runWithFault(true, delivered), "staged arm: the stream still completes (via canonical fallback)");
    std::set<uint64_t> unique(delivered.begin(), delivered.end());
    Check(unique.size() == delivered.size(),
          "staged arm: NO duplicate rows are delivered after the mid-stream failure");
    Check(unique.size() == 5,
          "staged arm: the delivered set is COMPLETE (4 hits + conservative noise class)");
    for (uint64_t id = 600; id < 604; ++id) {
        Check(unique.find(id) != unique.end(), "staged arm: every candidate row is present");
    }

    // Red-first mechanism proof: the pre-fix deliver-as-you-go arm leaks
    // duplicates under the SAME fault (rows delivered before the failure are
    // re-delivered by the canonical re-stream).
    std::vector<uint64_t> legacyDelivered;
    Check(runWithFault(false, legacyDelivered), "legacy arm: the stream completes");
    std::set<uint64_t> legacyUnique(legacyDelivered.begin(), legacyDelivered.end());
    Check(legacyDelivered.size() > legacyUnique.size(),
          "red-first proof: the legacy arm delivers duplicates (the hazard staged delivery removes)");
}

// Campaign case F: cancellation / early stop during candidate streaming.
void TestCandidateStreamCancellationStopsCleanly() {
    using namespace ffindexstore;
    const std::string dbPath = FreshDbPath("ffindexstore_cancel_stream.db");
    Store store;
    Check(store.Open(dbPath), "cancel fixture: store opens");
    const VolumeRowId vol = *store.GetOrCreateVolume(MakeVolumeKeyForSuperset(51));
    std::vector<EntryChange> batch;
    for (uint64_t index = 0; index < 3; ++index) {
        batch.push_back({EntryChangeKind::Upsert,
                         MakeEntry(700 + index, 5,
                                   u"cancel_hit_" + std::u16string(1, static_cast<char16_t>(u'a' + index)) + u".txt")});
    }
    Check(store.ApplyBatch(vol, batch), "cancel fixture: rows commit");

    std::vector<PlannerDecision> decisions;
    auto reader = store.OpenReadSession(ProbeOptions(decisions));
    Check(reader != nullptr && reader->IsOpen(), "cancel read session opens");
    if (!reader) return;
    size_t delivered = 0;
    Check(reader->ForEachEntryWithAsciiNameUntil(vol, "cancel_hit", [&](const EntryRecord&) {
        ++delivered;
        return false;  // caller cancellation / result cap reached
    }), "an early visitor stop on the candidate route is a successful bounded read");
    Check(delivered == 1, "early stop delivers exactly the rows the visitor accepted");
    Check(!decisions.empty() && decisions.back().routeUsed,
          "the cancelled query was on the candidate route (admission decided before delivery)");

    // Mid-stream fault while staging: the fallback delivers the COMPLETE
    // set atomically -- every row exactly once, no partial-prefix leak.
    // Review lane B P3: the planner probe is asserted too, so this half
    // proves the CANDIDATE ROUTE was actually taken before the fault (a
    // vacuous unconditional-fallback pass cannot satisfy it).
    std::vector<uint64_t> leaked;
    StoreReadSessionOptions faultOptions;
    std::vector<PlannerDecision> faultDecisions;
    faultOptions.candidatePlannerProbe = [&faultDecisions](bool routeUsed, size_t counted) {
        faultDecisions.push_back({routeUsed, counted});
    };
    auto faultCounter = std::make_shared<int>(0);
    faultOptions.faultInjector = [faultCounter](ReadSessionFaultPoint point) {
        if (point == ReadSessionFaultPoint::CandidateStreamError) {
            return ++(*faultCounter) >= 2;
        }
        return false;
    };
    auto faultedReader = store.OpenReadSession(faultOptions);
    const bool faultStreamed = faultedReader != nullptr && faultedReader->ForEachEntryWithAsciiNameUntil(
        vol, "cancel_hit", [&](const EntryRecord& entry) {
            leaked.push_back(entry.id.low);
            return true;
        });
    const std::set<uint64_t> leakedUnique(leaked.begin(), leaked.end());
    Check(!faultDecisions.empty() && faultDecisions.back().routeUsed,
          "the faulted query WAS on the candidate route (route proven before the fault)");
    Check(faultStreamed && leakedUnique.size() == 3 && leaked.size() == 3,
          "mid-stream candidate failure falls back atomically (complete set, zero duplicates)");
}

// --- durable-cold-search-index-and-hybrid-residency 5.6 (review lane B) ------
// Query-time readiness re-validation: the candidate rung must never consult a
// derived index that is not READY in the session's snapshot (mid-rebuild
// window: state=0 with partially-populated structures) or that was built
// under a different host fold signature. Either condition must degrade the
// query to the canonical stream (complete results), never silently truncate.
// Red-first: before the readiness gate the rung consulted the partial index
// and delivered a truncated set as a successful search.
void TestCandidateRungDegradesWhenIndexNotReady() {
    using namespace ffindexstore;
    const std::string dbPath = FreshDbPath("ffindexstore_readiness_gate.db");
    Store store;
    Check(store.Open(dbPath), "readiness fixture: store opens");
    const VolumeRowId vol = *store.GetOrCreateVolume(MakeVolumeKeyForSuperset(61));
    std::vector<EntryChange> batch;
    for (uint64_t index = 0; index < 3; ++index) {
        batch.push_back({EntryChangeKind::Upsert,
                         MakeEntry(800 + index, 5,
                                   u"ready_hit_" + std::u16string(1, static_cast<char16_t>(u'a' + index)) + u".txt")});
    }
    batch.push_back({EntryChangeKind::Upsert, MakeEntry(810, 5, u"\u6771\u4EAC_ready_noise.txt")});
    Check(store.ApplyBatch(vol, batch), "readiness fixture: rows commit");

    // Premise (fail-loud): the CJK noise row is NOT side-set on this host,
    // so the candidate route's exact set is {800,801,802} while the
    // canonical fallback's conservative stream is {800,801,802,810}.
    const std::u16string noiseName = u"\u6771\u4EAC_ready_noise.txt";
    Check(!IsSideSetName(noiseName),
          "premise: the CJK noise row is not side-set on this host");
    const std::set<uint64_t> candidateSet{800, 801, 802};
    const std::set<uint64_t> canonicalSet{800, 801, 802, 810};

    const auto tamper = [&](const char* sql) {
        sqlite3* raw = nullptr;
        const bool ok = sqlite3_open(dbPath.c_str(), &raw) == SQLITE_OK &&
                        sqlite3_exec(raw, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
        if (raw != nullptr) sqlite3_close(raw);
        return ok;
    };
    const auto collect = [&](std::vector<PlannerDecision>& decisions,
                             std::set<uint64_t>& delivered, bool& streamed) {
        decisions.clear();
        delivered.clear();
        auto reader = store.OpenReadSession(ProbeOptions(decisions));
        streamed = reader != nullptr && reader->ForEachEntryWithAsciiNameUntil(
            vol, "ready_hit", [&](const EntryRecord& entry) {
                delivered.insert(entry.id.low);
                return true;
            });
    };

    // Baseline: ready index -> candidate route, exact sharp union.
    std::vector<PlannerDecision> decisions;
    std::set<uint64_t> delivered;
    bool streamed = false;
    collect(decisions, delivered, streamed);
    Check(streamed && !decisions.empty() && decisions.back().routeUsed,
          "ready index: the candidate route serves the query");
    Check(delivered == candidateSet,
          "ready index: exact sharp union (hits only; noise row not side-set)");

    // Mid-rebuild window simulation: the rebuild's first transaction clears
    // the marker, and its per-volume rebuild has deleted some indexed terms
    // but not yet repopulated them. Row 800's terms row is deleted here: a
    // pre-gate candidate rung would serve a TRUNCATED set as success (red-
    // first discrimination); the readiness gate must fall back instead.
    Check(tamper("DELETE FROM entry_name_terms WHERE rowid="
                 "(SELECT term_rowid FROM entry_name_index_map WHERE frn_low=800);"
                 "UPDATE entry_name_index_meta SET state=0;"),
          "readiness fixture: mid-rebuild state simulated (marker cleared + partial terms)");
    collect(decisions, delivered, streamed);
    Check(streamed && !decisions.empty() && !decisions.back().routeUsed,
          "mid-rebuild state: the candidate route is refused (canonical fallback)");
    Check(delivered == canonicalSet && delivered.find(800) != delivered.end(),
          "mid-rebuild state: results are COMPLETE incl. row 800 (no silent truncation) - red-first gate");

    // Host-semantic mismatch: ready marker but wrong fold signature.
    Check(tamper("UPDATE entry_name_index_meta SET state=1, fold_signature=fold_signature+1;"),
          "readiness fixture: fold signature tampered (host mismatch)");
    collect(decisions, delivered, streamed);
    Check(streamed && !decisions.empty() && !decisions.back().routeUsed,
          "host mismatch: the candidate route is refused (canonical fallback)");
    Check(delivered == canonicalSet,
          "host mismatch: results are COMPLETE (no silent truncation)");

    // Restored: marker and content both repaired -> the route serves again.
    Check(tamper("INSERT INTO entry_name_terms(rowid, name_folded) "
                 "SELECT term_rowid, 'ready_hit_a.txt' FROM entry_name_index_map WHERE frn_low=800;"
                 "UPDATE entry_name_index_meta SET fold_signature=fold_signature-1;"),
          "readiness fixture: marker and terms row restored");
    collect(decisions, delivered, streamed);
    Check(streamed && !decisions.empty() && decisions.back().routeUsed,
          "restored marker: the candidate route serves the query again");
    Check(delivered == candidateSet,
          "restored marker: exact sharp union again");
}


int main() {
    TestDurableCandidateSupersetAcrossAdversarialNames();
    TestCandidateCapFallsBackToCanonicalScan();
    TestCandidateAdmissionIsVolumeScoped();
    TestCandidateAdmissionCountsExceptionCandidates();
    TestCandidateAdmissionLocallyBroadFallsBack();
    TestCandidateStreamFailureIsAtomic();
    TestCandidateStreamCancellationStopsCleanly();
    TestCandidateRungDegradesWhenIndexNotReady();
    TestIndependentSnapshotOverlapsWriter();
    TestReadSessionFaultsFailClosed();
    TestAsciiNamePrefilterIsConservativeAndPrecise();
    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
