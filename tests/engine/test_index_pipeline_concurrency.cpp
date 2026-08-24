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

ffprotocol::UsnDeltaV1 MakeUsnDeleteRecord(uint64_t id, uint64_t parent, std::u16string name) {
    auto record = MakeUsnRecord(id, parent, std::move(name));
    record.fixed.reason = 0x00000200; // USN_REASON_FILE_DELETE
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
        // Bounded hang watchdog only: correctness is established by the
        // condition-variable rendezvous, not by this wall-clock allowance.
        return condition_.wait_for(lock, std::chrono::seconds(10), [this] { return arrived_; });
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

void TestOperationalMetadataDoesNotInvalidateActiveSearch() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_query_metadata_epoch.db")),
          "metadata epoch pipeline opens");
    ffindexstore::VolumeKey key;
    key.serialNumber = 906;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"G:";
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"metadata-entry.txt"),
    }), "metadata epoch fixture commits");

    PhaseBarrier workBarrier;
    pipeline.SetSearchTestHooks({[&workBarrier](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase == ffengine::IndexPipeline::SearchPhase::WorkStarted) {
            workBarrier.ArriveAndWait();
        }
    }});
    ffprotocol::IndexedSearchRequest request;
    request.requestId = 40;
    request.scope = 3;
    request.maxResults = 64;
    request.query = L"metadata";
    ffengine::IndexPipeline::SearchTimings timings;
    ffprotocol::IndexedSearchResponse response;
    std::thread search([&] { response = pipeline.SearchIndexed(request, &timings); });
    Check(workBarrier.WaitForArrival(), "metadata query reaches active read phase");

    Check(pipeline.SetScanCursor(volume, {1, 2, 3}), "scan cursor metadata update succeeds");
    Check(pipeline.SetJournalPosition(volume, 77, 88), "journal position metadata update succeeds");
    Check(pipeline.MarkScanComplete(volume), "scan-complete metadata update succeeds");
    Check(pipeline.SetLastReconciliationTime(volume, 99),
          "last-reconciliation metadata update succeeds");
    pipeline.SetBulkScanActive(true);
    pipeline.SetBulkScanActive(false);
    Check(pipeline.SetVolumeAvailable(volume, true, 100),
          "an unchanged availability write succeeds without a visibility transition");
    Check(pipeline.ApplyMftBatch(volume, std::vector<ffprotocol::MftRecordV1>{}),
          "an empty MFT batch keeps the writer contract without a visible mutation");
    Check(pipeline.ApplyUsnBatch(volume, std::vector<ffprotocol::UsnDeltaV2>{}),
          "an empty USN batch keeps the writer contract without a visible mutation");
    ffprotocol::UsnDeltaV2 invalidUsn{};
    invalidUsn.fixed.fieldMask = 0x8000;
    Check(!pipeline.ApplyUsnBatch(volume, {invalidUsn}),
          "an invalid USN field mask fails without a visible mutation");
    Check(!pipeline.SetVolumeAvailable(volume + 1000, false, 101),
          "a missing volume availability update fails closed without a visible mutation");
    const auto noOpDiagnostics = pipeline.GetReadPlaneDiagnostics();

    workBarrier.Release();
    search.join();
    pipeline.SetSearchTestHooks({});
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed,
          "operational metadata does not make an active query unavailable");
    Check(timings.generationRestarts == 0,
          "operational metadata does not restart an active query");
    const auto diagnostics = pipeline.GetReadPlaneDiagnostics();
    Check(diagnostics.metadataOnlyUpdates >= 6 && diagnostics.metadataOnlyInvalidationsAvoided >= 6,
          "metadata-only updates are counted as avoided invalidations");
    Check(diagnostics.queryVisibleEpoch == noOpDiagnostics.queryVisibleEpoch,
          "empty and failed mutations do not advance the query-visible epoch");

    pipeline.BeginReconciliationPass(volume);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"metadata-entry.txt"),
    }), "a no-removal reconciliation pass observes every existing row");
    const auto beforeNoRemovalFinish = pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch;
    pipeline.FinishReconciliationPass(volume);
    Check(pipeline.GetReadPlaneDiagnostics().queryVisibleEpoch == beforeNoRemovalFinish,
          "reconciliation bookkeeping without removals does not invalidate a query");
    pipeline.Close();
}

void TestScopedSearchIgnoresUnrelatedVolumeMutation() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_scoped_epoch_dependencies.db")),
          "scoped dependency pipeline opens");
    ffindexstore::VolumeKey firstKey;
    firstKey.serialNumber = 907;
    const auto firstVolume = pipeline.ResolveVolume(firstKey);
    ffprotocol::VolumeSetting firstSetting;
    firstSetting.key = L"H:";
    pipeline.SetVolumeRules(firstVolume, firstSetting);
    ffindexstore::VolumeKey secondKey;
    secondKey.serialNumber = 908;
    const auto secondVolume = pipeline.ResolveVolume(secondKey);
    ffprotocol::VolumeSetting secondSetting;
    secondSetting.key = L"I:";
    pipeline.SetVolumeRules(secondVolume, secondSetting);
    Check(pipeline.ApplyMftBatch(firstVolume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"first-target.txt"),
    }), "scoped dependency first volume commits");
    Check(pipeline.ApplyMftBatch(secondVolume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"second-target.txt"),
    }), "scoped dependency second volume commits");

    PhaseBarrier workBarrier;
    pipeline.SetSearchTestHooks({[&workBarrier](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase == ffengine::IndexPipeline::SearchPhase::WorkStarted) {
            workBarrier.ArriveAndWait();
        }
    }});
    ffprotocol::IndexedSearchRequest request;
    request.requestId = 41;
    request.scope = 2; // CurrentDrive
    request.scopePath = L"H:\\";
    request.maxResults = 64;
    request.query = L"first-target";
    ffengine::IndexPipeline::SearchTimings timings;
    ffprotocol::IndexedSearchResponse response;
    std::thread search([&] { response = pipeline.SearchIndexed(request, &timings); });
    Check(workBarrier.WaitForArrival(), "scoped query reaches active read phase");
    Check(pipeline.ApplyMftBatch(secondVolume, {MakeRecord(11, 5, u"unrelated-write.txt")}),
          "unrelated volume mutation commits while scoped query is active");
    workBarrier.Release();
    search.join();
    pipeline.SetSearchTestHooks({});
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed,
          "a scoped query completes while an unrelated volume changes");
    Check(timings.generationRestarts == 0,
          "an unrelated volume does not invalidate a scoped query");
    Check(response.candidates.size() == 1 && response.candidates.front().name == L"first-target.txt",
          "a scoped query returns only the selected volume's result");
    pipeline.Close();
}

void TestGlobalSearchObservesEveryIncludedVolume() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_global_epoch_dependencies.db")),
          "global dependency pipeline opens");
    ffindexstore::VolumeKey firstKey;
    firstKey.serialNumber = 914;
    const auto firstVolume = pipeline.ResolveVolume(firstKey);
    ffprotocol::VolumeSetting firstSetting;
    firstSetting.key = L"O:";
    pipeline.SetVolumeRules(firstVolume, firstSetting);
    ffindexstore::VolumeKey secondKey;
    secondKey.serialNumber = 915;
    const auto secondVolume = pipeline.ResolveVolume(secondKey);
    ffprotocol::VolumeSetting secondSetting;
    secondSetting.key = L"P:";
    pipeline.SetVolumeRules(secondVolume, secondSetting);
    Check(pipeline.ApplyMftBatch(firstVolume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"first-global.txt"),
    }), "global dependency first volume commits");
    Check(pipeline.ApplyMftBatch(secondVolume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"second-before.txt"),
    }), "global dependency second volume commits");

    PhaseBarrier workBarrier;
    std::atomic<bool> firstWorkSeen{false};
    pipeline.SetSearchTestHooks({[&](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase != ffengine::IndexPipeline::SearchPhase::WorkStarted) return;
        bool expected = false;
        if (firstWorkSeen.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            workBarrier.ArriveAndWait();
        }
    }});
    ffprotocol::IndexedSearchRequest request;
    request.requestId = 52;
    request.scope = 3; // AllIndexedLocations.
    request.maxResults = 64;
    request.query = L"global-target";
    ffengine::IndexPipeline::SearchTimings timings;
    ffprotocol::IndexedSearchResponse response;
    std::thread search([&] { response = pipeline.SearchIndexed(request, &timings); });
    Check(workBarrier.WaitForArrival(), "global query reaches its first active read phase");
    Check(pipeline.ApplyMftBatch(secondVolume, {MakeRecord(11, 5, u"second-global-target.txt")}),
          "a relevant second-volume mutation commits during a global query");
    workBarrier.Release();
    search.join();
    pipeline.SetSearchTestHooks({});
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed,
          "a global query completes after an included volume changes");
    Check(timings.generationRestarts >= 1 && timings.relevantInvalidations >= 1,
          "a global query invalidates on a relevant mutation from any included volume");
    Check(response.candidates.size() == 1 && response.candidates.front().name == L"second-global-target.txt",
          "a global query observes the changed included volume without mixed rows");
    pipeline.Close();
}

void TestProjectionEvictionInvalidatesCapturedCursorSafely() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_projection_epoch_cursor.db")),
          "projection residency pipeline opens");
    ffindexstore::VolumeKey key;
    key.serialNumber = 916;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"Q:";
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"residency-entry.txt"),
    }), "projection residency fixture commits");

    PhaseBarrier workBarrier;
    std::atomic<bool> firstWorkSeen{false};
    pipeline.SetSearchTestHooks({[&](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase != ffengine::IndexPipeline::SearchPhase::WorkStarted) return;
        bool expected = false;
        if (firstWorkSeen.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            workBarrier.ArriveAndWait();
        }
    }});
    ffprotocol::IndexedSearchRequest request;
    request.requestId = 53;
    request.scope = 3;
    request.maxResults = 64;
    request.query = L"residency";
    ffengine::IndexPipeline::SearchTimings timings;
    ffprotocol::IndexedSearchResponse response;
    std::thread search([&] { response = pipeline.SearchIndexed(request, &timings); });
    Check(workBarrier.WaitForArrival(), "residency query captures a projection cursor");
    pipeline.UnloadVolumeFromProjection(volume);
    Check(pipeline.IsVolumeEvicted(volume), "residency mutation evicts the captured volume");
    workBarrier.Release();
    search.join();
    pipeline.SetSearchTestHooks({});
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed,
          "residency mutation restarts onto a coherent cold pass");
    Check(timings.generationRestarts >= 1 && response.candidates.size() == 1 &&
              response.candidates.front().name == L"residency-entry.txt",
          "residency mutation does not use a stale cursor or publish mixed rows");
    Check(pipeline.GetReadPlaneDiagnostics().activeDurableReaders == 0,
          "residency restart releases its durable reader");
    pipeline.Close();
}

void TestRuleChangeCannotLeakExcludedRows() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_search_rule_epoch.db")),
          "search rule epoch pipeline opens");
    ffindexstore::VolumeKey key;
    key.serialNumber = 912;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting initialSetting;
    initialSetting.key = L"M:";
    pipeline.SetVolumeRules(volume, initialSetting);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"Included", 0x10),
        MakeRecord(11, 5, u"Excluded", 0x10),
        MakeRecord(20, 10, u"keep.txt"),
        MakeRecord(21, 11, u"leak.txt"),
    }), "search rule epoch fixture commits without an exclude rule");

    PhaseBarrier workBarrier;
    pipeline.SetSearchTestHooks({[&workBarrier](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase == ffengine::IndexPipeline::SearchPhase::WorkStarted) {
            workBarrier.ArriveAndWait();
        }
    }});
    ffprotocol::IndexedSearchRequest request;
    request.requestId = 47;
    request.scope = 3;
    request.maxResults = 64;
    request.query = L"leak";
    ffengine::IndexPipeline::SearchTimings timings;
    ffprotocol::IndexedSearchResponse response;
    std::thread search([&] { response = pipeline.SearchIndexed(request, &timings); });
    Check(workBarrier.WaitForArrival(), "rule-change query reaches active read phase");
    ffprotocol::VolumeSetting excludedSetting = initialSetting;
    excludedSetting.rules.push_back({L"M:\\Excluded", false});
    pipeline.SetVolumeRules(volume, excludedSetting);
    workBarrier.Release();
    search.join();
    pipeline.SetSearchTestHooks({});
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed,
          "a rule change leaves the active query available");
    Check(timings.generationRestarts >= 1,
          "a rule change invalidates the old query context");
    Check(response.candidates.empty(),
          "a newly excluded hot row is not published by the restarted query");

    pipeline.UnloadVolumeFromProjection(volume);
    request.requestId = 48;
    request.scope = 2; // CurrentDrive: exercise the scoped rule dependency too.
    request.scopePath = L"M:";
    const auto coldResponse = pipeline.SearchIndexed(request);
    Check(coldResponse.status == ffprotocol::IndexedSearchStatus::Completed &&
              coldResponse.candidates.empty(),
          "the captured exclude rule also filters the durable cold fallback rows");

    Check(pipeline.ApplyMftBatch(volume, {MakeRecord(21, 10, u"moved-in.txt")}),
          "a renamed/moved row enters an included subtree");
    request.requestId = 49;
    request.query = L"moved-in";
    const auto movedIn = pipeline.SearchIndexed(request);
    Check(movedIn.status == ffprotocol::IndexedSearchStatus::Completed &&
              movedIn.candidates.size() == 1,
          "a scoped query returns a row moved into the included subtree");
    Check(pipeline.ApplyMftBatch(volume, {MakeRecord(21, 11, u"moved-out.txt")}),
          "a renamed/moved row enters an excluded subtree");
    request.requestId = 50;
    request.query = L"moved-out";
    const auto movedOut = pipeline.SearchIndexed(request);
    Check(movedOut.status == ffprotocol::IndexedSearchStatus::Completed &&
              movedOut.candidates.empty(),
          "a scoped query does not leak a row moved into the excluded subtree");

    pipeline.BeginReconciliationPass(volume);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"Included", 0x10),
        MakeRecord(20, 10, u"keep.txt"),
    }), "a rule-aware reconciliation observes only included durable rows");
    pipeline.FinishReconciliationPass(volume);
    request.requestId = 52;
    request.query = L"leak";
    const auto reconciled = pipeline.SearchIndexed(request);
    Check(reconciled.status == ffprotocol::IndexedSearchStatus::Completed &&
              reconciled.candidates.empty(),
          "reconciliation removes a stale durable row excluded by the captured rule");

    Check(pipeline.ApplyMftBatch(volume, {MakeRecord(30, 999, u"orphan.txt")}),
          "a partial-parent row remains durably recorded for deferred reconciliation");
    request.requestId = 53;
    request.query = L"orphan";
    const auto orphan = pipeline.SearchIndexed(request);
    Check(orphan.status == ffprotocol::IndexedSearchStatus::Unavailable &&
              orphan.candidates.empty(),
          "a rule-filtered partial parent chain fails closed instead of leaking a row");
    pipeline.Close();
}

void TestSustainedMftChurnUsesDurableFallback() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_sustained_mft_fallback.db")),
          "sustained MFT fallback pipeline opens");
    ffindexstore::VolumeKey key;
    key.serialNumber = 909;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"J:";
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"initial-churn.txt"),
    }), "sustained MFT fallback seed commits");

    std::atomic<size_t> validationCount{0};
    pipeline.SetSearchTestHooks({[&](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase != ffengine::IndexPipeline::SearchPhase::GenerationValidation) return;
        const size_t sequence = validationCount.fetch_add(1, std::memory_order_acq_rel);
        if (sequence < 4) {
            const bool applied = pipeline.ApplyMftBatch(volume, {
                MakeRecord(100 + sequence, 5,
                           std::u16string(u"churn-") + std::u16string(1, static_cast<char16_t>(u'0' + sequence)))
            });
            Check(applied, "a sustained MFT mutation commits during validation");
        }
    }});
    ffprotocol::IndexedSearchRequest request;
    request.requestId = 42;
    request.scope = 3;
    request.maxResults = 256;
    request.query = L"churn";
    ffengine::IndexPipeline::SearchTimings timings;
    const auto response = pipeline.SearchIndexed(request, &timings);
    pipeline.SetSearchTestHooks({});
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed,
          "sustained MFT churn completes instead of exhausting search availability");
    Check(timings.durableFallbackUsed && timings.durableFallbackSucceeded &&
              timings.durableFallbackAttempts == 1,
          "sustained MFT churn selects one successful durable fallback");
    Check(timings.generationRestarts >= 4 && validationCount.load(std::memory_order_acquire) >= 4,
          "the old restart budget is actually exceeded before fallback");
    Check(timings.durableFallbackRowsStreamed >= 5,
          "the durable fallback streams a bounded snapshot rather than reusing hot rows");
    bool newestRowSeen = false;
    for (const auto& candidate : response.candidates) {
        newestRowSeen = newestRowSeen || candidate.name == L"churn-3";
    }
    Check(newestRowSeen, "the durable fallback observes the final committed MFT state coherently");
    Check(pipeline.GetReadPlaneDiagnostics().successfulDurableFallbacks >= 1,
          "successful durable fallbacks are exposed in bounded diagnostics");
    pipeline.Close();
}

void TestSustainedUsnChurnUsesDurableFallback() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_sustained_usn_fallback.db")),
          "sustained USN fallback pipeline opens");
    ffindexstore::VolumeKey key;
    key.serialNumber = 910;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"K:";
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"FolderA", 0x10),
        MakeRecord(11, 5, u"FolderB", 0x10),
        MakeRecord(20, 10, u"usn-old.txt"),
        MakeRecord(21, 10, u"usn-survivor.txt"),
    }), "sustained USN fallback seed commits");

    std::atomic<size_t> validationCount{0};
    pipeline.SetSearchTestHooks({[&](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase != ffengine::IndexPipeline::SearchPhase::GenerationValidation) return;
        const size_t sequence = validationCount.fetch_add(1, std::memory_order_acq_rel);
        bool applied = false;
        if (sequence == 0) {
            applied = pipeline.ApplyUsnBatch(volume, {MakeUsnRecord(20, 10, u"usn-renamed-a.txt")});
        } else if (sequence == 1) {
            applied = pipeline.ApplyUsnBatch(volume, {MakeUsnRecord(20, 11, u"usn-moved.txt")});
        } else if (sequence == 2) {
            applied = pipeline.ApplyUsnBatch(volume, {MakeUsnRecord(20, 10, u"usn-renamed-b.txt")});
        } else if (sequence == 3) {
            applied = pipeline.ApplyUsnBatch(volume, {MakeUsnDeleteRecord(20, 10, u"usn-renamed-b.txt")});
        }
        if (sequence < 4) Check(applied, "a sustained USN rename/move/delete commits during validation");
    }});
    ffprotocol::IndexedSearchRequest request;
    request.requestId = 43;
    request.scope = 3;
    request.maxResults = 256;
    request.query = L"usn";
    ffengine::IndexPipeline::SearchTimings timings;
    const auto response = pipeline.SearchIndexed(request, &timings);
    pipeline.SetSearchTestHooks({});
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed,
          "sustained USN churn completes from a coherent durable snapshot");
    Check(timings.durableFallbackSucceeded && timings.generationRestarts >= 4,
          "sustained USN churn exceeds the restart budget and succeeds via fallback");
    bool survivorSeen = false;
    bool deletedTargetSeen = false;
    for (const auto& candidate : response.candidates) {
        survivorSeen = survivorSeen || candidate.name == L"usn-survivor.txt";
        deletedTargetSeen = deletedTargetSeen || candidate.name == L"usn-old.txt" ||
                            candidate.name == L"usn-renamed-a.txt" ||
                            candidate.name == L"usn-moved.txt" ||
                            candidate.name == L"usn-renamed-b.txt";
    }
    Check(survivorSeen && !deletedTargetSeen,
          "USN rename/move/delete churn does not publish a mixed-generation target");
    pipeline.Close();
}

void TestDurableFallbackSnapshotStaysStableWhileWriterCommits() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_durable_snapshot_stability.db")),
          "durable snapshot stability pipeline opens");
    ffindexstore::VolumeKey key;
    key.serialNumber = 913;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"N:";
    pipeline.SetVolumeRules(volume, setting);
    Check(pipeline.ApplyMftBatch(volume, {
        MakeRecord(5, 5, u".", 0x10),
        MakeRecord(10, 5, u"snapshot-before.txt"),
    }), "durable snapshot stability seed commits");

    std::atomic<size_t> validationCount{0};
    std::atomic<bool> validationMutationsSucceeded{true};
    PhaseBarrier fallbackBarrier;
    pipeline.SetSearchTestHooks({[&](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase == ffengine::IndexPipeline::SearchPhase::GenerationValidation) {
            const size_t sequence = validationCount.fetch_add(1, std::memory_order_acq_rel);
            if (sequence < 4) {
                if (!pipeline.ApplyMftBatch(volume, {
                    MakeRecord(400 + sequence, 5,
                               std::u16string(u"snapshot-churn-") +
                                   std::u16string(1, static_cast<char16_t>(u'0' + sequence)))
                })) {
                    validationMutationsSucceeded.store(false, std::memory_order_release);
                }
            }
        } else if (phase == ffengine::IndexPipeline::SearchPhase::DurableFallback) {
            fallbackBarrier.ArriveAndWait();
        }
    }});

    ffprotocol::IndexedSearchRequest request;
    request.requestId = 49;
    request.scope = 3;
    request.maxResults = 128;
    request.query = L"snapshot";
    ffprotocol::IndexedSearchResponse response;
    ffengine::IndexPipeline::SearchTimings timings;
    std::thread search([&] { response = pipeline.SearchIndexed(request, &timings); });
    Check(fallbackBarrier.WaitForArrival(), "durable fallback opens its snapshot before streaming");
    Completion writerCompletion;
    std::atomic<bool> writerSucceeded{false};
    std::thread writer([&] {
        MarkEntered(writerCompletion);
        writerSucceeded.store(pipeline.ApplyMftBatch(volume, {MakeRecord(500, 5, u"snapshot-during.txt")}),
                              std::memory_order_release);
        MarkCompleted(writerCompletion);
    });
    Check(WaitForEntry(writerCompletion), "the snapshot-stability writer starts");
    Check(WaitForCompletion(writerCompletion),
          "the writer commits without waiting for durable fallback completion");
    fallbackBarrier.Release();
    search.join();
    writer.join();
    pipeline.SetSearchTestHooks({});
    Check(writerSucceeded.load(std::memory_order_acquire),
          "writer commits while durable fallback snapshot is held");
    Check(validationMutationsSucceeded.load(std::memory_order_acquire),
          "snapshot stability churn commits");
    Check(response.status == ffprotocol::IndexedSearchStatus::Completed && timings.durableFallbackSucceeded,
          "the durable snapshot query completes after concurrent writer progress");
    bool beforeSeen = false;
    bool duringSeen = false;
    for (const auto& candidate : response.candidates) {
        beforeSeen = beforeSeen || candidate.name == L"snapshot-before.txt";
        duringSeen = duringSeen || candidate.name == L"snapshot-during.txt";
    }
    Check(beforeSeen && !duringSeen,
          "the fallback response comes from one pre-commit snapshot, not a mixed reader state");
    request.requestId = 50;
    request.query = L"snapshot-during";
    const auto subsequent = pipeline.SearchIndexed(request);
    Check(subsequent.status == ffprotocol::IndexedSearchStatus::Completed &&
              subsequent.candidates.size() == 1 &&
              subsequent.candidates.front().name == L"snapshot-during.txt",
          "a fresh search sees the writer commit after the fallback snapshot closes");
    pipeline.Close();
}

void TestDurableFallbackCancellationAndReadFailureAreFailClosed() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_durable_fallback_faults.db")),
          "durable fallback fault pipeline opens");
    ffindexstore::VolumeKey key;
    key.serialNumber = 911;
    const auto volume = pipeline.ResolveVolume(key);
    ffprotocol::VolumeSetting setting;
    setting.key = L"L:";
    pipeline.SetVolumeRules(volume, setting);
    std::vector<ffprotocol::MftRecordV1> seed{MakeRecord(5, 5, u".", 0x10)};
    for (uint64_t id = 10; id < 70; ++id) {
        std::u16string name = u"fallback-";
        name.push_back(static_cast<char16_t>(u'0' + ((id / 10) % 10)));
        name.push_back(static_cast<char16_t>(u'0' + (id % 10)));
        seed.push_back(MakeRecord(id, 5, std::move(name)));
    }
    Check(pipeline.ApplyMftBatch(volume, seed), "durable fallback fault seed commits");

    std::atomic<size_t> validationCount{0};
    std::atomic<bool> validationMutationsSucceeded{true};
    PhaseBarrier fallbackBarrier;
    pipeline.SetSearchTestHooks({[&](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase == ffengine::IndexPipeline::SearchPhase::GenerationValidation) {
            const size_t sequence = validationCount.fetch_add(1, std::memory_order_acq_rel);
            if (sequence < 4) {
                if (!pipeline.ApplyMftBatch(volume, {
                    MakeRecord(200 + sequence, 5,
                               std::u16string(u"fallback-churn-") +
                                   std::u16string(1, static_cast<char16_t>(u'0' + sequence)))
                })) {
                    validationMutationsSucceeded.store(false, std::memory_order_release);
                }
            }
        } else if (phase == ffengine::IndexPipeline::SearchPhase::DurableFallback) {
            fallbackBarrier.ArriveAndWait();
        }
    }});
    ffprotocol::IndexedSearchRequest request;
    request.requestId = 44;
    request.scope = 3;
    request.maxResults = 256;
    request.query = L"fallback";
    std::atomic<bool> cancelled{false};
    ffprotocol::IndexedSearchResponse cancelledResponse;
    std::thread search([&] {
        cancelledResponse = pipeline.SearchIndexed(request, nullptr, [&cancelled] {
            return cancelled.load(std::memory_order_acquire);
        });
    });
    Check(fallbackBarrier.WaitForArrival(), "fallback cancellation reaches durable streaming");
    cancelled.store(true, std::memory_order_release);
    fallbackBarrier.Release();
    search.join();
    pipeline.SetSearchTestHooks({});
    Check(cancelledResponse.status == ffprotocol::IndexedSearchStatus::Cancelled,
          "cancellation during durable fallback publishes no result");
    Check(cancelledResponse.candidates.empty(),
          "cancelled durable fallback discards any accumulated candidates");
    Check(pipeline.GetReadPlaneDiagnostics().activeDurableReaders == 0,
          "durable fallback cancellation releases its read session");
    Check(validationMutationsSucceeded.load(std::memory_order_acquire),
          "fallback cancellation churn commits");

    validationCount.store(0, std::memory_order_release);
    validationMutationsSucceeded.store(true, std::memory_order_release);
    pipeline.SetSearchTestHooks({[&](const ffengine::IndexPipeline::SearchPhase phase) {
        if (phase != ffengine::IndexPipeline::SearchPhase::GenerationValidation) return;
        const size_t sequence = validationCount.fetch_add(1, std::memory_order_acq_rel);
        if (sequence < 4) {
            if (!pipeline.ApplyMftBatch(volume, {
                MakeRecord(300 + sequence, 5,
                           std::u16string(u"failure-churn-") +
                               std::u16string(1, static_cast<char16_t>(u'0' + sequence)))
            })) {
                validationMutationsSucceeded.store(false, std::memory_order_release);
            }
        }
    }});
    pipeline.SetReadSessionFaultInjector([](const auto point) {
        return point == ffindexstore::ReadSessionFaultPoint::Stream;
    });
    request.requestId = 45;
    const auto failed = pipeline.SearchIndexed(request);
    pipeline.SetReadSessionFaultInjector({});
    pipeline.SetSearchTestHooks({});
    Check(failed.status == ffprotocol::IndexedSearchStatus::Unavailable && failed.candidates.empty(),
          "a durable fallback stream failure is fail-closed without partial results");
    Check(validationMutationsSucceeded.load(std::memory_order_acquire),
          "fallback read-failure churn commits");

    request.requestId = 46;
    const auto recovered = pipeline.SearchIndexed(request);
    Check(recovered.status == ffprotocol::IndexedSearchStatus::Completed,
          "subsequent writes and searches recover after a durable fallback read failure");
    Check(pipeline.GetReadPlaneDiagnostics().failedDurableFallbacks >= 1,
          "failed durable fallbacks are exposed in bounded diagnostics");
    pipeline.Close();
}

} // namespace

int main() {
    TestSearchDoesNotMonopolizePipelineState();
    TestReadPlaneFaultsAndColdCancellation();
    TestPipelineCloseCancelsActiveRead();
    TestLifecycleMutationsDoNotDeadlockActiveRead();
    TestOperationalMetadataDoesNotInvalidateActiveSearch();
    TestScopedSearchIgnoresUnrelatedVolumeMutation();
    TestGlobalSearchObservesEveryIncludedVolume();
    TestProjectionEvictionInvalidatesCapturedCursorSafely();
    TestRuleChangeCannotLeakExcludedRows();
    TestSustainedMftChurnUsesDurableFallback();
    TestSustainedUsnChurnUsesDurableFallback();
    TestDurableFallbackSnapshotStaysStableWhileWriterCommits();
    TestDurableFallbackCancellationAndReadFailureAreFailClosed();
    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
