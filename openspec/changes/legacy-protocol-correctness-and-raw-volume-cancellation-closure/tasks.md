# Tasks

Apply-session work for `legacy-protocol-correctness-and-raw-volume-cancellation-closure`. Cite as `legacy-protocol-correctness-and-raw-volume-cancellation-closure N.N`. All boxes start unchecked. Investigation and reproducers come before corrections. Do not mark unproven cancellation or hosted certification complete.

Inherited still-open work from `final-residual-hardening-and-hosted-ci-recovery` (do not check those predecessor boxes; restated here): 2.3 raw-volume completion bound, 5.5 push/HEAD verification, 6.1 hosted six-job inspect, 6.2 billing-blocked recording, 6.6 predecessor-ledger plus hosted handoff.

## 1. Live baseline and isolated branch

- [ ] 1.1 Record live branch, HEAD, `origin/main`, ahead/behind, worktrees, concurrent activity, lifecycle, CI workflow, and recent hosted runs without resetting to the planning SHA.
- [ ] 1.2 Create isolated feature branch `feat/legacy-protocol-correctness-and-raw-volume-cancellation-closure` (or equivalent worktree) for all product edits; do not merge substantial privileged-service changes directly to unprotected `main`.
- [ ] 1.3 Launch parallel read-only streams for protocol/terminals (A), raw-volume cancellation (B), hosted CI/protection (C), and independent regression challenge (D) with explicit file ownership.

## 2. Legacy protocol investigation and reproducer

- [ ] 2.1 Trace negotiation, `ProtocolSupportsScanFailure`, `SendScanTerminal`, scan-record delivery, transport failure/disconnect, `OnScanComplete` / `OnScanFailed`, reconciliation finalization, durable cursors, indexed-volume state, and retry/reconnect for versions 1.x, 2.0, 2.1, and 2.2.
- [ ] 2.2 Add a deterministic mixed-version failed-scan reproducer that drives the real service terminal and engine durable outcome; cover setup failure and mid-enumeration failure separately from genuine success.
- [ ] 2.3 Establish whether a pre-{2,2} engine would `MarkScanComplete`, advance durable state incorrectly, or `FinishReconciliationPass` on incomplete coverage; if a real defect exists, the reproducer MUST fail against the original behavior.
- [ ] 2.4 Distinguish an intentional compatibility rule from a safe compatibility implementation in a short evidence note; do not implement a correction until 2.2–2.3 complete.

## 3. Legacy failure correction

- [ ] 3.1 If 2.3 confirms false success, implement the narrowest correction so a scan that did not complete is never represented as successfully completed merely because the peer cannot understand `ScanFailed` (candidate: no `ScanComplete` on failure, no `ScanFailed` to a peer that cannot parse it, connection teardown and recovery).
- [ ] 3.2 Preserve successful-scan behavior, valid legacy wire formats, connection-scoped identities, authentication, durable cursor correctness, reconciliation rollback, bounded retry, and current `{2,2}` `ScanFailed` semantics; avoid a protocol bump unless 2.1 proves a breaking change is required.
- [ ] 3.3 Update the existing `{2,1}` terminal expectation that required `ScanComplete` on setup failure so it asserts the invariant instead of the old fallback.
- [ ] 3.4 Demonstrate: normal legacy scans still complete; failed legacy scans cannot produce authoritative false success; modern peers receive `ScanFailed`; interrupted reconciliation never deletes valid records from incomplete coverage; recovery cannot reuse stale connection-scoped state; negotiation stays on the documented matrix.
- [ ] 3.5 Run focused protocol, command-surface, scanner-terminal, engine session-manager, index-pipeline, and index-persistence tests for the correction.

## 4. Raw-volume cancellation investigation and fixture

- [ ] 4.1 Trace every raw-volume `DeviceIoControl` in scanner and journal paths (volume data, USN enum, MFT record fetch, journal query/create/read), plus scanner cancellation, worker exceptions, disconnect, shutdown, supersede, and handle destruction; identify which operations may block and which thread owns each resource.
- [ ] 4.2 Add a hermetic fault-injection fixture that simulates an operation failing to complete promptly without hanging a production volume; cover cancellation during active scanning and journal operations.
- [ ] 4.3 Measure separately: time until cancellation is requested, time until the control path responds, time until the worker stops, whether cleanup requires kernel I/O completion, whether new work can be admitted, whether the service can shut down, and whether a previous worker can affect a replacement session.
- [ ] 4.4 Evaluate overlapped I/O per FSCTL/handle, `CancelIoEx`, dedicated worker ownership, explicit lifecycle states, bounded admission/shutdown, and an isolated helper process; reject detach, unsafe handle close, `TerminateThread`, unbounded detached workers, and reporting requested cancel as completed I/O.

## 5. Cancellation architecture (conditional)

- [ ] 5.1 Implement the selected architecture only after 4.4 records its supported guarantees and compatibility with closed commands, authentication, connection-scoped identities, and fail-closed service behavior.
- [ ] 5.2 Add deterministic tests for normal completion, cancel before IOCTL, cancel during simulated stall, cancel plus disconnect, service shutdown, repeated stop/start, multiple volumes, worker exceptions, late completion after cancel, and recovery with fresh connection state.
- [ ] 5.3 Run sanitizer and lifecycle tests against ownership and teardown; assert no use-after-free, no unbounded worker accumulation, no stale worker writing to a replacement connection, no authentication bypass, no false scan-completion, no cancellation deadlock, and no durable-index loss.
- [ ] 5.4 Inherited residual-hardening 2.3: if a finite in-process completion bound cannot be proven for arbitrary synchronous raw-volume `DeviceIoControl`, leave this task and the predecessor 2.3 unchecked; do not mark them complete because cooperative simulated-driver tests passed.

## 6. Hosted CI and protected integration

- [ ] 6.1 Recheck GitHub Actions once for the then-current candidate; if jobs can start, inspect the current workflow's required six executions, steps, and artifacts; if billing still rejects jobs before steps, record `HOSTED_CI = BLOCKED_EXTERNAL` with the actual run/annotation and do not dispatch known-unstartable workflows (inherits residual 6.1 / 6.2).
- [ ] 6.2 Inspect live branch protection and rulesets on `main`; record what actually applies; propose review plus passing checks for privileged-service changes without changing billing, subscription, or repository permissions.
- [ ] 6.3 Keep local CI parity labeled local-only; never characterize it as hosted certification.
- [ ] 6.4 Push the feature branch without force and open or update a reviewable PR; if hosted CI remains blocked, leave the PR ready for certification rather than merging on local parity (inherits residual 5.5 push/HEAD verification for the feature branch).

## 7. Integrated validation ladder

- [ ] 7.1 After implementation stabilizes, run protocol compatibility/negotiation, modern and legacy scan-failure semantics, raw-volume cancellation fixtures, worker lifetime and service shutdown, IPC partial-frame regressions, and reconnect/reauthentication on the exact candidate identity.
- [ ] 7.2 Run Debug full CTest, release-test full CTest, Analyze, relevant ASan suites, strict OpenSpec, provenance and governance tests, release-candidate qualification, and full local CI parity on that candidate.
- [ ] 7.3 Run applicable UIA, E2E, and visual runtime tests; retain existing machine-readable UIA skip classifications; do not convert failures into skips.
- [ ] 7.4 Do not silently reuse evidence invalidated by implementation changes; rebuild and rebind executable identity after product commits.

## 8. Adversarial review

- [ ] 8.1 Reviewer A — protocol compatibility: attempt false successful completion on every supported legacy negotiation; challenge failure-terminal behavior, partial reconciliation, cursor state, and version transitions.
- [ ] 8.2 Reviewer B — cancellation safety: attempt deadlocks, stale workers, use-after-free, unsafe handle closure, unbounded threads, and misleading timeout claims; challenge every asserted completion bound.
- [ ] 8.3 Reviewer C — service security: verify closed command surface, peer authentication, heartbeat revalidation, fail-closed behavior, and connection-scoped identities.
- [ ] 8.4 Reviewer D — certification integrity: challenge provenance, exact-SHA evidence, skipped tests, local/hosted separation, and release-candidate qualification.
- [ ] 8.5 Reviewer E — scope and regression: identify unnecessary architecture changes, unsupported guarantees, and regressions outside this campaign.
- [ ] 8.6 Fix confirmed material findings with reproducers; do not implement speculative suggestions without a concrete invariant violation.

## 9. Residual ledger and certification handoff

- [ ] 9.1 Reconcile residual-hardening tasks 2.3, 5.5, 6.1, 6.2, and 6.6 plus residual-defect 1.4/5.4 against actual evidence; inherit residual 6.6; never check the raw-volume completion-bound task unless the required guarantee is genuinely established.
- [ ] 9.2 Recheck `verify/certification/external-frontier.json` for production signing/pins, clean-host, elevated live MFT/USN, physical DPI/High Contrast/mixed-monitor, authorized disposable guest, and hosted Actions; record prerequisite, canonical command, expected evidence, success criterion, and unlock action for each unresolved item.
- [ ] 9.3 Produce the A–J report: repository/integration state; legacy protocol; raw-volume cancellation; hosted CI; branch protection; validation; adversarial review; external certification; remaining work classified `LOCAL_ACTIONABLE` / `BLOCKED_EXTERNAL` / `UNPROVEN` / `SPECULATIVE` / `FUTURE_PRODUCT_WORK`; verdicts stated separately for local implementation, local certification, hosted CI, integration/merge readiness, privileged runtime certification, and production release certification.
- [ ] 9.4 Avoid documentation-only commits that invalidate binary identity or exist merely to trigger blocked workflows; prefer recording hosted results in the handoff or an external evidence record.
