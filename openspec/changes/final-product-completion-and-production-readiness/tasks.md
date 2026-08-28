## 1. Density and settings

- [x] 1.1 Add `DensityPreference` to `ffprotocol::Settings`, persist `appearance.density`, default Compact on missing field. — `Settings.h` / `Settings.cpp`
- [x] 1.2 Add density combo to Settings General page; save/load through the draft/OK path. — `SettingsDialog.cpp`
- [x] 1.3 Publish `gUiDensityProfile` from `WindowShell::ApplyTheme` / settings apply.
- [x] 1.4 Extend `TestSettingsUtf8RoundTripAndCorruptionRecovery` for density round-trip and missing-field default.

## 2. Sidebar product map

- [x] 2.1 Extend `NavigationTarget` with `isSearch` / `isStorage` and keep empty-path tools from hitting This PC / unavailable.
- [x] 2.2 Insert Fast Search and Storage Analysis rows after Home; glyphs + selected state when that workspace is visible. — `BuildSidebarDestinations`
- [x] 2.3 Collapsed rail: Search and Storage icons with tooltips.
- [x] 2.4 Headless test for sidebar destination order and tool-vs-unavailable classification. — `ffsidebar_destinations_tests`

## 3. Chrome shell

- [x] 3.1 Swap chrome rows: tabs on top, toolbar below; keep 72 DIP height.
- [x] 3.2 Add Up button wired to `navigation.up`; disable at Home/This PC.
- [x] 3.3 Add Fast Search chrome field that invokes `search.focus`; hide drive combo.
- [x] 3.4 Reparameterize breadcrumb paint/hit-test/address-edit to the toolbar row.
- [x] 3.5 Narrow-width: keep search reachable as icon-sized control.

## 4. Workspace switching

- [x] 4.1 WindowShell sidebar tool activations open Search/Storage and hide the other.
- [x] 4.2 Folder/Home navigation hides Search and Storage.
- [x] 4.3 Register Ctrl+Shift+S for `storage.analyze`; keep Ctrl+F for search.
- [x] 4.4 Update shortcut tests. — `ffcommand_tests`

## 5. Home

- [x] 5.1 Add Analyze Storage quick action; `kHomeActionCount` = 5.
- [x] 5.2 Update ColumnView labels/commands and Home layout/UIA tests.

## 6. Search and Storage presentation

- [ ] 6.1 Search empty/degraded/scope chrome uses shared tokens; scope remains distinct from path.
- [ ] 6.2 Storage toolbar/status consume the same component language as the shell.

## 7. Validation

- [ ] 7.1 Focused UI/protocol/command/home tests.
- [ ] 7.2 Debug configure/build + full CTest.
- [ ] 7.3 release-test configure/build + full CTest.
- [ ] 7.4 Analyze warning-clean.
- [ ] 7.5 Authoritative ASAN slice.
- [ ] 7.6 Strict OpenSpec + doc-drift + `git diff --check`.
- [ ] 7.7 Commit/push without force; inspect hosted CI for the exact SHA.
- [ ] 7.8 Record remaining external blockers (signing, privileged MFT, live UIA host) without converting them to PASS.
