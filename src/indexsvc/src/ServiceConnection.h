#pragma once
#include <windows.h>
#include <string>

#include "ConnectionRegistry.h"

namespace ffindexsvc {

// Runs the full lifetime of one Ctrl-pipe connection: Handshake (with
// mutual authentication), the closed command dispatch loop, periodic
// re-validation on Heartbeat, and teardown of any owned scans/journals on
// disconnect (tasks 3.4-3.9). Takes ownership of pipeHandle -- closes it
// before returning.
//
// installDir is the ACL-locked directory both FastFilesIndexSvc.exe and
// FastFilesEngine.exe are installed into; registry is shared across all
// connections so Stop/Close ownership scoping works across the whole
// service process, not just within one connection.
void RunCtrlConnection(HANDLE pipeHandle, const std::wstring& installDir, ConnectionRegistry& registry);

// index-storage-and-scanning streams real MFT/USN batches asynchronously
// over the same Ctrl connection that issued StartVolumeScan/OpenUsnJournal
// (see ServiceConnection.cpp) rather than over this Data pipe -- doing so
// keeps VolumeId's existing connection-scoped ownership rules
// (ConnectionRegistry) intact without inventing a second connection's
// worth of authentication/correlation machinery, consistent with this
// change not reopening the transport design established in
// establish-architecture-foundation. The Data pipe therefore still
// carries no defined traffic; this just accepts and closes the connection
// so the pipe itself remains reachable/secured (task 3.2) without leaving
// a thread parked on undefined behavior.
void RunDataConnection(HANDLE pipeHandle);

} // namespace ffindexsvc
