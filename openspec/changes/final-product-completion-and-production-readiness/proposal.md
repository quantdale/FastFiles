## Why

FastFiles already has a working engine, index, search, storage analysis, Column View, Home, and a tokenized UI-v3 layer, and hosted CI on `3fa9fd44d5cbee68cdf07a6522e182a2de0a564b` is green. It is still not a finished product: the shell reads as stacked custom Win32 surfaces, Fast Search and Storage Analysis are command-hidden rather than first-class workspaces, density is not a user setting, and prior appearance campaigns closed checkboxes without meeting a demanding visual bar. This change is the final locally controllable product-completion campaign: finish the experience architecture, close remaining local defects, and certify the resulting tree.

## What Changes

- Rebuild the primary shell composition so tabs, navigation, location, and Fast Search read as one Windows 11-style navigation system rather than independent widgets.
- Make the sidebar a product map: Home, Fast Search, Storage Analysis, pinned folders, known folders, and drives.
- Make Fast Search first-class: chrome search field, sidebar destination, Ctrl+F, Home action, and command palette, with a dedicated search workspace distinct from path navigation and current-folder filtering.
- Make Storage Analysis a real workspace reachable from the sidebar, Home, and commands, not a bolted-on dialog.
- Productize Compact/Comfortable density as a persisted appearance setting.
- Elevate Home, Column View, preview/details, settings, dialogs, empty/error/loading, and file-operation surfaces onto one design system.
- Close remaining locally reproducible P0/P1 defects discovered during the campaign; do not reopen privileged signing, clean-host MFT/USN, or host-foreground UIA as local product work.
- Run exact-SHA Debug/release-test/Analyze/ASAN/OpenSpec/release qualification and hosted CI on the final committed tree.
- Treat stale PR #6 as already-superseded evidence (`docs/pr6-stabilization-reconciliation.md`); do not merge it.

Non-goals:

- Rewriting the three-process privilege model, protocol, or index engine for visual reasons.
- Pixel-cloning Files.community.
- Claiming production-signed release readiness without signer/pins.
- Fabricating live UIA PASS from a host that cannot attach a foreground provider.

## Capabilities

### New Capabilities

- `product-shell`: primary workspaces (Browse, Home, Fast Search, Storage Analysis), sidebar product map, chrome tab/toolbar/omnibar composition, and workspace switching that preserves existing Home/filesystem identity and UIA contracts.

### Modified Capabilities

- `bookmarks-and-sidebar`: sidebar exposes Fast Search and Storage Analysis as persistent destinations above folder groups.
- `search-ui`: Fast Search is discoverable from chrome, sidebar, keyboard, Home, and command palette; search remains a workspace over `contentRect`, never a fake filesystem path.
- `storage-overview`: Storage Analysis is a first-class workspace reachable without knowing a hidden command.
- `quick-access-home`: Home quick actions include Storage Analysis; Home remains the “where next” surface.
- `settings-ui`: appearance includes Compact/Comfortable density, persisted and applied without restart of the window process beyond ApplyTheme-style fan-out.
- `ui-appearance`: shell hierarchy, density, and component states apply across chrome, sidebar, Home, browse, search, storage, details, settings, and dialogs.
- `keyboard-shortcuts`: Up is a visible chrome command; Fast Search keeps Ctrl+F; Storage Analysis gains a documented global shortcut.
- `theming`: density profile is part of appearance, High Contrast still suppresses token overlays.

## Impact

- **UI:** `WindowShell`, `NavigationChrome`, `NavigationSidebar`, `SearchPanel`, `StorageAnalysis`, `ColumnView` Home, `SettingsDialog`, `UITheme`/`UiMetrics`/`UiComponents`, `ShellLayout`.
- **Persistence:** additive `appearance.density` in `ffprotocol::Settings` (missing field defaults Compact; no schema bump required).
- **Commands:** existing `search.focus` / `storage.analyze` / `navigation.up` become visible product entry points; no protocol command added.
- **Tests:** new headless sidebar/chrome/density seams; existing Home action-count, shell-layout (chrome height remains 72 DIP), settings round-trip, shortcut, and UIA provider tests updated where contracts change.
- **Security / engine:** no change to process boundaries, pipe ACLs, Authenticode pins, closed command protocol, or snapshot search.
- **Release:** final SHA must pass the local certification ladder; production signing and privileged MFT remain external.
