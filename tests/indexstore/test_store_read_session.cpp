#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ffindexstore/Store.h"
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
        const auto ids = collect(u"soo");
        Check(contains(ids, 102),
              "the long-s name streams for an ASCII needle (host-fold conservative admission)");
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
        // A no-match ASCII needle streams exactly the conservative
        // non-ASCII class and nothing else (documents the bounded overmatch).
        const auto ids = collect(u"zzz");
        Check(ids == nonAsciiClass,
              "a no-match needle streams only the conservative non-ASCII class");
    }
}

int main() {
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
