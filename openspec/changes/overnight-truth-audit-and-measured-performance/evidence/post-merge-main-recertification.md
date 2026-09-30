# Post-merge re-certification of `main` at `9e2dddc`

Recorded after `main` was advanced `ea3d64f..9e2dddc` (fast-forward, no force) and PR #11
auto-closed as merged. Local `main` and `origin/main` are both `9e2dddc4bb4857ab932dd63c5bf1da8f3caa10d7`;
the detached `scythe-cloud` worktree at `0dc3356` was not touched and the merged feature branch was
deleted on both sides, leaving `main` as the only branch.

All runs below are **local**. Hosted CI could not execute: run `36653772504` for the `main` push and
runs `36641036824`/`36641041809` for the branch tip each ran six jobs with zero steps and the
account-payment / spending-limit annotation. Each was inspected once and none was relaunched.
`LOCAL-PARITY-NOT-HOSTED-CI`.

## Build identity

Identity verified against committed SHA `9e2dddc`. The debug, release-test, and analyze presets each
embed it as UTF-16 in the version resource of all five identity-bearing executables
(`FastFilesEngine.exe`, `FastFilesIndexSvc.exe`, `FastFilesSetup.exe`, `FastFiles.exe`, `fftest.exe`).
`ffbuild_identity_binding_tests` PASSES. E2E provenance reports
`buildIdentity = 9e2dddc4bb4857ab932dd63c5bf1da8f3caa10d7`, `boundToHead = true`,
`worktreeState = clean`.

The asan preset's executables carry no embedded identity (no version resource is compiled into
that configuration) — this is pre-existing and unchanged by this campaign; the asan test slice
itself passes.

## Gate results

| Gate | Command | Result |
| --- | --- | --- |
| Local CI parity | `verify/Invoke-CiParity.ps1` | **11 PASS / 0 FAIL / 0 SKIP** |
| - toolchain preflight | | PASS (3.1 s) |
| - markers + diagnostic policy + drift + diff-check | | PASS (2.8 s) |
| - strict OpenSpec `--all --strict` | | **89 passed, 0 failed** (5.5 s) |
| - build[debug] | | PASS (8.9 s) |
| - build[release-test] | | PASS (8.5 s) |
| - build[analyze] | | PASS (20.7 s) |
| - `ctest --preset debug` | | PASS (257.7 s) |
| - `ctest --preset release-test` | | PASS (234.2 s) |
| - asan slice (4 targets + 4 tests) | | PASS (72.4 s) |
| - hygiene: status + contracts + visual + exact-tree | | PASS (83.4 s) |
| - unsigned release candidate | | PASS (46.7 s) |
| End-to-end | `dev.ps1 test-e2e` | **8 passed, 0 failed, 0 skipped**, `boundToHead=true` |
| UIA core | `verify.ps1 run -Capability windows-ui-automation-validation -CapabilityOption UiaScenario=core` | **16 passed / 0 skipped / 0 failed** |
| UIA full | same with `UiaScenario=all` | **23 passed / 6 skipped / 0 failed** |
| Visual catalog | `verify.ps1 run -Capability windows-ui-visual-validation` | **26 captures passed / 27 skipped-blocked / 0 failed** |

## UIA flake, recorded not hidden

The **first** full `UiaScenario=all` run in this session returned
`status=FAIL classification=ASSERTION_FAILURE reason=search-deep-navigation-unexpected` for
`search-deep-result-navigation` ("search query 'alpha' was not accepted on AutomationId 7101;
observed=''").

It did not reproduce, and it is not being treated as a product pass or a product failure:

- `UiaScenario=core` passed 16/16 on the same binaries.
- `UiaScenario=search-deep-result-navigation` alone passed 3/3 on the same binaries.
- `UiaScenario=all` then passed 23/6/0.

This matches the isolated, unreproduced search-input flake already recorded by the apply session.
The full-run failure above is retained as a real observed result, not discarded. No product source
was changed in response, because the same scenario passes in isolation and in aggregate on the
identical binaries — the evidence points at a timing sensitivity in the search-input seeding, not at
a regression. A future session that reproduces it deterministically should fix the seeding, not the
product.

## Environment obstructions recorded as skips

Six UIA scenarios and 27 visual captures are skipped with recorded reasons: interactive-foreground
drag paths, the dual-pane scenario whose right-pane provider has no clickable semantic point, and the
live DPI / High-Contrast display variants that this host does not expose. None is reported as a pass
and none is treated as locally closable.

## No product change

`git diff ea3d64f..9e2dddc -- src cmake` is empty. This re-certification added no product source,
no test source, and no build change — only this record.
