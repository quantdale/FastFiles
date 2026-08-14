# Authentication negative-test evidence

Recorded 2026-08-14 on `main`. This is local regression evidence only; the
temporary certificates are not production signer evidence.

## Requirement matrix

| Task 4.2 clause | Status | Evidence |
|---|---|---|
| Unsigned binary rejected | PASS | `ffengine_authenticode_verification_tests` rejects a temporary unsigned file with a configured non-placeholder pin. |
| Mismatched signer rejected | PASS | The same test signs a copied PE with a temporary trusted test leaf, then verifies a deliberately modified leaf thumbprint; production `VerifyPinnedSignature` rejects it. |
| Placeholder signer/pin rejected | PASS | All-zero pins are rejected before file verification, and the valid signed fixture is also rejected when verified with an all-zero pin. |
| Expired signer rejected | BLOCKED_ENVIRONMENT / NOT VERIFIED | The fixture is designed to trust a uniquely named temporary root only in `CurrentUser\Root`, accept the copied PE and exact pin while valid, then reject the same unchanged non-timestamped PE after `NotAfter` through `VerifyAuthenticodeAndGetThumbprintAtPath` / `VerifyPinnedSignature`; the custom-root chain reports `NotTimeValid`. On this host the bounded CurrentUser trust operation times out and production WinTrust reports `0x800B0109` (`CERT_E_UNTRUSTEDROOT`) before expiry, so the required trusted acceptance and expiration transition are not claimed. The separate timestamp-longevity regression remains covered by the release-tooling/custom-root matrix. |
| Untrusted signer rejected | PASS | A temporary code-signing leaf signed by a uniquely named development root signs a copied PE; the root is not added to CurrentUser\Root and real WinTrust rejects the chain. |
| Partially populated pin set fails closed | PASS | Existing partial-pin coverage verifies the populated peer remains usable while the placeholder peer is rejected. |
| Shipped diagnostic escape hatch disabled | PASS | `verify/check_diagnostic_policy.ps1` passes; CMake rejects `FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED` outside explicit Debug diagnostics and the release/release-test presets do not enable it. Hosted CI invokes the same source/configuration gate. |
| Release pin provisioning | BLOCKED_EXTERNAL | No authorized production certificate, production thumbprints, or signed release was supplied. No local test certificate is recorded as production authorization. |

## Fixture handling

`tests/engine/AuthenticodeFixture.ps1` generates only uniquely named
`TEST CERTIFICATE / NON-PRODUCTION` roots/leaves in the CurrentUser personal
store, signs a copy of the test PE with the Windows SDK `signtool.exe`, and
uses an in-memory `CustomRootTrust` chain policy for the diagnostic validity
check. The `Expired` and `TimestampedExpired` cases additionally install only
the exact public test root into `CurrentUser\Root` with the supported
`certutil -user -f -addstore Root` path, which is the trust location used by
real WinTrust. The fixture checks the exact SHA-1 registry key before and after
that mutation, never enumerates the hosted runner's protected-root view, and
removes only that generated root key during cleanup; no broad registry or
LocalMachine trust operation is used. A thumbprint-derived temporary DER file
is deleted in `finally`, and cleanup also removes that exact path if a worker
is interrupted. The timestamped case runs the WinTrust worker separately so
the parent can remove the root after all provider handles have exited. State
files are written before trust-store mutation, cleanup removes the exact root,
leaf, timestamp signer, private key containers, PE, state, thumbprint, and
temporary certificate file, and the C++ test checks both the cleanup exit code
and file absence. No PFX is used.

The required production-path acceptance is not claimed on the current
development host. The bounded focused run's `certutil -user -f -addstore Root`
operation timed out, so the `Expired`, `TimestampedExpired`, and native
timestamped CMS tests failed before their trusted-acceptance assertions. A
separate probe using the documented Crypt32 `CERT_SYSTEM_STORE_UNPROTECTED_FLAG`
also hung while opening the protected CurrentUser Root provider; it was
terminated and its generated personal-store certificates were removed. The
untrusted-signer negative checks and all exact cleanup checks passed. A prior
direct-registry/provider probe left the production WinTrust result at
`0x800B0109` (`CERT_E_UNTRUSTEDROOT`) before expiry. This is recorded as
`BLOCKED_ENVIRONMENT / NOT VERIFIED`; custom-root chain validity is not used as
a substitute for the required real WinTrust proof.

The release-tooling fixture matrix separately covers self-signed and untrusted
cases through the production Windows trust policy, and missing-EKU, expired,
not-yet-valid, and valid cases through a private in-memory `CustomRootTrust`
seam. That seam makes the test root trusted without changing the host trust
store, so the EKU and signing-credential validity failures are independently
observable. Its timestamped manifest/PE probes use the real SignTool
`timestamp /p7` or PE RFC3161 path and the final PowerShell verifier. Every
CurrentUser personal-store fixture cleanup is verified. The valid cases are
explicitly marked `TEST CERTIFICATE / NON-PRODUCTION` and are not authorized
production evidence.

## Timestamp-longevity evidence

The signing-credential policy remains strict: a new signer must be currently
valid, trusted, non-self-signed, Code Signing EKU-constrained, private-key
backed, and exactly pinned. Signed-artifact verification is a separate policy:
it accepts only a WinTrust-valid Authenticode result, known non-self-signed
Code Signing leaf, exact signer provenance, and a valid timestamp state; it does
not add a current-wall-clock validity failure after Windows has accepted the
timestamped artifact.

Detached manifest signatures are produced as detached `SignedCms` content and
then timestamped with the production command shape `signtool timestamp /p7
/tr <RFC3161-URL> /td SHA256 <signature.p7s>`. The loopback test responder
feeds that actual SignTool path, and both `Test-PackageManifestSignature` and
`DefaultManifestSignatureVerifier` accept the final bytes only when the
manifest binding, exact signer thumbprint, RFC3161 token authenticity, TSA
timestamp EKU/chain, and signer Code Signing chain at the timestamp all pass.
Changed manifest bytes, changed signature/timestamp bytes, and wrong signer
pins fail closed. No manifest bytes are modified by timestamping.

## Revocation boundary

Release signer preflight reports `NOT_CHECKED_OFFLINE` and does not require an
online OCSP/CRL result. It independently requires current validity, Code
Signing EKU, and a fully trusted Windows chain. Runtime WinTrust keeps
`WTD_REVOKE_NONE` so peer authentication remains offline; this is documented
as a separate revocation fact, not as proof that revocation is good.
