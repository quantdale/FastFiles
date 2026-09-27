# Tasks

## 1. Scan transport and retry truth

- [x] 1.1 Change scan execution to return an explicit outcome; never append a terminal after a failed frame write.
- [x] 1.2 Abort the owning connection after a transport-fatal scan outcome and preserve the H1/H2 teardown/security contracts.
- [x] 1.3 Add volume/error metadata to the transport-independent scan diagnostic.
- [x] 1.4 Add deterministic behavioral tests for pre-write failure, partial-frame failure, cancellation, terminal delivery, and worker exception handling. — done: closed by `final-residual-hardening-and-hosted-ci-recovery` 2.1/2.2/2.4/2.5 plus `legacy-protocol-correctness-and-raw-volume-cancellation-closure` 2.2/3.3/4.2/5.2, which add the mixed-version terminal binding, non-terminal legacy failures, successful-scan byte identity, and the hermetic cancellation/stall fixtures. The residual caveat in the successor ledger (raw-volume kernel completion bound) is tracked there as residual 2.3 and does not reopen this behavioral-test task.
- [x] 1.5 Add bounded retry of delivered initial/reconciliation `ScanFailed` outcomes and regression coverage for retry state.

## 2. Strict OpenSpec and CI parity

- [x] 2.1 Add pure step/aggregate/tool-unavailability classification with an explicit truth table.
- [x] 2.2 Resolve `npm`/`npx` before invocation; distinguish bootstrap unavailability from validator failure.
- [x] 2.3 Make required skips and missing tools non-PASS; align hosted and local pinned commands.
- [x] 2.4 Add controlled regression fixtures for success, validator failure, missing tool, exit-2, and aggregate status.

## 3. Visual catalog truth

- [x] 3.1 Add the canonical 16-core/4-gallery scenario registry and pure missing/duplicate/unexpected comparator.
- [x] 3.2 Run the comparator on normal and early-failure paths; add a headless contract test.
- [x] 3.3 Repair the current settings/theme precondition and rerun exact-tree core and gallery catalogs.
- [x] 3.4 Record the historical 20 = 16 + 4 mapping and explicit disposition of older shell-only states.

## 4. UIA reliability and navigation boundaries

- [x] 4.1 Distinguish unavailable foreground observations from proven ownerless/foreign state and update converter tests.
- [x] 4.2 Propagate native input success and cleanup failures; honor geometry settle results.
- [x] 4.3 Require repeated fresh Explorer-source attempts before the automation-limitation skip; preserve input/geometry evidence.
- [x] 4.4 Use virtual-screen evidence bounds and retain/report diagnostics-copy failures.
- [x] 4.5 Wrap navigation-helper exceptions, stop dependent Search setup after failed navigation, and add behavioral boundary tests.
- [x] 4.6 Run at least ten fresh trials for practical nonprivileged scenarios and retain a machine-readable ledger.

## 5. Integrated certification

- [x] 5.1 Run focused protocol, IPC, service, engine, UIA-driver, and verification tests.
- [x] 5.2 Run debug/release-test builds and full CTest, Analyze, ASAN, strict OpenSpec, and local CI parity.
- [x] 5.3 Run UIA, E2E, visual catalog, provenance, governance, and display/clean-host harness contracts.
- [x] 5.4 Run an independent adversarial review wave and disposition every confirmed finding. — done: `legacy-protocol-correctness-and-raw-volume-cancellation-closure` tasks 8.1–8.6 ran five independent read-only reviewers and dispositioned every confirmed material finding (fixed with reproducers: unthrottled legacy reissue, paused-drop reconciliation authority, journal fixture coverage, stop-vs-setup-failure terminal suppression, seam-policy exclusivity, supersede fixture race, stale comments, citation drift); report-only residuals are recorded in that change's handoff report §G.
- [x] 5.5 Update lifecycle, workstate, and evidence; commit, push without force, and inspect exact-SHA hosted status.
