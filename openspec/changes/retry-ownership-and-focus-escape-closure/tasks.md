## 1. Baseline and lifecycle

- [x] 1.1 Re-read the live branch, HEAD, `origin/main`, the feature-branch tip, ahead/behind counts, porcelain status, worktrees, and PR #10 head before editing. Do not reset to `9c0ad128`. Do not merge local `main` into this branch.
- [x] 1.2 Register this change in `openspec/lifecycle.json` as `active`, with evidence paths to `proposal.md`, `design.md`, and `tasks.md`. Do not check `legacy-protocol-correctness-and-raw-volume-cancellation-closure` task 5.4 or `final-residual-hardening-and-hosted-ci-recovery` task 2.3, and do not mark the predecessor completed or superseded.

## 2. Retry-ownership reproducers

- [x] 2.1 Add a deterministic engine test that delivers `ScanFailed` for an in-flight already-indexed reconciliation and disconnects from the existing snapshot-ready callback before the retry is armed. Assert whether a durable floor or a replacement slot still exists. The test must fail on the current source. Do not sleep.
- [x] 2.2 Add a deterministic scheduler test that pauses after a due retry is removed from `failedScanRetries_` and before it is adopted, then disconnects. Assert whether queue, session, or durable-floor ownership remains. The test must fail on the current source.
- [x] 2.3 Do not edit the production owner transitions until 2.1 and 2.2 fail. `TestFailedReconciliationRetrySurvivesDisconnect` does not satisfy either task.

## 3. Single-owner retry transitions

- [x] 3.1 Implement design D2: one owner per durable volume, moved only under `scoped_lock(mutex_, wakeMutex_)`, with `SCHEDULER_DISPATCHING` visible to disconnect. Do not decide from `active_` outside that lock. Do not hold the locks across snapshot publication or pipe writes.
- [x] 3.2 Preserve one intent per volume, the five-second floor, no tight reconnect loop, no stale ephemeral id, no overlapping reconciliation pass, and shutdown clearing. Authoritative completion may drop the owner only because the intent is satisfied.
- [x] 3.3 Cover the index-engine delta edges: scan-failure race, scheduler race, failed submission, disconnect during submission, disconnect immediately after admission, pause, disable, disappearance before transfer, two volumes, repeated failure, shutdown, reauthentication failure, fresh ephemeral id, already-indexed recovery, initial-scan recovery, and bounded queue state. Move due times directly. Do not sleep through five seconds or the ordinary poll.
- [x] 3.4 Keep the existing sequential disconnect-transfer test passing. A new owner record that loses that transfer is not done.

## 4. Protocol 2.3 adversarial review

- [x] 4.1 Add tests that a 2.2 peer neither receives nor depends on `ScanAttemptStarted`, a 2.3 peer negotiates it, and unsupported versions stay rejected by the existing policy.
- [x] 4.2 Add exact-payload tests: expected type, exact volume-id size, no trailing bytes, no path or handle, and fail-closed malformed frames. Confirm a client request of that type is rejected by the closed command surface.
- [x] 4.3 Bind a test to the production service order: join the old worker, write the barrier, then start the replacement. A failed barrier write must not start the worker.
- [x] 4.4 Add engine-authority tests for frames before the barrier, frames after it, duplicate and unsolicited barriers, unknown volume id, stale connection, disconnect before the barrier, disconnect immediately after it, replacement start failure after a written barrier, a failed client start with a possible partial write, and a legacy peer connection reset.
- [x] 4.5 Add a test that a barrier from an unauthenticated, stale, or replaced transport generation does not move current scan authority.
- [x] 4.6 If 4.1 through 4.5 pass, record that no protocol bump is required. If one fails in a way the current message cannot express, stop and record the failing case before adding a message. Do not resize `ScanComplete` or `ScanFailed`, and do not add an open-path command.

## 5. Combined authority and retry

- [x] 5.1 Add one deterministic regression for an already-indexed volume: reconciliation, pause, dropped batch, resume replacement, old worker with no terminal, barrier, replacement failure, `ScanFailed` racing disconnect, reconnect under a fresh id, transferred retry, and one later authoritative completion. Assert no live-row deletion, no false completion, no lost or duplicate retry, no stale id, the five-second throttle, and exactly one completion.
- [x] 5.2 Run the focused engine, protocol, IPC, and service tests that this change touches. Do not treat 5.1 as passed because an older log passed.

## 6. Chrome focus escape

- [x] 6.1 Add a headless test for design D4 that fails on the current wrap: forward from the last chrome control returns to the shell owner, and reverse from the first chrome control returns to the shell owner. Do not wrap.
- [x] 6.2 Cover entry from the owner, overflow present, overflow absent, several tabs, address-edit mode not stolen, settings and search reachable, disabled Back/Forward/Up skipped, and hidden overflow not a stop. Include content re-entry after an overflow activation where the chrome harness can do that without a new focus target.
- [x] 6.3 Change `HandleTabNavigation` to use the seam and `SetFocus` on the existing shell owner at both boundaries. Do not call `IsDialogMessage` and do not add a content child window. Preserve address-edit close focus, shortcuts, mouse behavior, and UIA exposure.

## 7. Exact-head local certification

- [x] 7.1 Commit the production and test fixes. Require a clean tree. Record that implementation SHA. Do not add evidence or handoff commits until executable qualification finishes. Do not reuse `9c0ad128` or `2d470134`.
- [x] 7.2 Rebuild the identity-bearing executables from that SHA and reject stale, empty, and mismatched identity. Every certification-relevant binary that embeds identity must embed the candidate.
- [x] 7.3 Run the full Debug build and Debug CTest, the release-test build and CTest, and Analyze. Report actual totals. Do not hardcode an old count.
- [x] 7.4 Run the current authoritative ASan slice for the protocol, IPC, service, and engine tests this change affects. Do not reuse a historical ASan result.
- [x] 7.5 Run strict OpenSpec validation, build-identity freshness, governance and drift checks, `git diff --check`, and the canonical local CI parity runner. Label parity `LOCAL_CI_PARITY`. Require pass greater than zero, fail zero, and skip zero unless the current local-equivalent definition says otherwise.
- [x] 7.6 Run the canonical E2E suite and the UIA contract, core runtime, full runtime, and visual catalog against the candidate, including forward and reverse focus traversal and the many-tabs overflow scenario. Preserve honest skips. Do not claim DPI or High Contrast configurations the host did not provide.

## 8. Review, pull request, and external holds

- [x] 8.1 After the fixes land, re-read the retry transitions, the barrier order, the focus cycle, the candidate identity, and the certification labels for a zero-owner or multi-owner window, a false completion, a chrome-only cycle, a stale artifact pass, or a hosted result mislabeled as local.
- [x] 8.2 Reply on each unresolved PR #10 thread with whether it reproduced, the regression test, the fixing commit, and the validation result. Resolve a thread only when that evidence addresses it.
- [x] 8.3 Push without force. Inspect hosted CI once. If the six required jobs fail before any step executes, record `HOSTED_CI = BLOCKED_EXTERNAL` and leave the NO-MERGE HOLD. Do not change billing, branch rules, signing, or trust stores.
- [x] 8.4 Update the handoff only after 7.2 through 7.6. State the raw-volume completion bound as `UNPROVEN`. Do not mark predecessor task 5.4 or 2.3 complete.
