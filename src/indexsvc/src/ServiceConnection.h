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
//
// shutdownEvent is the owning PipeListener's manual-reset stop signal (see
// PipeListener::ShutdownEvent). The control loop blocks on it alongside the
// inbound read so listener shutdown wakes the loop deterministically instead
// of waiting out the full inbound idle deadline. May be null, in which case
// the loop falls back to the deadline-bounded read alone.
void RunCtrlConnection(HANDLE pipeHandle, HANDLE shutdownEvent, const std::wstring& installDir,
                       ConnectionRegistry& registry);

} // namespace ffindexsvc
