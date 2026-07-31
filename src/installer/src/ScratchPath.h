#pragma once
#include <optional>
#include <string>

namespace ffinstaller {

// Task 6.2: creates a randomized, per-run scratch directory under
// %ProgramData%\FastFiles\InstallScratch and verifies it is not a reparse
// point before returning it -- the classic elevated-installer TOCTOU class
// is a predictable scratch path plus a junction/symlink swapped in before
// the installer writes to it; a random name defeats prediction, and the
// reparse-point check catches a race that won by other means. Returns
// std::nullopt if creation or verification fails; the caller must treat
// that as fatal for whatever step needed scratch space, not fall back to
// a predictable path.
std::optional<std::wstring> CreateVerifiedScratchDirectory();

} // namespace ffinstaller
