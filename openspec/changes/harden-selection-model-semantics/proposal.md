# Proposal — harden-selection-model-semantics

## Why

The canonical `multi-selection-and-dragdrop` capability defines plain-click, Ctrl-click, Shift-click range, Ctrl+A, and per-pane selection semantics. The production implementation is concentrated in the small `SelectionModel.h` state machine and the `ColumnView` input paths that call it, but the current automated suite does not directly exercise that state machine. Existing `ffselection_identity_tests` prove identity restoration across reorder/replacement, while `ffcolumnview_uia_tests` prove provider plumbing over a fake surface; neither is an executable regression matrix for the actual plain/Ctrl/Shift/Ctrl+A interaction rules.

The live UIA capability already proves that Column View exposes selection patterns, so this is now a code-achievable gap rather than an external/manual validation item.

## What changes

- Add deterministic headless coverage for the real `SelectionModel.h` helpers, including plain replacement, Ctrl add/remove, Shift ranges in both directions, repeated Shift from a stable anchor, Shift with no prior anchor, focus behavior, and Select All including an empty list.
- Ensure the production Ctrl+A path uses the same tested Select All semantic seam (or an equivalently centralized implementation) so the headless proof cannot drift from the actual keyboard path.
- Strengthen Column View/UIA tests so Selection/SelectionItem observations agree with the real selection model after mutations and remain pane-scoped.
- Add an isolated live UIA scenario that operates a real scratch directory through real mouse/keyboard modifier input and verifies selection semantically through UIA — no OCR, no pixel-state assertions, no test-only production hook.
- If the new tests expose a real supported-path defect, make only the narrowest selection-model fix required by the canonical contract.
- Record durable implementation/validation evidence and reconcile the historical `selection-model test depth` follow-up when complete.

## Non-goals

- No drag-source/drop-target behavior changes and no expansion of OLE drag-and-drop coverage.
- No file-operation, conflict-resolution, undo/history, search, storage, index-store, protocol, schema, or wire-format change.
- No redesign of visual selection styling.
- No production signing, trust-store mutation, `FastFilesIndexSvc`, MFT/USN, clean-host, or VM-provisioning work.
- No new accessibility-only selection state; UIA remains a projection of the same real model.

## Success

The change is COMPLETE only when the pure selection state machine and the real Column View input path are both proven against the canonical semantics, live selection interaction is deterministic and semantically observable, the full non-privileged validation matrix is green, evidence/workstate are truthful, the final commit is pushed with `HEAD == origin/main` and a clean tracked worktree, and hosted CI for the final SHA is queried successfully.