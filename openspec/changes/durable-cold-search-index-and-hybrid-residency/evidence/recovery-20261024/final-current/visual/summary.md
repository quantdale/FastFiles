# FastFiles deterministic visual capture

- Application SHA-256: ee46d8702a52425b25f143444ee76b20c5799969fb5cc87ff1c26755f5df1f9a
- Commit: 269d1c36036cb84b40c3ef01baee3c2b6c990579
- Tree: 705a8de99de9b4c909944a7e254be2c63367333e
- Provenance: exact-committed-tree | worktree=clean | exactTreeBound=True
- Started (UTC): 2026-10-24T14:12:36.6012984Z
- Finished (UTC): 2026-10-24T14:14:07.2735872Z

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
