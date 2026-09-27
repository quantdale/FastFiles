# Certification residuals and handoff

Status: local implementation and exact-candidate gates are still in progress.
This report records the external and inherited limits that must remain visible
when the local candidate is handed off; it does not claim hosted certification.

## Raw-volume completion bound — UNPROVEN

The arbitrary synchronous raw-volume `DeviceIoControl` completion bound is
**UNPROVEN**. `CancelSynchronousIo` requests cancellation of a worker's
synchronous operation, but it does not provide a completion deadline.
`StopAndJoin` and `StopAndJoinAll` still end in unbounded `std::thread::join()`
calls, so supersede, disconnect, and service shutdown can wait for the kernel
operation to return. The prior campaign's simulated stall and pipe-I/O
cancellation tests do not prove a finite bound for raw volume or USN journal
control calls.

Source inventory and limits: `openspec/changes/legacy-protocol-correctness-and-raw-volume-cancellation-closure/evidence/raw-volume-ioctl-inventory.md` and `openspec/changes/legacy-protocol-correctness-and-raw-volume-cancellation-closure/evidence/raw-volume-cancellation-architecture.md`.

Keep `legacy-protocol-correctness-and-raw-volume-cancellation-closure` task
5.4 and `final-residual-hardening-and-hosted-ci-recovery` task 2.3 unchecked.
Do not mark the predecessor complete or superseded on the strength of this
campaign.

## Hosted CI and merge hold

Apply-start run `36302493823` belongs to SHA
`1172c1a1e6aa9c1a726d52abdade0975b6979e4b`: all six jobs failed before any
step because account billing prevented job start. No job executed. The
protection and ruleset API reads returned HTTP 403 on the private free plan;
the protection state therefore remains unknown. Neither fact grants permission
to merge.

Do not dispatch another copy of those jobs before the one authorized
post-push recheck. Keep the feature branch reviewable with an explicit no-merge
hold while the required hosted jobs cannot start. Local CI parity must be
reported as local evidence, never as hosted CI.

## Interactive-environment limits

- A prior full UI Automation run had one `drag-out-to-explorer` failure because
  the user's `NAYEON_16: StudyMaker` window occluded the Explorer drop point.
  The separate UIA core run passed 16/16. Leave the unrelated foreground
  window untouched; retain the full-run result as an environment obstruction.
- The available desktop reports 120 DPI / 125% scaling. Alternate live DPI
  configurations were unavailable, so no other live DPI variant is certified.
- No production signing pins, certificate provisioning, trust-store mutation,
  elevated service installation, or privileged live MFT/USN validation is in
  scope for this closure. The degraded/unprivileged path remains the ordinary
  validation path.

## Candidate-specific local evidence

The exact executable candidate, embedded identity, required local gates,
interactive outcomes, and any skipped or externally unavailable checks are
recorded here after the final local validation pass. Until that section is
completed, prior parity and UI evidence remain historical evidence for their
recorded SHAs only.
