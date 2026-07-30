## Context

`establish-architecture-foundation` fixed the process architecture (`FastFilesIndexSvc` / `FastFilesEngine` / `FastFiles`) and the Column View browsing surface; `index-storage-and-scanning` (proposed, capability `filesystem-index-store`) is where folder item-count/total-size data actually gets computed and persisted; `file-operations` (proposed, capability `multi-selection-and-dragdrop`) is where the Ctrl-click/Shift-click/Ctrl+A selection model and drag-and-drop live. This change does not redesign any of those — it consumes their outputs. Specifically:

- "What's selected right now" is state owned by `column-view-browsing` (single active item per column) and `multi-selection-and-dragdrop` (extended multi-select set). This change's three capabilities — `file-preview`, `properties-and-details`, `status-bar` — are all **read-only observers** of that state, not new selection models.
- "Does the engine already know this folder's size/item count" is answered by `filesystem-index-store` when the subtree is fully indexed; when it isn't, this change needs an asynchronous compute-and-deliver path that fits the same "never block the UI thread on I/O" discipline the foundation change already established for directory enumeration (D5 of that design: degraded-mode enumeration; D3: snapshot-generation notifications instead of blocking round-trips).
- The foundation change's security model (D4) established a hard rule: `FastFilesIndexSvc` never forwards file *content* ($DATA), only metadata ($STANDARD_INFORMATION/$FILE_NAME). Preview rendering needs actual file bytes (image pixels, text content), so it structurally cannot be served from the index/privileged pipe — it has to read the file directly, unprivileged, from `FastFiles` itself.

This change is scoped as three independent, additive capabilities with no modified specs. It is UI-surface work layered on top of already-decided infrastructure, not a new architectural layer.

## Goals / Non-Goals

**Goals:**
- Define a preview provider extension point (registry + interface) such that adding a new previewable format later is "write a provider, register it" — zero changes to `FastFilesEngine`, the index schema, or the IPC protocol.
- Ship two MVP providers on that extension point: images (via WIC) and plain text/source-code files (monospace, no highlighting).
- Define a graceful, non-crashing fallback for every file type without a registered/matching provider.
- Define an in-app properties/details panel for files and folders that never hands off to Explorer's native Properties dialog, so navigation context is never lost.
- Define how folder metadata (item count, total size) is sourced from the index when known and computed asynchronously, with a visible "Calculating…" state, when not — without blocking navigation or input.
- Define a minimal, read-only status bar that reflects existing selection/navigation state and owns none of it.

**Non-Goals (this change):**
- Syntax highlighting for source-code preview (explicitly deferred; plain monospace text rendering is sufficient for MVP).
- Any provider beyond images and text/source-code — document (PDF/Office) and media (audio/video) providers are noted as future work on the *same* extension point, not designed in detail here.
- Thumbnail/preview on-disk caching. Addressed only as a stated forward-compatible intent (see D6) so the provider interface doesn't need to change shape when a later change adds it.
- Editing any property (rename, attribute toggling) from the properties panel — that is `file-operations` territory; this panel is read-only display.
- Arbitrary shell property-handler integration (`IPropertyStore`, custom Explorer property sheet pages) — only the fixed field set named in the proposal is in scope.
- Windows Explorer thumbnail cache integration — a separate, unrelated caching system from the preview pane described here.
- Redesigning `column-view-browsing`, `multi-selection-and-dragdrop`, or `filesystem-index-store` — this change only defines how it *reads* from them.

## Decisions

### D1: Preview provider extension point — a registry owned by `FastFiles`, not `FastFilesEngine`

`IPreviewProvider` is a small interface with roughly:
- `GetPriority(const FileDescriptor&) -> MatchResult` (`NoMatch` / `ExtensionMatch` / `ContentSniffMatch`, plus a numeric tiebreak) — cheap, metadata-only, called for every candidate provider on every selection change.
- `CreatePreview(const FileDescriptor&, CancellationToken) -> PreviewResult` — does the actual I/O/decode; only called for the single winning provider.

A `PreviewProviderRegistry` (a simple ordered list, populated at process startup by explicit registration calls, e.g. `registry.Register(std::make_unique<ImagePreviewProvider>())`) resolves the winning provider in a fixed priority order: **(1) exact/registered file-extension match, in registration order; (2) content-sniff probes, in registration order, for extensionless or ambiguous files; (3) no match → graceful fallback.** Providers never talk to `FastFilesEngine`, the index, or the IPC protocol — they take a `FileDescriptor` (path + already-known basic metadata, sourced from the existing selection/snapshot state) and do their own unprivileged file I/O.

**Why the registry lives in `FastFiles` (UI process), not the engine:** preview rendering is fundamentally a UI-process concern (Direct2D bitmap creation, DirectWrite text layout both require the UI process's rendering device), and — more importantly — putting it in the engine would put file *content* reads inside the same process that talks to the privileged service, which is exactly the boundary the foundation change's security model (D4: "never forward file content") was designed to keep clean. Keeping preview entirely UI-side means adding a provider never touches `FastFilesEngine`, the wire protocol, or the index schema, which is the explicit goal from the proposal.

**Alternatives considered:**
- *Engine-hosted preview generation, streamed to the UI.* Rejected: forces file content through a process boundary that was deliberately designed to carry metadata only, and gains nothing — the engine has no rendering device to hand back anything more useful than raw bytes anyway.
- *Reflection/plugin-DLL-based provider loading* (drop a DLL in a folder, auto-discovered). Rejected for MVP: real flexibility win for a mature product, but adds a code-signing/loading-hardening problem (this app already hand-rolls DLL-search-order hardening per the foundation design) for a benefit — third-party preview plugins — nobody has asked for yet. Static compile-time registration achieves the actual stated goal ("adding a format is a new provider, not a core-engine change") without that cost. Revisit if/when third-party extensibility becomes a real requirement.

### D2: Preview content is read directly from disk by `FastFiles`, never via the index or privileged pipe

Every provider opens the file itself with standard unprivileged Win32 file APIs (`CreateFile`/`IWICImagingFactory::CreateDecoderFromFilename` etc.), using the path resolved from the current selection. No preview data ever flows through `FastFilesEngine` or `FastFilesIndexSvc`.

**Why:** this is a direct, deliberate continuation of the foundation change's D4 security decision, not a new judgment call — content was kept out of the privileged path specifically so a compromised or malformed volume can't leak file bytes through a machine-wide-visible index. Extending "content stays out of the trusted path" to previews too is the only choice consistent with that existing decision.

### D3: Image preview via WIC (Windows Imaging Component)

Chosen over bundling a third-party decode library (`stb_image`, `libpng`+`libjpeg-turbo`, etc.).

**Why:** it's already a natural fit for the fixed stack (native C++, COM-heavy) chosen in the foundation design; it ships and is security-patched as part of Windows itself rather than being a dependency this app must vendor and patch; and it already covers every common raster format (JPEG, PNG, BMP, GIF, TIFF, and HEIC/WebP where the relevant OS codec is present) plus built-in format sniffing from file headers, which D1's content-sniff fallback path can reuse directly instead of hand-rolling magic-byte detection.

**Alternative considered:** a vendored decode library would remove a dependency on OS-installed codecs (e.g., HEIC codec availability varies), but that's a minor coverage gap against a meaningfully larger maintenance/security surface for an MVP — not worth it now. Revisit only if a specific missing codec becomes a real user complaint.

### D4: Text/source-code preview — plain read, no highlighting

The text provider reads the file as bytes, sniffs encoding (UTF-8/UTF-16 BOM detection, falling back to the system ANSI code page), and renders it monospace via DirectWrite. Syntax highlighting is explicitly out of scope for MVP per the proposal. To avoid a single huge log/data file stalling the preview pane or exhausting memory, the provider caps how much it reads/renders (a fixed byte/line ceiling) and shows a visible "preview truncated" indicator rather than silently showing an incomplete file with no explanation.

**Why capped, not full-file:** a preview pane's job is a quick look, not a full editor; capping bounds worst-case latency and memory use for the one file type most likely to be arbitrarily large (multi-gigabyte logs), keeping preview generation fast enough to feel instant even on rapid selection changes.

### D5: Preview generation runs off the UI thread with single-flight cancellation

Each preview request (triggered by a selection change) is submitted as a work item to a small background thread pool. Only the most recently requested item's job is allowed to complete and update the UI — if the user moves the selection again (e.g., holding an arrow key through a column) before a job finishes, that job's result is discarded on delivery (or its `CancellationToken` is signalled if the provider checks it during long operations) rather than racing to render a stale preview. The final hand-off to the UI thread (creating the `ID2D1Bitmap`, submitting the `IDWriteTextLayout`) happens only for the winning, still-current request.

**Why:** matches the same "index/engine can be slow, UI must never wait on it" discipline already established for folder enumeration and metadata (see D8) — a decode taking even a few hundred milliseconds must not make column-to-column keyboard navigation feel laggy.

### D6: Thumbnail/preview caching is a Non-Goal now, but the interface is shaped for it

MVP always decodes/reads on demand, in-memory only, discarded once the item is no longer selected. No on-disk cache is built in this change. The stated forward-compatible intent for a later change: a lazy-generated, on-disk cache keyed by file identity (the same immutable `FileReferenceNumber` the foundation design already uses as the canonical file key, not a re-derived path) plus last-modified timestamp, invalidated whenever either changes.

**Why decide this now even though it's a non-goal:** `CreatePreview` already returns an opaque `PreviewResult` and takes only a `FileDescriptor` + `CancellationToken` — a future caching layer can wrap the registry (check cache before calling the real provider, populate it after) with zero change to the `IPreviewProvider` contract or to individual providers. Calling this out now is what prevents a later change from having to redesign the extension point.

### D7: Properties/details panel — in-app, embedded, read-only

The properties/details view is a panel embedded in `FastFiles`'s own window (not a modal dialog, not a call-out to Explorer's `SHObjectProperties`/native Properties sheet). It has two content modes:

- **Single file selected:** filename, extension, size (bytes + human-readable), created/modified/accessed timestamps, full path, attributes (read-only/hidden/system/etc., via the already-available `WIN32_FIND_DATA`/`GetFileAttributesEx` fields), and a friendly file-type description (via `SHGetFileInfo(SHGFI_TYPENAME)`, a standard shell call that reads the same registered-association data Explorer itself uses — no new dependency).
- **Single folder selected:** item count and total size — sourced from `filesystem-index-store` when that subtree is already fully indexed; otherwise requested asynchronously per D8, with a visible "Calculating…" state until the result arrives.
- **Multiple items selected (of either kind):** an aggregate view — total count and total size across the selection — rather than attempting to show single-item detail fields that don't generalize (there's no single "created date" for 12 mixed items).

**Why in-app rather than Explorer's dialog:** stated directly in the proposal's Why — the point of this whole change is that the user never has to leave the app's navigation context to understand what they've selected. A callout to `SHObjectProperties` would open a separate OS-owned window, defeating that goal outright.

**Alternative considered:** a floating/undockable properties window (still in-process, still not Explorer's). Rejected for MVP as unnecessary chrome complexity — an embedded panel satisfies "don't lose navigation context" with less implementation surface; a detachable variant is a plausible future enhancement, not a blocker now.

### D8: Asynchronous folder metadata computation reuses the existing notification pattern, doesn't invent a new one

When the properties panel (or status bar) needs a folder's item count/total size and `filesystem-index-store` doesn't already have it fully known, `FastFilesEngine` is asked to compute it in the background (walking its own index data plus, for not-yet-indexed subtrees, an unprivileged directory walk — the same degraded-mode enumeration mechanism the foundation change already implements). The UI shows "Calculating…" immediately and non-blockingly. The result is delivered back asynchronously through the same generation/notification channel already used for snapshot updates (D3 of the foundation design), rather than a new bespoke async mechanism — the UI was already built to react to "something changed, go re-read the relevant data" events, so folder-aggregate results reuse that shape instead of adding a second one.

If the selection has moved on by the time a requested computation completes, the result is simply not applied to the (no-longer-relevant) panel — the computation itself is allowed to finish (its result may still be useful if the user reselects the same folder shortly after) rather than being forcibly torn down, since folder aggregation, unlike preview decode, is typically a one-shot background walk rather than a resource continuously re-triggered on every keystroke.

**Why not block:** identical reasoning to why directory enumeration itself is asynchronous in the foundation design — a folder-size computation over a large or slow (e.g., network-mounted) tree could take seconds to minutes; blocking any UI thread on that is a hard product-quality regression the rest of the app has already committed to avoiding.

### D9: Status bar is a thin, read-only projection — it triggers on two events only

The status bar subscribes to exactly two kinds of change: **selection change** (from `column-view-browsing`/`multi-selection-and-dragdrop`) and **navigation change** (current path). On either, it recomputes and redraws: selection count, total size of the current selection, and the current path. It holds no independent state of its own and performs no computation beyond summing already-known sizes over the current selection (it does not itself trigger new async folder-size computations — that's the properties panel's job per D8; if a selected folder's size isn't yet known, the status bar's "total size of selection" simply reflects only the items it already has a size for, and is expected to update again if/when the properties panel's async request resolves and the selection is unchanged).

**Why kept this simple:** the proposal is explicit that this should be a thin read-only display; giving it its own async-fetch responsibility would duplicate D8's logic for no benefit — the properties panel already owns "go find out this folder's size," and the status bar just reflects whatever is already known at redraw time.

## Risks / Trade-offs

- **[Risk] Malformed or adversarially crafted image/text files could hang or crash the decode path** (WIC codecs, or a pathological text file with degenerate encoding). → **Mitigation:** decoding happens off the UI thread (D5), so a hang there doesn't freeze input; text preview enforces a hard byte/line cap (D4) before attempting to render; a provider that throws/fails is treated as "no match" and falls through to the next-priority provider or the graceful fallback, never a visible crash.
- **[Risk] Extension-based provider selection can be wrong** (a renamed image with a `.txt` extension, or a corrupt file with a valid-looking extension). → **Mitigation:** WIC's own decode attempt fails cleanly on a real format mismatch, which the registry treats as "this provider doesn't apply" and falls through per D1's priority order, rather than rendering garbage.
- **[Risk] Rapid selection changes (holding an arrow key across many columns/items) could queue many overlapping decode jobs and waste CPU.** → **Mitigation:** D5's single-flight/most-recent-wins cancellation model.
- **[Risk] Folder-size computation over very large or slow (network) trees could run for a long time, and the user may navigate away before it resolves.** → **Mitigation:** D8's async, non-blocking model with a visible "Calculating…" state; stale results are simply not applied to a no-longer-relevant panel rather than being forced to complete synchronously or corrupting a different folder's displayed properties.
- **[Risk] `SHGetFileInfo(SHGFI_TYPENAME)` can be slow on first call for a given extension (populates/reads from the shell's association cache) and can behave unexpectedly for paths on slow/disconnected network or removable volumes.** → **Mitigation:** call it off the UI thread alongside the rest of the properties gather step, not synchronously on selection change; if it's slow or fails, fall back to a generic "`<EXT>` File" description rather than blocking the panel.
- **[Risk] No preview/thumbnail caching in MVP means repeatedly reselecting the same large image or text file re-decodes/re-reads every time.** → **Mitigation:** accepted for MVP scope (small images/text decode fast enough that this is imperceptible in practice); D6 documents the exact forward-compatible seam so a later change can add caching purely as a wrapper with no interface churn.
- **[Risk] Showing per-item detail fields (created/modified/accessed, attributes) doesn't generalize to a multi-item selection.** → **Mitigation:** D7's explicit aggregate mode for multi-selection avoids showing misleading or arbitrarily-chosen single-item data when several items are selected.

## Migration Plan

Greenfield addition — no existing preview, properties, or status bar functionality exists yet in `FastFiles` to migrate away from, and no persistent data model (schema, on-disk format) is introduced by this change (the on-disk preview cache is explicitly a Non-Goal, see D6). Deployment is simply: these three capabilities ship as new UI surfaces inside `FastFiles.exe` alongside whatever `column-view-browsing` release train they land in. No rollback concerns beyond normal build rollback, since no durable state is written.

## Open Questions

- **Exact size/line caps for text preview** (D4) — needs empirical tuning once real hardware/file samples are available; not fixed by this design.
- **Whether `SHGetFileInfo(SHGFI_TYPENAME)` is the final answer for "file type" or whether a simpler internal extension→description table is preferable** for consistency/performance reasons — left to implementation; D7 picks the shell API as the reasonable default.
- **Whether a future multi-select preview should show the most-recently-focused item's preview instead of no preview at all** — MVP deliberately picks "no live preview, aggregate properties only" (D7) for simplicity; open to revisit once real usage patterns are observed.
- **Exact on-disk cache eviction policy (size budget, LRU vs. TTL)** for the future caching work flagged in D6 — intentionally undecided here since it's out of scope for this change.
- **Whether the properties panel is always visible (persistent) or user-toggleable** — this design fixes the data/behavior contract (what's shown, when it updates) but leaves exact panel chrome/visibility/layout to implementation alongside `navigation-and-workspace`.
