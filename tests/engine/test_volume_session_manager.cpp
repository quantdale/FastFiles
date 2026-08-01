#include <cstdio>
#include <filesystem>
#include <string>

#include "IndexPipeline.h"
// This test deliberately exercises the session manager's callback path,
// rather than duplicating its resume-vs-reconcile decision in a helper.
#define private public
#include "VolumeSessionManager.h"
#undef private

namespace {

int g_failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    } else {
        std::printf("ok: %s\n", description);
    }
}

std::string FreshDbPath(const char* name) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
    return path.string();
}

void AddSession(ffengine::VolumeSessionManager& manager, ffindexstore::VolumeRowId volumeId) {
    manager.sessionsByEphemeralId_.emplace(7, ffengine::VolumeSessionManager::VolumeSession{volumeId, L'C', 0, false});
}

void TestMatchingJournalResumesWithoutReconciliation() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_journal_resume.db")), "pipeline opens for matching-journal test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 1;
    const auto volumeId = pipeline.ResolveVolume(key);
    Check(pipeline.SetJournalPosition(volumeId, 41, 900), "persisted journal position is established");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.Start();
    connection.journalOpenedCallback_(ffprotocol::VolumeId{7}, 41, 1000);

    Check(!pipeline.IsReconciliationPassActive(volumeId), "matching journal identity resumes instead of reconciling");
    const auto metadata = pipeline.GetVolumeMetadata(volumeId);
    Check(metadata && metadata->journalId && *metadata->journalId == 41 && metadata->resumeUsn == 900,
          "matching journal preserves the persisted resume position");
}

void TestChangedJournalTriggersReconciliation() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_journal_discontinuity.db")), "pipeline opens for discontinuity test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 2;
    const auto volumeId = pipeline.ResolveVolume(key);
    Check(pipeline.SetJournalPosition(volumeId, 41, 900), "persisted prior journal position is established");

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    manager.Start();
    connection.journalOpenedCallback_(ffprotocol::VolumeId{7}, 42, 1000);

    Check(pipeline.IsReconciliationPassActive(volumeId), "changed journal identity begins reconciliation rather than blind resume");
    const auto metadata = pipeline.GetVolumeMetadata(volumeId);
    Check(metadata && metadata->journalId && *metadata->journalId == 42 && metadata->resumeUsn == 1000,
          "changed journal resets the stored position to the new journal's current point");
}

void TestDisappearanceMarksUnavailableAndWithdrawsPublishedVolume() {
    ffengine::IndexPipeline pipeline;
    Check(pipeline.Open(FreshDbPath("engine_test_volume_disappearance.db")),
          "pipeline opens for volume-disappearance test");
    ffindexstore::VolumeKey key;
    key.serialNumber = 3;
    const auto volumeId = pipeline.ResolveVolume(key);

    ffengine::PrivilegedConnection connection;
    ffengine::VolumeSessionManager manager(pipeline, connection);
    AddSession(manager, volumeId);
    ffindexstore::VolumeRowId callbackVolume = 0;
    wchar_t callbackDrive = L'\0';
    manager.SetVolumeUnavailableCallback([&](ffindexstore::VolumeRowId id, wchar_t driveLetter) {
        callbackVolume = id;
        callbackDrive = driveLetter;
    });

    manager.Start();
    connection.volumeListCallback_({});

    const auto metadata = pipeline.GetVolumeMetadata(volumeId);
    Check(metadata && !metadata->available, "a disappeared volume is retained but marked unavailable");
    Check(callbackVolume == volumeId && callbackDrive == L'C',
          "a disappeared volume requests withdrawal of its published drive snapshot");
}

} // namespace

int main() {
    TestMatchingJournalResumesWithoutReconciliation();
    TestChangedJournalTriggersReconciliation();
    TestDisappearanceMarksUnavailableAndWithdrawsPublishedVolume();
    return g_failures == 0 ? 0 : 1;
}
