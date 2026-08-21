# FastFiles real-user dogfooding campaign ledger

Date started: 2026-08-21
Repository: `quantdale/FastFiles`
Baseline: `main` / `4ded78a02efe4d0bcfa67b1c32836227bf61b7c6`
Current campaign HEAD: `ca2a3ff81ca89fa47cf5295930617b385acf7dea`
Hosted baseline CI: run `32468067892` (`CI`, current baseline SHA), reported success by the release audit worker; re-query before final handoff.
Campaign OpenSpec: `openspec/changes/real-user-dogfooding-release-engineering/`

This ledger is a new campaign record. The 2026-08-20 production-readiness audit remains historical evidence and is not a backlog here. An old item is reopened only when a current run produces a new reproducible regression.

## Evidence policy

- Disposable fixtures and raw run output stay outside tracked source unless sanitized and intentionally summarized.
- File-operation claims require independent filesystem truth, not only UI status.
- Unavailable signing, elevation, clean-host, VM, network, removable, or display prerequisites are recorded as `BLOCKED-EXTERNAL`, never as PASS.
- Every finding requires a reproducer, observed evidence, root cause, fix/regression reference, or an explicit unresolved classification.

## Current campaign status

| Area | Status | Evidence |
| --- | --- | --- |
| Git/branch/worktree reconciliation | PASS at baseline | `git status`, `HEAD == origin/main` |
| Prior closure hosted CI | PASS | GitHub Actions run `32467948824` for `0d47bd5…` |
| Fixture generator small profile | PASS | 120 bulk items; 168 manifest entries; independent comparison and hashed snapshot comparison |
| Medium/large physical fixture | PASS | Medium 10,023 files and Large 100,023 files independently compared and reset; see `performance.md` |
| Interactive product dogfood | PARTIAL-PASS | Real diagnostic executable drove through repository UIA; committed core wrapper run `20260821-122919`; Computer Use native channel unavailable |
| First-run profile and Home | PASS-DEGRADED | State backup + clean profile; UIA run `20260821-115745`; Home overflow fixed and verified in `20260821-120225` |
| Search/selection/error UIA | PASS-DEGRADED | Runs `20260821-115103`, `20260821-115432`, `20260821-115534` |
| File-operation independent truth | PASS-E2E | `fffileoperations_e2e_tests` passed in 17.59s |
| Large-directory/performance soak | PASS-DEGRADED | 100K physical fixture comparison, indexed benchmark, committed 30s UI/engine resource soak; interactive large-folder latency remains unmeasured |
| Production signing / privileged install | BLOCKED-EXTERNAL | No authorized certificate/pins; fail-closed path preserved |
| Clean-host/VM/network/removable matrix | BLOCKED-EXTERNAL | No provisioned external environment claimed |

## Findings

No new P0/P1/P2/P3 product finding has been recorded at campaign start. Harness defects found while building this campaign are fixed before acceptance evidence is counted and are not product findings.

Use `findings.md` for one-row-per-finding updates. Keep the row stable after discovery; append evidence and resolution rather than rewriting history.
