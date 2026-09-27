## Context

See [proposal.md](proposal.md) for motivation and the capability specs under `specs/` for the observable contracts. Explore notes are in [evidence/explore-findings.md](evidence/explore-findings.md).

Live HEAD at planning was `9566f1cc3210278ffab8d156473e01e27a5a1e7d` on `main` (clean, matching `origin/main`). That SHA matches the campaign prompt's historical checkpoint; apply-session work MUST still re-read live git and lifecycle rather than treating this design as a frozen tree.

Current protocol is `{2,2}`. `ProtocolSupportsScanFailure` is true only for `{2,2}+`. `SendScanTerminal` sends `ScanFailed` when `!success` and the gate is true; otherwise it always sends `ScanComplete`, including on failed scans. `ffvolumescanner_terminal_tests` currently requires `ScanComplete` for a `{2,1}` setup failure. `VolumeSessionManager::OnScanComplete` marks complete and may finish a reconciliation pass when the session is in-flight, unpaused, and not batch-failed. Disconnect clears ephemeral sessions and does not mark complete. Silent cancel/transport-failure already emit no terminal.

Scanner and journal workers issue synchronous `DeviceIoControl` (volume data, USN enum, per-record MFT fetch, journal query/create/read). `RequestWorkerStop` sets a stop flag and calls `CancelSynchronousIo`. `StopAndJoin` / `StopAndJoinAll` then `join()`. Residual-hardening task 2.3 remains open for that reason. Named-pipe cancellation is already proven bounded in a focused fixture; that result is not a raw-volume bound.

`final-residual-hardening-and-hosted-ci-recovery` is the current `active` campaign with open tasks 2.3, 5.5, 6.1, 6.2, and 6.6. Hosted Actions has historically rejected all six jobs before steps with a billing/spending-limit annotation. Local `Invoke-CiParity.ps1` is a local oracle. Foundation design D2–D4 and Risks remain the process/IPC/security authority.

## Goals / Non-Goals

**Goals:**

- Sequence investigation, reproducer, then the narrowest correction that preserves the three campaign invariants.
- State cancellation guarantees only as far as they can be proven, with a recorded architecture choice.
- Make this change the single active campaign and inherit residual open work without checking it off.
- Keep apply-session product work on an isolated feature branch with a reviewable PR.

**Non-Goals:**

- Implementing product behavior in the propose session that produced these artifacts.
- Reopening PA-002..PA-006 durability work without a new reproducer.
- Claiming a universal IOCTL deadline, detaching privileged workers, or using `TerminateThread`.
- Changing billing, permissions, production pins, or treating local parity as hosted certification.

## Decisions

### D1 — One campaign change supersedes the overlapping residual active

**Why:** Two overlapping `active` campaigns would split residual task 2.3 and hosted-CI closure. This change continues that work and adds the legacy-terminal investigation. **Alternative:** Keep residual-hardening `active` and add a second campaign; rejected because lifecycle and apply ownership would duplicate 2.3 / 5.5 / 6.1 / 6.2 / 6.6. **Trade-off:** Residual-hardening becomes `superseded` with those tasks left unchecked and restated here.

`index-durability-false-success-closure` stays `completed`. Its D1 treated legacy `ScanComplete` on failure as wire compatibility. This change supersedes that **legacy failure-terminal** rule if the reproducer confirms false success; it does not reopen the `{2,2}` `ScanFailed` message, engine `OnScanFailed` path, USN seen-set, or integrity-verdict work.

### D2 — Investigation and reproducer before any protocol correction

**Why:** The campaign forbids assuming impact from source inspection. Current tests encode the fallback (`{2,1}` failed scan → `ScanComplete`). **Alternative:** Patch `SendScanTerminal` immediately; rejected because it would change behavior the existing terminal test requires without proving engine false success. **Trade-off:** Apply starts red on a new mixed-version engine/service fixture, then corrects.

The reproducer MUST drive the real `SendScanTerminal` / `OnScanComplete` path for negotiated 1.x, 2.0, 2.1, and 2.2, covering setup failure and mid-enumeration failure, plus a genuine success case.

### D3 — Candidate correction: no success terminal on failure; teardown instead of `ScanComplete`

**Why:** The invariant is that a non-completed scan must never be represented as successful merely because the peer cannot understand `ScanFailed`. Sending `ScanFailed` to a legacy exact-length parser is unsafe. Sending `ScanComplete` on failure is the suspected defect. Disconnect without a success terminal matches today's silent-cancel path, which already refuses to mark complete. **Alternative considered and deferred until the lifecycle trace:** a protocol bump or a new legacy-visible failure type. Use those only if teardown strands cursors, retries, or authentication. **Alternative rejected:** keep emitting `ScanComplete` on failure as "compatibility"; that is the rule under investigation.

Supersedes `index-durability-false-success-closure` design D1 **only** for the sentence that legacy peers keep receiving `ScanComplete` on the same **failure** paths. Successful-scan byte identity for pre-{2,2} peers remains in force.

Do not increment the protocol version unless D2's lifecycle trace proves a breaking change is required.

### D4 — Engine trust stays completion-vs-failure; do not special-case version inside `OnScanComplete`

**Why:** If the service never emits `ScanComplete` on failure, the existing engine rules already ignore disconnect and honor `ScanFailed`. Version-specific distrust of `ScanComplete` would punish genuine legacy successes. **Alternative:** treat every pre-{2,2} `ScanComplete` as non-authoritative; rejected because it would break legitimate legacy completions. **Trade-off:** the service owns the invariant; the engine keeps D2 from the durability change for paused/batch-failed/stale terminals.

Recovery after teardown MUST re-handshake, re-enumerate, and use new connection-scoped ids (already true on non-Active).

### D5 — Cancellation: measure first; prefer overlapped I/O only where the FSCTL and handle support it

**Why:** `CancelSynchronousIo` plus `join()` is the current residual. A request is not completion. **Evaluation order for apply:**

1. Hermetic stall fixture (no production volume hang).
2. Which of `FSCTL_GET_NTFS_VOLUME_DATA`, `FSCTL_ENUM_USN_DATA`, `FSCTL_GET_NTFS_FILE_RECORD`, `FSCTL_QUERY_USN_JOURNAL`, `FSCTL_CREATE_USN_JOURNAL`, `FSCTL_READ_USN_JOURNAL` actually complete overlapped I/O on the production handle shape.
3. `CancelIoEx` on those overlapped operations.
4. Explicit worker lifecycle + bounded admission so Stop/disconnect/supersede return on the control plane even when a worker remains in kernel.
5. Isolated helper process only if in-process containment cannot stop a stuck worker from blocking shutdown or writing to a replacement connection.

**Rejected without evidence:** `TerminateThread`; detach while stack-owned `writeMutex`/pipe/volume handle are live; closing the volume handle from another thread while the IOCTL may still reference it; claiming a universal deadline against a non-cooperative storage driver.

If step 2–5 cannot prove a finite in-process completion bound, keep inherited task 2.3 / this campaign's matching task **unchecked** and distinguish:

- bounded control-plane responsiveness (Stop returns, new work admission policy is explicit)
- actual worker termination (unproven)

Named-pipe bounded cancellation remains a separate, already-tested guarantee.

### D6 — Feature branch, one hosted recheck, local parity stays local

**Why:** The campaign forbids merging substantial privileged-service changes into unprotected `main` while required hosted checks cannot run, and forbids treating local parity as hosted certification. **Alternative:** implement on `main` as residual-hardening did; rejected by this campaign's integration policy. **Trade-off:** apply lands on `feat/legacy-protocol-correctness-and-raw-volume-cancellation-closure` (or equivalent), PR stays open if Actions are billing-blocked.

Recheck Actions once for the then-current SHA. If the annotation is still billing/spending-limit, record `HOSTED_CI = BLOCKED_EXTERNAL` and do not dispatch further known-unstartable runs. Inspect live branch protection and rulesets; propose review + required checks; do not change permissions or billing.

### D7 — Certification handoff is A–J with classified residuals

**Why:** Residual-hardening already has an A–I report shape; this campaign adds protocol, cancellation, branch protection, and a split verdict (J). External frontiers stay in `verify/certification/external-frontier.json`. **Alternative:** close residual tasks in a docs-only commit on `main`; rejected because that invalidates binary identity and retriggers blocked workflows, and would silently complete 2.3.

Adversarial reviewers A–E from the campaign run after implementation stabilizes, not during propose.

## Risks / Trade-offs

- **[Risk] The mixed-version "defect" is only the durability change's compatibility rule and the engine already ignores it.** → D2's reproducer must include engine durable state, not only the service frame type. If the engine does not mark complete, record that and do not "fix" a non-defect; still remove `ScanComplete`-on-failure if it violates the invariant on the wire.
- **[Risk] Disconnect-on-legacy-failure looks like transport failure and storms retries.** → Trace retry/reconnect before implementing; keep the existing five-second failed-scan cadence and connection-scoped identity reset.
- **[Risk] Existing `{2,1}` terminal tests become the regression that blocks the fix.** → Update them as part of the correction, after the new reproducer is red on the old behavior.
- **[Risk] Overlapped raw-volume IOCTLs are not actually cancelable for some FSCTLs.** → D5 records per-operation support; unimplemented operations keep the unproven bound.
- **[Risk] A helper process becomes a new privileged surface.** → Allowed only if in-process options fail; it would still be a constrained broker with the same closed protocol, not a generic open-handle service (foundation D4).
- **[Risk] Hosted CI starts later and fails for an in-scope reason.** → Fix repository defects; do not relabel billing failures as test failures.
- **[Risk] Documentation-only identity drift.** → Evidence commits that do not rebuild binaries MUST be labeled as a documentation tip, not a newly bound executable candidate.

## Migration Plan

No on-disk index migration. Wire change, if D2 confirms, is behavioral: legacy failure paths stop emitting `ScanComplete`; `{2,2}` `ScanFailed` stays. Rollout stays on the feature branch until hosted checks can run or the PR is explicitly left waiting on `BLOCKED_EXTERNAL`. Rollback is a revert of the service terminal change; durable indexes that were never falsely marked complete need no repair. Indexes already falsely marked complete (if the reproducer proves that happened in the field) are repaired by the next authoritative reconciliation after a correct failure, not by a store rewrite.

## Open Questions

- After the reproducer, does teardown-without-terminal interact badly with an in-flight reconciliation cursor that already applied some batches? (Does not change the invariant; may add a cursor-freeze test.)
- Do any of the listed FSCTLs refuse overlapped calls on `FILE_FLAG_BACKUP_SEMANTICS` volume handles on the current Windows versions FastFiles supports?
- Are repository rulesets already requiring PR checks despite conventional branch protection being off?
