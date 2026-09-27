# Explore findings — legacy protocol correctness & raw-volume cancellation

Campaign source: `D:\Downloads\fastfiles next campaing.txt`
Explore date: 2026-09-27
Stance: read-only; no product code was changed.

## Live baseline

| Fact | Value |
|---|---|
| Branch | `main` |
| HEAD | `9566f1cc3210278ffab8d156473e01e27a5a1e7d` |
| `origin/main` | `9566f1cc3210278ffab8d156473e01e27a5a1e7d` |
| Ahead/behind | 0 / 0 |
| Working tree | clean (`git status --short` empty) |
| Prompt's "previously verified HEAD" | same SHA as live HEAD |
| Active lifecycle campaign | `final-residual-hardening-and-hosted-ci-recovery` (`active`) |
| Predecessor | `residual-defect-closure-and-certification-completion` (`superseded` by the residual-hardening change) |
| Completed related change | `index-durability-false-success-closure` (`completed`) |

Open residual-hardening tasks (unchecked, not silently closable):

- **2.3** — raw-volume `DeviceIoControl` completion bound unproven; `CancelSynchronousIo` then `std::thread::join()`
- **5.5** — push validated executable candidate; verify HEAD equals `origin/main` before hosted certification
- **6.1** — recheck hosted availability for the final pushed SHA; inspect six jobs if they start
- **6.2** — if GitHub rejects jobs before execution, retain `HOSTED_CI = BLOCKED_EXTERNAL`
- **6.6** — predecessor task 1.4 / 5.4 reconciliation plus final handoff push and hosted inspect

`index-durability-false-success-closure` is complete, including the intentional legacy fallback that this campaign reopens as a suspected defect. Do not reopen its PA-002..PA-006 work without a new reproducer.

## Objective 1 — legacy negotiated-protocol false-success

```
  negotiated version
        │
        ▼
  ProtocolSupportsScanFailure(v)
        │
        ├── true  ({2,2}+)  → Send ScanFailed on failure
        └── false (1.x, 2.0, 2.1) → Send ScanComplete even when success=false
                                      │
                                      ▼
                         VolumeSessionManager::OnScanComplete
                         treats a valid in-flight ScanComplete as
                         MarkScanComplete + possible FinishReconciliationPass
```

Live evidence (source, not a runtime reproducer):

- `src/protocol/include/ffprotocol/Version.h` — `kCurrentProtocolVersion{2,2}`; `ProtocolSupportsScanFailure` is `major > 2 || (major == 2 && minor >= 2)`.
- `src/protocol/src/Version.cpp` — compatible peers are same major or exactly one major behind, so 1.x still handshakes.
- `src/indexsvc/src/ServiceConnection.cpp` — same-major negotiated minor is `min(client, server)`; a major-1 peer keeps its 1.x version.
- `src/indexsvc/src/VolumeScanner.cpp` `SendScanTerminal` — if `!success && ProtocolSupportsScanFailure` send `ScanFailed`; **else always `SendScanComplete`**, including `success=false`.
- Failure paths that call `SendScanTerminal(..., false)`: backup-privilege enable, raw-volume open, `FSCTL_GET_NTFS_VOLUME_DATA`, degenerate geometry, mid-enum non-EOF / short reply, and `ScanOutput::Finish(enumerationFailed=true)`.
- Cancellation and transport-failed writes still emit **no** terminal (silent). Disconnect clears ephemeral sessions (`OnConnectionStateChanged`) and does not `MarkScanComplete`.
- `src/engine/src/VolumeSessionManager.cpp` `OnScanComplete` — if `scanInProgress` and not paused and not `scanBatchFailed`, it calls `pipeline_.MarkScanComplete` and may `FinishReconciliationPass`. It has no negotiated-version check. `OnScanFailed` never marks complete and aborts any pass.
- Tests currently **require** the legacy fallback: `tests/indexsvc/test_volume_scanner_terminal.cpp` `RunTerminalCase({2,1}, ScanComplete, "a setup-failed scan keeps the legacy ScanComplete for a {2,1} peer")`. Protocol tests document the same gate.
- Existing spec `index/scan-completion-truthfulness` "Legacy peers keep current behavior" only requires: do not send `ScanFailed`, and **successful-scan** wire bytes stay unchanged. It does **not** require `ScanComplete` on failure. The implementation is stricter than that spec in the unsafe direction.
- Original durability design D1 named the fallback as wire compatibility ("today's ScanComplete byte-for-byte"). That is an intentional compatibility rule, not yet proven safe against the campaign invariant.

Campaign invariant (must survive into specs/tasks):

> A scan that did not complete must never be represented as successfully completed merely because the peer cannot understand the newer failure-terminal message.

Preferred correction to evaluate after a reproducer (not prescribed as already proven): **do not send `ScanComplete` on failure to any peer**. For a peer that cannot parse `ScanFailed`, terminate the connection without a success terminal (existing silent-cancel / disconnect path already refuses to mark complete). Do not send an unparseable `ScanFailed`. Avoid a protocol bump unless the lifecycle trace proves a breaking change is required.

Required apply-session work: a deterministic mixed-version reproducer that must fail against the original behavior if the engine would mark an incomplete scan authoritative. Investigation stays unchecked until that reproducer exists.

## Objective 2 — raw-volume synchronous IOCTL cancellation

```
  StopVolumeScan / supersede / disconnect / service shutdown
        │
        ▼
  RequestWorkerStop
        ├── stopFlag = true
        └── CancelSynchronousIo(thread.native_handle())   ← request only
        │
        ▼
  StopAndJoin / StopAndJoinAll
        └── std::thread::join()   ← waits for kernel return; no deadline
```

Live evidence:

- `src/indexsvc/src/ServiceWorkerLifecycle.h` comments this explicitly: cancellation is a request; join waits for the kernel request to return.
- Residual task 2.3 restates the same residual.
- Residual spec already forbids describing a cancellation request as a finite completion guarantee.
- Named-pipe `CancelSynchronousIo` is proven bounded in `TestStopAndJoinCancelsSynchronousWorkerIo` (pipe read, not raw volume).
- Synchronous `DeviceIoControl` sites (no `OVERLAPPED`):
  - Scanner: `FSCTL_GET_NTFS_VOLUME_DATA`, `FSCTL_ENUM_USN_DATA`, per-record `FSCTL_GET_NTFS_FILE_RECORD`
  - Journal: `FSCTL_QUERY_USN_JOURNAL`, `FSCTL_CREATE_USN_JOURNAL`, `FSCTL_GET_NTFS_FILE_RECORD`, `FSCTL_GET_NTFS_VOLUME_DATA`, `FSCTL_READ_USN_JOURNAL`
- `StartVolumeScan` supersedes by `StopAndJoin` of the same volume, then starts a new worker. A stuck join blocks admission of the replacement.
- Connection teardown `StopAndJoinAll` can block service disconnect/shutdown on a stuck IOCTL.
- Forbidden shortcuts (campaign + current residual design): `TerminateThread`, detach while stack-owned state is live, close handles the kernel/worker may still reference, unbounded detached workers, reporting requested cancel as completed I/O, claiming a universal deadline against a non-cooperative storage driver.

Architectures to evaluate in apply (do not pick before a hermetic stall fixture):

1. Overlapped `DeviceIoControl` + `CancelIoEx` where the operation and handle actually support it
2. Dedicated worker ownership + explicit lifecycle states + bounded admission (control plane returns; worker still may be live)
3. Isolated helper process if in-process containment cannot bound an indefinitely blocked IOCTL
4. Keep request+join and leave 2.3 an explicit unresolved kernel limitation

A cooperative simulated-driver test that returns promptly is **not** proof of a finite bound for arbitrary storage drivers.

## Objective 3 — hosted CI / integration while Actions may be unavailable

- Workflow: `.github/workflows/ci.yml` — six required executions: debug, release-test, analyze, ASan protocol/IPC/lifecycle, repository-hygiene-and-contracts, unsigned-release-candidate-qualification. `release` preset is excluded.
- Local parity: `verify/Invoke-CiParity.ps1` is labeled local-only; residual D4 forbids treating it as hosted certification.
- Historical hosted runs fail before steps with billing/spending-limit annotations. Campaign names run `36261023218`; WORKSTATE names later runs similarly blocked. Recheck once; do not repeatedly dispatch known-unstartable workflows.
- Do not change billing, payment, or repository permissions.
- Conventional branch protection was previously observed disabled on `main`. Recheck rulesets. Propose review + passing checks for privileged-service changes; do not silently alter permissions.
- Integration policy from the campaign: isolated feature branch / worktree; local gates + independent review; reviewable PR; if hosted CI remains blocked, leave the PR ready for certification.

## Objective 4 — residual ledger + evidence-backed certification handoff

- Do not check residual 2.3 unless a genuine finite completion bound is established.
- Distinguish historical completion from current validation.
- Avoid documentation-only commits that invalidate binary identity and retrigger blocked workflows.
- Recheck `verify/certification/external-frontier.json`. Preserve independent boundaries: production signing/pins, clean-host, elevated live MFT/USN, physical DPI/High Contrast/mixed-monitor, authorized disposable guest, hosted GitHub Actions.
- Final report sections A–J as specified by the campaign. Residuals classified `LOCAL_ACTIONABLE` / `BLOCKED_EXTERNAL` / `UNPROVEN` / `SPECULATIVE` / `FUTURE_PRODUCT_WORK`.
- Verdicts stated separately: local implementation, local certification, hosted CI, integration/merge readiness, privileged runtime certification, production release certification.

## Lifecycle recommendation

One campaign change, not four disconnected proposals. The four objectives share the privileged scan lifecycle and the residual ledger.

Supersede `final-residual-hardening-and-hosted-ci-recovery` with the new change so there is a single active campaign. Keep predecessor tasks 2.3 / 5.5 / 6.1 / 6.2 / 6.6 **unchecked** and restate them as inherited open work. Do not mark them complete by supersession.

Leave `index-durability-false-success-closure` `completed`. This campaign is a follow-on that challenges the legacy `ScanComplete`-on-failure compatibility rule.

## Three campaign invariants (must appear verbatim in intent)

1. A non-completed scan must never be represented as successful merely because the peer cannot understand `ScanFailed`.
2. A cancellation request is not completed I/O.
3. Local parity is not hosted certification.

## This explore session does not

- Reproduce the engine false-success in a failing test
- Change `SendScanTerminal` or worker joins
- Dispatch GitHub Actions
- Apply OpenSpec or implement product code
