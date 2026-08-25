# Baseline evidence — Pane.1 after Toggle Dual Pane

## Scope

This is the pre-change baseline for `interactive-ui-runtime-determinism`. It was
captured before any product or verification implementation edit in this change.
Ignored `verify/runs/` artifacts are not read or copied here; this file records only
the bounded command result and sanitized environment facts needed to reproduce the
failure.

## Repository and command

- Branch: `main`
- HEAD: `338a0ab86b9ce7fccd09ba5a11724efd5e3c6c8d`
- `origin/main`: `338a0ab86b9ce7fccd09ba5a11724efd5e3c6c8d`
- Worktree: clean before the run
- Command: `pwsh ./verify/verify.ps1 run -Capability windows-ui-automation-validation -Configuration debug -CapabilityOption UiaScenario=selection-model-interactions -Format human`
- Result: exit code `1`, `status=FAIL`, `reason=scenario-failed`
- The run was isolated and used the repository's diagnostic/debug UIA executable
  selection and disposable session lifecycle.

## Observed precondition facts

- Before launch, no repo-build `FastFiles.exe` or `FastFilesEngine.exe` process was
  present. A separately installed `FastFilesIndexSvc` process was present and was
  not targeted by the harness.
- The foreground window before launch belonged to an unrelated interactive process
  titled `browser-blackhole`; this is evidence for input-routing classification,
  not proof that it caused the Pane.1 failure.
- The capability's summary reported only `scenario-failed`; the current helper did
  not expose the selected palette result, command-dispatch return, workspace pane
  count, ColumnView pane count, provider generation, or post-command UIA tree in its
  caller-visible result. That missing evidence is itself a harness gap under this
  change.
- The previous handoff records the concrete failure stage as:
  `Pane.1 did not appear after 'Toggle Dual Pane'`.

## Baseline classification

At baseline the failure is **unclassified**, with candidates in classes B, C, D,
E, F, G, H, and I from the campaign brief. It is not accepted as a product defect,
an environment skip, or a successful command until the semantic diagnostic path
records the state transition at each ownership layer.

## Required next evidence

The next reproduction must capture sanitized values for the UI PID, observed
repo-owned engine PID(s), main/foreground HWND, workspace and ColumnView pane
counts, active pane, palette result identity/selection, command dispatch, provider
runtime IDs and availability, popup/focus state, geometry, persisted test state,
and product diagnostics before and after the command. It must re-root provider
queries after the expected ColumnView replacement and retain the first failure.
