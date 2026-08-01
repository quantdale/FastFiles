## 1. Preview Provider Extension Point (Foundation)

- [x] 1.1 Define the `IPreviewProvider` interface (`GetPriority(FileDescriptor) -> MatchResult`, `CreatePreview(FileDescriptor, CancellationToken) -> PreviewResult`) in `FastFiles`
- [x] 1.2 Define `FileDescriptor` as a lightweight, already-known-metadata struct (path, size, basic attributes) sourced from existing selection/snapshot state — no new engine query required to construct it
- [x] 1.3 Implement `PreviewProviderRegistry`: ordered provider list, extension-match resolution pass, content-sniff resolution pass, explicit registration API (e.g. `Register(std::unique_ptr<IPreviewProvider>)`)
- [x] 1.4 Implement fallthrough behavior: a provider that fails during `CreatePreview` is treated as non-applicable and resolution proceeds to the next-priority provider or the no-match fallback
- [x] 1.5 Implement the background thread-pool submission path for preview requests, with single-flight "most-recent-selection-wins" cancellation/discard of superseded results
- [x] 1.6 Implement the no-preview-available fallback UI state (used whenever no provider matches or all matching providers fail)
- [ ] 1.7 Unit tests: extension match takes priority over content sniff; content sniff runs only when no extension match exists; a throwing/failing provider falls through instead of propagating; a superseded request's result is discarded, not rendered

## 2. Image Preview Provider (WIC)

- [x] 2.1 Implement `ImagePreviewProvider` registered for common raster extensions (`.jpg`/`.jpeg`, `.png`, `.bmp`, `.gif`, `.tiff`)
- [x] 2.2 Implement WIC decode via `IWICImagingFactory::CreateDecoderFromFilename`, off the UI thread
- [x] 2.3 Convert the decoded frame to a UI-thread-created `ID2D1Bitmap` for rendering, performed only for the current (non-superseded) request
- [x] 2.4 Handle decode failure (corrupted/truncated file, mismatched extension/content) by reporting non-applicable rather than crashing or rendering a partial image
- [ ] 2.5 Manual/integration test: valid image of each supported format renders; a corrupted file with an image extension falls back gracefully instead of crashing

## 3. Text/Source-Code Preview Provider

- [x] 3.1 Implement `TextPreviewProvider` registered for plain-text and common source-code extensions
- [x] 3.2 Implement encoding sniffing (UTF-8 BOM, UTF-16 BOM, fallback to system code page)
- [x] 3.3 Implement the preview size/line ceiling and the "truncated" indicator shown when a file exceeds it
- [x] 3.4 Implement monospace rendering via DirectWrite (no syntax highlighting, per MVP scope)
- [ ] 3.5 Manual/integration test: small text file renders fully; an oversized file renders truncated with the indicator visible; a non-UTF-8/16 legacy-encoded file still renders via the code-page fallback rather than showing garbled/mojibake text uncontrolled

## 4. Preview Pane UI Integration

- [x] 4.1 Wire the preview pane to the existing selection-change notification (from `column-view-browsing`/`multi-selection-and-dragdrop`) so it requests a new preview only when exactly one item is selected
- [x] 4.2 Implement the "no live preview" state for zero-selection and multi-selection cases
- [x] 4.3 Confirm the preview pane never opens a file handle or reads content via `FastFilesEngine`/IPC — direct unprivileged disk I/O only, consistent with the established security boundary
- [ ] 4.4 End-to-end test: navigating rapidly across many items (e.g., holding an arrow key) never renders a stale preview and never visibly stalls input

## 5. Properties/Details Panel — Files

- [x] 5.1 Implement the in-app properties panel shell embedded in the `FastFiles` window (no call-out to Explorer's native Properties dialog)
- [x] 5.2 Implement single-file property gathering: name, extension, size, created/modified/accessed timestamps, full path, attributes (via `GetFileAttributesEx`/`WIN32_FIND_DATA`)
- [x] 5.3 Implement friendly file-type description via `SHGetFileInfo(SHGFI_TYPENAME)`, executed off the UI thread, with a generic "`<EXT>` File" fallback if it fails or is slow
- [ ] 5.4 Manual test: selecting a single file of a known type (e.g., `.txt`, `.jpg`) and an unknown/unregistered extension both show a complete, correctly labeled property set

## 6. Properties/Details Panel — Folders and Async Computation

- [x] 6.1 Implement single-folder property display: item count and total size fields in the panel
- [ ] 6.2 Implement the index-sourced path: query `filesystem-index-store` for a folder's item count/total size when the subtree is already fully indexed, and display immediately with no "Calculating…" state
- [ ] 6.3 Implement the asynchronous compute path: request `FastFilesEngine` to compute item count/total size for a not-fully-indexed subtree, display "Calculating…" immediately, and update the panel via the existing snapshot/notification channel when the result arrives
- [ ] 6.4 Implement stale-result handling: a computation result that arrives after the user has navigated/selected elsewhere is not applied to the (no longer current) panel
- [ ] 6.5 Implement the multi-selection aggregate view: total item count and total size across all selected files and folders, replacing single-item detail fields
- [ ] 6.6 Manual test: selecting an unindexed large folder shows "Calculating…" immediately, the UI remains responsive (navigation/selection still work) while it resolves, and the panel updates once the result is ready; selecting a different folder mid-computation does not later show the first folder's stale result

> Consolidation disposition: tasks 6.2-6.5 remain open because the shared folder-aggregate contract does not yet exist. Completion must add an index-store read API, an engine request/response or snapshot-notification payload for pending aggregates, and a request identity/generation used to reject stale results. The preview UI must consume that shared contract rather than introduce a parallel recursive filesystem walk.

## 7. Status Bar

- [x] 7.1 Implement the persistent status bar UI element (always visible during Column View browsing): selection count, total selection size, current path
- [x] 7.2 Wire status bar updates to the selection-change notification (count and size)
- [x] 7.3 Wire status bar updates to the navigation-change notification (current path), independent of selection changes
- [x] 7.4 Implement the zero-selection default display (count and size both zero, path still shown)
- [x] 7.5 Confirm the status bar performs no independent computation and triggers no new asynchronous folder-size requests of its own — it only reflects already-known sizes at redraw time
- [ ] 7.6 Manual test: selecting/deselecting items and navigating between folders each independently update the correct part of the status bar without requiring the other kind of change to occur first

## 8. Cross-Cutting Integration & Validation

- [x] 8.1 Verify all three capabilities (`file-preview`, `properties-and-details`, `status-bar`) consume selection state identically and consistently from `column-view-browsing`/`multi-selection-and-dragdrop`, with no divergent or duplicated selection-tracking logic
- [x] 8.2 Verify adding a hypothetical new preview provider (e.g., a stub for a future format) requires no changes outside `FastFiles`'s preview registry code — no touching `FastFilesEngine`, the index schema, or the IPC protocol
- [ ] 8.3 Fuzz/stress test: malformed image and text files (truncated, wrong-extension, adversarial headers) across both preview providers, confirming no crash or hang
- [ ] 8.4 Regression test: rapid combined selection + navigation changes (stress-driven) across preview, properties, and status bar simultaneously, confirming no stale data is ever displayed and the UI never blocks
