# Phase-8 convergence audit

This session ledger records only findings and evidence created or revalidated by
the Phase-8 release-readiness campaign. Historical visual/run artifacts remain
unchanged and are not copied into this ledger.

## Scope

- Git/branch/working-tree and PR #6 disposition against current `origin/main`.
- Tracked source, tests, verification/provider scripts, release tooling, CI,
  OpenSpec ledgers, and maintained project-status documentation.
- High-risk seams: degraded UIA session startup, engine discovery, provider
  preflight, status/report serialization, release qualification, IPC/parser and
  persistence regression coverage.
- No trust-store mutation, production signing, guest provisioning, or clean-host
  privileged installation was attempted.

## Result

The locally reproducible P1/P2 findings in `findings.md` were fixed with focused
regression coverage. No new locally reproducible P0 remains. The remaining
frontier is external: a lawful Windows guest/template, authorized production
signer/pins, a clean elevated host, and the physical/interactive matrix.

## Evidence commands

The final handoff records the exact commit-bound results. The repeatable local
commands are:

```powershell
pwsh ./verify/status/tests/Test-ProjectStatus.ps1
pwsh ./verify/providers/tests/Test-ProviderContract.ps1
pwsh ./verify/release/tests/Test-ReleaseCandidate.ps1 -RepoRoot . -BuildRoot ./build/release-test
pwsh ./verify/status/New-ReleaseReadinessReport.ps1 -SkipCtest
npx --yes --package=@fission-ai/openspec@1.8.0 openspec validate --all --strict --no-interactive
```

The direct diagnostic UIA acceptance command uses the explicitly non-shipping
`debug-diagnostic` preset and the normal degraded path; it does not bypass the
privileged service protocol.
