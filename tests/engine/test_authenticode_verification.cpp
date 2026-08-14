// Mutual-authentication pin enforcement (resolve-raw-volume-privilege-
// insufficiency task 3.1 / design.md D4 "Symmetric Mutual Authentication").
//
// The production privileged path only activates when a peer passes
// ffsetup::VerifyPinnedSignature against a configured non-placeholder
// thumbprint. These tests pin that contract:
//   * unconfigured (all-zero) pins must fail closed -- reject every binary;
//   * unsigned binaries must be rejected even when a pin is configured;
//   * a signed binary with a mismatched pin must be rejected;
//   * a signed binary matching its real thumbprint must be accepted;
//   * a signed-but-untrusted-root binary must be rejected by real WinTrust.
//
// The positive case re-derives the thumbprint of a known Microsoft-signed
// system binary with the same WinVerifyTrust call the production code uses,
// so the test stays valid after the release signing pipeline provisions the
// real pins.
//
// PORTABLE SUITE: this executable never mutates a trust store (no
// CurrentUser\Root writes) and needs no elevation, production signer, or
// production pins. The real-WinTrust CurrentUser\Root integration cases
// (trusted signer acceptance, expiry transition, RFC3161 timestamp
// longevity) live in the separate, opt-in
// ffengine_authenticode_wintrust_integration_tests target -- see AGENTS.md
// "Normal FastFiles development" and verify/run_privileged_trust_tests.ps1.

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "ffsetup/AuthenticodeVerification.h"
#include "ffsetup/PinnedSignatures.h"
#include "AuthenticodeFixtureSupport.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

void TestUntrustedWinTrustFixture() {
    // close-independent-validation-gaps 4.2: a signed PE whose issuing root
    // was never trusted must be rejected by real WinTrust. This case never
    // installs anything into CurrentUser\Root, so it stays in the portable
    // suite: it needs only SignTool and a CurrentUser\My self-signed
    // certificate, both of which are ordinary non-elevated operations.
    const auto modulePath = CurrentModulePath();
    if (!modulePath.has_value()) {
        Check(false, "untrusted WinTrust fixture requires the current test executable path");
        return;
    }
    const std::filesystem::path scriptPath = modulePath->parent_path() / L"AuthenticodeFixture.ps1";
    Check(std::filesystem::exists(scriptPath), "untrusted WinTrust fixture: fixture helper is deployed beside the test");
    if (!std::filesystem::exists(scriptPath)) {
        return;
    }

    const auto fixtureRoot = CreateFixtureRoot();
    Check(fixtureRoot.has_value(), "untrusted WinTrust fixture: isolated temporary directory is created");
    if (!fixtureRoot.has_value()) {
        return;
    }

    TestRealWinTrustFixture(scriptPath, *fixtureRoot, L"Untrusted",
                            "untrusted WinTrust fixture: untrusted development root certificate is generated and signed",
                            /*expectedAcceptance=*/false);

    std::error_code error;
    const bool removed = std::filesystem::remove(*fixtureRoot, error) && !error;
    Check(removed, "untrusted WinTrust fixture: isolated temporary directory is empty after cleanup");
}

void TestUnconfiguredPinsFailClosed() {
    const std::wstring unsignedFile = TempUnsignedFile();
    const ffsetup::Thumbprint placeholder{};
    Check(!ffsetup::VerifyPinnedSignature(unsignedFile, placeholder),
          "unconfigured pins: an all-zero pin rejects every peer, even an unsigned one");
    Check(!ffsetup::VerifyPinnedSignature(L"C:\\definitely-not-a-file.exe", placeholder),
          "unconfigured pins: rejection does not depend on the file existing");
    DeleteFileW(unsignedFile.c_str());
}

void TestUnsignedBinaryRejectedWithConfiguredPin() {
    const std::wstring unsignedFile = TempUnsignedFile();
    const ffsetup::Thumbprint configuredPin = ThumbprintWithByte(0, 0xAB);
    Check(!ffsetup::VerifyPinnedSignature(unsignedFile, configuredPin),
          "configured pin: an unsigned binary is rejected (no signature, no match)");
    DeleteFileW(unsignedFile.c_str());
}

void TestMismatchedPinRejectsSignedBinary() {
    const auto signedBinary = FirstSignedSystemBinary();
    if (!signedBinary.has_value()) {
        std::fprintf(stderr, "SKIP: no signed system binary found to prove the mismatch path\n");
        return;
    }
    const std::optional<ffsetup::Thumbprint> actual = SignedThumbprint(*signedBinary);
    Check(actual.has_value(), "mismatched pin: re-derived thumbprint of the signed binary");
    if (actual.has_value()) {
        const ffsetup::Thumbprint wrongPin = ThumbprintWithByte(0, static_cast<uint8_t>(actual->at(0) ^ 0xFF));
        Check(!ffsetup::VerifyPinnedSignature(*signedBinary, wrongPin),
              "mismatched pin: a signed binary is rejected when the pin does not match its signer");
    }
}

void TestMatchingPinAcceptsSignedBinary() {
    const auto signedBinary = FirstSignedSystemBinary();
    if (!signedBinary.has_value()) {
        std::fprintf(stderr, "SKIP: no signed system binary found to prove the acceptance path\n");
        return;
    }
    const std::optional<ffsetup::Thumbprint> actual = SignedThumbprint(*signedBinary);
    Check(actual.has_value(), "matching pin: re-derived thumbprint of the signed binary");
    if (actual.has_value()) {
        Check(ffsetup::VerifyPinnedSignature(*signedBinary, *actual),
              "matching pin: a signed binary is accepted when the pin matches its signer");
    }
}

void TestThumbprintComparison() {
    const ffsetup::Thumbprint first = ThumbprintWithByte(0, 0x12);
    const ffsetup::Thumbprint same = ThumbprintWithByte(0, 0x12);
    const ffsetup::Thumbprint different = ThumbprintWithByte(19, 0x34);
    Check(ffsetup::ConstantTimeThumbprintEqual(first, same),
          "thumbprint comparison accepts identical fixed-size digests");
    Check(!ffsetup::ConstantTimeThumbprintEqual(first, different),
          "thumbprint comparison rejects a difference at the final byte");
}

void TestPartiallyPopulatedPinsRejectUnpinnedPeer() {
    const auto signedBinary = FirstSignedSystemBinary();
    if (!signedBinary.has_value()) {
        std::fprintf(stderr, "SKIP: no signed system binary found to prove partial-pin rejection\n");
        return;
    }
    const std::optional<ffsetup::Thumbprint> actual = SignedThumbprint(*signedBinary);
    Check(actual.has_value(), "partial pin set: re-derived thumbprint of the signed binary");
    if (actual.has_value()) {
        Check(ffsetup::VerifyPinnedSignature(*signedBinary, *actual),
              "partial pin set: the populated peer pin remains usable");
        const ffsetup::Thumbprint placeholder{};
        Check(!ffsetup::VerifyPinnedSignature(*signedBinary, placeholder),
              "partial pin set: the peer whose pin remains placeholder is rejected");
    }
}

} // namespace

int wmain() {
    // resolve-raw-volume-privilege-insufficiency task 3.1: pin enforcement.
    // close-independent-validation-gaps 4.2: run the untrusted-signer
    // WinTrust transition before the general system-signer probes. WinTrust
    // may retain provider/store state for the life of this process;
    // establishing the isolated test root first keeps that cache state from
    // interfering with the later probes.
    TestUntrustedWinTrustFixture();
    TestUnconfiguredPinsFailClosed();
    TestUnsignedBinaryRejectedWithConfiguredPin();
    TestMismatchedPinRejectsSignedBinary();
    TestMatchingPinAcceptsSignedBinary();
    TestThumbprintComparison();
    TestPartiallyPopulatedPinsRejectUnpinnedPeer();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
