## Why

Column View is the primary navigation model, but not every user wants it exclusively, and power users need efficient multi-location workflows. Conventional navigation patterns (address bar, back/forward, tabs, dual-pane, bookmarks) need to coexist with Column View rather than forcing a single paradigm.

## What Changes

- Implement the address/path bar with a breadcrumb mode (clickable hierarchy elements) and an editable-text mode (paste or type a full path), direct path entry, and clear, non-crashing feedback on invalid paths.
- Implement back/forward navigation history and drive/volume selection.
- Implement tabs: each tab maintains independent navigation state; opening a new tab never destroys another tab's location; tabs support closing, switching, and reopening a closed tab.
- Implement dual-pane mode: two independent filesystem views shown side by side, to make cross-location copy/move easier — coexisting with Column View, not replacing it.
- Implement bookmarks/favorites (user-added, quick to reach) and automatic discovery of common Windows known folders (Desktop, Documents, Downloads, Pictures, Videos, etc.), surfaced in a collapsible sidebar that doesn't consume excessive screen space.

## Capabilities

### New Capabilities
- `address-bar-and-history`: Breadcrumb/editable path bar, back/forward history, direct path entry, drive selection, invalid-path handling.
- `tabs`: Per-tab independent navigation state, with open/close/switch/reopen-closed.
- `dual-pane-mode`: Two independent, simultaneous filesystem views.
- `bookmarks-and-sidebar`: User favorites, automatic known-folder discovery, and the collapsible sidebar housing both plus drives.

### Modified Capabilities
(none — this is additive navigation surface around `column-view-browsing`, not a change to its existing requirements)

## Impact

- Builds directly on `column-view-browsing` from `establish-architecture-foundation`.
- `dual-pane-mode` and `tabs` both imply multiple concurrent navigation states within one `FastFiles.exe` process — each such state is an independent client of `FastFilesEngine`'s snapshot mechanism, not a separate engine connection.
