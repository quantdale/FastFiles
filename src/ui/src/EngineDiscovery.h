#pragma once
#include <string>
#include <vector>

namespace ffui {

// recovery-degraded-browsing D1: deterministic engine executable discovery
// for the UI's "lazy start" (EngineClient). Candidates are derived only from
// the UI's own location and the fixed install location -- never a PATH
// lookup or a disk-wide search -- so a rogue binary in an arbitrary
// directory can never be picked up.
//
// Priority order:
//   1. Same directory as the UI executable (installed layout: all
//      executables staged together under the ACL-locked install dir).
//   2. Sibling "engine" directory of the UI's parent (development layout:
//      build/<config>/src/ui -> build/<config>/src/engine), normalized so
//      embedded ".." can never escape the derived tree.
//   3. %ProgramFiles%\FastFiles (installer layout).
std::vector<std::wstring> EngineDiscoveryCandidates(const std::wstring& uiExecutablePath);

// True when `path` names an existing regular file (not a directory).
bool IsValidEngineExecutable(const std::wstring& path);

} // namespace ffui
