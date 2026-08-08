#pragma once
#include <array>
#include <cstddef>
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

// Compare the fixed-size digest without an early-exit equality operator. The
// thumbprint is not secret, but keeping the peer-authentication primitive
// constant-time avoids making timing behavior part of the trust decision.
inline bool ConstantTimeThumbprintEqual(const Thumbprint& left,
                                        const Thumbprint& right) noexcept {
    uint8_t difference = 0;
    for (size_t index = 0; index < left.size(); ++index) {
        difference = static_cast<uint8_t>(difference | (left[index] ^ right[index]));
    }
    return difference == 0;
}

} // namespace ffsetup
