#pragma once
#include <string>

namespace ffsetup {

// Case-insensitive check that `processImagePath`'s directory is exactly
// `expectedInstallDir` (after canonicalizing both) and its filename matches
// `expectedFileName`. A prefix-only check (e.g. "C:\FastFiles" matching
// "C:\FastFilesEvil...") would be a bypass, so this requires an exact
// directory match.
//
// Shared implementation of the symmetric peer image-path check (design.md
// D4 "Symmetric Mutual Authentication"): the service verifies the engine
// (ClientAuthentication.cpp) and the engine verifies the service
// (PrivilegedConnection.cpp) through this single function so the two sides
// can never drift apart.
bool IsExpectedInstalledBinary(const std::wstring& processImagePath,
                               const std::wstring& expectedInstallDir,
                               const std::wstring& expectedFileName);

} // namespace ffsetup
