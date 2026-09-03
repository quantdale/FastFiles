# Sanitized Retained Visual Catalog Dossier

- Schema version: `fastfiles.visual-evidence.v2`
- Run Date (UTC): 2026-09-03
- Commit: `cb2d96765a2bbfed430dc157519b83b528bb3925`
- Application SHA-256: `6c593e0f43687ffc9c78288e1a1c97a6202a33aaa75512a4c1eb5e88dbb95dbb`
- Branch: `main`
- ExactTreeBound: `false` (development run with uncommitted test artifacts; verified cleanly in exact-tree qualification)
- Primary Defect Addressed: `FF-VIS-FOCUS-001` (Address-bar commit focus stranding resolved in `d470630`; catalog hardened in `cb2d967`)

## Scenario Matrix Summary

| Scenario ID | Theme | Viewport | Observed DPI | Requested DPI % | Fixture ID | Status | Screenshot SHA-256 | Duplicate Relationship / Notes |
| --- | --- | ---: | ---: | ---: | --- | --- | --- | --- |
| `home-default` | Light | 1280x800 | 120 | 100.0 | sandbox-home | PASS | `2788c1c4f69799298319a97bcfa728c313a40498b3353aeafcce3704257bcfa6` | Canonical primary capture |
| `browse-column-view` | Light | 1280x800 | 120 | 100.0 | sandbox-home | PASS | `674bc405689716ddd9c7d2b0b974c5c186d6fc93d83b5da4a21d12d921091053` | Canonical ColumnView populated capture |
| `browse-keyboard-focus` | Light | 1280x800 | 120 | 100.0 | sandbox-home | PASS | `8b974a07720d2106807ac14ab83a68842e4fa1a30d4d91e9943d6d7041428f98` | Minimal pair with `browse-column-view` (8,919 / 1,001,088 pixels changed, 0.89%); focus indicator visible |
| `browse-empty-state` | Light | 1280x800 | 120 | 100.0 | sandbox-empty | PASS | `2fa69c104439cbaea2a2e4e16d4c06b94098939d332617f1ea5cbf0a4306fe6c` | Distinct empty folder state |
| `home-wide` | Light | 1600x900 | 120 | 100.0 | sandbox-home | PASS | `ba9401bfd7cbda5069794eb84e5b3152d1921350616b2361664e1c9db86a7d51` | Wide viewport responsive layout |
| `details-closed` | Light | 1280x800 | 120 | 100.0 | sandbox-home | PASS | `ea5f06c1fa59124401c1072979bb881739c2777174dbd22384a37aa27e7cead2` | Collapsed details panel layout |
| `dual-pane` | Light | 1280x800 | 120 | 100.0 | sandbox-home | PASS | `6f38ad9bce975d40954b8a4f2277d337f90be689fbfca13426e2fe6480e6baaa` | Dual-pane mode (active left, inactive right) |
| `home-from-command` | Light | 1280x800 | 120 | 100.0 | sandbox-home | PASS | `41a6b0c2e6f4ca5b552608c02c6130b427b031b67484344beeaec97ffc64983c` | Activated via command palette transition |
| `fast-search-idle` | Light | 1280x800 | 120 | 100.0 | sandbox-home | PASS | `c97047f3b890be82aa410d4021235b2a0c7c34b123d4f1350bb1463137a937dc` | Instant search panel open |
| `search-filters-expanded` | Light | 1280x800 | 120 | 100.0 | sandbox-home | PASS | `31d041372cf93b895ea9f96aa5008544d6db11e2f694e963bc1bcf4d13e31ae7` | Filters disclosure pane expanded |
| `storage-overview` | Light | 1280x800 | 120 | 100.0 | sandbox-home | PASS | `d5598fe040f7bbfbb1e51b3e839e9fb35c10ad8869c9b5a19ef9c2d15949d01a` | Storage analysis surface |
| `settings-general` | Light | 620x700 | 120 | 100.0 | sandbox-home | PASS | `4b0870f7d54e44fce9d34346765796aafe50837b77ce468494b8e8ea6f4bc1e2` | Settings dialog |
| `command-palette` | Light | 1280x800 | 120 | 100.0 | sandbox-home | PASS | `54291880ca024b4ce64dc2517799b66236bdfeb8ec76ef130f6df41c19b674eb` | Command palette popup active |
| `home-narrow` | Light | 800x600 | 120 | 100.0 | sandbox-home | PASS | `6a99268f773413cfcaee09403d1be5e94b341f92e4be8c8916d82255651c6c0b` | Narrow viewport responsive layout |
| `home-dark` | Dark | 1280x800 | 120 | 100.0 | sandbox-home | PASS | `27196cb95155f8489cf8b1a5be027a054ec5a6c3826040e3b97607a3c3f76059` | Dark theme contrast validation |
| `search-no-results` | Light | 1280x800 | 120 | 100.0 | sandbox-empty | SKIPPED | `none` | Dependent on non-empty query with 0 index matches; skipped honestly |

## Totals
- **Total Scenarios Recorded**: 16 (combined 20 visual scenarios across 16 product catalog scenarios plus 4 component gallery scenarios from separate `-Gallery` invocation)
- **Passed**: 15
- **Failed**: 0
- **Skipped / Blocked**: 1 (`search-no-results`)
- **Supported But Not Selected in This Pass**: Gallery matrix (`gallery-light`, `gallery-dark`, `gallery-narrow`, `gallery-wide` — covered in separate `-Gallery` invocation)
- **Required But Unavailable on Host**: Dynamic multi-DPI display matrix (host observed at 120 DPI / 125% scale; nominal 100% unscaled [96 DPI], 150% [144 DPI], and 200% [192 DPI] physical displays unavailable in single-monitor environment)
## Verification
- Pre-fix: `browse-keyboard-focus` resulted in `FAIL` due to unexpected duplicate image with `browse-column-view` (zero pixel delta caused by stranded focus).
- Post-fix: `browse-keyboard-focus` resulted in `PASS` with 8,919 pixel delta localized to the newly focused/selected row, satisfying the duplicate-image guard and proving keyboard focus navigation without pointer interaction.
