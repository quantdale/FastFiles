## 1. Subtree Gating (product; closes settings-and-appearance 2.5)

- [x] 1.1 Implement `IsPathIncluded(canonicalPath, VolumeSetting.rules)` longest-prefix-match predicate (ordered rules, longest match wins, include/exclude) as a pure, unit-testable function in the engine
- [x] 1.2 Add `IndexPipeline::SetVolumeRules(volumeRowId, rules)` and have `VolumeSessionManager::ReloadConfiguration` pass each configured volume's rules into the pipeline on config change
- [x] 1.3 Wire `IsPathIncluded` into `ApplyMftBatch` (initial MFT ingestion): reconstruct each record's canonical path via `Projection::ReconstructPath`, drop excluded records before persist/apply
- [x] 1.4 Wire `IsPathIncluded` into `ApplyUsnBatch` (USN journal deltas) so excluded subtrees are not upserted and excluded deletes are no-ops
- [x] 1.5 Wire `IsPathIncluded` into the reconciliation observed-set (`BeginReconciliationPass`/`FinishReconciliationPass`) so a now-excluded entry is reconciled away
- [x] 1.6 Wire `IsPathIncluded` into `RebuildAll` so excluded subtrees are not re-ingested during a rebuild
- [x] 1.7 Implement the per-volume deferred buffer for records whose parent chain is not yet resolvable, with a bounded size and decide-on-reconciliation fallback, so ingestion never stalls
- [x] 1.8 Add unit tests (existing `Check()` pattern) for include/exclude/overlap/longest-match/deferred-parent/reconciliation-removal; register the new CTest target in `tests/engine/CMakeLists.txt`
- [x] 1.9 Build green (`cmake --build --preset debug`) and run focused tests (`ctest --preset debug -R engine`); then full suite

## 2. UIA Driver (closes autonomous-runtime-verification 8.1)

- [x] 2.1 Create `verify/uia-driver/` module (PowerShell) wrapping `UIAutomationClient`/`UIAutomationTypes` COM + `SendInput`: element identity (Name/AutomationId/ControlType/ClassName/process), tree traversal (Raw/Control), pattern selection (Invoke/Selection/Scroll/Value/Window/Drag), property read/assert, event subscriptions, input abstraction, timeouts, diagnostic tree dumps (JSON + indented text)
- [x] 2.2 Implement availability probe (interactive UIA context + UIA provider presence) and precise `SKIPPED(reason)` for missing context/provider — no pixel fallback
- [x] 2.3 Implement headless unit tests with mock UIA providers / recorded trees for identity/tree/pattern/property/timeout logic; register as a CTest-equivalent PowerShell test
- [x] 2.4 Implement diagnostic tree-dump format and the "fail clearly when semantic target missing" contract

## 3. UIA End-To-End Validation (closes arv 8.2/8.3, file-operations 8.7-8.9, instant-search 7.6/8.5/10.6, shell-integration-and-commands 2.14, storage-analysis 7.1-7.4)

- [ ] 3.1 Launch `FastFiles`, confirm main window in UIA tree + ready state; record launch evidence
- [ ] 3.2 Multi-column navigation via keyboard (arrows/Enter) and mouse; confirm new column populated + selected item reflected in UIA tree, both input methods
- [ ] 3.3 Selection (single/multi) and scroll verification via UIA scroll patterns/properties
- [ ] 3.4 Connection-badge state verification (degraded vs instant) via UIA
- [ ] 3.5 Dialog and search verification via UIA; in-column error-state (permission-denied/no-longer-available) verification; UI remains responsive
- [ ] 3.6 Rendering-where-practical via UIA geometry/state; `SKIP` with reason where the custom surface exposes no UIA provider
- [ ] 3.7 Cross-window drag out of `FastFiles` into real Explorer (copy + move); verify filesystem side effects programmatically
- [ ] 3.8 Cross-window drag from Explorer into `FastFiles`; verify filesystem side effects programmatically
- [ ] 3.9 Drag between two `FastFiles` windows/panes; verify filesystem side effects programmatically
- [ ] 3.10 Storage-analysis UI validation: instant-open (already-indexed), mid-scan "Calculating..." path, degraded-mode end-to-end, treemap readability/click precision — all via UIA
- [ ] 3.11 Search UI validation: toggle `FastFilesIndexSvc` availability while search open; deep search-result navigation showing every intermediate column; end-to-end structured-filter query
- [ ] 3.12 Collect UIA trees, logs, event traces, screenshots, resulting filesystem state as evidence per scenario

## 4. Test-PKI Code Signing (closes resolve-raw-volume-privilege-insufficiency 4.1; resolves binary-authenticode FAIL)

- [ ] 4.1 Create `verify/capabilities/test-code-signing/` capability (manifest + module) implementing the capability interface
- [ ] 4.2 Generate/use an isolated self-signed test code-signing cert in the Current-User `My` store (no admin); write cert/PFX to gitignored `verify/.signing/`; never commit
- [ ] 4.3 Sign `FastFiles.exe`/`FastFilesEngine.exe`/`FastFilesIndexSvc.exe`/`FastFilesSetup.exe` with `signtool`
- [ ] 4.4 Verify signatures + pinned-thumbprint behavior; confirm `binary-authenticode` capability resolves PASS on the signed dev build
- [ ] 4.5 Implement the injectable production-cert gate (`FF_PRODUCTION_CERT_PFX`/`FF_PRODUCTION_CERT_PASSWORD`); runs only when cert present, else `SKIPPED(production-cert-not-provided)`; never disables verification
- [ ] 4.6 Add `.gitignore` entries for `verify/.signing/` and any cert/key artifacts

## 5. Signed Install And Service Validation (closes resolve-privilege 4.2-4.4, arv 7.4; needs elevation when available)

- [ ] 5.1 Run signed install under elevation (via `verify.ps1 install -Elevate` when an admin token is obtainable); record install integrity, ACLs, registry, task, service config
- [ ] 5.2 Run a real `StartVolumeScan` through the installed service; verify raw-volume open, journal query/read, scan publication, USN outcomes; capture token identity + privileges + raw-volume access evidence
- [ ] 5.3 Single-session crash/recovery validation: unexpected service termination and engine/UI crash -> recovery/degraded mode with crash-dump capture via Crash Analysis
- [ ] 5.4 Attach final evidence (account identity, privilege state, raw-volume/USN outcomes, service state, rollback) to the run tree
- [ ] 5.5 If elevation is unavailable, record `SKIPPED(elevation-required)` with machine evidence; do not fabricate

## 6. Disposable VM Environment Provider (closes arv 2.3/2.4)

- [ ] 6.1 Search approved local locations for Windows install media; record what is found
- [ ] 6.2 If no local media and network/licensing permit, obtain official Windows evaluation media automatically with checksum/signature verification; else document the lawful blocker
- [ ] 6.3 Build unattended-install config (`autounattend.xml`); create temp automation account; enable WinRM (HTTPS or credential-secured) or another secure channel
- [ ] 6.4 Install VS Build Tools + Windows SDK + pwsh prerequisites in the guest; create a clean reference snapshot
- [ ] 6.5 Implement the provider lifecycle (`provision -> activate -> collect-logs -> cleanup -> snapshot-restore`) behind the existing Environment Provider interface
- [ ] 6.6 Validate the same capability runs unchanged across `local` and the disposable VM; collect logs/artifacts back to the run tree
- [ ] 6.7 If no secure lawful disposable environment can be provisioned, evaluate Windows Sandbox / existing interactive runner / new disposable VM / supported cloud runner; select first lawful option; else record machine evidence and keep VM-gated tasks `REQUIRED-BUT-UNAVAILABLE`

## 7. Resumable Orchestrator, Future-Agent Docs, Flaky-Test Policy, Gate

- [ ] 7.1 Implement `verify/intake.ps1` (verb `autonomous`) driving `discover -> plan -> provision -> implement -> build -> test -> diagnose -> repair -> re-test -> validate -> collect-evidence -> update-tasks -> commit -> sync -> archive`; persistent `verify/runs/autonomous/<run-id>/state.json`; resume on re-invocation; per-step timeouts; bounded retries (<=3); failure classification (Class A/B/external); hard iteration cap; deterministic exit codes
- [ ] 7.2 Implement machine-readable status query (current phase, completed/remaining steps, authoritative open-task count)
- [ ] 7.3 Implement the flaky-test policy: preserve initial failure, reproduce under stress, capture timing/resource/thread/process/env data, root-cause, fix, regression coverage, repeated clean tests; "passed on retry" is not terminal without a root-cause fix
- [ ] 7.4 Write `AUTONOMOUS.md` (repo root) + `AGENTS.md` section documenting the one entry point, state file, dependency graph, and the 13-step future-agent intake sequence; write `verify/autonomous/contract.json`
- [ ] 7.5 Run the full autonomous loop end-to-end from a clean state; confirm the four-state archive gate resolves and passes (or correctly reports `REQUIRED-BUT-UNAVAILABLE` for genuinely-external tasks)
- [ ] 7.6 Mark each existing-change task `[x]` only after its required automated verification passes and evidence is recorded; never close a task on narrative proof alone