# Visual Catalog Mapping

## Canonical 20-record interpretation

The later historical `20/20` result is not twenty default product scenarios. It is the 16-scenario core catalog plus four conditional gallery variants selected with `-Gallery`.

| Current core ID | Historical mapping | Disposition |
|---|---|---|
| `home-default` | `home-default` / `shell-home-light` | Retained; current runner adds the real Home command assertion. |
| `browse-column-view` | `browse-column-view` / `shell-standard-folder` | Retained; Unicode, RTL, accented, emoji, and long-name stimuli are in the fixture. |
| `browse-keyboard-focus` | `browse-keyboard-focus` | Retained. |
| `browse-empty-state` | `browse-empty-state` / `browse-empty-directory` | Retained. |
| `home-wide` | `home-wide` / `shell-wide-window` | Retained. |
| `details-closed` | `details-closed` | Retained. |
| `dual-pane` | `dual-pane` / `shell-dual-pane-left` | Retained with direct `Pane.0`/`Pane.1` assertions. |
| `home-from-command` | `home-from-command` | Retained. |
| `fast-search-idle` | `fast-search-idle` | Retained. |
| `search-filters-expanded` | `search-filters-expanded` | Retained. |
| `search-no-results` | `search-no-results` | Retained; live-provider query polling prevents stale UIA reads. |
| `storage-overview` | `storage-overview` | Retained. |
| `settings-general` | `settings-general` / `shell-settings` | Retained. |
| `command-palette` | `command-palette` / `shell-command-palette` | Retained. |
| `home-narrow` | `home-narrow` / `shell-narrow-window` | Retained. |
| `home-dark` | `home-dark` / `shell-home-dark` | Retained. |
| `gallery-light` | `gallery-light` | Conditional gallery variant. |
| `gallery-dark` | `gallery-dark` | Conditional gallery variant. |
| `gallery-narrow` | `gallery-narrow` | Conditional gallery variant. |
| `gallery-wide` | `gallery-wide` | Conditional gallery variant. |

## Older shell-only states

The earlier shell-heavy catalog also contained states that are not current default scenarios: `shell-home-follow-system`, `shell-this-pc`, `shell-deep-path`, `shell-editable-address`, `shell-multiple-tabs`, and `shell-many-tabs-overflow`. They are explicitly retired from the default catalog because the current product normalizes startup to Home, uses the current semantic address/navigation surfaces, and covers tab/geometry behavior in focused navigation/UI tests. They are not counted as current core scenarios and are not represented as equivalent visual captures without a separate product requirement.

## Evidence rule

A run is complete only when the selected expected set has exactly one coherent PASS record per scenario, with semantic assertion and capture evidence. Missing, duplicate, skipped, or failed expected records do not certify a catalog. Gallery records are included only when `-Gallery` is selected.
