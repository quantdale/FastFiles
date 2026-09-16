## Context

The repository already contains a semantic UIA driver, marked dogfood
fixtures, live UIA capability scenarios, and visual-capture helpers. They are
spread across the verifier and are not exposed as one obvious developer
command. The UI target and several test directories are also physically flat,
although their responsibilities are already separable by the three-process
architecture. See `proposal.md` for the motivation and the new capability
contract.

## Goals / Non-Goals

**Goals:**

- Reuse the existing production UIA surface and live executable path.
- Add one explicit E2E entry point with deterministic fixture ownership,
  bounded waits, semantic assertions, filesystem checks, and failure evidence.
- Add cold-restore and lifecycle coverage without weakening the existing
  verifier's ownership rules.
- Improve CMake/IDE navigation through target folders and logical source
  groups, while keeping physical moves limited to clearly misplaced tooling.

**Non-Goals:**

- No new IPC primitive, service command, privilege grant, signing bypass, or
  hidden product-only test mode.
- No replacement of the established UIA driver or a second mock UI framework.
- No default inclusion of slow desktop E2E work in the ordinary CTest loop.
- No destructive cleanup outside a marker-validated generated fixture/artifact
  root.

## Decisions

### D1 — One explicit live runner over existing UIA primitives

The E2E command will compose the existing managed UIA driver and the existing
`New-UiaScenarioContext` lifecycle rather than introduce a parallel automation
stack. This preserves the same real providers used by the current verifier and
keeps test actions semantic; screenshots supplement assertions and never
replace them. A separate browser/coordinate automation framework was rejected
because it would create a second product path and would not validate the
custom UIA contracts already owned by FastFiles.

### D2 — Session reuse is explicit and marker-bound

Cold restore requires two real application lifecycles that share one temporary
`LOCALAPPDATA`/`APPDATA` sandbox. The context helper will accept a validated
existing session root only when its FastFiles UIA marker is present. Cleanup
will continue to remove only that exact marker-bound root and the exact process
identities created by the run.

### D3 — Stage deadlines and evidence are first-class

Each scenario reports a stage, elapsed time, expected state, and observed state.
Waits use a deadline rather than unbounded polling. On failure the runner
captures a current UIA tree, diagnostics tail, process snapshot, and screen
image when a live window is available. Generated evidence remains under the
ignored build E2E artifact root.

### D4 — Keep slow UI automation out of default CTest

The E2E source and target metadata will appear under `Tests/E2E` in CMake-aware
IDEs, but the live runner is invoked explicitly by `dev.ps1 test-e2e` and
labelled separately from unit/integration CTest. This preserves a fast edit/
compile loop while making the expensive gate discoverable.

### D5 — Logical grouping before risky physical refactors

The UI's existing stable paths remain unchanged. CMake `source_group(TREE ...)`
and responsibility-based groups, plus target `FOLDER` properties, supply the
IDE hierarchy. Only the legacy root build helper moves to
`tools/development/`, with its README reference updated, because it has no
product-source dependency and is explicitly a development tool.

## Risks / Trade-offs

- **[Risk]** UIA providers can be replaced while a client holds an element. →
  Reacquire dynamic surfaces after every navigation/query replacement and keep
  stale-element failures actionable.
- **[Risk]** Foreground and desktop availability vary by host. → Record an
  honest SKIPPED/REQUIRED-BUT-UNAVAILABLE result only for unavailable optional
  visual context; never convert a product assertion into a pass.
- **[Risk]** A shared restore sandbox can retain a process after the first
  lifecycle. → Track exact identity and pipe state before/after every launch;
  fail cleanup rather than killing unrelated same-name processes.
- **[Risk]** IDE source groups can drift from the source list. → Keep grouping
  declarations adjacent to the target source list and run configure/build plus
  source-marker/doc-drift checks after changes.

## Migration Plan

1. Add the capability spec and tasks, then implement the runner and CMake/IDE
   organization in small source-controlled slices.
2. Add the explicit `dev.ps1 test-e2e` command and README workflow after the
   runner is runnable against the diagnostic build.
3. Run focused E2E scenarios first, then the normal Debug/release-test and
   verifier gates; retain generated artifacts outside tracked source.
4. Roll back through normal Git reversion; no data, service, trust-store, or
   protocol migration is required.
