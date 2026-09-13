# FastFiles frontier audit → OpenSpec proposals (2026-09-14)

Planning-only campaign. Authoritative copy of the session report also lives in the implementer scratch `final-report.md`. Implementation tasks in the six new OpenSpec changes remain unchecked.

Inspected HEAD: `046dead1ba573727630ace49ea4a2cb43d68fd89` (`feat/refero-frontend-overhaul`).

## What this campaign did

Repository-wide audit against `openspec/lifecycle.json` and existing specs/changes. Confirmed the 2026-09-05 deferred P2s (PA-010..016) plus engine-side periodic peer revalidation (PA-034) are still present and were never specified. Converted them into six `openspec new change` proposals, strict-validated, and registered as `active` in `openspec/lifecycle.json`.

Did **not** edit `src/`, `tests/`, or `verify/` product code. Did **not** apply or archive the changes.

## Changes (apply order)

1. `close-file-ops-conflict-lifetime-and-outcome-truth` — conflict UAF, last-operation errors, skip/cancel accounting
2. `lock-clipboard-hdrop-read` — GlobalLock clipboard HDROP reads
3. `harden-workspace-state-multi-writer` — failed-load no-clobber + merge
4. `close-idle-drop-activity-race` — IdleManager drop vs activity
5. `harden-ipc-trust-boundary-gaps` — `PIPE_REJECT_REMOTE_CLIENTS` + engine heartbeat re-verify
6. `complete-ole-drag-source-move-effect` — drag-out MOVE completion (prefer after 1)

## Validation

- `openspec validate --changes --strict`: 56 passed, 0 failed
- Debug CTest: 103/103 PASS (does not cover these defects)
- `ffdoc_drift_tests` + `ffproject_status_ps_tests`: PASS after lifecycle registration

## Not re-proposed

Production signing/pins, elevated MFT/USN, clean-host, physical DPI/HC, hosted Actions billing, completed durability campaign, frontend overhaul already on this branch.

This document does not claim production-readiness.
