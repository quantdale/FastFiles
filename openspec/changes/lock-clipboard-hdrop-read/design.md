## Context

See proposal.md. `CopyFilesToClipboard` locks; `ReadFilesFromClipboard` does not (`QuickActions.cpp`). `OleDragDrop::ExtractPaths` already `GlobalLock`s `medium.hGlobal`. `HasFilesOnClipboard` only tests non-null `GetClipboardData` — a null check without lock is acceptable (presence), but any byte read must lock.

## Goals / Non-Goals

**Goals:** Lock/unlock every HGLOBAL actually dereferenced on the clipboard read path; failed lock → empty list.

**Non-Goals:** Changing cut vs copy Preferred DropEffect meaning; OLE drag-out.

## Decisions

### D1: Lock both CF_HDROP and Preferred DropEffect handles

Mirror the write path. Unlock in all exit paths (RAII locker around the handle).

**Why:** Win32 contract. The OLE extractor is the in-repo proof of the correct pattern.

### D2: Failed lock is a no-op paste, not a crash

Return empty `paths`, `cut = false`. Same as clipboard-open failure today.

**Rejected:** Retry loops — clipboard lock failure is exceptional; empty paste is safe.

## Risks / Trade-offs

- **[Risk] Some providers return a pointer-sized value that is not an HGLOBAL.** → Mitigation: `GlobalLock` failure covers that; we never dereference without a successful lock.
- **[Risk] Tests that touch the real clipboard are process-global.** → Mitigation: serialize in `test_quick_actions.cpp` with a unique owner HWND; skip (return non-zero only on logic fail) if `OpenClipboard` fails in a locked-down CI session — prefer an in-process HGLOBAL helper extracted from the read function so the lock pairing can be tested without the clipboard when needed.

## Migration Plan

None.

## Open Questions

None.
