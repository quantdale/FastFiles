// FastFilesEngine: the unprivileged, per-logon-session index owner.
//
// Manages the privileged connection to FastFilesIndexSvc (connect,
// handshake, heartbeat, reconnect, degrade), serves FastFiles (UI) clients
// over a same-privilege control pipe, provides the degraded-mode
// (unprivileged FindFirstFileEx + ReadDirectoryChangesW) directory
// browsing path Column View always has available regardless of privileged-
// path state (design.md D5, establish-architecture-foundation), and owns
// the real, whole-volume filesystem index -- durable SQLite storage plus
// an in-memory projection, kept in sync from the privileged service's
// real MFT scan/USN journal streams (index-storage-and-scanning). UI-
// facing consumption of that index (search, storage analysis) is later
// changes' scope; this process makes the data available and correct.
#include <windows.h>
#include <chrono>
#include <iterator>
#include <string>

#include "ffindexstore/IndexStore.h"
#include "ffsetup/Identifiers.h"
#include "ffsetup/ScheduledTaskRegistration.h"

#include "IdleManager.h"
#include "IngestionPipeline.h"
#include "PrivilegedConnection.h"
#include "UiServer.h"

namespace {

std::wstring GetOwnPath() {
    wchar_t path[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return length == 0 ? L"" : std::wstring(path, length);
}

std::wstring DirectoryOf(const std::wstring& filePath) {
    const size_t lastSlash = filePath.find_last_of(L"\\/");
    return lastSlash == std::wstring::npos ? L"" : filePath.substr(0, lastSlash);
}

// index-storage-and-scanning: the durable index database is strictly
// per-user-profile/per-engine-instance (design.md Open Questions --
// assumed, consistent with the foundation change's per-logon-session
// engine model), so %LOCALAPPDATA% is the natural home for it.
std::wstring GetIndexDatabasePath() {
    wchar_t localAppData[MAX_PATH * 2] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (length == 0 || length >= std::size(localAppData)) {
        return L"";
    }
    std::wstring dir = std::wstring(localAppData) + L"\\FastFiles";
    CreateDirectoryW(dir.c_str(), nullptr); // idempotent; ignore ERROR_ALREADY_EXISTS
    return dir + L"\\index.db";
}

// Task 4.10: drop the privileged connection after 10 minutes of no UI
// window and no significant recent filesystem-change activity.
constexpr std::chrono::milliseconds kIdleTimeout{10 * 60 * 1000};

} // namespace

int wmain() {
    const std::wstring ownPath = GetOwnPath();
    const std::wstring installDir = DirectoryOf(ownPath);

    // Task 4.1: idempotently (re-)ensure the per-user Scheduled Task
    // registration is current -- best-effort; the installer is the
    // primary place this happens (task 6.1), but self-healing here means
    // a manually-copied or repaired install still ends up registered.
    ffsetup::RegisterEngineScheduledTask(ownPath);

    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);

    ffengine::UiServer uiServer;
    if (!uiServer.Start(sessionId)) {
        return 1;
    }

    // index-storage-and-scanning: the durable index store, rebuilt into
    // an in-memory projection before anything else runs (task 3.1). This
    // change's own scope is the storage/scanning layer only -- UI-visible
    // effects land through the later instant-search/storage-analysis
    // changes, so nothing here touches uiServer/SnapshotPublisher.
    ffindexstore::IndexStore indexStore;
    const std::wstring indexDbPath = GetIndexDatabasePath();
    if (!indexDbPath.empty()) {
        if (!indexStore.Open(indexDbPath) || !indexStore.LastIntegrityCheckPassed()) {
            // Task 1.7: a failed integrity check falls back to a fresh
            // scan rather than operating on data known to be corrupt.
            indexStore.Close();
            DeleteFileW(indexDbPath.c_str());
            DeleteFileW((indexDbPath + L"-wal").c_str());
            DeleteFileW((indexDbPath + L"-shm").c_str());
            indexStore.Open(indexDbPath);
        }
        indexStore.RebuildProjectionFromStore([](ffindexstore::DurableVolumeId) {
            // Task 3.2: each volume's own snapshot generation would be
            // published here once a UI-facing consumer exists; this
            // change's scope ends at making the rebuilt data available.
        });
    }

    ffengine::IdleManager idleManager;
    ffengine::PrivilegedConnection privilegedConnection;
    ffengine::IngestionPipeline ingestionPipeline;

    uiServer.onActivity = [&idleManager] { idleManager.NotifyActivity(); };

    ingestionPipeline.Start(privilegedConnection, indexStore, [] {
        // Hook point for a future UI-facing publication step
        // (instant-search/storage-analysis); intentionally a no-op here.
    });

    privilegedConnection.Start(installDir, [&uiServer, &ingestionPipeline](ffengine::ConnectionState state, ffengine::UnavailableReason) {
        uiServer.SetEngineStatus(state == ffengine::ConnectionState::Active);
        ingestionPipeline.OnConnectionStateChanged(state);
    });

    idleManager.Start(
        kIdleTimeout,
        [&privilegedConnection] { privilegedConnection.DropForIdle(); },
        [&privilegedConnection] { privilegedConnection.RequestReconnect(); });

    // This process has no message loop or console of its own -- it's a
    // per-session background helper launched by the Scheduled Task (or
    // lazily by the UI); it simply runs until the logon session ends.
    HANDLE parkEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (parkEvent == nullptr) {
        return 1;
    }
    WaitForSingleObject(parkEvent, INFINITE);
    return 0;
}
