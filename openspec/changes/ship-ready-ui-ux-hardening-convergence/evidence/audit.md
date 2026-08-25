# Ship-ready UI/UX hardening convergence audit

## Baseline

- Repository: `quantdale/FastFiles`
- Target branch: `main`
- Baseline commit: `69e894f79319bdb4b67fff29281d72e56d1dd857`
- Baseline `origin/main`: `69e894f79319bdb4b67fff29281d72e56d1dd857`
- Baseline worktree: clean
- PR #6 branch: `origin/stabilization/ship-ready-2026-08-23`
- PR #6 head: `9ddf1a3189be32db11c97e7c79bb7404caac1d8f`
- PR #6 merge base with current main: `90fa560a99217cabcf456b1198c83351c46513ed`

The baseline was established after `git fetch --all --prune`. GitHub CLI could
not read the hosted PR because this environment has no GitHub authentication;
the local remote-tracking branch and commit objects provide the complete PR
diff for the code audit. PR administration remains an external task unless
authentication becomes available.

## PR #6 disposition

The twelve PR commits were inspected commit-by-commit and file-by-file against
the baseline. The disposition is recorded in `design.md`; the condensed result
is:

| PR commit | Area | Disposition |
| --- | --- | --- |
| `73fc9fc` | workspace breadcrumb 8.3 expansion | Superseded by the current shared `ffui::NormalizeDisplayPath` lineage; the current seam still needs a UNC/I/O safety fix. |
| `1525e91` | tab 8.3 expansion | Superseded; current tabs already consume the shared display-only seam. |
| `153d970` | theme palette | Product intent independently implemented on current main; do not port the stale palette churn. |
| `a16aab3` | Storage native control painting | Incompatible with the current owner-drawn radio/UIA implementation. Reimplement only the remaining push-button gap. |
| `0f83b12` | 8.3 test | Incorrect as a current implementation basis; its large stack fixture caused the historical `/analyze` C6262 failure. |
| `9bd408b` | Storage keyboard reachability | Requirement remains valid; current radio implementation supersedes the old header-only approach. |
| `a15b9e7` | test include cleanup | Superseded with the stale test fixture; no standalone value to port. |
| `bf599d4` | audit document | Historical evidence only; this campaign has a fresh ledger and current-main evidence. |
| `814889f` | dark accent contrast | Intent independently covered by current theme tokens and executable contrast tests. |
| `430c18a` | theme contrast tests | Superseded by stronger current tests in `tests/ui/test_ui_style.cpp`. |
| `5554432` | theme contract comments | Unnecessary documentation churn relative to current main. |
| `9ddf1a3` | tertiary contrast test | Superseded by current broader contrast coverage. |

## Current-main findings

1. `ffui::NormalizeDisplayPath` currently attempts `GetLongPathNameW` for any
   path containing `~`, including UNC and extended-UNC paths. That can cause
   network I/O from paint/UIA paths. The fix must gate resolution to local DOS
   drive paths and retain prefix stripping as a pure presentation operation.
2. `NavigationWorkspace::BeginAddressBarEdit` currently fills the editable
   address control with `NormalizeDisplayPath(currentPath)`. This loses the
   exact canonical path in a field whose purpose is path editing/copying.
3. New default bookmark labels are derived from the canonical leaf, so a short
   leaf can still leak into Sidebar/Home. Legacy auto-derived labels need the
   same display-only treatment while preserving explicitly different labels.
4. Storage radio controls already have the newer custom drawing, UIA, focus,
   arrow/mutual-exclusion, High Contrast, and theme-token path. The remaining
   current-main gaps are explicit radio-group Tab semantics, themed Back/Up
   push buttons, disabled Up state, and immediate status refresh when engine
   availability changes.
5. Current UIA fragment generation, stale-provider retirement, pane-local
   identity, command-palette dispatch, and process-ownership cleanup are newer
   mainline work. This campaign will add only headless semantic coverage around
   touched seams and will not alter provider identity or lifetime code.

## Boundaries and external evidence

No process boundary, IPC command, authentication pin, privileged service, index,
signing, trust store, or production MFT/USN behavior is in scope. Degraded and
unprivileged operation remains the supported local path. Hosted PR administration,
production-signed validation, clean-host privileged validation, and live desktop
foreground evidence remain external gates and are not converted into local
passes.

## Implemented local slice

- `DisplayPath.h` now strips extended prefixes without resolving and invokes
  `GetLongPathNameW` only for local DOS drive paths containing a short-name
  marker. Its lookup buffer remains heap-backed and bounded.
- Breadcrumbs/tabs/Sidebar/Home/Search/details/UIA display projections retain
  their canonical source values while consuming the display seam. Editable
  address mode and operation/copy values remain exact canonical paths.
- Path-derived bookmark labels are normalized at presentation boundaries;
  explicit labels that differ from the canonical path-derived forms are kept.
- Storage Back/Up controls use a lifetime-safe token-backed subclass with native
  input/UIA routing preserved. High Contrast delegates to native rendering.
  Back/Up are explicit Tab stops, the six mode radios have one Tab-entry group,
  Up is disabled at a root/no-parent scope, and engine availability refreshes
  visible status/data without changing aggregate request identity rules.
- Headless coverage now proves UNC resolver non-invocation, dynamic real-8.3
  handling when the volume supports it, exact address identity, Storage control
  Tab topology, degraded/stale labels, and the existing theme contrast floors.

Focused evidence completed so far:

```text
ffdisplay_path_tests                 PASS
ffnavigation_tests                   PASS
ffstorage_visibility_tests           PASS
ffstorage_radio_state_tests          PASS
ffui_style_tests                     PASS
FastFiles target build               PASS
```

The focused slice was followed by the complete local ladder: the final Debug
CTest was 81/81 in 277.37 seconds, and the final release-test CTest was 81/81
in 243.99 seconds. Analyze was warning-clean, including no C6262; ASAN
protocol/IPC was 3/3; `verify.ps1 build` and `verify.ps1 doctor` passed.
Source-marker and diagnostic-policy scripts passed; the final
`ffuia_driver_ps_tests`, `ffintake_gate_ps_tests`, `ffdoc_drift_tests`, and
`ffprovider_contract_ps_tests` rerun passed 4/4. Strict OpenSpec validation
passed 68/68 and `git diff --check` is clean. An earlier concurrent verifier
wrapper returned only its generic `status=FAIL` while Analyze was running; an
uncontended verifier rerun completed `status=PASS` and ran its own Debug and
release-test suites. No opaque wrapper result was accepted without a clean
rerun.

The first post-documentation release pass exposed one actionable test-harness
failure in `ffuia_driver_ps_tests`: the deterministic mock tree dump was still
subject to the live UIA timeout and lost one leaf on the slower release-test
PowerShell host. `verify/uia-driver/internal/Dump.ps1` now keeps mock dumps
time-unbounded while retaining depth/width limits; live dumps remain
wall-clock bounded. The direct driver suite and the focused release-test rerun
then passed, and the complete release-test rerun is repeated after this fix
before handoff.

The final Debug suite subsequently completed **81/81 PASS**. Storage coverage
is distributed across the executable seams rather than a screenshot fixture:
`ffstorage_visibility_tests` covers all six mode ownership transitions,
`ffstorage_radio_state_tests` covers native state precedence and Tab topology,
`ffstorage_percentage_tests` covers percentage/sort-key semantics,
`ffsnapshot_aggregate_tests` covers complete/partial/unavailable aggregation,
`fftreemap_filter_tests` covers category filtering, `ffshell_layout_tests`
covers DPI/layout geometry, and the new visibility status assertions cover
degraded/stale/unavailable wording. The full suite also exercises the compiled
Storage Analysis path and aggregate completion handlers.

## Live UIA evidence and harness reconciliation

The first complete live capability run exposed a real validation defect rather
than a product failure: the shared `search-results-discoverable` scenario kept
the `Search.Results` provider acquired before typing, even though a query
replacement retires that fragment. The stale provider could not be used as a
proof of the new result state. The scenario now reacquires the main window,
`Search.Results`, and the result item after the query mutation. This preserves
the current stale-provider contract and does not weaken the foreground guard.

Evidence after that correction:

- `search-results-discoverable` isolated live run: `PASS`.
- `search-deep-result-navigation`, `search-structured-filter-query`, and
  `search-allocated-size`: `PASS`; the latter also proved query replacement,
  degraded Size-on-Disk unknown semantics, and sort state.
- Error-state, treemap, selection, and drag-focused live groups: `PASS` for
  their product assertions when foreground was available.
- Aggregate isolated public capability result: `PASS`, 7 passed and 3
  honestly skipped. The skips were `interactive-foreground-unavailable` for
  Pane.1 activation/selection and `explorer-drag-source-unavailable` for the
  host's synthetic Explorer drag source. No downstream assertion ran after
  those guards.

The public capability's final complete invocation returned `PASS` with 23
passed, 6 recorded skips, and 0 failures. The `verify.ps1 run` wrapper was
also run three times after the fix (including explicit `UiaScenario=all`); each
wrapper invocation returned `status=FAIL` without exposing a subscenario in its
console output, while the same exported capability returned the complete
zero-failure result above. This wrapper discrepancy is retained as a
validation-harness/order-sensitive blocker rather than relabeled as a product
pass or hidden with retries. The headless UIA/provider suites remain green,
and no UIA provider implementation was changed.
