#pragma once
#include <optional>
#include <string>

#include "ffsetup/PinnedSignatures.h"

namespace ffsetup {

// Verifies the file at path has a valid Authenticode signature chaining to
// a trusted root (not merely "carries some signature blob") and returns
// the leaf signing certificate's SHA-1 thumbprint. Returns std::nullopt if
// the file is unsigned, tampered, or the chain does not verify.
std::optional<Thumbprint> VerifyAuthenticodeAndGetThumbprint(const std::wstring& filePath) noexcept;

// Verifies the signature AND that its thumbprint matches `expected`. Fails
// closed (returns false) if `expected` is still the all-zero placeholder,
// so an unconfigured pin can never be satisfied.
bool VerifyPinnedSignature(const std::wstring& filePath, const Thumbprint& expected) noexcept;

} // namespace ffsetup
