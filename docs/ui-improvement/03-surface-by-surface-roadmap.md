# 03 — Surface-by-Surface UI/UX Roadmap

## Priority model

- **P0:** dominates first impression or core file-management usability.
- **P1:** high-frequency workflow or important trust/feedback surface.
- **P2:** secondary workflow that should inherit the system after the shell is stable.

The order is deliberate. Do not polish every surface simultaneously.

---

## 1. Main shell and top chrome — P0

### Current responsibilities

The shell currently combines navigation chrome, sidebar, tabs, breadcrumb/address behavior, the main browsing surface, search/storage alternate workspaces, details/preview, status/progress, and global commands.

### Target experience

The top of the app should read as one quiet navigation/command region rather than a stack of unrelated controls.

Recommended hierarchy:

1. **Window/title layer:** application/window controls only; avoid duplicating app identity unnecessarily.
2. **Navigation/command layer:** Back, Forward, Up/Home where applicable, breadcrumb/address, search entry/global search affordance, and only the most common global commands.
3. **Tab layer:** visually subordinate to the active location but clearly persistent.
4. **Content layer:** begins immediately after shell navigation with minimal decorative separation.

The exact stacking can be refined through prototypes; the principle is to reduce visual fragmentation.

### Improvements

- Reduce the number of simultaneous borders/background bands.
- Give the address/breadcrumb control a dominant navigation role.
- Make Back/Forward/Up icon buttons one consistent family.
- Keep global commands visible only when frequent enough to justify permanent chrome.
- Move low-frequency commands to overflow or the command palette.
- Use the status/progress region for transient operational feedback rather than changing the title bar.
- Ensure the top chrome adapts instead of clipping at narrow widths.

### Acceptance criteria

- A user can identify current location, active tab, active pane, and search mode at a glance.
- No control appears visually primary unless its task priority warrants it.
- Narrow width has a designed state, not just compressed geometry.

---

## 2. Sidebar / Quick access — P0

### Target experience

The sidebar is orientation and fast navigation, not a second dashboard.

Recommended content order:

1. Home.
2. Pinned/bookmarked locations.
3. Known folders/drives, grouped with restrained headings.
4. Storage/settings or other feature destinations only if they are truly top-level product locations.

### Improvements

- Use one nav-item component with consistent selected, hover, focus, disabled, and unavailable states.
- Avoid heavy section boxes; use spacing and typography to group sections.
- Make collapsible section affordances obvious but visually quiet.
- Long bookmark/drive labels should trim safely.
- Unavailable locations need a distinct icon/status cue and a recovery/context action rather than only color.
- Collapsed sidebar should preserve recognizable icons and tooltips/accessibility names.
- Drag/reorder behavior, if present for bookmarks, needs a clear insertion indicator.

### Acceptance criteria

- The selected sidebar destination is unmistakable but does not overpower selected files.
- Every item is keyboard reachable in a logical order.
- Collapse/expand does not lose state or produce layout jumps in content.

---

## 3. Home — P0

### Current data model

Home aggregates pinned locations, recent folders, recent searches, known folders, and actions.

### Target experience

Home should answer: **“Where do I want to work next?”**

It should not look like a generic analytics dashboard.

### Recommended hierarchy

**Primary: Quick access / Pinned**  
Persistent user-curated destinations.

**Secondary: Recent folders**  
Recent work with a limited, useful count.

**Tertiary: Recent searches**  
Show only when history exists and the feature is useful; keep lower visual weight.

**Utility actions/known locations**  
Use compact links/nav items instead of giving every destination a large card.

### Improvements

- Prefer a restrained list/card hybrid to a dense equal-weight grid.
- Establish strict text bounds and ellipsis for paths; the recent `UX-001` overflow defect should never recur.
- Use a secondary path line or tooltip rather than stuffing full paths into primary labels.
- Empty sections should disappear or show a lightweight first-use explanation; do not leave dead boxes.
- Provide direct context actions for pinned/recent items without cluttering every row with permanent icons.
- Make first-run Home useful even with no recents.

### Acceptance criteria

- No overlap/clipping for long paths or localization expansion.
- Home remains useful at narrow widths.
- The first three likely actions are visually obvious without reading every section.

---

## 4. Column View / primary file browsing — P0

### Target experience

This is FastFiles' signature workspace. It should be visually cleaner than the shell around it and exceptionally clear about selection, focus, hierarchy, and overflow.

### Improvements

#### File rows

- Preserve real shell icons.
- File name is primary; secondary metadata only appears when it materially helps.
- Standardize row heights through density profiles.
- Keep text/icon baselines identical across columns.
- Use ellipsis safely; never horizontally paint outside row bounds.

#### Selection and focus

Define four visually distinct states:

1. hover;
2. focused row, not selected where the model allows it;
3. selected in active/focused pane;
4. selected in inactive/unfocused pane.

Do not let hover and soft selection look interchangeable.

#### Hierarchy

- Folder continuation/chevron affordance must be subtle but discoverable.
- Column separators should be low contrast until interaction requires more emphasis.
- Horizontal scrolling/column movement should communicate spatial continuity.

#### Empty/error states

A column representing an empty directory, denied directory, missing path, disconnected drive, or still-loading state must not become an unexplained blank rectangle.

### Acceptance criteria

- At any time, users can identify active pane and selected item without relying on color alone.
- Large directories preserve culling/scroll performance.
- Keyboard and pointer selection produce equivalent visible states.
- Long names, Unicode, hidden/system attributes, and unavailable paths are visually safe.

---

## 5. Tabs and dual-pane — P0/P1

### Tabs

Tabs should resemble a native workspace concept without copying a browser tab strip mechanically.

Improvements:

- Show concise folder/Home names, with full location available on hover/accessibility text.
- Make active tab hierarchy clear without a large accent block.
- Reveal close affordance on hover/focus while keeping keyboard access.
- Preserve a minimum useful width, then intelligently truncate rather than squeeze indefinitely.
- Define tab overflow behavior for many tabs.

### Dual-pane

Dual pane must make **active pane** obvious.

Recommended cues:

- slightly stronger active-pane boundary/header state;
- focused-selection treatment only in active pane;
- inactive selection remains visible but subdued;
- commands that act on the active pane should visually associate with it where necessary.

Avoid duplicating the full global toolbar inside each pane.

### Acceptance criteria

- Users never have to guess which pane receives Paste, New Folder, search scope, or keyboard commands.
- Tabs and dual-pane work in both density profiles and supported window widths.

---

## 6. Search workspace — P0

### Product intent

Search is a primary differentiator and should feel faster and clearer than a generic filter form.

### Recommended layout

**Top row:** prominent query field + clear search state.  
**Secondary controls:** scope/filter/sort, progressively disclosed when space is constrained.  
**Results:** dominant content area.  
**Status:** concise count/progress/error information.

### Improvements

- Make query focus immediately obvious.
- Provide a clear way to clear/cancel/reset a query.
- Use chips or a compact filter summary only for active filters; do not permanently show a wall of controls.
- Result name is primary; location/path and metadata are secondary.
- Match highlighting must remain readable in both themes and under selection.
- Searching, completed, no-results, stale/unavailable engine, and error states must be visually distinct.
- Reuse browse-row selection/focus semantics.
- Preserve `LVS_OWNERDATA`/virtualization or equivalent performance behavior.

### Acceptance criteria

- Query-to-results workflow is understandable without documentation.
- The user can tell whether results are still updating.
- No-results state explains scope/filters and offers a useful recovery action.
- Search results remain usable with thousands of entries.

---

## 7. Details / Preview pane — P1

### Target experience

Details should answer useful questions about the current selection without becoming a permanent wall of metadata.

### Improvements

- Prioritize preview and the 4–6 highest-value metadata fields.
- Group advanced metadata behind a secondary section or progressive disclosure.
- Use consistent label/value typography.
- Avoid large empty preview boxes when preview is unsupported.
- Provide explicit states for loading, unsupported type, too-large preview, permission failure, and preview disabled.
- For multi-selection, show aggregate information rather than stale single-item fields.
- Preserve reclaimed workspace width cleanly when hidden.

### Acceptance criteria

- Details never make the primary browsing pane feel cramped at normal widths.
- Narrow layouts collapse/hide details by design.
- State changes do not flash misleading empty/old content.

---

## 8. File operations and conflict handling — P1

### Target experience

Copy/move/delete/rename operations are trust-sensitive. FastFiles should clearly communicate what is happening without unnecessary modal interruption.

### Operation feedback

- Small/instant operations: lightweight confirmation only when needed.
- Longer operations: persistent status/progress surface with operation name, progress, and valid cancel action.
- Completion: brief non-blocking success state when useful.
- Failure: concise summary plus access to detailed failures.

### Conflict dialog

- State exactly which item conflicts and what the two sides are.
- Use clear action labels (`Replace`, `Skip`, `Keep both`) rather than vague confirmation language.
- Distinguish destructive actions without flooding the entire dialog red.
- For batches, make “apply to remaining conflicts” explicit.
- Keyboard default/cancel behavior must be predictable.

### Delete flows

- Differentiate Recycle Bin delete from permanent delete.
- Permanent delete deserves stronger confirmation and wording.
- Undo/restore availability should be surfaced where the operation model supports it.

### Acceptance criteria

- Users can always determine whether an operation is queued, active, completed, cancelled, or failed.
- Cancel is never shown when it cannot work.
- Failure details do not disappear before the user can act on them.

---

## 9. Command palette and contextual commands — P1

### Target experience

The command palette should be the high-speed escape hatch for power users, not a duplicate menu dump.

### Improvements

- Search field gets immediate focus and strong but restrained focus style.
- Results use icon + command name + optional shortcut + optional category.
- Ranking should favor relevant/available commands.
- Unavailable commands should either be omitted or clearly disabled with reason where useful.
- Selected result uses the same focus/selection grammar as other lists.
- The palette should open/close quickly with no decorative animation delay.

### Command placement rule

A command should live on the most appropriate surface:

- frequent + universal → top/global command surface;
- frequent + selection-specific → context/selection surface;
- discoverable but secondary → context menu/overflow;
- power-user/rare → command palette/shortcut;
- destructive → contextual and clearly labelled.

Do not expose every command everywhere merely because the command registry can.

---

## 10. Storage analysis and treemap — P1/P2

### Target experience

Storage analysis should feel like a focused analysis workspace, not a visualization embedded inside normal browsing chrome.

### Improvements

- Clear page/workspace title and active scope.
- Persistent total size / analyzed state near the scope.
- Treemap is primary, but pair it with a textual/category summary where useful.
- Selected tile/category uses shape/border/text in addition to color.
- Hover and keyboard focus must be different states.
- Drill-in/back navigation should be obvious.
- “Calculating” should show useful progress/indeterminate state without covering the visualization unnecessarily.
- Provide a semantic UIA equivalent for information conveyed spatially.

### Acceptance criteria

- The visualization remains understandable for users with color-vision deficiencies.
- Keyboard users can navigate meaningful entities and understand the selected scope/value.

---

## 11. Settings — P2

### Target experience

Settings should be boring in the best way: clear, stable, scannable, and difficult to break under scaling.

### Recommended structure

Use categories only when needed. Within a page:

- section heading;
- setting name;
- optional one-line description;
- aligned control;
- validation/error immediately associated with the setting.

### Improvements

- Create reusable `SettingRow` / `SettingSection` layout rules.
- Avoid manually aligned one-off controls.
- Keep destructive/reset operations in a clearly separated section.
- Theme and appearance options should preview or describe the result accurately.
- Long translations and 200% text/display scaling must not overlap.
- Consider a full settings workspace rather than a cramped modal only if content volume justifies it; do not expand the architecture merely for fashion.

---

## 12. Dialogs, menus, tooltips, and secondary surfaces — P2

Once the primary shell is stable, run a cross-surface consistency pass for:

- conflict dialogs;
- confirmation dialogs;
- context menus;
- tooltips;
- command overflow;
- diagnostics/export surfaces;
- unavailable-volume actions;
- rename editor;
- drag/drop feedback.

Checklist:

- same typography;
- same spacing rhythm;
- same icon family;
- same focus treatment;
- same primary/destructive action semantics;
- correct dark/contrast theme;
- no raw system-light islands;
- correct DPI and text scaling.

---

## Migration priority summary

| Wave | Surfaces | Why |
| --- | --- | --- |
| 1 | shell, top chrome, sidebar, canonical components | changes the perceived quality of the entire app |
| 2 | Home, Column View, tabs, dual pane | core navigation and browsing experience |
| 3 | search, command palette | high-value differentiator and power-user workflow |
| 4 | details/preview, file-operation feedback/conflicts | trust, clarity, and task completion |
| 5 | storage analysis | specialized but highly visible workspace |
| 6 | settings and secondary dialogs | inherit mature system after primary design settles |
| 7 | exhaustive consistency/polish pass | remove residual seams and validate all states |

The implementation plan in the next document turns these waves into engineering gates.