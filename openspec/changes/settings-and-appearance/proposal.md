## Why

Users need visibility into and control over what's being indexed, and need to understand their index's health well enough to explain why a search result might be missing, rather than treating the index as an opaque black box. Appearance and accessibility are also table-stakes for a professional utility.

## What Changes

- Implement the settings UI covering: indexed volumes and include/exclude rules, search behavior, appearance, navigation preferences, default actions, keyboard shortcut customization, preview behavior, and storage-analysis behavior.
- Implement per-volume index health/status display: fully indexed, currently indexing, partially indexed, unavailable, or needs reconciliation — clear enough that a user can understand why a search result may be missing.
- Implement light and dark themes with correct per-monitor high-DPI scaling, and keep animation minimal and non-interfering with navigation or performance.
- Implement diagnostic logging (indexing errors, inaccessible directories, volume state, database problems) without exposing unnecessary private information.
- Implement controls to pause, resume, enable, or disable indexing, and to add newly detected volumes to indexing.

## Capabilities

### New Capabilities
- `settings-ui`: The full settings surface — indexing configuration, search/appearance/navigation preferences, shortcut customization, preview and storage-analysis behavior.
- `theming`: Light/dark themes and correct high-DPI/per-monitor scaling.
- `index-health-and-diagnostics`: Per-volume index status display and diagnostic logging.

### Modified Capabilities
(none — include/exclude indexing rules are a new configuration surface read by `filesystem-index-store` and `privileged-index-service`, not a change to their existing requirement text; wiring this through is tracked as a coordination point with `index-storage-and-scanning` at implementation time)

## Impact

- `settings-ui`'s indexing include/exclude configuration is consumed by `filesystem-index-store`/`privileged-index-service` (from `index-storage-and-scanning`) — implementers should coordinate the exact hook-in point when both are being built.
- `index-health-and-diagnostics` reads volume/index state already tracked by `filesystem-index-store` and the `index-engine` connection state machine; it does not introduce new state of its own beyond display and logging.
