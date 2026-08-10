# Pinned-signature provisioning and rotation procedure

Recorded 2026-08-11 on `recovery/fast-files-20260809`.

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

Current repository state is intentionally fail-closed: both configured pins
remain placeholders in `src/setup/include/ffsetup/PinnedSignatures.h`, and no
privileged service was activated by this documentation slice.
