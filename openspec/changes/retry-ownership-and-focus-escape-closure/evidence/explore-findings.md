# Explore findings — 2026-09-29

Read-only except for this change's planning artifacts. No product code was edited.

## Live baseline

- Branch: `feat/scan-generation-integrity-and-residual-certification-closure`
- HEAD and `origin` feature tip: `9c0ad128f7bfeeef5bc0d66a390c0a8786c024a1`
- `origin/main`: `1172c1a1e6aa9c1a726d52abdade0975b6979e4b`
- Ahead/behind `origin/main`: 24 / 0
- Tracked tree: clean before this proposal
- Worktree: `.kilo/worktrees/scythe-cloud` detached at `0dc3356`, left untouched
- Local `main` is 3 commits ahead of `origin/main`. It is not this branch's base.
- PR #10 is open, mergeable, head `9c0ad128`, base `1172c1a1`. Hosted checks on that head failed in seconds. That is the known external block, not a new product result.
- Unresolved, not-outdated review threads: P1 `VolumeSessionManager.cpp` retry transfer; P2 `NavigationChrome.cpp` Tab wrap.

## P1 is present in source

`OnScanFailed` clears `scanInProgress` and releases `mutex_` before `ArmFailedScanRetry`. Disconnect sets `active_ = false`, then under both locks transfers only an in-flight scan or an already queued retry. `ArmFailedScanRetry` returns when `active_` is false. The sequential test `TestFailedReconciliationRetrySurvivesDisconnect` arms the retry before disconnect, so it does not cover the race.

The scheduler erases a due retry into a local vector before adoption. Disconnect cannot see that vector. If the session is gone when the scheduler resumes, it does not re-arm.

A third window: an arm that passed the `active_` check can insert a dead ephemeral id after disconnect has cleared the queue. Reconnect reads the durable floor, not that id.

`RepublishSnapshot(..., force=true)` runs in the failure window and is the red seam for the first race. The scheduler window needs the dispatching-owner seam in design D3.

## P2 is present in source

`HandleTabNavigation` wraps the chrome vector at both ends. The shell consumes Tab only when that function returns true, and it does not call `IsDialogMessage`. Returning false does not focus Column View or Home. The existing escape target is the shell owner, already used when the address edit closes.

## Protocol 2.3 was not given a bump

Current version is `{2, 3}`. The service joins the old worker, writes an exact-length barrier, then starts the replacement. The engine ignores a barrier unless that session is awaiting it. Those facts are not the adversarial proof. Tests come first; a new message is not selected.

## Not reopened

Raw-volume completion remains unproven. Predecessor tasks 5.4 and 2.3 stay unchecked. Many-tabs logical count, legacy false completion, and the closed command surface are not redesign targets.
