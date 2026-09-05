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
#include <cstring>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include "ffsetup/Identifiers.h"
#include "ffsetup/PinnedSignatures.h"
#include "ffsetup/ScheduledTaskRegistration.h"
#include "ffprotocol/Diagnostics.h"
#include "ffprotocol/Settings.h"

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
    std::string dbPathUtf8(static_cast<size_t>(utf8Length), '\0');
    const int converted = WideCharToMultiByte(CP_UTF8, 0, dbPathWide.c_str(), -1, dbPathUtf8.data(), utf8Length, nullptr, nullptr);
    if (converted <= 0) {
        return {};
    }
    dbPathUtf8.resize(static_cast<size_t>(converted - 1));
    return dbPathUtf8;
}

// Task 1.7's rebuild-from-fresh-scan fallback helper: deletes the database
// file plus its WAL/SHM siblings after a failed integrity check, so the
// next open starts from a clean, empty store that gets rebuilt by a fresh
// scan. The path arrives UTF-8 (Store's portable boundary); convert back
// to wide for the Win32 delete calls -- GetIndexDatabasePathUtf8 produced
// it from a wide path, so the round-trip is lossless.
void DeleteIndexDatabaseFiles(const std::string& dbPathUtf8) {
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, dbPathUtf8.c_str(), -1, nullptr, 0);
    if (wideLength <= 0) {
        return;
    }
    std::wstring dbPathWide(static_cast<size_t>(wideLength), L'\0');
    const int converted = MultiByteToWideChar(CP_UTF8, 0, dbPathUtf8.c_str(), -1, dbPathWide.data(), wideLength);
    if (converted <= 0) {
        return;
    }
    dbPathWide.resize(static_cast<size_t>(converted - 1));
    DeleteFileW(dbPathWide.c_str());
    DeleteFileW((dbPathWide + L"-wal").c_str());
    DeleteFileW((dbPathWide + L"-shm").c_str());
}

// Task 4.10: drop the privileged connection after 10 minutes without
// significant recent activity (UI requests or filesystem-change volume).
constexpr std::chrono::milliseconds kIdleTimeout{10 * 60 * 1000};

const wchar_t* ConnectionStateName(ffengine::ConnectionState state) {
    switch (state) {
        case ffengine::ConnectionState::Disconnected: return L"disconnected";
        case ffengine::ConnectionState::Connecting: return L"connecting";
        case ffengine::ConnectionState::Handshaking: return L"handshaking";
        case ffengine::ConnectionState::Active: return L"active";
    }
    return L"unknown";
}

const wchar_t* UnavailableReasonName(ffengine::UnavailableReason reason) {
    switch (reason) {
        case ffengine::UnavailableReason::None: return L"none";
        case ffengine::UnavailableReason::ServiceNotRunning: return L"service-not-running";
        case ffengine::UnavailableReason::UnverifiedServiceIdentity: return L"unverified-service-identity";
        case ffengine::UnavailableReason::HandshakeRejected: return L"handshake-rejected";
        case ffengine::UnavailableReason::IncompatibleProtocolVersion: return L"incompatible-protocol-version";
        case ffengine::UnavailableReason::IdleDropped: return L"idle-dropped";
    }
    return L"unknown";
}

} // namespace

int wmain() {
#ifndef _DEBUG
    if (!ffsetup::ArePinnedSignaturesConfigured()) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_AUTHENTICODE_PINS_UNCONFIGURED,
                                      {}, {}, L"authenticode-pins-unconfigured", ERROR_INVALID_DATA, 0});
        std::fwprintf(stderr,
                      L"FastFilesEngine: refusing non-debug startup because Authenticode pins are not provisioned.\n");
        return 1;
    }
#endif

    const std::wstring ownPath = GetOwnPath();
    const std::wstring installDir = DirectoryOf(ownPath);

    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);

    ffengine::UiServer uiServer;
    if (!uiServer.Start(sessionId)) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_ENGINE_UI_SERVER_START_FAILED,
                                      ownPath, {}, L"engine-ui-server-start-failed", GetLastError(), 0});
        return 1;
    }

    // Task 4.1: idempotently (re-)ensure the per-user Scheduled Task
    // registration is current -- best-effort; the installer is the
    // primary place this happens (task 6.1), but self-healing here means
    // a manually-copied or repaired install still ends up registered.
    // Keep this after UiServer::Start: Task Scheduler COM registration can
    // take several seconds on a cold desktop, and the UI control pipe must
    // become reachable immediately so the application does not spend its
    // bounded connection window retrying a helper that is already running.
    ffsetup::RegisterEngineScheduledTask(ownPath);

    ffengine::IndexPipeline indexPipeline;
    const std::string dbPathUtf8 = GetIndexDatabasePathUtf8();
    if (!dbPathUtf8.empty()) {
        // index-durability-false-success-closure D6 (PA-006): the
        // delete-and-rebuild fallback is reserved for a *proven* corrupt
        // verdict. A failed open whose integrity check could not reach a
        // verdict -- an exclusive lock held by a lingering process, an
        // I/O hiccup, BUSY/LOCKED/NOMEM/IOERR -- is retried a bounded
        // number of times with the file preserved; destroying a healthy
        // index because a check was inconclusive is the failure this
        // closes. Corruption remains fatal exactly as before (task 1.7).
        constexpr int kOpenAttempts = 3;
        constexpr DWORD kOpenRetryDelayMs = 500;
        bool opened = false;
        for (int attempt = 0; attempt < kOpenAttempts && !opened; ++attempt) {
            if (attempt > 0) {
                Sleep(kOpenRetryDelayMs);
            }
            bool integrityFailed = false;
            opened = indexPipeline.Open(dbPathUtf8, &integrityFailed);
            if (!opened && integrityFailed) {
                ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::DatabaseError,
                                              ffprotocol::DiagnosticCode::FFDIAG_CORRUPT_DB,
                                              {}, {}, L"index-database-corrupt", ERROR_INVALID_DATA, 0});
                // Task 1.7's defined fallback: an existing database that fails
                // its integrity check is never trusted -- delete it (plus its
                // WAL/SHM siblings) and reopen a fresh, empty store, which the
                // normal scan/reconciliation machinery then rebuilds from a
                // fresh scan.
                std::fwprintf(stderr,
                              L"FastFilesEngine: index database at %hs failed its integrity check -- deleting it and rebuilding from a fresh scan\n",
                              dbPathUtf8.c_str());
                indexPipeline.Close();
                DeleteIndexDatabaseFiles(dbPathUtf8);
                opened = indexPipeline.Open(dbPathUtf8);
                break; // the fresh-reopen result is terminal: never delete again
            }
            if (!opened && attempt + 1 < kOpenAttempts) {
                ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::DatabaseError,
                                              ffprotocol::DiagnosticCode::FFDIAG_DB_INTEGRITY_INCONCLUSIVE,
                                              {}, {}, L"index-database-open-inconclusive-retry", ERROR_RETRY, 0});
            }
        }
        if (!opened) {
            ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::DatabaseError,
                                          ffprotocol::DiagnosticCode::FFDIAG_DATABASE_OPEN_FAILED,
                                          {}, {}, L"index-database-open-failed", ERROR_OPEN_FAILED, 0});
            // An unopenable durable store (for reasons other than
            // integrity, or if the delete-and-reopen fallback itself
            // failed) is not fatal to the process -- the engine still
            // serves degraded-mode browsing; scanning simply has nowhere
            // durable to persist to until the next restart finds a
            // healthy database (e.g. after the file is manually removed).
            std::fwprintf(stderr, L"FastFilesEngine: failed to open index database at %hs -- indexing disabled this session\n",
                          dbPathUtf8.c_str());
        }
    }

    ffengine::IdleManager idleManager;
    ffengine::PrivilegedConnection privilegedConnection;
    ffengine::VolumeSessionManager volumeSessions(indexPipeline, privilegedConnection);

    // The engine owns applying indexing decisions. Reload is notify-then-pull:
    // settings.json is atomically written by the UI before this message.
    uiServer.onReloadIndexingConfig = [&volumeSessions] {
        volumeSessions.ReloadConfiguration(ffprotocol::LoadSettings(false).indexing);
    };
    uiServer.onRequestUnavailableVolumes = [&indexPipeline] {
        std::vector<ffprotocol::UnavailableVolumeRecord> records;
        for (const auto& metadata : indexPipeline.GetAllVolumes()) {
            if (metadata.available) {
                continue;
            }
            ffprotocol::UnavailableVolumeRecord record{};
            record.volumeRowId = metadata.rowId;
            std::memcpy(record.volumeGuid, metadata.key.volumeGuid.data(), metadata.key.volumeGuid.size());
            record.serialNumber = metadata.key.serialNumber;
            record.entryCount = metadata.entryCount;
            records.push_back(record);
        }
        return records;
    };
    uiServer.onForgetUnavailableVolume = [&indexPipeline](int64_t volumeRowId) {
        const auto metadata = indexPipeline.GetVolumeMetadata(volumeRowId);
        if (!metadata) {
            return ffprotocol::ForgetUnavailableVolumeStatus::NotFound;
        }
        if (metadata->available) {
            return ffprotocol::ForgetUnavailableVolumeStatus::VolumeAvailable;
        }
        return indexPipeline.ForgetVolume(volumeRowId)
            ? ffprotocol::ForgetUnavailableVolumeStatus::Removed
            : ffprotocol::ForgetUnavailableVolumeStatus::Failed;
    };
    uiServer.onRequestFolderAggregate = [&indexPipeline](ffindexstore::VolumeRowId volumeId, ffindexstore::FileId parentFrn) -> std::optional<ffindexstore::Projection::FolderAggregate> {
        return indexPipeline.GetFolderAggregate(volumeId, parentFrn);
    };
    uiServer.onRequestIndexedSearch = [&indexPipeline](const ffprotocol::IndexedSearchRequest& request,
                                                       const ffengine::UiServer::SearchCancellation& cancelled) {
        return indexPipeline.SearchIndexed(request, nullptr, cancelled);
    };
    uiServer.onRequestVolumeStatus = [&volumeSessions] {
        return volumeSessions.CollectVolumeStatus();
    };
    uiServer.onSetIndexingPaused = [&volumeSessions](uint8_t scope, bool paused) {
        volumeSessions.SetIndexingPaused(scope, paused);
    };
    const auto initialSettings = ffprotocol::LoadSettings(false);
    volumeSessions.ReloadConfiguration(initialSettings.indexing);

    uiServer.onActivity = [&idleManager] { idleManager.NotifyActivity(); };

    volumeSessions.SetSnapshotReadyCallback([&uiServer](ffindexstore::VolumeRowId,
                                                          std::map<std::wstring, ffprotocol::SnapshotDirectory> directories) {
        uiServer.MergeIndexDirectories(std::move(directories));
    });
    volumeSessions.SetVolumeUnavailableCallback(
        [&uiServer](ffindexstore::VolumeRowId, wchar_t driveLetter) { uiServer.RemoveVolumeDirectories(driveLetter); });
    volumeSessions.Start();

    // task 3.1/3.2: rebuild every already-known volume from the durable
    // store before the privileged connection even comes up, so a UI
    // window opened immediately at launch still sees whatever was
    // already persisted from a prior session, per volume, as soon as
    // that volume's rebuild completes.
    indexPipeline.RebuildAll([&uiServer, &indexPipeline](ffindexstore::VolumeRowId volumeId) {
        auto meta = indexPipeline.GetVolumeMetadata(volumeId);
        if (!meta || !meta->available) {
            return;
        }
        if (!meta->scanComplete) {
            // close-independent-validation-gaps 4.6: an interrupted initial
            // scan is rebuilt into the projection for durable correctness,
            // but its complete snapshot export is deferred. Exporting a
            // large incomplete volume synchronously here would block the
            // privileged connection from starting and delay cursor resume;
            // VolumeSessionManager forces the current view at ScanComplete.
            return;
        }
        const wchar_t driveLetter = ffengine::ResolveDriveLetterForVolumeKey(meta->key);
        if (driveLetter == L'\0') {
            return; // not currently mounted -- nothing to publish under yet
        }
        std::wstring prefix;
        prefix.push_back(driveLetter);
        prefix.push_back(L':');
        // memory-bounded-indexed-scale 5.1: bounded top-of-tree working-set
        // export at startup rebuild (see VolumeSessionManager scan-complete
        // publication for the same contract).
        uiServer.MergeIndexDirectories(indexPipeline.ExportDirectorySnapshotBounded(
            volumeId, prefix, ffengine::IndexPipeline::kInitialIndexedExportDirectories));
    });

    privilegedConnection.Start(installDir, [&uiServer, &volumeSessions](ffengine::ConnectionState state,
                                                                         ffengine::UnavailableReason reason) {
        // Keep the privileged-path failure stage visible in the metadata-only
        // diagnostic log. The UI intentionally exposes only degraded mode;
        // support and verification need to distinguish service absence from
        // mutual-authentication failure without logging peer contents.
        if (state == ffengine::ConnectionState::Disconnected && reason != ffengine::UnavailableReason::None) {
            const std::wstring detail = std::wstring(L"privileged-connection-")
                + ConnectionStateName(state) + L"/" + UnavailableReasonName(reason);
            ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                          ffprotocol::DiagnosticCode::FFDIAG_ENGINE_CONNECT_FAILED,
                                          {}, {}, detail, ERROR_INVALID_DATA, 0});
        }
        uiServer.SetEngineStatus(state == ffengine::ConnectionState::Active);
        volumeSessions.OnConnectionStateChanged(state);
    });

    idleManager.Start(
        kIdleTimeout,
        [&privilegedConnection, &indexPipeline] {
            const auto settings = ffprotocol::LoadSettings(false);
            // address-engineering-audit-findings 5.2: the idle callback is
            // the only production path allowed to attempt expensive VACUUM.
            indexPipeline.RunStoreMaintenance(true, settings.vacuumIntervalHours,
                                               settings.vacuumSizeGrowthPercent);
            privilegedConnection.DropForIdle();
        },
        [&privilegedConnection] { privilegedConnection.RequestReconnect(); });

    // This process has no message loop or console of its own -- it's a
    // per-session background helper launched by the Scheduled Task (or
    // lazily by the UI); it simply runs until the logon session ends.
    HANDLE parkEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (parkEvent == nullptr) {
        ffprotocol::AppendDiagnostic({ffprotocol::DiagnosticCategory::VolumeStateTransition,
                                      ffprotocol::DiagnosticCode::FFDIAG_ENGINE_PARK_EVENT_FAILED,
                                      ownPath, {}, L"engine-park-event-failed", GetLastError(), 0});
        return 1;
    }
    WaitForSingleObject(parkEvent, INFINITE);
    return 0;
}
