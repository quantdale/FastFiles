## Why

Power users expect Explorer-equivalent right-click actions, fast keyboard-driven workflows, and a way to discover and run advanced functionality without hunting through menus.

## What Changes

- Implement context menus appropriate to what's selected (single file, single folder, or multi-selection): open, open with, copy, cut, rename, delete, copy path, copy relative path, properties, open containing folder, open terminal/PowerShell here.
- Implement a command palette exposing application commands (New Folder, Copy Path, Open Terminal Here, Search, Analyze Storage, Toggle Column View, Toggle Dual Pane, Refresh, Settings, and others), discoverable and executable without the mouse.
- Implement a comprehensive keyboard shortcut set covering back/forward/refresh/copy/cut/paste/delete/rename/select-all/search/open-path, with the search shortcut usable globally — the user should never need to navigate to a specific folder first to start a search.
- Design the context-menu implementation with a clear extension point for future Windows shell registration (default file manager registration, "Open with" integration, Explorer-replacement behavior) without requiring any of that now.

## Capabilities

### New Capabilities
- `context-menus-and-quick-actions`: Right-click menus appropriate to the selected object, plus copy-path, copy-relative-path, open-containing-folder, and open-terminal-here actions.
- `command-palette`: The command discovery-and-execution surface.
- `keyboard-shortcuts`: The full shortcut map, including the global search hotkey.

### Modified Capabilities
(none)

## Impact

- This change invokes existing capabilities (`file-operations-core`, `conflict-resolution`, navigation from `navigation-and-workspace`, search from `instant-search`, analysis from `storage-analysis`) as the actions it exposes — it adds no new file-manipulation or navigation logic of its own, only entry points into what already exists.
- Deep Windows shell integration (registering as a default file manager, system-wide "Open with" entries) is explicitly out of scope here; this change only leaves room for it.
