## Why

The current mainline still has a reproducible runtime-verification failure in the
dual-pane lifecycle: the isolated `selection-model-interactions` scenario reports
that `Pane.1` did not appear after the real `Toggle Dual Pane` command. The prior
selection campaign proved the same scenario 10/10, while later UI, provider, and
harness changes expanded the state surface without preserving enough semantic
evidence to distinguish a product transition defect from command dispatch,
foreground/input ownership, stale-provider reconstruction, persistence
contamination, process contamination, or an unavailable interactive desktop.

This change closes that local determinism frontier without weakening the degraded
runtime, three-process security model, or the repository's honest external-gate
policy.

## What Changes

- Add a new cross-lifecycle runtime contract for deterministic dual-pane creation,
  activation, navigation, selection isolation, provider replacement, and teardown.
- Record and classify the pre-change Pane.1 failure with command, workspace,
  ColumnView, UIA, window, focus, process, and persisted-state evidence.
- Strengthen the production seams and headless tests for enable/disable cycles,
  active-pane migration, tab and workspace restoration, Home/filesystem mixtures,
  async refresh overlap, and stale-provider lifetime.
- Make command-palette invocation prove result identity, selected-result state,
  foreground ownership, activation, and command-specific postconditions.
- Replace dual-pane setup's send-and-search behavior with one structured semantic
  transaction and deterministic single-pane cleanup, including explicit failure
  classification and provider reacquisition.
- Harden UIA-driver waits, foreground/input preconditions, provider replacement,
  exact process ownership, disposable workspace state, cleanup, and diagnostics
  without using OCR, arbitrary retry-until-pass, or pixel assertions as truth.
- Add recorded-tree/driver regression coverage and a repeatability gate requiring
  20 consecutive clean isolated selection executions when a genuine interactive
  desktop is available.
- Update current OpenSpec evidence, verification documentation, and workstate while
  keeping historical campaign records historical.

## Capabilities

### New Capabilities

- `interactive-ui-runtime-determinism`: semantic runtime determinism and failure
  classification for interactive dual-pane, command, focus, provider, state, and
  lifecycle verification.

### Modified Capabilities

- `ui-automation`: dual-pane provider identity/lifetime, semantic readiness,
  focus/input preconditions, and post-mutation event behavior.
- `command-palette`: semantic command-result selection and activation evidence.
- `multi-selection-and-dragdrop`: dual-pane selection isolation across lifecycle
  reconstruction and active-pane changes.
- `quick-access-home`: Home/filesystem dual-pane state preservation and restoration.

## Impact

- `src/ui`: `WindowShell`, `CommandPalette`, `CommandSystem`, `NavigationWorkspace`,
  `ColumnView`, UIA providers, and persistence/lifecycle seams.
- `tests/ui`, `tests/navigation`, `tests/commands`, and relevant UIA/provider tests.
- `verify/capabilities/windows-ui-automation-validation`, `verify/uia-driver`, and
  PowerShell driver tests.
- `openspec/changes`, `docs/agent/WORKSTATE.md`, `progress.md`, `README.md`, and
  `CODE_INDEX.md` as required by the final evidence.

No service command, IPC wire format, signing policy, trust store, privileged scan,
or production engine lifetime changes are in scope.
