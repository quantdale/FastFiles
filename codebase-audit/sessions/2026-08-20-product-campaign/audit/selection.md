# Audit area: SELECTION &amp; INPUT — audit src/ui/src/SelectionModel.h, SelectionIdentity.h, SelectionDiagnostics.h, ColumnView.{h,cpp} input paths (mouse hit-testing, keyboard handling, Ctrl/Shift semantics, focus tracking), src/ui/include/ffui/SnapshotAggregate.h and SnapshotPathLookup.h identity mapping. Cover: selection identity attached to filesystem objects vs visual row index across sorting/filtering/snapshot replacement, Ctrl+A on empty and non-empty columns, click-empty-area clearing, PageUp/PageDown/Home/End, Enter/Escape, focus visibility, dual-pane selection isolation, behavior when selected items are deleted/renamed externally, rapid input re-entry and double-click hazards.

(agent agent-1, outcome completed)

All key paths are now traced through the shell (`WindowShell.cpp` WM_KEYDOWN routes everything except F9/Esc into `ColumnView::OnKeyDown`, which handles only Up/Down/Left/Right/Enter/Ctrl+A). Here is the audit report.

# Selection & Input Audit — FastFiles UI

## Defects

### [P2] Mouse hit-test is off by ~one row whenever the status banner is visible — src/ui/src/ColumnView.cpp:1469
- Symptom: While a column shows `Refreshing…` or `Showing previous results — refresh failed.` (i.e. every fresh navigation while loading, and *permanently* after a failed refresh), clicking a row selects/activates the wrong item (~24 DIPs below the clicked row ≈ the row underneath, or nothing on the last row).
- Evidence: Render shifts rows down when the banner draws: `contentTop += 24.0f;` (ColumnView.cpp:1477), rows drawn at `contentTop + r * kRowHeight - verticalScrollOffset` (ColumnView.cpp:1489). But `ComputeColumnHitTest` computes `contentY = clientPoint.y - UiMetrics::kBadgeHeight + verticalScrollOffset` with **no** banner offset (ColumnView.h:558-564). Same mismatch in the scroll clamp `itemCount * kRowHeight - visibleHeight` (ColumnView.cpp:578, 639) and `EnsureFocusedItemVisible` (ColumnView.cpp:633-640).
- Failure scenario: Open a folder; while "Refreshing…" shows, click "Report.txt" (visual row 3). Hit test resolves row 4 (or past the end). With `StaleWithError` after an engine hiccup the banner never goes away, so every click in that column is wrong until navigation succeeds.
- Root cause: The 24-DIP banner inset exists only in the render path; hit-testing, vertical-scroll clamping, and scroll-into-view were never updated.
- Suggested fix: Track the per-column banner inset (e.g. store `contentTopInset` on `Column`, set to 24 when Loading/StaleWithError) and add it in `ComputeColumnHitTest`, `ClampVerticalScrollForPane`, `EnsureFocusedItemVisible`, and `UiaScrollItemIntoView`.
- Regression test: Extend the pure `ComputeColumnHitTest` seam with a `contentTopInset` parameter; assert y = badge + 24 + 3*28 hits row 3 when inset=24.

### [P2] Enter does not open files — dead "Open" affordance for keyboard users — src/ui/src/CommandSystem.cpp:270
- Symptom: Keyboard users can select a file but pressing Enter does nothing (it just re-selects). Files can only be opened by double-click. `item.open` is registered with **no keyboard shortcut**, so the command palette lists "Open" but no chord triggers it.
- Evidence: `VK_RETURN` in `OnKeyDown` calls `ActivateItem(...)` which for a non-directory only calls `SelectSingle` and returns (ColumnView.cpp:303-306, 238-245); `RegisterOne(..., L"item.open", ..., ShortcutScope::ActiveView)` with no `KeyChord` (CommandSystem.cpp:270). Shell `WM_KEYDOWN` adds nothing for Enter except horizontal scroll-into-view (WindowShell.cpp:2195-2201).
- Failure scenario: Select "report.pdf" with arrow keys, press Enter → nothing. Standard file-manager contract broken.
- Root cause: Enter was wired only to the Finder-style folder-navigation path; the file-open half was never bound.
- Suggested fix: In `WM_KEYDOWN` (or `OnKeyDown`'s RETURN case), when the focused item is a non-directory, invoke `item.open`; keep folder behavior as-is.
- Regression test: headless seam asserting the RETURN routing chooses open-vs-navigate from `isDirectory`.

### [P2] PageUp/PageDown/Home/End are completely unhandled in filesystem columns — src/ui/src/ColumnView.cpp:286
- Symptom: In any column view, PgUp/PgDn/Home/End do nothing (Home only works as Alt+Home for the Home surface). Long directories cannot be keyboard-traversed efficiently.
- Evidence: `switch (virtualKey)` covers only `VK_UP/VK_DOWN/VK_LEFT/VK_RIGHT/VK_RETURN/'A'` (ColumnView.cpp:286-320); `WM_KEYDOWN` forwards everything else straight through and returns 0 (WindowShell.cpp:2181-2204), swallowing the key. Contrast the Home surface, which implements `VK_HOME/VK_END` (ColumnView.cpp:964-971).
- Failure scenario: Folder with 500 items; press End → nothing happens.
- Root cause: Never implemented; keys are silently eaten because the shell `return 0`s after `OnKeyDown`.
- Suggested fix: Add `VK_PRIOR/VK_NEXT` (± visible-row count, needs viewport height plumbed in or stored from Render like `lastViewportHeight_`) and `VK_HOME/VK_END` (row 0 / last row) using `SelectSingle` + `EnsureFocusedItemVisible`.
- Regression test: pure seam test on the index math; manual ctest for `ApplySelectionClick` anchor behavior.

### [P2] Clicking empty area never clears the selection — src/ui/src/ColumnView.cpp:224
- Symptom: After selecting items, clicking blank space below the rows (or in a column gap/right of the last column) leaves the selection intact. Every mainstream file manager clears on empty-area click; here the only ways to deselect are Ctrl-clicking each item or navigating.
- Evidence: `ComputeColumnHitTest` happily returns `itemIndex` past the end or `columnIndex` past `columns.size()` (ColumnView.h:563-564); `ActivateItem` then early-returns: `if (itemIndex < 0 || itemIndex >= static_cast<int>(column.items.size())) return;` (ColumnView.cpp:224-226, also 220-222).
- Failure scenario: Ctrl+A in a downloads folder, click whitespace intending to cancel before Delete → Delete still targets everything.
- Root cause: Out-of-range hits are treated as "ignore" instead of "clear".
- Suggested fix: In `ActivateItem`, when `columnIndex` is valid but `itemIndex` is out of range and neither Ctrl nor Shift is held, do `selected.clear(); anchor = focus = -1;` (and fire the UIA selection transition).
- Regression test: call the proposed clear-on-miss seam with `itemIndex == items.size()`; assert empty selection, anchor −1.

### [P2] Merely hovering the inactive pane steals active-pane (keyboard) focus in dual-pane mode — src/ui/src/ColumnView.cpp:418
- Symptom: Moving the mouse across the divider switches the active pane without any click. A user who selects files in the left pane, then parks the cursor in the right pane (or crosses it en route to a toolbar), finds Ctrl+A / Delete / Enter now operate on the *other* pane.
- Evidence: `OnMouseMove`: `if (hoveredPane != activePane_) { ActivatePane(hoveredPane); }` (ColumnView.cpp:416-419). `ActivatePane` sets `activePane_` which determines the target of every keyboard command and `ActiveSelectionPaths()` (ColumnView.cpp:1839-1849).
- Failure scenario: Select 20 files left, mouse drifts right, press Ctrl+A expecting "select all remaining" → operates on right pane; worse, Delete after a momentary hover-over targets the wrong pane's selection.
- Root cause: Hover coordinate bookkeeping was implemented as pane activation; hover-only tracking (like the paint-only hover model everywhere else) was intended but the side effect of changing the keyboard target is real UX damage.
- Suggested fix: Keep hover coordinates pane-local without calling `ActivatePane` from `OnMouseMove`; switch panes only in `OnMouseDown`. Alternatively keep hover-in-inactive-pane rendering but leave `activePane_` untouched.
- Regression test: assert `OnMouseMove` over pane 1 does not change `ActivePane()`.

### [P3] `UiaScrollPane` reads shared scroll state without the mutex — src/ui/src/ColumnView.cpp:2253
- Symptom: Benign-in-practice data race; torn/stale offset read possible while a UIA client scrolls concurrently with UI-thread updates.
- Evidence: `const float current = pane == 0 ? uiaGeometry_.hScroll0Dip : scrollOffset2_;` — no `columnsMutex_` acquisition, while every other accessor locks (cf. `UiaSetHorizontalScroll` at 2262-2272, `UiaVerticalScroll` at 2016-2020).
- Suggested fix: Take `std::lock_guard` for the read (the subsequent writers already do).
- Regression test: hard to unit-test; cover via /analyze or TSan-style review note only.

### [P3] Keyboard focus row is visually indistinguishable from other selected rows — src/ui/src/ColumnView.cpp:1517
- Symptom: With a multi-selection, the focused row (the one Enter/arrows act on, and the one inline rename positions against at WindowShell.cpp:641) renders identically to every other selected row — same accent pill, same text brush. Users cannot tell where keyboard focus sits.
- Evidence: Selection draw depends only on `isSelected && isFocusedColumn` (ColumnView.cpp:1517-1521, 1549-1550); no per-row focus stroke exists (unlike Home, which has `focusStrokeBrush_`, ColumnView.cpp:1723-1727).
- Suggested fix: Draw the 1-DIP `focusStroke` ring around `r == column.focusIndex` in the focused column.
- Regression test: UIA projection already exposes `focused` (ColumnView.cpp:1998); add a render-state seam assertion if desired.

### [P3] Hover highlight can stick to the wrong row after a snapshot refresh — src/ui/src/ColumnView.cpp:456
- Symptom: `hoverColumn_/hoverItem_` are raw indices, never invalidated when `RefreshColumnFromSnapshot` replaces/re-sorts items; until the mouse moves, the hover pill highlights whatever item now occupies the old index (and it suppresses the selection pill for that row via `!isSelected`, ColumnView.cpp:1502).
- Evidence: No invalidation in `RefreshColumnFromSnapshot` (ColumnView.cpp:1026-1100) or `NavigateToComputerRoot` resets it only for that one path (684-686).
- Suggested fix: Reset `hoverItem_/hoverColumn_` in `RefreshColumnFromSnapshot` when the item set changed.
- Regression test: not easily headless; low priority.

## Verified sound

- **Selection identity across refresh**: `RefreshColumnFromSnapshot` captures pre-refresh identities (incl. volumeRowId/fileId), rebuilds, sorts folders-first case-insensitively, and restores selection/anchor/focus by durable identity (ColumnView.cpp:1027-1063). `SameSelectionIdentity` correctly refuses to match an indexed identity by path alone (deletion→recreation at same path clears rather than guessing) — SelectionIdentity.h:61-73.
- **Ctrl+A on empty and non-empty columns**: routed through `SelectAllItems`, which sets anchor/focus to −1 on empty and 0-based on success (SelectionModel.h:29-39); keyboard path shares the same seam as `SelectAll()` (ColumnView.cpp:307-316).
- **Ctrl/Shift click semantics**: `ApplySelectionClick` — Shift range from anchor (falls back to plain select when anchor unset), Ctrl toggle preserving initial anchor, plain click single-select+anchor (SelectionModel.h:8-27). Ctrl/Shift clicks deliberately do not truncate/navigate (ColumnView.cpp:238-245).
- **Dual-pane selection isolation of the data model**: per-pane `columns_/columns2_`, per-pane focus indices and scroll offsets; `SavePaneState/RestorePaneState` refuse Home/unavailable panes; disabling dual-pane promotes the active pane instead of reverting (ColumnView.cpp:1786-1824, 1859-1917).
- **External delete/rename**: out-of-range restored identities are dropped silently; renamed degraded entries lose selection by design (documented in SelectionIdentity.h comments) — no stale-index crashes since every consumer bounds-checks indices (e.g. ActiveSelectionPaths ColumnView.cpp:489-493).
- **Double-click policy**: first click selects/navigates; `WM_LBUTTONDBLCLK` opens only a single selected *file* via `ShouldOpenSelectionOnDoubleClick` (ColumnView.h:528-530, WindowShell.cpp:2007-2020) — no double-activation of folders.
- **Home-surface focus model**: Clear stops reachable only by traversal (never by initial focus/Home/End), non-actionable cards excluded from focus/hover/activation, empty-path destinations rejected (ColumnView.h:644-832); stranded focus dropped on `SetHomeData` (ColumnView.cpp:864-881).
- **UIA event discipline**: selection/focus transition diffs computed against pre-mutation snapshots and fired post-lock (ColumnView.cpp:243-258, 2459-2483); structure events always post-lock.
- **SnapshotAggregate math**: saturation-checked uint64 accumulation, cycle guard via `activePaths`, allocated-size completeness pair never presents partial subtotals as exact (SnapshotAggregate.h:41-100).
