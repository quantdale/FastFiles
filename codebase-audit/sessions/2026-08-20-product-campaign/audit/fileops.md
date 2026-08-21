# Audit area: FILE OPERATIONS — audit src/ui/src/FileOperations.{h,cpp}, FileOperationPolicy.{h,cpp}, ConflictDialog.{h,cpp}, QuickActions.{h,cpp}, OleDragDrop.{h,cpp}. Cover: rename/delete/copy/move correctness (path construction, Unicode/surrogate pairs, long paths, read-only/locked/permission-denied sources and destinations), recycle-bin vs permanent delete semantics, conflict flows (overwrite/keep-both/cancel), progress reporting and cancellation, multi-item and mixed file/folder operations, drag-drop modes (intra-pane, cross-pane, Explorer interop), re-entry protection (double-invoke of one operation), verification that destructive operations act on exactly the intended paths, post-operation index/snapshot/UI/selection reconciliation.

(agent agent-3, outcome completed)

## Defects

### [P1] Copy/Move onto itself: no same-folder or self-target guard anywhere in the drop/paste path — WindowShell.cpp:611
- Symptom (user-visible): dragging a selection and dropping it back into the pane it came from (the easiest drag gesture there is) pops a "file name conflict" dialog for every item; choosing **Replace** in that dialog on a *copy* can destroy the source file.
- Evidence: `QueueTransfer` (`src/ui/src/WindowShell.cpp:611-616`) does `if (paths.empty() || destination.empty()) return false;` — no check that `destination` differs from each source's parent, nor that a dropped folder isn't an ancestor/descendant of a source. The drop handler (`WindowShell.cpp:998-1019`) and `BuildTransferPlan` (`FileOperationPolicy.cpp:65-110`) likewise never compare source parent vs destination. In `BuildTransferPlan`, copying `C:\A\x` into `C:\A` sees `exists(L"C:\A\x") == true` (it's the source itself), prompts, and on Replace pushes `{source, {}, replacesExisting=true}` — `CopyItem` of a file onto itself with `FOF_NOCONFIRMATION|FOF_NOERRORUI` (`FileOperations.cpp:326,346`).
- Failure scenario: select file `x.txt` in pane showing `C:\A`, drag 5 px, release. Conflict dialog appears claiming `C:\A` "already contains x.txt". User clicks Replace (the destructive-sounding fix) → shell copy-over-self; at best an error swallowed into the title bar, at worst truncation of the only copy.
- Root cause hypothesis: conflict detection was written for cross-folder transfers only; nobody added the trivial `source.parent == destination` / descendant guard that Explorer has.
- Suggested fix: in `QueueTransfer` and the drop handler, filter out sources whose parent path equals `destination` (case-insensitive) and sources that are ancestors of `destination`; if nothing remains, return false without enqueueing.
- Regression test idea: headless test on `BuildTransferPlan`/a new guard helper: plan(copy [`C:\A\x`], dest `C:\A`) must yield empty/no-op without invoking the prompt callback.

### [P1] Undo of Move and Restore silently overwrite existing files (FOF_NOCONFIRMATION with hand-built plans bypasses all conflict policy) — WindowShell.cpp:370
- Symptom: Ctrl+Z after a move restores the file by destroying whatever now occupies the original name, with no prompt and no error.
- Evidence: undo Move enqueues one request per path with a pre-baked `transferPlan` (`WindowShell.cpp:369-378`). Because `transferPlan` is non-empty, `Execute` skips `BuildTransferPlan` entirely (`FileOperations.cpp:250-252`), so no conflict prompt ever fires; flags are `FOF_NOCONFIRMATION | FOF_NOERRORUI` (`FileOperations.cpp:326`), which answers "Yes to All" to the shell's replace prompt. Same for Restore: `operation->MoveItem(recycled, restoreFolder, name, ...)` under the same flags (`FileOperations.cpp:362-373`).
- Failure scenario: move `report.docx` A→B; recreate `report.docx` in A (or another app writes it); press Undo. The new file is overwritten silently. Same for restoring from Recycle Bin over a newly created same-named file.
- Root cause hypothesis: undo paths were treated as "internal, already validated" and given a plan marked `replacesExisting=false`, but nothing re-checks occupancy at execution time.
- Suggested fix: for undo requests, leave `transferPlan` empty so the normal preflight/conflict flow runs, or before enqueueing check `GetFileAttributesW(target)` and route collisions through the conflict dialog.
- Regression test idea: test that `Execute`'s planning branch is entered whenever `request.recordHistory == false` too, i.e. a unit test asserting undo-shaped requests get conflict prompts (inject prompt via seam).

### [P2] Paste uses a stale app-local path vector that shadows the real clipboard — WindowShell.cpp:315
- Symptom: copy files in FastFiles, copy different files in Explorer, then Ctrl+V in FastFiles pastes the *old* FastFiles selection.
- Evidence: `std::vector<std::wstring> pathsToPaste = clipboardPaths_; if (pathsToPaste.empty()) pathsToPaste = ReadFilesFromClipboard(...)` and `cut = clipboardPaths_.empty() ? externalCut : clipboardIsCut_` (`WindowShell.cpp:315-317`). There is no `AddClipboardFormatListener`/`WM_CLIPBOARDUPDATE` handling anywhere in `src/ui` (grep confirms), so `clipboardPaths_` is never invalidated when another process takes the clipboard.
- Failure scenario: as above; also cut flag staleness — an external *copy* gets executed as a *move* because `clipboardIsCut_` from an earlier session action wins.
- Root cause hypothesis: the local vector was meant as a fallback ("temporarily owned by another process", comment at :307) but is implemented as a primary override.
- Suggested fix: prefer `ReadFilesFromClipboard` when it returns non-empty; use `clipboardPaths_` only as fallback. Or register for clipboard updates and clear `clipboardPaths_` on `WM_DESTROYCLIPBOARD` not caused by us.
- Regression test idea: hard without a clipboard seam; extract the decision into a pure helper `ResolvePasteSource(local, external, localCut, externalCut)` and unit-test the precedence.

### [P2] Cut-paste never clears the shell clipboard, enabling a broken second paste — WindowShell.cpp:321
- Symptom: after moving files via cut+paste, Ctrl+V again re-runs the move of the now-gone paths and reports failures ("0 of N — N failed" in the title bar).
- Evidence: only `clipboardPaths_.clear()` runs (`WindowShell.cpp:321-323`); the CF_HDROP + "Preferred DropEffect"=MOVE data placed by `CopyFilesToClipboard` (`QuickActions.cpp:105-161`) stays on the clipboard. `HasFilesOnClipboard` then still reports true (`WindowShell.cpp:294`), so Paste stays enabled.
- Root cause hypothesis: local state cleared, system clipboard forgotten.
- Suggested fix: on successful cut-move completion, post an internal "empty clipboard" (e.g. `EmptyClipboard` via `OpenClipboard(hwnd_)`) or set a null drop effect.
- Regression test idea: manual/UI-level; note in test checklist.

### [P2] Escape cancels the current operation even when none is running, and a cancel doesn't stop queued operations — WindowShell.cpp:2177, FileOperations.cpp:226
- Symptom: pressing Escape at any idle moment sets the window title to "FastFiles — cancelling operation…" permanently until the next event resets it (misleading dead affordance). Conversely, pressing Escape during op 1 of a queued batch cancels op 1 but op 2..N still run: `cancelRequested_ = false` is reset per dequeue (`FileOperations.cpp:226`), so a user trying to stop a batch must hammer Escape between items.
- Evidence: `case VK_ESCAPE: ... fileOperations_.CancelCurrent(); SetWindowTextW(hwnd_, L"FastFiles — cancelling operation…");` unconditionally (`WindowShell.cpp:2171-2179`); reset at `FileOperations.cpp:226`.
- Suggested fix: gate on `if (!fileOpInProgress_) { /* fall through to other Esc handling */ }`; add a `CancelAll()` that sets a sticky flag consulted at dequeue.
- Regression test idea: logic test on a queue-drain helper with a cancel injected mid-batch.

### [P2] Restart-after-Stop would leave every operation instantly cancelled — FileOperations.h:89 / FileOperations.cpp:164
- Symptom: latent; if `Start()` is ever called again after `Stop()` (e.g. window re-init), all operations complete as "Cancelled" with zero work.
- Evidence: `Stop()` sets `cancelRequested_ = true` (`FileOperations.cpp:170`) and never clears it; `Start()` resets only `stopping_` (`:164`).
- Suggested fix: add `cancelRequested_ = false;` in `Start()`.
- Regression test idea: unit-test Start→Stop→Start lifecycle with a fake request sink.

### [P3] Long paths (>260 chars) silently unsupported across the operation pipeline — FileOperationPolicy.cpp:255-equivalent in FileOperations.cpp:255
- Symptom: existence preflight (`GetFileAttributesExW`, `FileOperations.cpp:255`), rename collision check (`GetFileAttributesW`, `:281`), and item creation (`SHCreateItemFromParsingName`, `:140`) all fail past MAX_PATH without `\\?\`; preflight returning false means conflicts go undetected and the shell op fails with only a title-bar failure count.
- Suggested fix: normalize through the project's `ffprotocol::WindowsPath` (already a dependency of FileOperationPolicy) to extended-length form before Win32 calls.

### [P3] New folder/file inline-rename edit draws over the wrong row — WindowShell.cpp:1946-1949
- Symptom: after Create Folder, `BeginInlineRename` positions the edit using `FocusedItemIndex()` immediately after `NavigateWorkspace`, but the listing refresh is async (`engineClient_.RequestDirectory`, `:1957`), so the edit box visually attaches to whatever row was focused before the new item exists; it commits against the correct path but appears anchored elsewhere.
- Suggested fix: defer `BeginInlineRename` until the refresh for the created parent completes (e.g. trigger from the snapshot-updated handler when `createdPaths_` pending).

### [P3] Partial Restore shows "Undo unavailable" even when some items restored — WindowShell.cpp:1942-1945
- Symptom: restoring 10 recycled items where 1 fails pops "The deleted item can no longer be restored" as if the whole undo failed.
- Suggested fix: only show when `event->completed == 0`.

### [P3] Progress percent mixes byte-based and item-based units — FileOperations.cpp:77-78 vs :391
- Symptom: live progress events compute percent from the shell's byte/work-unit `UpdateProgress(total, soFar)`, while the final Completed event recomputes percent from item counts — the bar can jump backwards (e.g. one huge file finishes last: 99% → 50%). Cosmetic but visible in the status-bar progress strip.
- Suggested fix: keep final percent = 100 on success, or carry the last byte-based percent into the completion event.

## Verified sound

- **Frame math / buffer sizing** in `CopyFilesToClipboard` (QuickActions.cpp:107-137) and `FileDataObject::GetData` (OleDragDrop.cpp:78-100): overflow-guarded character counts, correct double-null termination, `pFiles`/`fWide` set correctly.
- **Conflict "apply to all" semantics**: `remainingChoice` correctly excludes Cancel, and Replace-on-planned-collision is downgraded to keep-both so an earlier batch source can't be overwritten (FileOperationPolicy.cpp:88-107); planned destinations reserved case-insensitively via `ffprotocol::CaseInsensitivePathLess`.
- **Keep-both name generation**: stem/extension split, bounded loop, occupied-set includes both filesystem and in-batch reservations.
- **Recycle-delete reversibility**: `PostDeleteItem` captures the recycled item's IDList onto the matching just-completed entry (FileOperations.cpp:50-62); entries without IDLists are filtered before recording history (:436); permanent delete skips FOF_ALLOWUNDO and requires an explicit confirm dialog (WindowShell.cpp:344-346).
- **Move undo eligibility**: batches containing any Replace are excluded from undo entirely, with documented rationale (FileOperations.cpp:417-434).
- **Re-entry protection**: worker serializes all requests through one queue; `Start()` rejects double-start; `Stop()` drains pending conflict questions and answers them so the worker never hangs on shutdown, and `RequestConflictDecision` polls `stopping_` with a timeout fallback (FileOperations.cpp:169-190, 241-245).
- **Event lifetime**: heap events are receiver-owned, posted with failure-safe release (FileOperations.cpp:204-208); conflict question objects freed by the poster after answer.
- **Drop destination resolution**: destination comes from the drop point's pane, not the active pane, with the DPI conversion documented (WindowShell.cpp:978-997); default effect is Explorer-consistent (same-volume move, cross-volume copy, Ctrl/Shift overrides — OleDragDrop.cpp:239-251).
- **`IsValidFileName`**: length, trailing dot/space, reserved DOS names including extension forms, invalid characters — matches NTFS/FAT rules; case-only renames correctly allowed via `_wcsicmp` sibling check (FileOperations.cpp:282).
- **CreateFolder/CreateFile**: missing created-IShellItem compensated by verifying the deterministic expected path before publishing it for inline rename (FileOperations.cpp:400-411).
