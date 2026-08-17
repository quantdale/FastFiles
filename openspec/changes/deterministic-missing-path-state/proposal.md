# Deterministic Missing-Path State

## Why

FastFiles' unprivileged engine path already has the pieces required to represent a disappeared directory honestly: `DegradedModeEnumerator` classifies missing paths as `NotFound`, the snapshot protocol carries `DirectoryEnumerationStatus::NotFound`, and Column View maps that status to the visible `NoLongerAvailable` state. However, the stabilized live UIA campaign still records `in-column-error-state-late` as non-deterministic because an externally deleted folder only produces the expected visible error on a refresh race; other runs can settle on an empty-success view or otherwise fail to expose the terminal state.

This is a product-state correctness problem, not merely a test problem. An empty existing directory and a path that no longer exists are semantically different and must never collapse into the same successful snapshot state.

The previous Treemap/UIA change is complete at baseline `2169e858f9cb3e757d613946756481593b188c2e`. Its live capability reached 21 PASS / 5 SKIP / 0 FAIL; this change targets the remaining non-privileged missing-path skip without reopening the parked production-signing / privileged MFT-USN campaign.

## What changes

- Define one deterministic path-state contract across degraded enumeration, indexed snapshot merge, watcher refresh, snapshot publication, Column View freshness, and UI Automation.
- Reproduce the externally-deleted-current-directory race before implementation and identify the exact stale-success or missed-refresh transition.
- Ensure a fresh explicit observation of `NotFound`, `AccessDenied`, or `IoError` for a requested/watched path cannot be overwritten by older/stale successful indexed data for that same path.
- Ensure watcher terminal failure/deletion produces one final path-state refresh instead of silently abandoning the last successful view.
- Keep confirmed `NotFound` distinct from an empty successful directory all the way to the UI.
- Preserve deliberate stale-row behavior only for transient refresh failures where the product has not positively observed that the path is gone.
- Make recreation/recovery deterministic: when the same path later exists and is successfully enumerated, the error state clears and the new contents replace the unavailable state.
- Convert the existing live `in-column-error-state-late` validation from a race-dependent SKIP into a deterministic assertion using the normal engine/UI path, with no private test protocol and no pixel/OCR fallback.

## User impact

- If a folder currently open in FastFiles is deleted or disconnected, its column reliably changes to `This folder is no longer available.` instead of sometimes looking like an empty folder.
- Stale contents are not presented as current after the engine has positively observed that the path disappeared.
- If the folder is recreated, an ordinary re-request/refresh recovers without restarting FastFiles.
- Assistive technology sees the same deterministic status element and wording as the visible UI.

## Non-goals

- Production signing, trust-store changes, Authenticode pin provisioning, clean-host privileged validation, or real MFT/USN release certification.
- Changing degraded enumeration into a filesystem polling loop.
- Treating all I/O failures as `NotFound`.
- Removing the existing bounded watcher/LRU/overflow safety behavior.
- Reworking unrelated Column View, Search, Treemap, drag/drop, or storage-analysis behavior.
