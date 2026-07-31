// FastFilesEngine: the unprivileged, per-logon-session index owner.
//
// Manages the privileged connection to FastFilesIndexSvc (connect,
// handshake, heartbeat, reconnect, degrade), serves FastFiles (UI) clients
// over a same-privilege control pipe, and provides the degraded-mode
// (unprivileged FindFirstFileEx + ReadDirectoryChangesW) directory
// browsing path Column View falls back to when the privileged path is
// unavailable. Real MFT/USN-backed indexing (index-storage-and-scanning)
// is owned by IndexPipeline/VolumeSessionManager, wired in below. See
// openspec/changes/establish-architecture-foundation and
// openspec/changes/index-storage-and-scanning.
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>

#include <chrono>
#include <cstdio>
#include <iterator>
#include <string>

#include "ffsetup/Identifiers.h"
#include "ffsetup/ScheduledTaskRegistration.h"

#include "IdleManager.h"
#include "IndexPipeline.h"
#include "PrivilegedConnection.h"
#include "UiServer.h"
#include "VolumeIdentity.h"
#include "VolumeSessionManager.h"

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

// index-storage-and-scanning: the durable SQLite store lives under the
// current user's local (non-roaming) profile -- per-user, per-machine,
// never shared across logon sessions (design.md "Open Questions",
// assumed consistent with the foundation change's per-logon-session
// engine model).
std::string GetIndexDatabasePathUtf8() {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)) || localAppData == nullptr) {
        return {};
    }
    std::wstring dir(localAppData);
    CoTaskMemFree(localAppData);
    dir += L"\\FastFiles";
    CreateDirectoryW(dir.c_str(), nullptr); // idempotent; ignore ERROR_ALREADY_EXISTS

    const std::wstring dbPathWide = dir + L"\\index.db";
    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, dbPathWide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0) {
        return {};
    }
    std::string dbPathUtf8(static_cast<size_t>(utf8Length) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, dbPathWide.c_str(), -1, dbPathUtf8.data(), utf8Length, nullptr, nullptr);
    return dbPathUtf8;
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

    ffengine::IndexPipeline indexPipeline;
    const std::string dbPathUtf8 = GetIndexDatabasePathUtf8();
    if (!dbPathUtf8.empty() && !indexPipeline.Open(dbPathUtf8)) {
        // Task 1.7's engine-side policy: an unopenable/corrupt durable
        // store is not fatal to the process -- the engine still serves
        // degraded-mode browsing; scanning simply has nowhere durable to
        // persist to until the next restart finds a healthy database
        // (e.g. after the file is manually removed).
        std::fwprintf(stderr, L"FastFilesEngine: failed to open index database at %hs -- indexing disabled this session\n",
                      dbPathUtf8.c_str());
    }

    ffengine::IdleManager idleManager;
    ffengine::PrivilegedConnection privilegedConnection;
    ffengine::VolumeSessionManager volumeSessions(indexPipeline, privilegedConnection);

    uiServer.onActivity = [&idleManager] { idleManager.NotifyActivity(); };

    volumeSessions.SetSnapshotReadyCallback([&uiServer](ffindexstore::VolumeRowId,
                                                          std::map<std::wstring, ffprotocol::SnapshotDirectory> directories) {
        uiServer.MergeIndexDirectories(std::move(directories));
    });
    volumeSessions.Start();

    // task 3.1/3.2: rebuild every already-known volume from the durable
    // store before the privileged connection even comes up, so a UI
    // window opened immediately at launch still sees whatever was
    // already persisted from a prior session, per volume, as soon as
    // that volume's rebuild completes.
    indexPipeline.RebuildAll([&uiServer, &indexPipeline](ffindexstore::VolumeRowId volumeId) {
        auto meta = indexPipeline.GetVolumeMetadata(volumeId);
        if (!meta) {
            return;
        }
        const wchar_t driveLetter = ffengine::ResolveDriveLetterForVolumeKey(meta->key);
        if (driveLetter == L'\0') {
            return; // not currently mounted -- nothing to publish under yet
        }
        std::wstring prefix;
        prefix.push_back(driveLetter);
        prefix.push_back(L':');
        uiServer.MergeIndexDirectories(indexPipeline.ExportDirectorySnapshot(volumeId, prefix));
    });

    privilegedConnection.Start(installDir, [&uiServer, &volumeSessions](ffengine::ConnectionState state, ffengine::UnavailableReason) {
        uiServer.SetEngineStatus(state == ffengine::ConnectionState::Active);
        volumeSessions.OnConnectionStateChanged(state);
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
