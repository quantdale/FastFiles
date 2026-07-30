## Why

Understanding what's consuming disk space should feel like part of the same filesystem experience, not a separate utility — reusing the same index rather than re-traversing the filesystem whenever the user opens the storage view.

**Sequencing dependency:** depends on `filesystem-index-store` (from `index-storage-and-scanning`) for size/hierarchy data, and on `column-view-browsing`/`navigation-and-workspace` for "select a region, navigate there" integration.

## What Changes

- Implement the storage overview: available volumes with total capacity, used capacity, free capacity, and usage percentage.
- Implement folder/file size aggregation sourced from the index (never recomputed if already known; computed asynchronously without blocking navigation if not yet known), largest-files and largest-folders views, and hierarchical drill-down showing each item's size and percentage of its parent and of the whole volume at every level.
- Implement an interactive treemap visualization: proportionally sized regions for files/directories, hover metadata (path, size, percentage of parent, percentage of volume), click-to-navigate into the normal file browser at that location.
- Implement configurable file-type/extension categorization (video, image, document, archive, executable, development files, virtual machine images, games, etc.) for breakdown-by-category views — configurable, not exclusively hardcoded.
- Implement sorting by size, filename, type, and other relevant metadata within storage views.
- Storage analysis never performs destructive actions on its own; deleting or moving anything found here is always an explicit, separate action through `file-operations-core`.

## Capabilities

### New Capabilities
- `storage-overview`: Volume-level capacity display (total/used/free/percentage) and drive selection for analysis.
- `storage-drilldown-and-treemap`: Hierarchical size drill-down (largest files/folders, percentage contribution at every level) plus the interactive treemap visualization.
- `file-type-categorization`: Configurable extension/category classification used by breakdown-by-type views.

### Modified Capabilities
(none)

## Impact

- Depends on `filesystem-index-store` for size data; falls back to on-demand, asynchronous computation where the index doesn't yet have a size (e.g., mid-scan).
- Depends on `column-view-browsing`/`navigation-and-workspace` as the navigation target when a user clicks a treemap region or drill-down row.
- Does not perform file operations itself — see `file-operations` for copy/move/delete.
