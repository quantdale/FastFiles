#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#if __has_include("ffsetup/PinnedSignaturesConfig.h")
#include "ffsetup/PinnedSignaturesConfig.h"
#endif

namespace ffsetup {

// SHA-1 Authenticode leaf-certificate thumbprints pinned for mutual
// authentication (design.md D4 "Symmetric Mutual Authentication"). The
// release build generates these values from the signing pipeline; an
// unprovisioned development configuration retains the all-zero fallback so
// pinning fails closed rather than accepting an unsigned or unexpectedly
// signed binary.
using Thumbprint = std::array<uint8_t, 20>;

#ifdef FASTFILES_ENGINE_SIGNATURE_THUMBPRINT_INITIALIZER
constexpr Thumbprint kExpectedEngineSignatureThumbprint =
    FASTFILES_ENGINE_SIGNATURE_THUMBPRINT_INITIALIZER;
#else
constexpr Thumbprint kExpectedEngineSignatureThumbprint{};
#endif

#ifdef FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT_INITIALIZER
constexpr Thumbprint kExpectedIndexSvcSignatureThumbprint =
    FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT_INITIALIZER;
#else
constexpr Thumbprint kExpectedIndexSvcSignatureThumbprint{};
#endif

constexpr bool IsPlaceholderThumbprint(const Thumbprint& thumbprint) noexcept {
    for (uint8_t byte : thumbprint) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

// Both peers must be pinned before a non-debug build can activate the
// privileged path.  This is also used by each executable's startup self-check
// as defense in depth if a generated header is replaced after configuration.
constexpr bool ArePinnedSignaturesConfigured() noexcept {
    return !IsPlaceholderThumbprint(kExpectedEngineSignatureThumbprint) &&
           !IsPlaceholderThumbprint(kExpectedIndexSvcSignatureThumbprint);
}

inline std::wstring ThumbprintToHex(const Thumbprint& thumbprint) {
    constexpr wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring result;
    result.reserve(thumbprint.size() * 2);
    for (const uint8_t byte : thumbprint) {
        result.push_back(kHex[(byte >> 4) & 0x0F]);
        result.push_back(kHex[byte & 0x0F]);
    }
    return result;
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
