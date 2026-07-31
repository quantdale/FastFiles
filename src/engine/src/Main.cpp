// FastFilesEngine: the unprivileged, per-logon-session index owner.
//
// Manages the privileged connection to FastFilesIndexSvc (connect,
// handshake, heartbeat, reconnect, degrade), serves FastFiles (UI) clients
// over a same-privilege control pipe, and provides the degraded-mode
// (unprivileged FindFirstFileEx + ReadDirectoryChangesW) directory
// browsing path that Column View reads from in this change -- real
// MFT/USN-backed indexing lands in a follow-up change. See
// openspec/changes/establish-architecture-foundation.
#include <windows.h>
#include <chrono>
#include <iterator>
#include <string>

#include "ffsetup/Identifiers.h"
#include "ffsetup/ScheduledTaskRegistration.h"

#include "IdleManager.h"
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

    ffengine::IdleManager idleManager;
    ffengine::PrivilegedConnection privilegedConnection;

    uiServer.onActivity = [&idleManager] { idleManager.NotifyActivity(); };

    privilegedConnection.Start(installDir, [&uiServer](ffengine::ConnectionState state, ffengine::UnavailableReason) {
        uiServer.SetEngineStatus(state == ffengine::ConnectionState::Active);
    });

    idleManager.Start(
        kIdleTimeout,
        [&privilegedConnection] { privilegedConnection.DropForIdle(); },
        [&privilegedConnection] { privilegedConnection.RequestReconnect(); });

    // This process has no message loop or console of its own -- it's a
    // per-session background helper launched by the Scheduled Task (or
    // lazily by the UI); it simply runs until the logon session ends.
    HANDLE parkEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    WaitForSingleObject(parkEvent, INFINITE);
    return 0;
}
