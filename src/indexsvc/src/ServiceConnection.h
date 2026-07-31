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

// The Data pipe carries no defined traffic until real MFT/USN batch
// streaming lands in the follow-up change (task 3.8's scan/journal calls
// are stubbed) -- this just accepts and closes the connection so the pipe
// itself exists and is reachable/secured (task 3.2) without leaving a
// thread parked on undefined behavior.
void RunDataConnection(HANDLE pipeHandle);

} // namespace ffindexsvc
