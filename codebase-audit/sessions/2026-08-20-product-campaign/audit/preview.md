# Audit area: PREVIEW &amp; DETAILS PANE — audit src/ui/src/Preview.{h,cpp}, src/ui/include/ffui/DetailsLayout.h and DetailsAggregateState.h, and their integration points in WindowShell/ColumnView selection flow. Cover: asynchronous preview staleness (can an old preview attach to a newer selection?), main-thread blocking on large files/images, fallbacks for unsupported/corrupt/inaccessible/deleted files, metadata correctness (logical size, allocated size, timestamps, attributes, path display/truncation), rapid selection-change churn (leaks/cancellation), bitmap/stream resource cleanup, DPI scaling of preview content.

(agent agent-4, outcome completed)

## Defects

### [P1] Full preview pixel buffer copied on every render frame — WindowShell.cpp:1482
- Symptom (user-visible): Selecting a large image makes the whole window sluggish/fan spins; UI thread burns tens of ms per frame and up to 64 MB of transient allocations per repaint.
- Evidence: `WindowShell.cpp:1481-1482`: `PreviewResult preview; { std::lock_guard<std::mutex> lock(previewMutex_); preview = preview_; }` — `PreviewResult` contains `std::vector<uint8_t> pixels` capped at 64 MB (`Preview.cpp:72`), copied by value under the mutex on **every** `RenderDetails` call.
- Failure scenario: Preview any photo (~6000×4000 ≈ 91 MB decoded > 64 MB cap rejected; a 3000×2000 ≈ 23 MB buffer passes). Every animation tick / mouse move that triggers repaint deep-copies 23 MB while holding `previewMutex_`, also stalling the worker completion callback.
- Root cause hypothesis: convenience copy instead of shared ownership (`shared_ptr<const PreviewResult>` published by the completion lambda) or a `const` reference swap.
- Suggested fix: publish `std::shared_ptr<const PreviewResult>` from the completion callback; `RenderDetails` only takes the `shared_ptr` under the lock (no deep copy).
- Regression test idea: not unit-testable headlessly; add an assert/instrumentation test that `RenderDetails` performs no O(pixels) allocation (or refactor so `PreviewResult` is non-copyable, forcing the fix).

### [P1] Synchronous `GetFileAttributesExW` on the UI thread every paint frame — WindowShell.cpp:1404
- Symptom (user-visible): With the details pane open and a file selected on a network share / disconnected drive, the UI freezes for seconds per frame; for a just-deleted file the pane shows `Created: 1601-01-01 00:00`.
- Evidence: `WindowShell.cpp:1403-1404`: `WIN32_FILE_ATTRIBUTE_DATA data{}; GetFileAttributesExW(selection->path.c_str(), ...);` — return value ignored, executed inside `RenderDetails` (per repaint), a blocking filesystem call. On failure `data` stays zeroed and `FormatTime({0,0})` renders `1601-01-01 00:00` (`WindowShell.cpp:92-98` returns "Unavailable" only if the conversion APIs fail, not for a zero FILETIME).
- Failure scenario: Select a file on `\\server\share` where the server is unreachable (SMB timeout ~10–20 s). Every repaint re-issues the call → app appears hung.
- Root cause hypothesis: metadata refresh was put in the paint path instead of being fetched once per selection change (async like `RequestFileType`).
- Suggested fix: fetch attributes once in `RefreshSelectionPresentation` (ideally off-thread, posted back like `WM_APP_REPAINT`), cache in a member; on failure show "Unavailable" instead of rendering epoch-zero times.
- Regression test idea: headless seam for the attribute fetch asserting single fetch per selection change and zero-FILETIME → "Unavailable".

### [P2] Image preview bitmap not invalidated on D2D device loss — WindowShell.cpp:1134-1162 / 1489
- Symptom (user-visible): After a GPU reset/driver update (TDR) or theme/device recreation, an image that was previewing turns into a permanently blank area until the user changes selection.
- Evidence: `MarkDeviceDirty()` resets brushes/formats/cross-fade/icon cache but never `previewBitmap_`; `RenderDetails` only creates the bitmap `if (!previewBitmap_ && !preview.pixels.empty())` (`WindowShell.cpp:1489`). A bitmap created on the old device cannot be drawn by the recreated device context (`DrawBitmap` fails with a wrong-resource-domain error, silently ignored).
- Failure scenario: select image → preview shown → display driver TDRs → `Renderer` recreates target → `ConsumeDeviceRecreated` → `MarkDeviceDirty` → next frame draws everything except the preview image.
- Root cause hypothesis: `previewBitmap_` omitted from the device-dirty fan-out list.
- Suggested fix: add `previewBitmap_.Reset();` in `MarkDeviceDirty()` next to `crossFadeBitmap_.Reset()`.
- Regression test idea: none practical headless; code-review-level guard — a checklist test that every device-dependent member reset in `ApplyTheme` is also reset in `MarkDeviceDirty`.

### [P2] Folder "Size on disk" computed by the engine but silently discarded — WindowShell.cpp:1860-1865, 1424-1428
- Symptom (user-visible): For a selected folder the details pane shows only logical "Total size"; the allocated-size data the engine computes and ships is thrown away, contradicting the `allocated-size-storage-accuracy` change whose stated goal is size-on-disk "carried for the details pane".
- Evidence: the V2 payload carries `totalAllocatedSizeBytes, allocatedBytesKnownCount, allocatedBytesTotalCount` (`WindowShell.cpp:1425-1428`), but the handler stores only three fields: `pendingAggregateStatus_/ItemCount_/TotalSize_` (`WindowShell.cpp:1861-1863`); `WindowShell.h:150-152` has no allocated members, and the folder branch (`1440-1445`) renders no "Size on disk" line — unlike the file branch (`1451-1453`).
- Failure scenario: select any folder → wait for aggregate → pane shows `Items: N / Total size: X` with no size-on-disk although the engine delivered it.
- Root cause hypothesis: V2 payload plumbing stopped at the message handler; render side never updated.
- Suggested fix: add `pendingAggregateAllocatedSize_` (+ known/total counts) members, store them in `WM_APP_FOLDER_AGGREGATE`, and render `Size on disk:` when `allocatedBytesKnownCount > 0`.
- Regression test idea: extend the existing folder-aggregate tests to assert the UI-state seam receives/stores the V2 allocated fields.

### [P2] Image preview drawn stretched to pane rect — aspect ratio destroyed — WindowShell.cpp:1493
- Symptom (user-visible): Every photo preview is distorted to fill the card width×height; portraits are squashed, panoramas squashed vertically.
- Evidence: `context->DrawBitmap(previewBitmap_.Get(), D2D1::RectF(cardLeft + pad, previewTop, cardRight - pad, previewBottom))` — destination rect ignores `preview.width/height`; no letterboxing math anywhere in the branch.
- Failure scenario: preview any non-square image taller than wide.
- Root cause hypothesis: missing fit-and-center computation.
- Suggested fix: compute scale = min(destW/w, destH/h), center the destination rect within the card before `DrawBitmap`.
- Regression test idea: pure-function test for the fit-rect helper once extracted (e.g. portrait 100×200 into 300×100 → 50×100 centered).

### [P2] "Preview Limit" menu items have no effect on text previews — WindowShell.cpp:778-781, 2216-2218 vs Preview.cpp:97
- Symptom (user-visible): User picks "Preview Limit: 64 MB", opens a 5 MB `.log`, and still sees `[Preview truncated]` at 256 KB — the advertised control does nothing for the most common truncated case.
- Evidence: `maxAutoPreviewBytes` only gates *whether* a preview runs (`WindowShell.cpp:1302`); `TextPreviewProvider` hard-caps at `kByteCeiling = 256 * 1024` regardless (`Preview.cpp:97,105-106`). There is no plumbing of the setting into the provider.
- Failure scenario: settings → Preview Limit: 64 MB → select a 2 MB .md file → truncated at 256 KB anyway.
- Root cause hypothesis: limit designed as auto-preview gate only; menu labels promise more.
- Suggested fix: either pass the configured byte ceiling into `TextPreviewProvider`/`FileDescriptor`, or relabel the menu items ("Auto-preview files up to…") so they don't claim truncation control.
- Regression test idea: provider-level test that a descriptor carrying a 1 MB ceiling yields >256 KB of text.

### [P3] Non-BOM UTF-8 text previewed as ANSI mojibake — Preview.cpp:114,141
- Symptom (user-visible): UTF-8 source files without BOM (the overwhelming default today) show `Ã©`-style garbage for non-ASCII characters on Western-locale machines.
- Evidence: `UINT codePage = GetACP();` (`Preview.cpp:114`) and BOM-less content decoded with `MultiByteToWideChar(codePage, ...)` (`Preview.cpp:141,148`) — no UTF-8 validity sniffing fallback.
- Failure scenario: preview a BOM-less `.md` containing "café" on a CP1252 machine → "cafÃ©".
- Root cause hypothesis: only BOM detection implemented.
- Suggested fix: try `CP_UTF8` with `MB_ERR_INVALID_CHARS` first; fall back to `GetACP()` if it fails.
- Regression test idea: provider test feeding BOM-less UTF-8 bytes expects correct wide-char output.

### [P3] Unbounded growth of type-name cache and async future list — WindowShell.cpp:1312-1333
- Symptom (user-visible): slow memory growth over long sessions browsing many directories; shutdown can block briefly.
- Evidence: `fileTypes_` map only ever inserts (`WindowShell.cpp:1315-1316,1329`), never evicts; `fileTypeWork_` holds one `std::async` future per requested path until ready, and `~future` of `std::async` blocks on completion — the vector destructor at window close joins stragglers (including ones stuck on dead network paths, compounding the P1 above).
- Failure scenario: hours of browsing thousands of unique paths → cache grows without bound; close app while an `SHGetFileInfoW` on an unreachable UNC path is in flight → close hangs seconds.
- Root cause hypothesis: caches added without eviction policy.
- Suggested fix: cap `fileTypes_` (LRU or clear on navigation); use `std::thread` + detached/postMessage pattern (like other workers here) instead of `std::async` so destruction doesn't block.
- Regression test idea: harness calling `FileTypeFor` for N≫cap paths and asserting bounded map size after extraction into a testable component.

## Verified sound

- **Stale-preview gating is correct end-to-end**: `PreviewController::Request` cancels the previous token, drops queued work, and workers do a final `requestId_ == requestId && cancellation_ == cancellation` identity check before invoking the callback (`Preview.cpp:249-270`); `WindowShell` independently gates on `activePreviewRequest_` under `previewMutex_` (`WindowShell.cpp:155-159, 1300-1309`), and both increment sites hold the same mutex — no stale attach path found.
- **Destructor/shutdown**: sets `stopping_`, cancels, clears queue, joins both workers (`Preview.cpp:203-216`) — no leak or join-deadlock; worker COM init/uninit pairing handles `S_FALSE` correctly.
- **Malformed-input safety**: registry catches all provider exceptions (`Preview.cpp:185`); image decoder bounds dimensions ≤16384 and rejects >64 MB decodes pre-allocation (`Preview.cpp:62,72`); text read capped at 256 KB; UTF-16 LE/BE paths avoid unaligned casts (`Preview.cpp:126`).
- **DetailsLayout math**: DPI division clamped ≥0.01, collapse rail vs full pane thresholds coherent, geometry consistent between layout and hit-test seam (`DetailsLayout.h:28-63`).
- **FolderAggregateState seam**: `NeedsRequest/Begin/SetRequestId/Accepts/Invalidate` lifecycle correctly invalidates on selection change and snapshot regeneration (`WindowShell.cpp:1297-1299, 1670`); engine send-failure path synchronously invokes the callback with `NotFound` so the pane shows "—" rather than hanging on "Calculating…" (`EngineClient.cpp:934-947`).
- **Aggregate payload lifetime**: posted raw pointer released only on successful `PostMessageW`, deleted in handler (`WindowShell.cpp:1429-1434, 1866`) — no leak/use-after-free found.
- **Selection-change bitmap hygiene**: `RefreshSelectionPresentation` resets `previewBitmap_` and clears `preview_` before issuing a new request, so the normal (non-device-loss) path never shows the previous file's image (`WindowShell.cpp:1291-1301`).
