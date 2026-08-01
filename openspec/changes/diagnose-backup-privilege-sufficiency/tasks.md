## 1. Real Call-Site Instrumentation

- [x] 1.1 Add a token-state capture helper (privilege held/enabled per relevant privilege, account SID/name) callable at an arbitrary point in `FastFilesIndexSvc`, reusable by both the existing probe and the real scan path
- [x] 1.2 Call the capture helper immediately before the raw volume `CreateFileW` in `src/indexsvc/src/VolumeScanner.cpp`, and immediately after, recording the call's result/error code alongside the pre-call token state
- [x] 1.3 Implement the four-way outcome classification (privilege absent / present-not-enabled / enabled-but-denied / succeeded) as an explicit enum or tagged result, not a bare bool
- [x] 1.4 Route the classified record through structured, persistent logging (local diagnostic log or the existing verification-evidence path), not console/stderr only
- [x] 1.5 Unit test the classification logic against synthetic `TOKEN_PRIVILEGES`/error-code inputs covering all four outcomes, independent of a real elevated environment

## 2. Reproduction Under the Real Service

- [x] 2.1 Confirm this instrumentation is a behavioral no-op: run the existing test suite and confirm no change in pass/fail outcomes
- [ ] 2.2 Once a real elevated Windows environment with the installed `FastFilesIndexSvc` service is available (tracked separately for `autonomous-runtime-verification`), run a real volume scan and capture the classified outcome for the account's actual raw-volume-open attempt
- [ ] 2.3 If the outcome is "privilege absent" or "present-not-enabled," inspect `src/setup/src/ServiceRegistration.cpp` for grant/enable timing relative to service start, and confirm whether restarting the service after grant, or moving the enable-call inside the service's own startup path, resolves it
- [ ] 2.4 If a token/configuration fix is identified in 2.3, apply it and re-run 2.2 against the real service to confirm the outcome becomes "succeeded"
- [ ] 2.5 If the outcome is "enabled but denied" and persists after ruling out 2.3's causes, stop here — do not attempt an architecture change in this task list

## 3. Evidence and Hand-off

- [ ] 3.1 Record the final classified outcome, the exact error codes observed, and whether a token/configuration fix was applied, as an evidence artifact (e.g. alongside the existing `verify/runs/index-storage-and-scanning/...` evidence for task 4.6)
- [ ] 3.2 If the verdict is "token/configuration defect, fixed and re-verified": update `index-storage-and-scanning` task 4.6 and `establish-architecture-foundation` task 7.1 to reflect the real-service-verified pass, citing this change's evidence
- [ ] 3.3 If the verdict is "genuine privilege-model insufficiency": write up the specific evidence (classification, error codes, account context) as input for a follow-up change proposal against `establish-architecture-foundation` D1/D2 — do not decide or scope that redesign here
- [ ] 3.4 Leave `establish-architecture-foundation` task 7.1 and `index-storage-and-scanning` task 4.6 exactly as they are (open/blocked) if this change's evidence-gathering itself cannot complete without the real elevated environment from section 2 — do not mark them closed on partial evidence
