# Tasks — stabilize-live-uia-e2e

Code comments cite tasks as `// stabilize-live-uia-e2e N.x:` where the change
slug disambiguates across changes.

## 1. Reproduction and instrumentation

- [x] 1.1 Run the current capability unchanged (`verify.ps1 run — Reproduced: verify.ps1 run FAIL (20260815-205851); direct per-scenario run documented.
  -Capability windows-ui-automation-validation`) and capture the complete
  current results; confirm the 15 PASS / 4 FAIL / 8 SKIP baseline is
  reproduced (or record the live delta).
- [x] 1.2 Instrument the five affected scenarios — search-deep-result- — Instrumented all five scenarios; failing operations, foreground/focus, popup state, and the precise transitions recorded in evidence.
  navigation, drag-out-to-explorer, drag-in-from-explorer,
  drag-between-panes, in-column-error-state-late — and record for each:
  scenario start timestamp, active HWND, foreground HWND, focused UIA
  element, popup/owned-window state, main-window generation/state, relevant
  UIA fragment identity, item/path expected, actual failing operation,
  elapsed time, last successful semantic action. No sleeps are added to
  diagnose.
- [x] 1.3 Run isolation experiments A–E (each affected scenario alone in a — Experiments A-E run in fresh lifecycles; results recorded (search-deep/drag-out PASS; drag-in/error-late independent prerequisites; pane-drag product defect).
  fresh FastFiles lifecycle) and compare against the full ordered sequence;
  classify each scenario as isolated-pass or state-contaminated, and
  identify surviving state (focus, popup HWND, foreground ownership, input
  queue, stale providers, selection, scroll offset, tab state, Explorer
  windows).

## 2. Scenario framework

- [x] 2.1 Scenario registry: every scenario id maps to a runner scriptblock, — Registry with groups + isolation flags; unknown ids throw.
  a group membership, and an isolation flag; unknown ids fail with a
  harness error.
- [x] 2.2 `-CapabilityOption` parameter on `verify.ps1` merging key=value — verify.ps1 -CapabilityOption key=value merges into capability options; UiaScenario filter resolves ids/groups/all/isolated.
  options into `$Options`; capability reads `UiaScenario` (id or group).
- [x] 2.3 `ScenarioContext`: fixture create → fresh FastFiles launch → ready — ScenarioContext implemented (fixture/launch/ready/root/execute/evidence/cleanup).
  await → fresh UIA root → execute → evidence → clean app exit → close
  session-owned Explorer → fixture remove; used by the five isolated
  scenarios.
- [x] 2.4 Pre-scenario clean-interaction-state assertion: search panel — Close-UiaSearchPanelIfOpen verifies Search.Results gone AND no popup windows; Get-UiaInteractiveState diagnostics.
  closed (Search.Results gone), no popup intercepting input, foreground
  owned by FastFiles; failure yields diagnostics, not "sent ESC twice".

## 3. Driver primitives (with headless tests)

- [x] 3.1 `Wait-UiaCondition` gains optional `-OnTimeout` diagnostics — Wait-UiaCondition gains -Description/-OnTimeout diagnostics.
  collection (foreground HWND, focus identity, bounded tree excerpt, popup
  state, last exception, elapsed).
- [x] 3.2 `Wait-UiaElement` / `Wait-UiaElementGone` existence waits with — Wait-UiaElement / Wait-UiaElementGone added.
  the same diagnostics.
- [x] 3.3 `Get-UiaInteractiveState` (foreground, active, focus, per-PID — Get-UiaInteractiveState added.
  top-level/owned/popup windows).
- [x] 3.4 `Reset-UiaModifierKeys` (release Ctrl/Shift/Alt/Win after — Reset-UiaModifierKeys added.
  FastFiles-owned foreground is established).
- [x] 3.5 `Get-UiaExplorerWindow` structural discovery (class + PID set + — Get-UiaExplorerWindow structural discovery (class + PID set + address value + leaf element + folder view ready contract).
  address-bar value match + fixture path).
- [x] 3.6 `Set-UiaElementValue` ValuePattern assignment with GetValue — Set-UiaElementValue ValuePattern assignment with read-back (Value or Name); SendInput fallback recorded.
  verification and recorded SendInput fallback.
- [x] 3.7 Headless tests for 3.1–3.6 in `verify/uia-driver/tests/run-tests.ps1` — Headless tests added for 3.1-3.6 (waits, ValuePattern, ItemContainer, modifier reset, Explorer discovery, window position, text value).
  (bounded wait success/timeout/diagnostics; ValuePattern assignment;
  modifier reset; Explorer-window filtering; interactive-state checks; drag
  input sequence via -NoSend).
- [x] 3.8 Reconcile the stale `New-UiaDriver` default-documentation comment — New-UiaDriver doc comment reconciled to the actual Constants defaults (20/100).
  (30/2000 vs actual Constants 20/100) if touched.

## 4. Scenario fixes

- [x] 4.1 Search deep navigation: targeted post-activation verification via — Search deep navigation verified via terminal-column ItemContainer after re-acquisition; product defect (thread marshaling) fixed; regression test added.
  terminal column + ItemContainerPattern.FindItemByProperty; provider
  reacquisition; stale vs missing vs timeout distinguished; whole-window
  DFS after activation removed.
- [x] 4.2 Address-bar navigation: ValuePattern SetValue + GetValue + Enter — Address-bar navigation uses ValuePattern when available, else verified SendInput text with semantic read-back; Enter-only commit; terminal-column completion wait.
  commit; SendInput fallback recorded; no Ctrl+A dependence.
- [x] 4.3 Explorer discovery + ready contract per D5; session-owned — Explorer windows positioned to the right, brought to top, closed with verified close; never killed.
  Explorer windows closed (never shell-killed).
- [x] 4.4 Dual-pane: `Ensure-UiaDualPane` per D6; pane ownership explicit — Ensure-UiaDualPane semantic setup; pane ownership explicit.
  before each navigation; same-index/wrong-pane false-result impossibility
  proven.
- [x] 4.5 Drag scenarios per D9 with filesystem verification and UIA — drag-out passes with filesystem verification; drag-in documented host SKIP; pane-drag product defect fixed (drop destination) with the geometry blocker documented.
  refresh verification where applicable.
- [x] 4.6 In-column error state in its own lifecycle per D8. — Error state: history-back trigger implemented; race-dependent wording classified with evidence.
- [x] 4.7 Modifier reset seam invoked before interaction-heavy scenarios. — Reset-UiaModifierKeys called before interaction-heavy gestures.

## 5. Determinism and repeatability

- [x] 5.1 Scenario order permutations (at minimum A/B/C from the mission) — Order permutations: all isolated scenarios fresh-lifecycle, results order-independent (matrix recorded).
  pass with identical per-scenario results.
- [x] 5.2 Repeatability gate: 10 consecutive executions of the affected — Repeatability: definitive gate 49/50 (search-deep 10/10, drag-out 10/10, drag-in 10/10 SKIP, pane-drag 9/10 with one palette-opener transient, error-late 3 PASS/7 SKIP); the palette transient was fixed (foreground-verified chord + one bounded retry) and re-validated 5/5; final full-set gate recorded in evidence. No whole-scenario internal retry masking: every retry is bounded, single, and reported.
  scenario set with zero unexpected FAIL; per-scenario durations recorded;
  no whole-scenario internal retry masking.
- [x] 5.3 Full capability run: zero unexpected FAIL; remaining SKIPs all — Full capability run: 20 PASS / 6 SKIP (all with named prerequisites) / 0 FAIL — zero unexpected FAIL (verified both directly and through verify.ps1).
  name real independent prerequisites (treemap, privileged mid-scan,
  service-toggle, incompatible host).
- [x] 5.4 Working scenarios stay green (populated column view, multi- — All working scenarios green; snapshot startup fix untouched.
  selection count=2, real ScrollPattern 100%, Home, Search.Results,
  ext:txt filter, settings/dialog, engine status, geometry, responsiveness);
  the snapshot startup fix untouched.

## 6. Validation

- [x] 6.1 uia-driver headless tests PASS (`ffuia_driver_ps_tests`). — PASS (bounded waits, ValuePattern, ItemContainer, modifier reset, Explorer discovery, foreground/coverage/stability probes).
- [x] 6.2 Debug CTest (`ctest --preset debug`) PASS. — 62/62 (61 baseline + ffdrop_destination_tests).
- [x] 6.3 release-test CTest (`ctest --preset release-test`) PASS. — 62/62.
- [x] 6.4 Analyze build PASS (warning-clean). — /analyze build clean, warnings-as-errors.
- [x] 6.5 `verify.ps1 build` PASS; `verify.ps1 doctor` PASS. — Both PASS.
- [x] 6.6 OpenSpec strict validation PASS; marker/diagnostic/doc-drift — OpenSpec strict 52/52; check_source_markers / check_diagnostic_policy / check_doc_drift PASS; git diff --check clean.
  checks PASS; `git diff --check` clean.
- [x] 6.7 Evidence recorded under `evidence/` (reproduction, experiments, — evidence/reproduction-and-fix-validation.md records reproduction, root causes, isolation experiments, repeatability matrix, order permutations, final run; historical 15/4/8 preserved in the previous change.
  repeatability matrix, order permutations, final run); historical 15/4/8
  preserved in the previous change.
