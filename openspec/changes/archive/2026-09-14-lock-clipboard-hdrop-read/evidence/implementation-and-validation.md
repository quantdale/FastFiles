# Implementation and validation — lock-clipboard-hdrop-read

Applied 2026-09-14 on `feat/refero-frontend-overhaul` (change proposed at `d214f1e`).

## What changed

- `ReadFilesFromClipboard` now reads both clipboard HGLOBALs through a locked
  view: a new `ReadDropHandle(HGLOBAL, HGLOBAL, paths, cut)` seam `GlobalLock`s
  the `CF_HDROP` handle before `DragQueryFileW` and the Preferred DropEffect
  handle before reading the `DWORD`, with an RAII guard that unlocks on every
  exit path (design D1). The pattern mirrors the write path and the OLE drop
  extractor.
- A failed lock (null/invalid handle) returns an empty path list and
  `cut == false` — an empty paste, never a dereference of an unlocked pointer
  (design D2).
- `HasFilesOnClipboard` keeps its null-handle presence check (no byte
  dereference, so no lock required) and now says so.

## Files

- `src/ui/src/QuickActions.{h,cpp}`
- `tests/ui/test_quick_actions.cpp`

## Tests added

- In-process `DROPFILES` handle + Preferred DropEffect handle round trip
  through `ReadDropHandle` (paths and cut intent).
- Null handle: reports lock failure, returns an empty list, leaves `cut` false.
- Real-clipboard round trip through `CopyFilesToClipboard` /
  `ReadFilesFromClipboard` (opens the real clipboard, verifies paths and cut
  intent, then clears it); skipped with a message when a locked-down session
  refuses `OpenClipboard`.

## Validation

- `ctest --test-dir build/debug -R ffquick_actions --output-on-failure`:
  `ffquick_actions_tests` PASS.
- Full Debug CTest on the exact normalized tree (2026-09-14): **104/104 PASS**
  (267 s), including `ffdoc_drift_tests` and `ffproject_status_ps_tests`.
- `openspec validate --changes --strict`: 56/56 passed.
