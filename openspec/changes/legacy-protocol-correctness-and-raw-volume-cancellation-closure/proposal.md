## Why

The residual-hardening campaign left an unproven raw-volume join bound, hosted GitHub Actions still cannot be treated as a local-parity substitute, and a completed durability change still emits `ScanComplete` to pre-{2,2} peers on failed scans. `VolumeSessionManager::OnScanComplete` treats a valid in-flight `ScanComplete` as durable authority. Until a mixed-version reproducer exists, that combination is a suspected false-success, not a proven one — and a cancellation request is still not completed I/O.

## What Changes

- Investigate negotiated protocol versions (1.x, 2.0, 2.1, 2.2), `ProtocolSupportsScanFailure`, `SendScanTerminal`, engine completion, reconciliation, durable cursors, and retry. Add a deterministic mixed-version failed-scan reproducer **before** any correction. If the engine would mark an incomplete scan authoritative, the reproducer MUST fail against the original behavior.
- If confirmed, apply the narrowest compatible correction so a scan that did not complete is never represented as successfully completed merely because the peer cannot understand `ScanFailed`. Preserve successful-scan bytes, valid legacy layouts, connection-scoped identities, authentication, cursor/reconciliation correctness, bounded retry, and current {2,2} `ScanFailed` semantics. Prefer connection teardown without a success terminal over sending an unparseable `ScanFailed` or a misleading `ScanComplete`. Avoid a protocol bump unless the lifecycle trace proves a breaking change is required.
- Trace every synchronous raw-volume `DeviceIoControl` (NTFS volume data, USN enum, MFT record fetch, journal query/create/read), cancellation, exceptions, disconnect, shutdown, supersede, and handle destruction. Add a hermetic stall fixture. Evaluate overlapped I/O, `CancelIoEx`, dedicated worker ownership, bounded admission, and an isolated helper process. Implement only a production-safe architecture whose guarantees are stated and tested separately. Do not detach live stack-owned workers, close handles the kernel may still use, call `TerminateThread`, accumulate unbounded detached workers, or report a requested cancellation as a completed cancellation. If an in-process finite completion bound cannot be proven for an arbitrary storage driver, keep predecessor task 2.3 unresolved.
- Recheck GitHub Actions once; do not repeatedly dispatch known-unstartable workflows; do not change billing or repository permissions. Inspect actual branch protection/rulesets. If Actions runs, certify the exact candidate SHA from the six current jobs. If it remains blocked, retain `HOSTED_CI = BLOCKED_EXTERNAL` and leave a reviewable feature-branch PR ready for certification. Local CI parity is not hosted certification.
- Inherit residual-hardening tasks 2.3, 5.5, 6.1, 6.2, and 6.6 **unchecked**. Reconcile the residual ledger against evidence. Recheck the external-frontier inventory. Produce the campaign A–J certification handoff. Supersede `final-residual-hardening-and-hosted-ci-recovery` as the single active campaign rather than running two overlapping actives.

## Capabilities

### New Capabilities

- `legacy-scan-failure-truthfulness`: Mixed-version failed-scan terminals must not produce durable false success; `{2,2}+` keeps `ScanFailed`; successful legacy wire bytes stay unchanged.
- `raw-volume-cancellation-bounds`: Distinguish cancellation requests from completed raw-volume I/O; prove only the guarantees the selected architecture actually supports; leave an unproven kernel bound explicit.
- `hosted-ci-integration-recovery`: Hosted Actions status, branch-protection facts, and PR integration policy while billing may block runners; local parity stays a separate oracle.
- `residual-ledger-and-certification-handoff`: Inherit open residual tasks, refresh the external-frontier inventory, and produce an evidence-backed A–J handoff without silently closing unproven work.

### Modified Capabilities

- `privileged-index-service`: Failed scans MUST NOT be completed as success on the wire solely because `ScanFailed` is unsupported; Stop/disconnect/shutdown MUST request worker cancellation without treating that request as kernel completion; empty-volume responsiveness is unchanged.
- `index-engine`: `ScanComplete` is durable authority only for a scan that actually completed; disconnect or silent failure MUST NOT `MarkScanComplete` or `FinishReconciliationPass`; mixed-version recovery MUST use fresh connection-scoped state.

## Impact

- Service/protocol: `SendScanTerminal` / `VolumeScanner.cpp`, handshake negotiation (`ServiceConnection.cpp`, `Version.h`), possibly connection abort on legacy failure. Closed command allowlist unchanged unless investigation proves a new type is required (**not** expected). Foundation design D2–D4 and Risks remain authoritative.
- Engine: `VolumeSessionManager` completion trust, reconciliation abort, retry/reconnect, durable cursors (`IndexPipeline` / store). Degraded mode MUST keep working.
- Workers: `ServiceWorkerLifecycle.h` `RequestWorkerStop` / `StopAndJoin(All)`, scanner and journal synchronous IOCTLs. Named-pipe bounded cancellation stays distinct from raw-volume I/O.
- Verification: focused protocol/service/engine/IPC tests, hermetic stall fixtures, ASan/lifecycle, strict OpenSpec, local CI parity, UIA/E2E/visual as applicable. Hosted six-job matrix is an external oracle.
- Lifecycle: this change becomes the sole `active` campaign; `final-residual-hardening-and-hosted-ci-recovery` is `superseded` with inherited tasks left unchecked. `index-durability-false-success-closure` stays `completed`.
- Out of scope: production signing/pins, clean-host install, elevated live MFT/USN certification, display-matrix mutation, billing/permission changes, reopening completed UI/visual campaigns without a reproducer.

## Goals

1. Prove or disprove the legacy `ScanComplete`-on-failure false-success with a deterministic reproducer, then apply the narrowest safe correction if confirmed.
2. Establish which raw-volume cancellation guarantees are actually achievable and test them; keep an unproven kernel bound explicit.
3. Put hosted certification and protected integration on evidence, not local parity.
4. Hand off an honest residual ledger and A–J report.

## Non-goals

- Implementing in this propose session (markdown only).
- Assuming the legacy defect without a reproducer.
- Marking residual task 2.3 complete because a cooperative simulated driver returned.
- Treating local CTest/CI parity as hosted or production certification.
- Changing billing, payment, repository permissions, or production trust stores.

## Open Questions

- After the mixed-version reproducer, does a `{2,1}`/`{2,0}`/`{1,x}` engine actually `MarkScanComplete` (or finalize a reconciliation pass) on a failed scan's `ScanComplete`, or is some other session flag already blocking authority?
- Is disconnect-without-terminal sufficient for every legacy failure path, including mid-enumeration non-EOF and terminal-write races, without stranding durable cursors?
- Which raw-volume FSCTLs on the production volume handle actually support overlapped I/O and `CancelIoEx`?
- Can GitHub Actions start jobs for a later candidate SHA, or does billing still reject the run before steps?
