# Remaining external blockers

Recorded for `final-product-completion-and-production-readiness` task 7.8.
These are not local product defects and must not be converted to PASS.

## Production signing and pins

Release configuration remains fail-closed without authorized
`FASTFILES_ENGINE_SIGNATURE_THUMBPRINT` /
`FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT`. Placeholder pins stay all-zero.
No production signer was supplied to this campaign.

## Privileged MFT/USN and clean-host

`FastFilesIndexSvc` is implemented but inert in ordinary use because mutual
auth fails closed on placeholder pins. Degraded mode remains the live path.
Clean-host privileged install/runtime and real MFT/USN evidence require an
authorized elevated host.

## Live foreground UIA

Headless UIA provider tests pass, and the corrected Search/Filters and
selection runs are recorded separately as 20/20 isolated PASS evidence. The
remaining full live foreground/Explorer journey is host-sensitive: physical
foreground ownership, pointer/drop-target composition, and broader
display/DPI/High Contrast coverage require an appropriate interactive desktop.
The earlier Filters-disclosure diagnosis is historical and is not a current
local blocker.

## PR #6

Superseded by mainline work (`docs/pr6-stabilization-reconciliation.md`).
Public GitHub API reports the pull request **closed** (draft, not merged) as
of 2026-08-29. It is historical evidence, not an open administrative task.

## Historical hosted CI (pre-density-follow-up)

SHA `d21bc2300ebeec7af44e9cf890aafac516ae8c95` hosted run `33202519685`
completed success across hygiene, Analyze, Debug, ASAN, release-test, and
unsigned qualification. That result is historical evidence, not the SHA of
any later follow-up commit.
