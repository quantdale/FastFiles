## Why

FastFiles is now on a substantially newer mainline than draft PR #6, but the
PR remains a useful audit record for ship-readiness defects in navigation chrome,
theme convergence, Storage Analysis, and accessibility. A fresh reconciliation
is needed because the old implementation cannot be merged safely, and current
main still has a display-path I/O leak and a few Storage Analysis state/focus
gaps that the historical audit did not close.

## What Changes

- Audit PR #6 commit-by-commit and file-by-file against the current `main`, with
  an explicit disposition for every historical change.
- Harden display-only path presentation across breadcrumbs, tabs, editable versus
  non-editable address presentation, Home/recent labels, details, Search, UIA,
  sidebar, and Storage-derived labels. Local 8.3 expansion remains best-effort;
  UNC/network paths never trigger filesystem resolution from paint or layout.
- Preserve canonical navigation, history, index, file-operation, and copy-path
  identities byte-for-byte; synthetic Home and This PC locations remain typed.
- Make Storage Analysis Back/Up and mode controls theme-coherent, keyboard-correct,
  visibly focused, DPI-aware, degraded-mode truthful, and UIA-semantic while
  retaining native control behavior and High Contrast fallbacks.
- Add deterministic headless coverage for display-path edge cases, canonical
  identity separation, Storage control navigation/state, theme contrast, and
  runtime status/theme invalidation; avoid the historical `/analyze` C6262 stack
  regression by keeping large fixtures heap-backed.
- Extend current UIA regression evidence only at touched semantic seams; retain
  live foreground limitations as honest external skips.
- Record the final PR #6 disposition, validated implementation, deferred external
  release evidence, and exact local/hosted validation results.

## Capabilities

### New Capabilities

- `ship-ready-ui-ux-hardening`: Cross-surface display-identity separation,
  Storage Analysis interaction/theme contracts, and reconciliation evidence for
  this campaign.

### Modified Capabilities

- `address-bar-and-history`: display-only breadcrumb normalization and exact
  canonical text when entering editable path mode.
- `tabs`: display-only tab-label normalization without changing tab locations.
- `quick-access-home`: recent/pinned display labels remain local, typed, and
  display-only.
- `storage-drilldown-and-treemap`: Storage toolbar keyboard, focus, state,
  degraded-mode, theme, and DPI behavior.
- `theming`: critical token contrast and immediate invalidation of Storage
  controls during theme transitions.
- `ui-automation`: truthful Storage control semantics and no regression of
  current dynamic provider replacement/stale-element behavior.

## Impact

The primary implementation areas are `src/ui/include/ffui/DisplayPath.h`,
`NavigationWorkspace`, navigation chrome and Home presentation, `StorageAnalysis`,
shared UI theme/state seams, and their plain C++ tests/CMake registrations. No
process boundary, IPC command, index identity, privileged-service behavior,
signing configuration, trust store, or production release gate changes. Live
UIA/visual evidence remains conditional on the interactive desktop; production
signing, elevated MFT/USN qualification, and clean-host release evidence remain
external prerequisites.
