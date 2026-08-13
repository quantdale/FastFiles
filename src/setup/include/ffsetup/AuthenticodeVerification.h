#pragma once
#include <windows.h>

#include <optional>
#include <string>

#include "ffsetup/PinnedSignatures.h"

namespace ffsetup {

// Verifies the exact file object represented by `fileHandle` with WinTrust
// and returns the leaf signing certificate's SHA-1 thumbprint. The canonical
// path is retained for WinTrust's required diagnostic path field; hFile is the
// object-binding field and must remain open until WinTrust state is closed.
// Returns std::nullopt if the file is unsigned, tampered, or the chain does
// not verify.
std::optional<Thumbprint> VerifyAuthenticodeAndGetThumbprint(
    HANDLE fileHandle, const std::wstring& canonicalPath) noexcept;

// Opens a regular non-reparse file with delete/write sharing disabled and
// delegates to the handle-bound verifier. This is the only path convenience
// API; it never invokes path-only WinTrust.
std::optional<Thumbprint> VerifyAuthenticodeAndGetThumbprintAtPath(
    const std::wstring& filePath) noexcept;

// Verifies the signature AND that its thumbprint matches `expected`. Fails
// closed (returns false) if `expected` is still the all-zero placeholder,
// so an unconfigured pin can never be satisfied.
bool VerifyPinnedSignature(const std::wstring& filePath, const Thumbprint& expected) noexcept;

} // namespace ffsetup
