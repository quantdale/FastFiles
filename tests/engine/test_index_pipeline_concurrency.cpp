#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "IndexPipeline.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

ffprotocol::MftRecordV1 MakeRecord(uint64_t id, uint64_t parent, std::u16string name,
                                   uint32_t attributes = 0, uint64_t size = 0) {
    ffprotocol::MftRecordV1 record{};
    record.fixed.fileReferenceNumber = id;
    record.fixed.parentFileReferenceNumber = parent;
    record.fixed.fileAttributes = attributes;
    record.fixed.sizeBytes = size;
    record.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
    record.fileName = std::move(name);
    return record;
}

ffprotocol::UsnDeltaV1 MakeUsnRecord(uint64_t id, uint64_t parent, std::u16string name,
                                     uint32_t attributes = 0, uint64_t size = 0) {
    ffprotocol::UsnDeltaV1 record{};
    record.fixed.fileReferenceNumber = id;
    record.fixed.parentFileReferenceNumber = parent;
    record.fixed.fileAttributes = attributes;
    record.fixed.sizeBytes = size;
    record.fixed.reason = 0x00000001; // USN_REASON_DATA_OVERWRITE
    record.fixed.fileNameLengthChars = static_cast<uint16_t>(name.size());
    record.fileName = std::move(name);
    return record;
}

class PhaseBarrier {
public:
    void ArriveAndWait() {
        std::unique_lock<std::mutex> lock(mutex_);
        arrived_ = true;
        condition_.notify_all();
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

struct Completion {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool completed = false;
};

bool WaitForEntry(Completion& completion) {
    std::unique_lock<std::mutex> lock(completion.mutex);
    return completion.condition.wait_for(lock, std::chrono::seconds(2), [&completion] {
        return completion.entered;
    });
}

void MarkEntered(Completion& completion) {
    {
        std::lock_guard<std::mutex> lock(completion.mutex);
        completion.entered = true;
    }
    completion.condition.notify_all();
}

void MarkCompleted(Completion& completion) {
    {
        std::lock_guard<std::mutex> lock(completion.mutex);
        completion.completed = true;
    }
    completion.condition.notify_all();
}

bool IsCompleted(Completion& completion) {
    std::lock_guard<std::mutex> lock(completion.mutex);
    return completion.completed;
}

bool WaitForCompletion(Completion& completion) {
    std::unique_lock<std::mutex> lock(completion.mutex);
    return completion.condition.wait_for(lock, std::chrono::seconds(2), [&completion] {
        return completion.completed;
    });
}

void TestSearchDoesNotMonopolizePipelineState() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_pre_refactor_contention.db")),
          "the contention reproduction pipeline opens");

    ffindexstore::VolumeKey key;
    key.serialNumber = 902;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"C:";
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"Folder", 0x10),
        MakeRecord(11, 10, u"needle.txt", 0, 1),
    }), "the contention reproduction fixture ingests its seed rows");

    PhaseBarrier searchBarrier;
    pipeline.SetSearchTestHooks({[&searchBarrier](ffengine::IndexPipeline::SearchPhase phase) {
        if (phase == ffengine::IndexPipeline::SearchPhase::WorkStarted) {
            searchBarrier.ArriveAndWait();
        }
    }});

    ffprotocol::IndexedSearchRequest request;
    request.requestId = 1;
    request.scope = 3;
    request.maxResults = 64;
    request.query = L"needle";
    ffprotocol::IndexedSearchResponse searchResponse;
    ffengine::IndexPipeline::SearchTimings timings;
    std::thread search([&] { searchResponse = pipeline.SearchIndexed(request, &timings); });
    Check(searchBarrier.WaitForArrival(),
          "the query reaches the deterministic parked search phase");

    Completion writerCompletion;
    std::thread writer([&] {
        MarkEntered(writerCompletion);
        const bool applied = pipeline.ApplyMftBatch(volume, {MakeRecord(12, 10, u"writer.txt", 0, 2)});
        if (!applied) {
            std::fprintf(stderr, "contention reproduction writer batch failed\n");
        }
        MarkCompleted(writerCompletion);
    });

    Completion usnCompletion;
    std::thread usn([&] {
        MarkEntered(usnCompletion);
        const bool applied = pipeline.ApplyUsnBatch(
            volume, {MakeUsnRecord(13, 10, u"usn-writer.txt", 0, 3)});
        if (!applied) {
            std::fprintf(stderr, "contention reproduction USN batch failed\n");
        }
        MarkCompleted(usnCompletion);
    });

    Completion aggregateCompletion;
    std::thread aggregate([&] {
        MarkEntered(aggregateCompletion);
        (void)pipeline.GetFolderAggregate(volume, ffindexstore::FileId{10, 0});
        MarkCompleted(aggregateCompletion);
    });

    Completion snapshotCompletion;
    std::thread snapshot([&] {
        MarkEntered(snapshotCompletion);
        (void)pipeline.ExportDirectorySnapshot(volume, L"C:");
        MarkCompleted(snapshotCompletion);
    });

    Check(WaitForEntry(writerCompletion), "the writer reaches the parked-search overlap");
    Check(WaitForEntry(usnCompletion), "the USN writer reaches the parked-search overlap");
    Check(WaitForEntry(aggregateCompletion), "the aggregate reaches the parked-search overlap");
    Check(WaitForEntry(snapshotCompletion), "the snapshot reaches the parked-search overlap");

    // nonblocking-indexed-read-plane 2.4: the parked phase is outside the
    // mutation/lifecycle critical section. These operations must complete
    // before the search barrier is released; the pre-refactor harness showed
    // the opposite for the same condition-variable ordering.
    Check(WaitForCompletion(writerCompletion),
          "ingestion progresses while indexed search is parked");
    Check(WaitForCompletion(usnCompletion),
          "USN ingestion progresses while indexed search is parked");
    Check(WaitForCompletion(aggregateCompletion),
          "aggregate progresses while indexed search is parked");
    Check(WaitForCompletion(snapshotCompletion),
          "snapshot export progresses while indexed search is parked");

    searchBarrier.Release();
    search.join();
    writer.join();
    usn.join();
    aggregate.join();
    snapshot.join();
    pipeline.SetSearchTestHooks({});

    Check(searchResponse.status == ffprotocol::IndexedSearchStatus::Completed,
          "the parked search completes after the reproduction release");
    Check(timings.generationRestarts >= 1,
          "the parked search restarts instead of publishing a mixed-generation result");
    Check(IsCompleted(writerCompletion), "the reproduction writer eventually completes");
    Check(IsCompleted(usnCompletion), "the reproduction USN writer eventually completes");
    Check(IsCompleted(aggregateCompletion), "the reproduction aggregate eventually completes");
    Check(IsCompleted(snapshotCompletion), "the reproduction snapshot eventually completes");

    request.requestId = 2;
    request.query = L"writer";
    const auto subsequent = pipeline.SearchIndexed(request);
    bool writerSeen = false;
    bool usnWriterSeen = false;
    for (const auto& candidate : subsequent.candidates) {
        writerSeen = writerSeen || candidate.name == L"writer.txt";
        usnWriterSeen = usnWriterSeen || candidate.name == L"usn-writer.txt";
    }
    Check(subsequent.status == ffprotocol::IndexedSearchStatus::Completed && writerSeen && usnWriterSeen,
          "a subsequent query sees the committed MFT and USN mutations");
    pipeline.Close();
}

void TestReadPlaneFaultsAndColdCancellation() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_read_plane_faults.db")),
          "read-plane fault pipeline opens");
    ffindexstore::VolumeKey key;
    key.serialNumber = 903;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"D:";
    pipeline.SetVolumeRules(volume, setting);
    std::vector<ffprotocol::MftRecordV1> seed;
    seed.push_back(MakeRecord(5, 5, u".", 0x10));
    for (uint64_t id = 10; id < 40; ++id) {
        std::u16string name = u"entry";
        name.push_back(static_cast<char16_t>(u'0' + (id / 10)));
        name.push_back(static_cast<char16_t>(u'0' + (id % 10)));
        seed.push_back(MakeRecord(id, 5, std::move(name), 0, id));
    }
    Check(pipeline.ApplyMftBatch(volume, seed), "read-plane fault fixture commits");

    ffprotocol::IndexedSearchRequest request;
    request.requestId = 10;
    request.scope = 3;
    request.maxResults = 64;
    request.query = L"entry";
    pipeline.SetReadSessionFaultInjector([](const auto point) {
        return point == ffindexstore::ReadSessionFaultPoint::Open;
    });
    Check(pipeline.SearchIndexed(request).status == ffprotocol::IndexedSearchStatus::Unavailable,
          "reader-open failure returns Unavailable");
    pipeline.SetReadSessionFaultInjector({});
    const auto diagnosticsAfterOpenFault = pipeline.GetReadPlaneDiagnostics();
    Check(diagnosticsAfterOpenFault.readerFailures >= 1,
          "reader-open failure increments bounded diagnostics");

    pipeline.UnloadVolumeFromProjection(volume);
    pipeline.SetReadSessionFaultInjector([](const auto point) {
        return point == ffindexstore::ReadSessionFaultPoint::FindEntry;
    });
    request.requestId = 11;
    Check(pipeline.SearchIndexed(request).status == ffprotocol::IndexedSearchStatus::Unavailable,
          "durable path lookup failure returns Unavailable instead of dropping a matched row");

    pipeline.SetReadSessionFaultInjector([](const auto point) {
        return point == ffindexstore::ReadSessionFaultPoint::Stream;
    });
    request.requestId = 12;
    Check(pipeline.SearchIndexed(request).status == ffprotocol::IndexedSearchStatus::Unavailable,
          "mid-stream failure returns Unavailable instead of partial results");
    pipeline.SetReadSessionFaultInjector({});

    PhaseBarrier coldBarrier;
    pipeline.SetSearchTestHooks({[&coldBarrier](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase == ffengine::IndexPipeline::SearchPhase::ColdStreaming) {
            coldBarrier.ArriveAndWait();
        }
    }});
    std::atomic<bool> cancelled{false};
    ffprotocol::IndexedSearchResponse cancelledResponse;
    request.requestId = 13;
    std::thread search([&] {
        cancelledResponse = pipeline.SearchIndexed(request, nullptr, [&cancelled] {
            return cancelled.load(std::memory_order_acquire);
        });
    });
    Check(coldBarrier.WaitForArrival(), "cold search reaches its cancellable stream phase");
    cancelled.store(true, std::memory_order_release);
    coldBarrier.Release();
    search.join();
    pipeline.SetSearchTestHooks({});
    Check(cancelledResponse.status == ffprotocol::IndexedSearchStatus::Cancelled,
          "cancellation while cold streaming is prompt and explicit");
    const auto diagnosticsAfterCancel = pipeline.GetReadPlaneDiagnostics();
    Check(diagnosticsAfterCancel.activeDurableReaders == 0,
          "cancelled cold search releases its durable reader");

    request.requestId = 14;
    const auto recovered = pipeline.SearchIndexed(request);
    Check(recovered.status == ffprotocol::IndexedSearchStatus::Completed,
          "a normal cold search works after reader fault and cancellation");
    pipeline.Close();
}

void TestPipelineCloseCancelsActiveRead() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_read_plane_shutdown.db")),
          "shutdown cancellation pipeline opens");
    ffindexstore::VolumeKey key;
    key.serialNumber = 904;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"E:";
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"shutdown-entry.txt"),
    }), "shutdown cancellation fixture commits");
    pipeline.UnloadVolumeFromProjection(volume);

    PhaseBarrier coldBarrier;
    pipeline.SetSearchTestHooks({[&coldBarrier](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase == ffengine::IndexPipeline::SearchPhase::ColdStreaming) {
            coldBarrier.ArriveAndWait();
        }
    }});
    ffprotocol::IndexedSearchRequest request;
    request.requestId = 20;
    request.scope = 3;
    request.maxResults = 64;
    request.query = L"shutdown";
    ffprotocol::IndexedSearchResponse response;
    std::thread search([&] { response = pipeline.SearchIndexed(request); });
    Check(coldBarrier.WaitForArrival(), "shutdown test reaches active cold read");

    std::atomic<bool> closeFinished{false};
    std::thread closer([&] {
        pipeline.Close();
        closeFinished.store(true, std::memory_order_release);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!closeFinished.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    Check(closeFinished.load(std::memory_order_acquire),
          "pipeline shutdown does not wait for parked read-plane work");
    coldBarrier.Release();
    search.join();
    closer.join();
    pipeline.SetSearchTestHooks({});
    Check(response.status == ffprotocol::IndexedSearchStatus::Cancelled,
          "pipeline shutdown cancels the active indexed search explicitly");
    Check(pipeline.GetReadPlaneDiagnostics().activeDurableReaders == 0,
          "pipeline shutdown releases the active durable reader");
}

void TestLifecycleMutationsDoNotDeadlockActiveRead() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_read_plane_lifecycle.db")),
          "lifecycle mutation pipeline opens");
    ffindexstore::VolumeKey key;
    key.serialNumber = 905;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"F:";
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"lifecycle-entry.txt"),
    }), "lifecycle mutation fixture commits");

    PhaseBarrier workBarrier;
    pipeline.SetSearchTestHooks({[&workBarrier](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase == ffengine::IndexPipeline::SearchPhase::WorkStarted) {
            workBarrier.ArriveAndWait();
        }
    }});
    ffprotocol::IndexedSearchRequest request;
    request.requestId = 30;
    request.scope = 3;
    request.maxResults = 64;
    request.query = L"lifecycle";
    ffprotocol::IndexedSearchResponse response;
    std::thread search([&] { response = pipeline.SearchIndexed(request); });
    Check(workBarrier.WaitForArrival(), "lifecycle test reaches an active read");

    pipeline.SetBulkScanActive(true);
    pipeline.SetBulkScanActive(false);
    Check(pipeline.SetVolumeAvailable(volume, false, 100),
          "volume-unavailable transition completes during an active read");
    Check(pipeline.SetVolumeAvailable(volume, true, 101),
          "volume-available transition completes during an active read");
    pipeline.BeginReconciliationPass(volume);
    Check(pipeline.IsReconciliationPassActive(volume),
          "reconciliation begins during an active read");
    pipeline.FinishReconciliationPass(volume);
    Check(!pipeline.IsReconciliationPassActive(volume),
          "reconciliation finishes during an active read");
    pipeline.SetProjectionMemoryBudget(1024 * 1024);
    Check(pipeline.SetVolumeAvailable(volume, false, 102),
          "volume becomes unavailable before forget during an active read");
    Check(pipeline.ForgetVolume(volume), "forget-volume transition completes during an active read");

    workBarrier.Release();
    search.join();
    pipeline.SetSearchTestHooks({});
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed ||
              response.status == ffprotocol::IndexedSearchStatus::Unavailable,
          "lifecycle generation changes do not publish an invalid search result");
    Check(pipeline.GetReadPlaneDiagnostics().activeDurableReaders == 0,
          "lifecycle changes release the active read session");
    pipeline.Close();
}

} // namespace

int main() {
    TestSearchDoesNotMonopolizePipelineState();
    TestReadPlaneFaultsAndColdCancellation();
    TestPipelineCloseCancelsActiveRead();
    TestLifecycleMutationsDoNotDeadlockActiveRead();
    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
