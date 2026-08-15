# E2E validation evidence — semantic-uia-core-surfaces

Run: 2026-08-15, live `build/debug/src/ui/FastFiles.exe` on the dev host,
driven by `verify/capabilities/windows-ui-automation-validation`
(`Invoke-UiAutomationValidationCapability`) with the extended uia-driver.

## Result

`OVERALL: PASS (12 passed, 13 skipped with recorded reasons; 0 failed)`.

## Scenarios that now PASS (provider-absence SKIPs eliminated)

- `launch-main-window` — the custom fragment root keeps the Window element
  (`AutomationId=MainWindow`) and the native chrome children (verified: the
  runtime auto-includes native child HWNDs; no duplicate/conflicting
  children).
- `keyboard-address-bar-navigation` — keyboard navigation still works with
  the provider attached.
- `settings-dialog-structure` / `settings-dialog-close` /
  `storage-analysis-open` / `search-panel-open` / `ui-responsive` /
  `rendering-geometry-click-precision` / `mouse-tab-operation` — native
  chrome and dialogs unaffected.
- `home-surface-structure` — Alt+Home shows `Pane.0.Home` with the section
  groups (Pinned, Recent folders with `Clear recent folders` Button +
  InvokePattern, Recent searches with `Clear recent searches`, Common
  locations, Quick actions) — the full Home tree is discoverable.
- `connection-badge-degraded` — degraded status text visible via UIA.
- `in-column-error-state` (early phase) — when the engine-less state lands,
  the in-column error wording is a discoverable Text element; on this host
  the Unavailable transition did not land within the poll window, so the
  scenario recorded SKIPPED(`engine-connect-unavailable`) with the
  structural provider surface covered by the headless
  `ffcolumnview_uia_tests` status-element tests.

## Scenarios recorded SKIPPED with an independent environmental blocker

`engine-connect-unavailable` — the host's engine cannot complete the
UI↔engine handshake (`engine-connect-snapshot-map-failed`, error 0xd:
the engine never publishes an initial snapshot generation because the
durable store is empty and the privileged service is unverifiable). This was
reproduced on the unmodified baseline (changes stashed, baseline rebuilt):
identical failure. It is unrelated to this change. Scenarios gated on it:

- `column-populated-and-selected-item-in-tree` (data-dependent item
  assertions; the provider structure is asserted unconditionally and
  passes).
- `selection-and-scroll-patterns` / `scroll-pattern-real-viewport` (pattern
  surfaces asserted unconditionally and pass; data-dependent selection/
  scroll assertions gated).
- `search-results-discoverable` / `search-deep-result-navigation` /
  `search-structured-filter-query` (result data gated; the `Search.Results`
  provider surface is asserted unconditionally).
- `drag-out-to-explorer` / `drag-in-from-explorer` / `drag-between-panes`
  (need engine-served items; the driver gained `Send-UiaMouseDrag` +
  modifier-aware `Send-UiaMouseInput` for the engine-connected case).
- `in-column-error-state` late phase (engine-connected hosts exercise the
  missing-path wording).

Remaining SKIPs from independent blockers: `treemap-drawn-on-d2d-surface`
(out of scope, documented follow-up), `privileged-scan-unavailable`,
`service-not-running`, `connected-state-observed` (non-degraded).

## Root-cause notes

- Search results: the `SysListView32` list is `LVS_OWNERDATA`; the UIA/MSAA
  bridge does not enumerate virtual owner-data children, so no item
  provider existed. This change subclasses the list and answers
  `WM_GETOBJECT` with a custom fragment root (`Search.Results`), keeping the
  control fully native for input/painting/virtualization. With data
  unavailable on this host, the native-bridge behavior with populated
  results could not be observed live; the custom provider is validated by
  the headless `ffsearch_uia_tests` (metadata, selection, activation,
  virtualization, stale safety, several-thousand-result enumeration).
- Column View/Home: the main window answered no `WM_GETOBJECT`; the custom
  fragment root now exposes the full semantic tree.

## Driver changes consumed by this run

`verify/uia-driver`: `ScrollItem` + `ItemContainer` patterns,
`Send-UiaMouseDrag`, `-Modifier` on `Send-UiaMouseInput`, and an ESC
dismissal after the ALT-tap in `Set-UiaForeground` (the ALT-tap activates a
menu-bearing window's menu bar, which swallowed the next chord; ESC closes
it). All driver headless tests pass.
