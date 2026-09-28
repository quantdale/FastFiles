# Apply and validation — retry-ownership-and-focus-escape-closure

## Live baseline at apply start

- Branch `feat/scan-generation-integrity-and-residual-certification-closure`, HEAD and remote
  feature tip `9c0ad128f7bfeeef5bc0d66a390c0a8786c024a1`, `origin/main`
  `1172c1a1e6aa9c1a726d52abdade0975b6979e4b`, 25 ahead / 0 behind, clean tracked tree.
- PR #10 open, base `1172c1a1`, head `9c0ad128`, unresolved threads P1 (retry transfer) and
  P2 (chrome Tab wrap). Local `main` was 3 ahead of `origin/main` and was not merged.
- Worktree `.kilo/worktrees/scythe-cloud` (`0dc3356`) untouched; the installed
  `FastFilesIndexSvc` (PID 16216) untouched.

## Red reproducers (task 2)

Two deterministic tests failed on the pre-fix tree (with only the null dispatch seam added)
before any owner transition was edited:

- `TestScanFailedRacingDisconnectFromPublicationKeepsRetryOwner` — delivers `ScanFailed` for an
  in-flight already-indexed reconciliation whose snapshot-ready callback disconnects the
  manager. Pre-fix the failure cleared `scanInProgress` then republished, the disconnect saw no
  owner, and the retry died (`FAIL: an already-indexed reconciliation failure owns its retry
  before the publication window` and the replacement-reattach check). No sleeps.
- `TestSchedulerDispatchRacingDisconnectKeepsRetryOwner` — forces a due retry through the
  captured production public seam is not needed: the D3 seam pauses after the queue extraction
  and disconnects. Pre-fix the local vector owned the intent and disconnect dropped it
  (`FAIL: a due retry that left its queue still owns the durable volume across the disconnect`;
  `FAIL: the scheduler does not clobber the durable floor after the racing disconnect`).

Red run: `build/_red_retry_ownership.log`, exit 1, four failures. Green after D2: six consecutive
clean runs plus every later full-suite run.

## Implementation (task 3)

Design D2 is implemented in `src/engine/src/VolumeSessionManager.{h,cpp}`:

- One owner of a durable volume's retry intent at a time: the in-flight attempt
  (`scanInProgress`), the connection-scoped queue (`failedScanRetries_`), the scheduler's
  in-hand dispatch (`schedulerDispatchRetries_`, new), or the durable reconnect floor
  (`scanReissueFloors_`).
- Every transition runs under `scoped_lock(mutex_, wakeMutex_)`. `OnScanFailed` /
  `OnScanComplete` install the next owner inside the same critical section that closes the
  attempt, so the publication callback (the real disconnect window) can never observe the
  intent unowned. The scheduler moves a due retry into the visible dispatch owner in the same
  critical section, runs the lock-free D3 seam, then admits, re-queues, or floors it.
- Disconnect, disappearance, and disable transfer an owned retry to the durable floor before
  the connection-scoped ids are erased. `TriggerReconciliation` consumes a queued/dispatch owner
  atomically; its `respectOwnedRetry` flag makes the ordinary interval sweep defer to an owned
  slot's five-second cadence while a new journal-gap trigger may consume it.
- A failed retry submission (`RearmFailedAdmissionLocked`) restores the single bounded slot
  instead of leaving a claim with no legal submission.
- `RetryOwnerStateLocked` is the production derivation; tests use a data-member oracle so the
  private method's access-mangled symbol is never referenced.

New edge coverage: disable transfer, disappearance transfer, and the combined
barrier/failure/disconnect/reconnect/one-completion regression
(`TestCombinedPauseDisconnectReconnectStaleTerminalRecovers`, extended with the
`ScanFailed`-racing-publication window and exactly-one-owner/once assertions). Existing tests
were adapted to the single-owner model where their fixtures drove the inactive-manager path
(batch-failure and paused-completion recovery now drive the owned slot), and one pre-existing
drain/notify race in `TestRetryQueueStaysBoundedThrottledAndPauseAware` was removed.

## Protocol 2.3 adversarial review (task 4) — no bump

- Version gating is covered by `TestVersionCompatibility`: `{2,2}` does not support the barrier,
  `{2,3}`/`{3,0}` do, and unsupported majors stay rejected.
- Client rejection is covered by `test_command_surface.cpp`: the entire 0..0xFFFF range accepts
  exactly the six client requests and rejects `ScanAttemptStarted`.
- Service order is covered by `test_worker_exception_boundary_policy.cpp`: join, negotiated
  capability gate, barrier write under the shared write mutex, then replacement registration.
- New adversarial tests: exact-size acceptance, truncated and extended payloads fail closed,
  unsolicited/duplicate/unknown-id barriers move no authority, disconnect before the barrier
  keeps the durable floor and no false completion, and a stale-id barrier cannot release the
  replacement connection's attempt.
- Decision: no protocol bump and no new message. `ScanAttemptStarted` remains the exact-length
  reply-only barrier; `ScanComplete`/`ScanFailed` payloads are unchanged.

## Chrome focus escape (task 6)

`src/ui/include/ffui/ChromeTabTraversal.h` adds the headless seam
(`IsChromeTabTraversalEligible`, `AppendChromeTabStop`, `NextChromeTabTarget`).
`NavigationChrome::HandleTabNavigation` uses it and `SetFocus(owner_)` at both boundaries
instead of wrapping. No `IsDialogMessage`, no content child window, and the address-edit close
focus path is untouched. `tests/ui/test_chrome_tab_traversal.cpp` (31 checks) covers entry,
adjacency, both escapes, empty/dead-owner safety, disabled Back/Forward/Up, hidden overflow,
several tabs, and address-edit eligibility. The live `shell-many-tabs-overflow` scenario now
also proves forward and reverse traversal live.

## Certification ladder (task 7)

Implementation candidate (production + tests): `f6393058c7d98ef7ababd3bd6f7f311b21f6ae7f`.
Final executable identity (adds the verification-harness robustness fix):
`f3bbddfe70ae9afd336319f3c5ca11ddf88bb582`; clean tree, every identity-bearing binary embeds it.

| Check | Result |
| --- | --- |
| Identity binding + freshness | PASS (all five binaries + package-trust fixture share the candidate) |
| Debug CTest | 124 / 124 passed, 0 failed, 0 skipped |
| release-test CTest | 124 / 124 passed, 0 failed, 0 skipped |
| Analyze | configured and built warning-clean |
| ASan slice (protocol fuzz, IPC framing + listener, scanner terminal, scan manager, legacy binding) | 6 / 6 passed |
| Strict OpenSpec | 88 / 88 passed (also in the parity leg) |
| Local CI parity (`verify/Invoke-CiParity.ps1`) | PASS 11 / 0 / 0, labeled `LOCAL-PARITY-NOT-HOSTED-CI` |
| Real E2E (`dev.ps1 test-e2e`) | 8 / 8 passed, `boundToHead=true`, identity `f3bbddfe` |
| UIA core runtime | 16 passed, 0 skipped, 0 failed |
| UIA full runtime | 23 passed, 6 skipped with recorded reasons, 0 failed |
| Visual catalog (`-Gallery`) | 20 / 20, 0 failed, 0 skipped/blocked, clean teardown |
| Focused `shell-many-tabs-overflow` (forward + reverse Tab traversal) | 1 / 1 passed |
| `git diff --check` | clean |

The visual capability's lifecycle searched for the main window with a 160-node recursive
desktop walk; on this loaded desktop the walk exhausted its budget in unrelated trees. It now
uses the same bounded direct-children walk as the UIA capability (exact class + process
identity), and the focused scenario passes. No DPI or High-Contrast configuration beyond the
host's 120 DPI / 125% scale was claimed.

## Review and external holds (task 8)

- Self-review: owner transitions are single-owner (zero-owner and multi-owner windows closed),
  no terminal grants authority before the barrier, the chrome traversal cannot cycle, and the
  certified artifacts embed the candidate identity.
- PR #10 stays unmerged under its NO-MERGE HOLD. Hosted CI is recorded separately below; local
  parity is not hosted CI.
- The raw-volume synchronous `DeviceIoControl` completion bound stays `UNPROVEN`.
  `legacy-protocol-correctness-and-raw-volume-cancellation-closure` task 5.4 and
  `final-residual-hardening-and-hosted-ci-recovery` task 2.3 remain unchecked.

## PR and hosted outcome

- PR #10 review threads: both reproduced, replied with the regression test, fixing commit, and
  validation result, and resolved after the fix landed. P1 = retry transfer atomic with
  disconnect (`src/engine/src/VolumeSessionManager.cpp`); P2 = focus returned to the shell when
  tabbing past chrome (`src/ui/src/NavigationChrome.cpp`).
- Push: `9c0ad12..c68212d` without force; local HEAD and the feature remote tip both
  `c68212ddac74eba0bd7239d65c950a2f39bf4f88`. `origin/main` is `1172c1a1`, 28 ahead / 0 behind.
- Hosted CI for the pushed SHA: push run `36474313210` and its `pull_request` mirror
  `36474317807` failed all six required jobs in about four seconds with zero executed steps
  (`steps: 0`, each job starting under one second after the run began). That is the known
  billing/spending-limit external block, so `HOSTED_CI = BLOCKED_EXTERNAL`. No billing, plan,
  branch rule, signing, or trust-store configuration was changed, and PR #10 remains under its
  NO-MERGE HOLD. Local parity remains labeled `LOCAL-PARITY-NOT-HOSTED-CI`.
