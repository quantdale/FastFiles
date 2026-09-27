## 1. Baseline and lifecycle registration

- [x] 1.1 Record the live branch, HEAD, `origin/main`, ahead/behind counts, porcelain status, and worktrees at apply time. Do not reset or rewrite history to match a remembered SHA. (evidence: `evidence/apply-baseline.md`)
- [x] 1.2 Register this change in `openspec/lifecycle.json` without checking `legacy-protocol-correctness-and-raw-volume-cancellation-closure` task 5.4 or `final-residual-hardening-and-hosted-ci-recovery` task 2.3, without reporting the raw-volume completion bound as proven, and without marking the predecessor completed or superseded merely to keep a single active campaign.

## 2. Scan-authority reproducer

- [x] 2.1 Add a deterministic engine test for an incomplete initial scan: apply a batch, pause, drop a later batch, resume so replacement work is issued, deliver the original completion before the replacement finishes, then fail or disconnect the replacement. The test's own evidence SHALL distinguish the original attempt from the replacement even though the terminal carries only a volume id.
- [x] 2.2 Add the already-indexed reconciliation form of that pause-then-terminal-after-resume order. `TestPausedReconciliationDropIsNonAuthoritative` does not satisfy this task, because resume there does not clear the coverage hole.
- [x] 2.3 Do not edit production scan-authority code until 2.1 and 2.2 fail for the reported order, or until a source-backed impossibility proof explains why that order cannot occur. A passing paused-reconciliation test is not that proof.

## 3. Replacement-scan authority

- [x] 3.1 If 2.1 or 2.2 reproduces a stale terminal, implement in-process retirement from design D2 and D3. A local generation counter alone is not sufficient while old and new terminals are indistinguishable on the wire. Do not resize `ScanComplete` or `ScanFailed`. Do not add a command that accepts a client-supplied path, handle, or device string. Point the change at `establish-architecture-foundation` where the closed command surface is involved.
- [x] 3.2 If, after 3.1, a volume-id-only terminal can still authorize the replacement, stop and record that evidence before adding a message. Any attempt discriminator SHALL be a new negotiated message type, not a longer completion or failure payload.
- [x] 3.3 Add tests that a stale terminal is rejected or retired, an incomplete initial scan stays incomplete, missing records are retried from the last applied cursor, an incomplete reconciliation does not delete valid records, a failed replacement does not inherit false authority, ordinary pause/resume and a successful scan still complete, disconnect cannot restore stale authority, and a peer that cannot represent scan failure still does not get a false completion.

## 4. Interrupted reconciliation retry

- [x] 4.1 Add a deterministic test that starts reconciliation of an already-indexed volume, drops the connection before an authoritative terminal, and reconnects with a new ephemeral volume id. Record whether the retry is queued at the five-second floor or left to the ordinary scheduler. `TestLegacyFailureReissueIsThrottledAfterConnectionLoss` does not satisfy this task.
- [x] 4.2 If 4.1 shows the ordinary scheduler is the only wake, transfer the durable retry intent onto exactly one slot for the new volume id, honor the five-second floor, wake the retry scheduler, and do not send the discarded connection-scoped id. Do not start an immediate reconnect/rescan loop, and do not create a second overlapping reconciliation for that volume.
- [x] 4.3 Cover repeated failure, multiple volumes, volume disappearance, a disabled volume, a paused volume, disconnect during retry admission, a failed request submission, service reauthentication failure, and shutdown while a retry is pending. Each volume keeps at most one outstanding retry, and repeated failure stays throttled.
- [x] 4.4 Drive due times with a controllable clock or direct deadline update. Do not sleep through the ten-minute poll. Assert that queue state stays bounded.

## 5. Combined authority and retry

- [x] 5.1 Add one regression that pauses, resumes, interrupts reconciliation, disconnects, reconnects, delivers a stale terminal, and then recovers on an authoritative attempt. Do not treat Finding A or Finding B as resolved until this test passes.
- [x] 5.2 Run the affected engine, index-persistence, IPC, and protocol tests after 3.1 and 4.2, including the existing mixed-version legacy-failure tests.

## 6. Many-tabs overflow

- [x] 6.1 Reproduce `shell-many-tabs-overflow` at the scenario's narrow width. Record separately the logical tab count, the visible tab-button count, whether the overflow affordance is present, and the count the harness currently asserts.
- [x] 6.2 Keep the logical-count floor at least 8. Do not lower it to 4. Do not treat a hidden overflow tab as a deleted tab.
- [x] 6.3 If the logical count is at least 8 and a hidden tab activates the correct workspace, fix the harness so it measures that semantic state. If the logical count is below 8, fix the tab-creation defect. Preserve the responsive overflow layout.
- [x] 6.4 Prove overflow reachability, keyboard activation, close and switch identity, the narrow width, and a normal width where every open tab is visible. Report visual, UI Automation, and semantic outcomes separately.

## 7. Targeted-build identity

- [x] 7.1 Map `fastfiles_build_identity`, every `fastfiles_add_version_resource` consumer, and any other binary that embeds a source revision, including targeted ASan, diagnostic, and test binaries and the exact commands local and hosted CI use.
- [x] 7.2 In an isolated worktree or equivalent fixture, configure and build at revision A, advance the source identity to revision B, run those targeted commands, and read the embedded identity. The check must fail if it accepts an artifact that a different target already refreshed. Check the four release executables and the targeted diagnostic/test binaries separately. (`evidence/targeted-build-identity-isolated.ps1` and `.json`: 8 identity-bearing targets each passed in a separate worktree; six targeted ASan binaries were classified as having no identity carrier.)
- [x] 7.3 If a certification-relevant binary can keep a stale identity after a successful targeted build, correct only that dependency edge and do not force a global recompile. If the current edges already refresh every such binary, record the no-code closure and do not edit the build files. Keep a malformed `FASTFILES_BUILD_ID` fail-closed, and keep a well-formed override embedded as given. (`tests/setup/CMakeLists.txt` now directly orders both header-consuming package-trust targets after `fastfiles_build_identity`; the isolated A→B proof passes, the probe's well-formed override is embedded as given, and `ffbuild_identity_freshness_tests` covers empty/malformed rejection.)

## 8. Hosted certification hold

- [x] 8.1 Recheck GitHub Actions once at apply start. (evidence: `evidence/apply-baseline.md`; run `36302493823`, six jobs, 0 steps, billing annotation) Record the live run result or the live error. Do not present a workflow id named only by the campaign prompt as a new observation. Do not change billing, spending limits, plans, branch protection, or rulesets.
- [ ] 8.2 If jobs still fail before any step because of billing, do not dispatch more of those runs. Record the protection and ruleset API result, including HTTP 403, and do not treat that failure as permission to merge. Open or update a reviewable PR with an explicit no-merge hold.
- [ ] 8.3 After the candidate is pushed, recheck hosted CI once. If the six jobs start, require `debug`, `release-test`, `analyze`, `asan-protocol-ipc-lifecycle`, `repository-hygiene-and-contracts`, and `unsigned-release-candidate-qualification` to execute and pass on the executable candidate. Label local CI parity as local. A documentation-only tip is not that candidate unless executable-tree equivalence is recorded.

## 9. Raw-volume completion bound

- [x] 9.1 Do not redesign privileged worker ownership, overlapped device control, `CancelIoEx` policy, or process isolation. In the handoff, classify the synchronous raw-volume `DeviceIoControl` completion bound as unproven. Leave predecessor task 5.4 and residual-hardening task 2.3 unchecked. Do not treat simulated stall tests as that proof. (evidence: `evidence/certification-residuals-and-handoff.md`)

## 10. Local certification and review

- [ ] 10.1 On the exact executable candidate, run the new scan-authority and retry regressions, the combined pause/disconnect/reconnect scenario, mixed-version legacy-failure tests, index-persistence and reconciliation tests, worker-lifecycle tests without closing the completion bound, IPC framing tests, debug and release-test full CTest, static analysis, the relevant ASan tests, strict OpenSpec, provenance, governance, and documentation-drift checks, the targeted-build identity result, local CI parity, E2E, UI Automation, the visual core catalog, the conditional gallery, and the many-tabs scenario. Record revision, binary identity, command, and pass/fail/skip counts. Do not convert an unavailable external environment into pass.
- [ ] 10.2 Run independent read-only reviews for scan authority, retry scheduling, tab and visual integrity, build provenance, and certification scope. A material finding needs a concrete reproducer or a source-level proof. Fix confirmed defects and rerun the affected tests. Do not reopen unrelated architecture for an unsupported hypothetical.
- [ ] 10.3 Commit only the campaign's coherent slices, push the feature branch without force, and do not merge to `main` while required hosted checks are blocked. Do not push documentation-only commits solely to chase a workflow that cannot start.
