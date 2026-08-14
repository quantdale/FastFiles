# Pinned-signature provisioning and rotation procedure

Recorded 2026-08-14 on `main`.

This evidence note retains the D5 procedure without storing a certificate,
private key, trusted thumbprint, signing artifact, or secret:

1. A controlled release job signs the final Engine and IndexSvc binaries with
   the authorized Authenticode certificate. The private key and certificate
   package remain in the protected signing system and never enter source
   control, the repository workspace, or WORKSTATE.
2. The job extracts the Authenticode leaf-certificate thumbprint from the
   exact binaries that will ship. Engine and service peer pins are populated
   through the reviewed release-only pin source/build input; development
   builds retain the all-zero placeholders.
3. The job rebuilds or packages the final binaries, signs the final outputs,
   and verifies that each shipped image's leaf thumbprint matches the pin
   expected by its peer. A mismatch, missing certificate, placeholder pin, or
   unsigned image fails the release gate.
4. Rotation is a single-pin transaction: update the protected pin input and
   the corresponding signed binaries together, deploy/restart both peers, and
   verify that the new peer authenticates while the old signer fails closed.
   There is no dual-acceptance window.
5. The release record retains only public verification facts and command
   results. It does not retain private material or invent a thumbprint in the
   source tree. The existing unsigned/mismatch/placeholder/partial-pin tests
   remain the local regression gate; elevated signed-service, real MFT/USN,
   privilege-diagnostics, and clean-host matrix validation remain separate
   external tasks.

## Signer certificate policy

`Test-ProductionCertificate` now treats technical signer policy as distinct
from operator authorization. The selected leaf must be present, non-self-
signed, currently valid for a new release, carry the Code Signing EKU
`1.3.6.1.5.5.7.3.3`, and build a fully successful trusted Windows X509 chain
under the release operator's CurrentUser/LocalMachine trust policy. The
signing-provider path additionally requires the private key and rejects a
selected certificate whose thumbprint differs from the expected signer.

Release preflight deliberately uses `X509RevocationMode.NoCheck` and reports
`NOT_CHECKED_OFFLINE`. Online revocation is not made a hidden network
dependency of release tooling or product runtime: offline build hosts cannot
deterministically establish an online OCSP/CRL result. Chain trust, validity,
Code Signing EKU, and revocation status are therefore separate facts. Runtime
peer WinTrust likewise retains `WTD_REVOKE_NONE` for the offline architecture;
it independently establishes Authenticode trust and the pinned leaf identity.

The local certificate-policy regression uses uniquely named temporary
`TEST CERTIFICATE / NON-PRODUCTION` roots and leaves for self-signed,
untrusted, missing-EKU, expired, not-yet-valid, and valid cases. Self-signed
and untrusted cases exercise the default Windows policy; trusted-root EKU and
validity cases use a private in-memory `CustomRootTrust` policy seam. No
system or CurrentUser Root store entry is added. CurrentUser personal-store
fixtures are removed in `finally` paths and verified absent. No fixture result
is production signer authorization.

Current repository state is intentionally fail-closed: both configured pins
remain placeholders in `src/setup/include/ffsetup/PinnedSignatures.h`, and no
privileged service was activated by this documentation slice.
