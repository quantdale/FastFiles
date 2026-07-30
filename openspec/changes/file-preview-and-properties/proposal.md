## Why

Users need to understand a selected file or folder — what it is, how big it is, when it changed — without leaving their navigation context or opening a separate Explorer-style dialog.

## What Changes

- Implement a file preview pane, built modularly by file type so new formats can be added without touching the core filesystem engine: images first, then text/source-code files, with later formats (documents, media) added progressively.
- Implement an in-app properties/details view: for files — filename, extension, size, creation/modified/accessed dates, full path, attributes, file type; for folders — item count and total size where already known from the index, computed asynchronously (never blocking navigation) when not.
- Implement a persistent status bar showing the number of selected items, their total size, and the current path.

## Capabilities

### New Capabilities
- `file-preview`: Modular preview rendering keyed by file type, with a clear extension point for adding formats later.
- `properties-and-details`: The in-app properties/details view for files and folders, replacing the need for a separate Explorer Properties dialog.
- `status-bar`: Persistent display of selection count, selection size, and current path.

### Modified Capabilities
(none)

## Impact

- Reads metadata from `filesystem-index-store` where available (folder item counts/total sizes); falls back to asynchronous on-demand computation otherwise.
- Reads current selection state from `multi-selection-and-dragdrop` (from `file-operations`).
- Preview rendering is intentionally isolated from the core filesystem/index engine so new format support never requires touching it.
