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
  the connection-scoped ids are erased. `TriggerReconciliation` never preempts an owned retry
  slot: a queued or floored slot keeps its bounded due time and its owed kind (an interrupted
  initial scan is not upgraded to a reconciliation), and the scheduler's dispatch path admits
  the slot by moving it to the visible dispatch owner first.
- A replacement that supersedes an attempt still in flight (a pause/resume cycle, even with no
  dropped batch) is provisional on a {2,3} peer and resets the connection on an older peer, so
  a retired worker's only terminal can never be accepted as the replacement's.
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
  and immediately after an accepted barrier keep the durable floor and no false completion,
  and a stale-id barrier cannot release the replacement connection's attempt.
- The failed barrier write is executed: hindsight-free `ApplyScanAttemptBarrier`
  (`src/indexsvc/src/ScanAttemptBarrier.h`) is invoked for every outcome (not negotiated,
  written, failed) in `ffservice_exception_boundary_policy_tests`, and the same test binds that
  `TearDown` disconnects before the replacement worker is registered.
- Transport generation: `PrivilegedConnection::DispatchReceivedFrame` rejects any frame that
  arrives while the connection is not the current authenticated `Active` transport, so an
  unauthenticated, stale, or replaced generation cannot move scan authority by a barrier,
  batch, or terminal; `TestBarrierFromNonActiveTransportIsRejected` executes that for
  Disconnected, Connecting, and Handshaking.
- Reauthentication reattachment: `TestReauthenticationFailurePreservesFloorKindForFreshId`
  keeps an interrupted initial scan's owed kind across a failed reauthentication and attaches
  it to a fresh id; `TestDisconnectImmediatelyAfterAcceptedBarrier` does the same for an
owed reconciliation.
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

## Review repair

A standards/spec review of `0e15982..91f85cf` found two hard standards violations, missing
coverage, and two implementation gaps. Repairs landed in
`6c40ab35793f1f9c03837f4b4dfa84b5964a032b`:

- Citations: every comment this change authored now cites
  `retry-ownership-and-focus-escape-closure` (with its task number or D-number); the
  predecessor's own 3.4 barrier and index-durability D2/D4 citations are unchanged.
- Task ledger: each checked task carries its implementing symbol or evidence per
  `openspec/config.yaml`.
- Failed barrier write: the decision is extracted to `ScanAttemptBarrier.h` and executed
  (not-negotiated/written/failed), with the TearDown-before-worker order bound in
  `ffservice_exception_boundary_policy_tests`.
- Generation check: `DispatchReceivedFrame` refuses frames outside the authenticated `Active`
  transport; the engine tests now model that state and cover the non-Active rejection.
- Floor kind and cadence: a journal discontinuity or interval sweep defers to an already-owned
  slot instead of issuing before its due time, and the durable floor's owed kind travels with
  the transfer.
- In-flight supersede: any replacement over an in-flight attempt is provisional on {2,3} and
  resets the connection on an older peer (`TestLegacyPeerInFlightSupersedeResetsConnectionAuthority`).
- Reauthentication reattachment and disconnect-immediately-after-barrier coverage added.
- Honest skips: the visual catalog records 20/20 scenarios while its coverage matrix reports
  live DPI variants and engine-served surfaces as `REQUIRED_BUT_UNAVAILABLE` (this host is
  120 DPI / 125% and the runner is not engine-backed); UIA keeps six recorded skip reasons.
- Second review (`5c5176e`): the product repair held. Its handoff findings are fixed here:
  `openspec/lifecycle.json` now names hosted push run `36490265081` for the full SHA
  `5c5176e8553b25fbbe007c9b43e1225989b62686` and this file contains both, so project status
  resolves the binding; the stale remote-tip statement is corrected; the two remaining new
  citations (`ScanAttemptBarrier.h` and its test) were retargeted to
  `retry-ownership-and-focus-escape-closure`; and the `DispatchReceivedFrame` guard comment now
  states that `ResetForScanAuthority` flips the state before the reader join so a frame in that
  window is intentionally dropped.

## Certification ladder (task 7)

Implementation candidate (production + tests): `6c40ab35793f1f9c03837f4b4dfa84b5964a032b`;
clean tree, every identity-bearing binary embeds it. The earlier `f639305`/`f3bbddfe`
certification is historical and does not certify this repair.

| Check | Result |
| --- | --- |
| Identity binding + freshness | PASS (all five binaries + package-trust fixture share `6c40ab3`) |
| Debug CTest | 124 / 124 passed, 0 failed, 0 skipped |
| release-test CTest | 124 / 124 passed, 0 failed, 0 skipped |
| Analyze | configured and built warning-clean |
| ASan slice (protocol fuzz, IPC framing + listener, scanner terminal, scan manager, legacy binding) | 6 / 6 passed |
| Strict OpenSpec | 88 / 88 passed (also in the parity leg) |
| Local CI parity (`verify/Invoke-CiParity.ps1`) | PASS 11 / 0 / 0, labeled `LOCAL-PARITY-NOT-HOSTED-CI` |
| Real E2E (`dev.ps1 test-e2e`) | 8 / 8 passed, `boundToHead=true`, identity `6c40ab3` |
| UIA core runtime | 16 passed, 0 skipped, 0 failed |
| UIA full runtime | 23 passed, 6 skipped with recorded reasons, 0 failed (one search-input flake was isolated and did not reproduce; the full aggregate re-ran green) |
| Visual catalog (`-Gallery`) | 20 / 20 recorded scenarios passed, 0 failed, clean teardown |
| Focused `shell-many-tabs-overflow` (forward + reverse Tab traversal) | 1 / 1 passed |
| `git diff --check` | clean |

The visual capability's lifecycle searched for the main window with a 160-node recursive
desktop walk; on this loaded desktop the walk exhausted its budget in unrelated trees. It now
uses the same bounded direct-children walk as the UIA capability (exact class + process
identity), and the focused scenario passes. No DPI or High-Contrast configuration beyond the
host's 120 DPI / 125% scale was claimed.

## Review and external holds (task 8)

- Self-review and review repair: owner transitions are single-owner (zero-owner and
  multi-owner windows closed), no caller preempts an owned slot's bounded cadence, an in-flight
  supersede cannot accept a retired terminal, only the authenticated Active transport can move
  authority, the chrome traversal cannot cycle, and the certified artifacts embed the candidate
  identity.
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
- Review-repair push (the tip this record's hosted binding names): `5c5176e8553b25fbbe007c9b43e1225989b62686`
  (`91f85cf..5c5176e`), without force; local HEAD and the feature remote tip were that SHA,
  `origin/main` `1172c1a1`, 28 ahead / 0 behind. Earlier handoff pushes were
  `c68212ddac74eba0bd7239d65c950a2f39bf4f88` and `91f85cf3769b97859f2ceafb5340fbcb4c6affc4`.
- Hosted CI, push run `36490265081` for `5c5176e8553b25fbbe007c9b43e1225989b62686` (and its
  `pull_request` mirror `36490270571`): all six required jobs failed with zero executed steps in
  about five seconds, the known billing/spending-limit external block, so
  `HOSTED_CI = BLOCKED_EXTERNAL`. The earlier push runs `36474313210`/`36474317807` (for
  `c68212d`) and `36474839438`/`36474845702` (for `91f85cf`) failed the same way. The bindings
  in `openspec/lifecycle.json` name the run id and full SHA recorded here; each later push is
  inspected once and its live status is reported in the session response rather than edited
  into this file. No billing, plan, branch rule, signing, or trust-store configuration was
  changed, and PR #10 remains under its NO-MERGE HOLD. Local parity remains labeled
  `LOCAL-PARITY-NOT-HOSTED-CI`.
