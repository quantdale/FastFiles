# Baseline — OpenSpec frontier lifecycle truthfulness

Date: 2026-08-25
Baseline commit: `b0ed5803b0aeba452822208b5230dd12e00f3832`

## Reconciliation

- `git fetch --prune origin` completed before edits.
- Branch: `main`.
- `HEAD == origin/main`: `b0ed5803b0aeba452822208b5230dd12e00f3832`.
- Worktree: clean.

## Reproduced defect

`pwsh ./verify/status/Get-ProjectStatus.ps1 -SkipCtest` reported 29 unchecked
OpenSpec tasks across 9 changes. The generated frontier classified these six
items as `localOpen`:

- `nonblocking-indexed-read-plane/8.5`
- `nonblocking-indexed-read-plane/8.6`
- `phase8-release-readiness-convergence/4.5`
- `real-user-dogfooding-release-engineering/5.4`
- `runtime-certification-and-verifier-convergence/8.2`
- `ship-ready-ui-ux-hardening-convergence/6.1`

The first five are historical delivery/verification work or a host-bound
repeatability task; the last is explicitly external GitHub administration. The
classifier gave all six `SKIPPED` with the reason “locally actionable unless
evidence proves otherwise.” This is the false frontier that the new lifecycle
model must remove without hiding real external gates.

The remaining 23 tasks were distributed among interactive, provider,
production-signing, clean-host, and elevated buckets by prose keywords. The
raw unchecked inventory remains authoritative and is captured by the live
status JSON generated during validation; no ignored run or signing material is
used as evidence.
