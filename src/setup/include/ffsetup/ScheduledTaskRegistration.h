#pragma once
#include <string>

#include "ffsetup/SetupResult.h"

namespace ffsetup {

// Registers the per-user FastFilesEngine Scheduled Task: a logon trigger
// for members of the built-in Users group, RunLevel=LUA (least privilege)
// (task 4.1, 6.1). Idempotent -- re-running updates the existing
// registration (TASK_CREATE_OR_UPDATE) rather than failing if it already
// exists, so it is also safe to call on upgrade.
//
// enginePath must be the fully-qualified path to FastFilesEngine.exe under
// the install directory. Must be called from a context that already has
// COM initialized (CoInitializeEx) on the calling thread, OR will
// initialize/uninitialize COM itself if not already initialized -- see
// implementation.
SetupResult RegisterEngineScheduledTask(const std::wstring& enginePath) noexcept;

// Reverses RegisterEngineScheduledTask (task 6.4 uninstall path).
// Not-found is treated as success.
SetupResult UnregisterEngineScheduledTask() noexcept;

} // namespace ffsetup
