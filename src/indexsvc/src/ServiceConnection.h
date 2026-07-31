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

// The Data pipe carries no defined traffic: index-storage-and-scanning's
// real MFT/USN batch streaming (tasks.md 4.4/5.4) runs on the same Ctrl
// connection the StartVolumeScan/OpenUsnJournal request arrived on
// (a background thread per active scan/journal writes batch frames to
// that same pipe handle, serialized against the Ctrl loop's own writes --
// see ServiceConnection.cpp) rather than needing a second pipe. This just
// accepts and closes the connection so the Data pipe itself exists and is
// reachable/secured (task 3.2) without leaving a thread parked on
// undefined behavior.
void RunDataConnection(HANDLE pipeHandle);

} // namespace ffindexsvc
