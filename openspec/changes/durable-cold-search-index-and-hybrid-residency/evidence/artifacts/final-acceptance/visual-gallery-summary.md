# FastFiles deterministic visual capture

- Application SHA-256: eb85fdae4f924afeddde2e5b510ffe2b0756ecfb1cef68dbbdaeb9ec09116678
- Commit: 5d7e98ae412cd893e47e7ebc9a9bdea4c609d0ac
- Tree: cec82cef9d62d842c6c50bf36a7c88a79dbac9ac
- Provenance: exact-committed-tree | worktree=clean | exactTreeBound=True
- Started (UTC): 2026-10-23T14:19:09.1709836Z
- Finished (UTC): 2026-10-23T14:20:42.8005241Z

| Scenario | Theme | Width | Pane | DPI | Status | Assertion | Image | Reason |
| --- | --- | ---: | --- | ---: | --- | --- | --- | --- |
| home-default | Light | 1280 | single | 96 | PASS | PASS | home-default.png |  |
| browse-column-view | Light | 1280 | single | 96 | PASS | PASS | browse-column-view.png |  |
| browse-keyboard-focus | Light | 1280 | single | 96 | PASS | PASS | browse-keyboard-focus.png |  |
| browse-empty-state | Light | 1280 | single | 96 | PASS | PASS | browse-empty-state.png |  |
| home-wide | Light | 1600 | single | 96 | PASS | PASS | home-wide.png |  |
| details-closed | Light | 1280 | single | 96 | PASS | PASS | details-closed.png |  |
| dual-pane | Light | 1280 | dual | 96 | PASS | PASS | dual-pane.png |  |
| home-from-command | Light | 1280 | single | 96 | PASS | PASS | home-from-command.png |  |
| fast-search-idle | Light | 1280 | single | 96 | PASS | PASS | fast-search-idle.png |  |
| search-filters-expanded | Light | 1280 | single | 96 | PASS | PASS | search-filters-expanded.png |  |
| search-no-results | Light | 1280 | single | 96 | PASS | PASS | search-no-results.png |  |
| storage-overview | Light | 1280 | single | 96 | PASS | PASS | storage-overview.png |  |
| settings-general | Light | 620 | dialog | 96 | PASS | PASS | settings-general.png |  |
| command-palette | Light | 1280 | single | 96 | PASS | PASS | command-palette.png |  |
| home-narrow | Light | 800 | single | 96 | PASS | PASS | home-narrow.png |  |
| home-dark | Dark | 1280 | single | 96 | PASS | PASS | home-dark.png |  |
| gallery-light | Light | 1280 | single | 96 | PASS | PASS | gallery-light.png |  |
| gallery-dark | Dark | 1280 | single |  | FAIL | FAIL |  | {"Pass":false,"Detail":"window title='FastFiles — Gallery (debug)' after F9 dark toggle; lightMean=234.8 darkMean=234.8"} |
| gallery-narrow | Light | 800 | single |  | FAIL | FAIL |  | {"Pass":false,"Detail":"window title='FastFiles — Gallery (debug)' at 800px; mean=32.4"} |
| gallery-wide | Light | 1600 | single |  | FAIL | FAIL |  | {"Pass":false,"Detail":"window title='FastFiles — Gallery (debug)' at 1600px; mean=28"} |
| catalog-coverage | Light | 1280 | single |  | FAIL | FAIL |  | missing=; duplicate=; unexpected= |
