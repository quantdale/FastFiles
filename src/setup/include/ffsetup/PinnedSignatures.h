#pragma once
#include <array>
#include <cstdint>

namespace ffsetup {

// SHA-1 Authenticode leaf-certificate thumbprints pinned for mutual
// authentication (design.md D4 "Symmetric Mutual Authentication"; spec
// "Symmetric Mutual Authentication"). Populated by the release signing
// pipeline once a code-signing certificate is provisioned. Left as
// all-zero placeholders until then so pinning fails closed -- rejects
// every peer -- rather than silently accepting an unsigned or
// unexpectedly-signed binary.
using Thumbprint = std::array<uint8_t, 20>;

constexpr Thumbprint kExpectedEngineSignatureThumbprint{};
constexpr Thumbprint kExpectedIndexSvcSignatureThumbprint{};

constexpr bool IsPlaceholderThumbprint(const Thumbprint& thumbprint) noexcept {
    for (uint8_t byte : thumbprint) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

} // namespace ffsetup
