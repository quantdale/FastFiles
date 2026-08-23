# FastFiles deterministic visual capture

- Application SHA-256: baa7530dbbebae00f422e2ab5c9e73d9699fa8f801ff5d438d5c951b5594ae48
- Started (UTC): 2026-08-23T12:15:56.4117795Z
- Finished (UTC): 2026-08-23T12:20:16.7392300Z

| Scenario | Theme | Width | Pane | Status | Assertion | Image | Reason |
| --- | --- | ---: | --- | --- | --- | --- | --- |
| shell-first-launch | Light | 1280 | single | PASS | PASS | shell-first-launch.png |  |
| shell-home-light | Light | 1280 | single | PASS | PASS | shell-home-light.png |  |
| shell-home-dark | Dark | 1280 | single | PASS | PASS | shell-home-dark.png |  |
| shell-home-follow-system | Follow System | 1280 | single | PASS | PASS | shell-home-follow-system.png |  |
| shell-this-pc | Light | 1280 | single | PASS | PASS | shell-this-pc.png |  |
| shell-standard-folder | Light | 1280 | single | PASS | PASS | shell-standard-folder.png |  |
| shell-deep-path | Light | 1280 | single | PASS | PASS | shell-deep-path.png |  |
| shell-editable-address | Light | 1280 | single | PASS | PASS | shell-editable-address.png |  |
| shell-command-palette | Light | 1280 | single | PASS | PASS | shell-command-palette.png |  |
| shell-settings | Light | 1280 | single | PASS | PASS | shell-settings.png |  |
| shell-multiple-tabs | Light | 1280 | single | PASS | PASS | shell-multiple-tabs.png |  |
| shell-many-tabs-overflow | Light | 720 | single | PASS | PASS | shell-many-tabs-overflow.png |  |
| shell-normal-window | Light | 1280 | single | PASS | PASS | shell-normal-window.png |  |
| shell-narrow-window | Light | 720 | single | PASS | PASS | shell-narrow-window.png |  |
| shell-wide-window | Light | 1600 | single | PASS | PASS | shell-wide-window.png |  |
| shell-dual-pane-left | Light | 1280 | dual-left | PASS | PASS | shell-dual-pane-left.png |  |
| shell-dual-pane-right | Light | 1280 | dual-right | BLOCKED_ENVIRONMENT | FAIL |  | right-pane provider has no clickable semantic point |
| browse-empty-directory | Light | 1280 | single | PASS | PASS | browse-empty-directory.png |  |
| browse-unicode-emoji | Light | 1280 | single | PASS | PASS | browse-unicode-emoji.png |  |
| browse-long-names | Light | 1280 | single | PASS | PASS | browse-long-names.png |  |
| browse-missing-path | Light | 1280 | single | SKIPPED | FAIL |  | the current degraded watcher did not publish an unavailable status after the disposable folder was removed |
| search-empty-query | Light | 1280 | single | PASS | PASS | search-empty-query.png |  |
| search-query-focused | Light | 1280 | single | PASS | PASS | search-query-focused.png |  |
| search-filters-expanded | Light | 1280 | single | PASS | PASS | search-filters-expanded.png |  |
| search-no-results | Light | 1280 | single | SKIPPED | FAIL |  | no-results status was not observable while the engine/degraded snapshot was unavailable |
| storage-analysis-base | Light | 1280 | single | PASS | PASS | storage-analysis-base.png |  |
| storage-analysis-dark | Dark | 1280 | single | PASS | PASS | storage-analysis-dark.png |  |
| storage-treemap | Light | 1280 | single | PASS | PASS | storage-treemap.png |  |
| browse-access-denied | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | safe access-denied reproduction would require changing ACLs |
| browse-hover | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | current fixture provider did not expose a safe semantic hover transition |
| browse-selected-focused | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | requires engine-served list rows |
| browse-selected-unfocused | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | requires engine-served list rows |
| browse-multi-selection | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | requires engine-served list rows |
| browse-rtl-content | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | requires engine-served list rows |
| browse-drag-target | Light | 1280 | dual | BLOCKED_ENVIRONMENT | FAIL |  | drag target needs a populated engine-backed pane |
| browse-inline-rename | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | safe inline mutation requires a populated engine-backed row |
| search-populated-results | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | requires engine-served snapshot results |
| search-selected-result | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | requires engine-served snapshot results |
| search-searching | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | minimum observable search timing is not deterministic on this host |
| preview-image | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | requires engine-served selected file and preview fixture |
| preview-text-metadata | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | requires engine-served selected file |
| preview-unsupported | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | requires engine-served selected file |
| file-operation-queued | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | file operation requires engine-backed selection |
| file-operation-conflict | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | conflict fixture is not reproducible without a populated engine-backed selection |
| file-operation-confirmation | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | destructive operation is intentionally not run in the real workspace |
| settings-long-page | Light | 720 | single | PASS | PASS | settings-long-page.png |  |
| settings-reset-section | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | reset action is never invoked against the user configuration |
| dpi-125-live | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | host exposes only 100 percent DPI; headless geometry tests cover 125 percent |
| dpi-150-live | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | host exposes only 100 percent DPI; headless geometry tests cover 150 percent |
| dpi-200-live | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | host exposes only 100 percent DPI; headless geometry tests cover 200 percent |
| high-contrast-live | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | High Contrast is not changed by this campaign because safe restoration cannot be proven |
| animations-off-live | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | host SPI animation settings are not mutated; headless animation gates cover disabled mode |
| mixed-dpi-transition | Light | 1280 | single | BLOCKED_ENVIRONMENT | FAIL |  | single-monitor host cannot provide a live mixed-DPI transition |
