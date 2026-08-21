# 04 — Implementation and Migration Plan

## Constraint

This document defines future implementation. **Nothing in `docs/ui-improvement-roadmap` implements the UI.**

The recommended strategy is an incremental native migration that preserves FastFiles' existing behavior, UIA semantics, virtualization, performance-sensitive code, and engine boundaries.

## Architectural position

### Keep the current native stack for UI v3

FastFiles already has:

- Win32 window/input plumbing;
- Direct2D/DirectWrite rendering;
- DirectComposition-era/native window presentation;
- centralized design tokens;
- shared styling/animation helpers;
- system icon caching;
- UI Automation providers;
- mature navigation state;
- virtualized search;
- performance-oriented engine snapshot consumption.

A framework rewrite would mix two risks at once: **recreating working behavior** and **discovering the desired design**. Do the design work first on the current architecture.

### Re-evaluate framework choice only after UI v3

A later architecture review can compare the measured cost of maintaining the custom native system against a WinUI 3/hybrid migration. That decision should be evidence-driven and separate from the visual redesign.

---

# Phase 0 — Baseline and design proof

**Goal:** establish visual truth before touching production UI.

## Tasks

1. Create a deterministic UI state catalog driven by existing UIA/test seams.
2. Capture representative screenshots for:
   - first-run Home;
   - populated Home;
   - This PC/drive root;
   - normal folder browsing;
   - long file/path names;
   - active and inactive selection;
   - tabs;
   - dual pane;
   - search idle/searching/results/no results/error;
   - details/preview states;
   - file-operation progress/failure;
   - command palette;
   - storage analysis/treemap;
   - settings;
   - conflict dialog;
   - degraded engine state.
3. Repeat key states for light/dark, at minimum 100/150/200% scale, and narrow/normal/wide windows.
4. Produce shell wireframes and high-fidelity reference mockups.
5. Build or plan the native component gallery.
6. Approve component/state specifications from `02-design-system-direction.md`.

## Gate

No broad production styling changes until reviewers can compare **current baseline vs target** for the shell, file rows, Home, search, and narrow layout.

---

# Phase 1 — UI foundation v3

**Goal:** turn existing styling helpers into a strict component/layout foundation.

## Primary code areas

- `src/ui/src/UITheme.h`
- `src/ui/src/UiStyle.*`
- `src/ui/src/UiAnimation.*`
- `src/ui/src/IconCache.*`
- shared layout headers under `src/ui/include/ffui/`
- new native component/helper files only where they remove duplicated behavior

## Work

### 1. Semantic token cleanup

Audit every color/spacing/typography use and map it to named semantic roles. Do not create dozens of near-identical tokens. The objective is a small understandable system.

### 2. Component primitives

Implement shared measurement/render/state logic for the most repeated visual elements:

- icon button;
- navigation item;
- tab;
- breadcrumb segment;
- text/search field styling;
- file/result row state styling;
- state panel;
- status/progress surface;
- focus indicator.

These do not need to become heavyweight object hierarchies. Pure layout/render functions are acceptable where that better matches the existing architecture.

### 3. Density contract

Introduce density-aware metrics as one coherent structure rather than branching on individual magic numbers.

### 4. DPI contract

Audit the current Per-Monitor-V2 path end-to-end. In particular, validate the use of `GetDpiForSystem()` in `UiDpiScale()` against the current window's DPI after monitor transitions. If runtime evidence confirms stale/mismatched scale, establish a per-window DPI source and propagate it through layout/render code.

### 5. Visual-state contract

Centralize state resolution where practical so hover/focus/selected/disabled/High Contrast do not diverge between surfaces.

## Gate

- Component gallery/reference harness demonstrates approved states.
- No new file-local raw UI colors for ordinary themes.
- No regression in High Contrast fallback.
- DPI transition tests exist for geometry-sensitive helpers.
- Shared primitives have headless geometry/state tests where practical.

---

# Phase 2 — Shell, navigation, and responsive layout

**Goal:** make the application look coherent before redesigning individual workspaces.

## Primary code areas

- `WindowShell.cpp/.h`
- `NavigationChrome.cpp/.h`
- `NavigationSidebar.cpp/.h`
- `NavigationWorkspace.cpp/.h` only where presentation needs additional stable state, not for cosmetic churn
- shell/detail layout helpers under `src/ui/include/ffui/`

## Work

1. Implement approved top-shell silhouette.
2. Normalize icon-button family.
3. Redesign tab presentation.
4. Redesign breadcrumb/address transformation.
5. Normalize sidebar hierarchy and selected state.
6. Define command overflow behavior.
7. Implement narrow/normal/wide layout states.
8. Make details-pane visibility/collapse behavior part of those states.
9. Define dual-pane minimum/active-pane presentation.
10. Preserve existing Back/Forward/address/tab/pane history semantics.

## Risks

- accidental shortcut/focus regressions in custom chrome;
- reduced usable column viewport from excessive padding;
- mismatch between Win32 child-control rectangles and D2D layout at non-100% DPI;
- UIA bounds becoming stale after layout changes.

## Gate

The full navigation acceptance sequence must pass by mouse and keyboard, with UIA bounds/names correct at the supported scale/window matrix.

---

# Phase 3 — Home and Column View

**Goal:** redesign the two highest-frequency content surfaces.

## Primary code areas

- `ColumnView.cpp/.h`
- `HomeData.h`
- Home layout/test helpers
- `IconCache.*` only for required presentation changes
- `ColumnViewUiaProvider.*` only when geometry/semantics require updates

## Home work

- implement the approved content hierarchy;
- replace equal-weight clutter with prioritized sections;
- standardize recent/pinned item components;
- robust path trimming and bounds;
- intentional empty sections/first-run state;
- responsive reflow.

## Column work

- apply canonical file-row component;
- unify hover/focus/selection/unfocused-selection states;
- improve active-column and active-pane cues;
- normalize separators and row geometry;
- refine empty/error/loading states;
- preserve culling, smooth scrolling, icon-cache behavior, selection identity, drag/drop, and inline rename.

## Gate

- Home long-path regression case remains fixed.
- Browse screenshots approved at light/dark and representative DPI.
- 10K+ item/large-directory bounded performance does not regress materially from baseline.
- Keyboard and UIA selection semantics remain correct.

---

# Phase 4 — Search and command surfaces

**Goal:** make FastFiles' power-user workflows feel intentional and fast.

## Primary code areas

- `SearchPanel.cpp/.h`
- `SearchUiaProvider.*`
- `CommandPalette.cpp/.h`
- `CommandSystem.cpp/.h`
- `QuickActions.cpp/.h`

## Work

1. Restructure search around query + results.
2. Move filters/sort to responsive secondary controls.
3. Define active-filter representation.
4. Use canonical result-row states.
5. Improve searching/no-results/error/degraded presentations.
6. Retain match highlighting and virtualization.
7. Redesign command palette result hierarchy with shortcuts/categories where useful.
8. Audit command duplication across global toolbar, context menu, quick actions, and palette.

## Gate

- rapid query replacement remains responsive;
- large search result virtualization stays intact;
- result selection/focus matches browse semantics;
- search state is understandable from screenshots without relying on logs;
- all primary search interactions work keyboard-only.

---

# Phase 5 — Details, preview, and file-operation feedback

**Goal:** improve trust and reduce clutter around selection and operations.

## Primary code areas

- `WindowShell::RenderDetails`
- `Preview.cpp/.h`
- details layout/state helpers
- `FileOperations.cpp/.h`
- `FileOperationPolicy.cpp/.h` only when UX requires exposing existing semantic state
- `ConflictDialog.cpp/.h`
- drag/drop presentation paths

## Work

- redesign details hierarchy;
- define multi-selection aggregate presentation;
- create explicit preview unavailable/loading/unsupported states;
- normalize progress/status presentation;
- improve completed/cancelled/failure feedback;
- redesign conflict dialog layout and wording without changing operation semantics;
- ensure drag/drop target visuals use the same focus/selection grammar.

## Gate

File-operation E2E truth tests remain green, while UIA/visual scenarios prove each externally visible operation state.

---

# Phase 6 — Storage analysis, settings, and secondary dialogs

## Storage analysis

Primary files:

- `StorageAnalysis.cpp/.h`
- `TreemapView.cpp/.h`
- `TreemapUiaProvider.*`
- `CategoryEngine.*`

Work:

- clarify analysis scope and status;
- refine treemap selection/focus/hover distinction;
- provide color-independent cues;
- normalize drill navigation and textual summary;
- improve calculating/error/partial states.

## Settings

Primary files:

- `SettingsDialog.cpp/.h`
- settings protocol/model files only where presentation requires already-supported data

Work:

- reusable setting section/row layout;
- improve grouping and descriptions;
- fix scaling/reflow issues found by the validation matrix;
- normalize theme, reset, startup, preview, and other setting controls;
- retain native controls where they are visually/accessibly acceptable.

## Secondary dialogs

Run a final common-component migration across dialogs, menus, tooltips, rename editor, diagnostics/export surfaces, and unavailable-volume flows.

---

# Phase 7 — Hardening and release acceptance

**Goal:** prove UI v3 as a product, not merely as a successful build.

## Required gates

- deterministic visual state catalog reviewed;
- light/dark/Follow System coverage;
- Windows contrast-theme coverage;
- animations on/off;
- 100/125/150/200% minimum scale matrix, with extra coverage where the product supports more;
- monitor-to-monitor DPI transition;
- narrow/normal/wide/dual-pane layout;
- keyboard-only primary workflows;
- Narrator/manual assistive-technology pass;
- UIA automated acceptance;
- long names/paths/localization stress strings;
- large directory/search performance comparison;
- no idle animation timer/resource leak;
- no new visual P0/P1 defects.

---

# Engineering work-packet strategy

The implementation is parallelizable, but **do not parallelize multiple teams across the same shell primitives before Phase 1 is stable**.

Recommended work packets after foundation lands:

1. shell/top chrome;
2. sidebar/navigation adaptive layout;
3. Home;
4. Column View file rows/states;
5. tabs/dual-pane;
6. search;
7. command palette/quick actions;
8. details/preview;
9. file-operation feedback/conflict dialog;
10. storage/treemap;
11. settings/secondary dialogs;
12. visual automation/accessibility matrix.

Each packet should own a limited file set and consume the shared component contracts rather than invent new styles.

---

# Suggested branch/merge discipline for future code work

For the actual implementation, use a dedicated UI-v3 integration branch or small ordered branches off a stable baseline.

Recommended sequence:

1. `ui-v3/foundation`
2. `ui-v3/shell`
3. surface branches based on the merged foundation/shell
4. `ui-v3/hardening`

Keep functional changes separate from pure presentation changes whenever possible. If a UX improvement requires behavior changes, create an explicit OpenSpec/change document so behavioral semantics are reviewable rather than hidden inside styling commits.

---

# Rollback strategy

Avoid a big-bang switch.

During implementation:

- land shared primitives first;
- migrate one surface at a time;
- preserve existing logic paths;
- keep screenshots for before/after comparison;
- use small reversible commits;
- consider a temporary internal/debug UI-v3 toggle only if migration risk warrants it;
- remove old style paths once a surface passes its acceptance matrix, rather than maintaining two designs indefinitely.

---

# File ownership map

| Experience | Primary implementation files |
| --- | --- |
| Shell | `WindowShell.*`, shell/detail layout helpers |
| Theme/system | `UITheme.h`, `UiStyle.*`, `UiAnimation.*` |
| Icons | `IconCache.*` |
| Navigation model | `NavigationWorkspace.*` |
| Navigation chrome | `NavigationChrome.*`, `NavigationSidebar.*` |
| Browse/Home | `ColumnView.*`, `HomeData.h` |
| Browse accessibility | `ColumnViewUiaProvider.*` |
| Search | `SearchPanel.*`, `SearchUiaProvider.*` |
| Commands | `CommandSystem.*`, `CommandPalette.*`, `QuickActions.*` |
| Preview/details | `Preview.*`, `WindowShell::RenderDetails`, detail layout/state helpers |
| File operations | `FileOperations.*`, `ConflictDialog.*`, `OleDragDrop.*` |
| Storage | `StorageAnalysis.*`, `TreemapView.*`, `CategoryEngine.*`, `TreemapUiaProvider.*` |
| Settings | `SettingsDialog.*` |
| Rendering | `Renderer.*` |

This mapping should be used to prevent parallel agents from repeatedly editing the same large files without coordination.

---

# Exit criteria for the entire program

The UI improvement program is complete only when:

1. the target shell/component system is implemented consistently;
2. every primary surface has migrated;
3. old one-off visual paths are removed or explicitly justified;
4. visual snapshots, accessibility, UIA, DPI, theme, keyboard, and performance gates pass;
5. runtime dogfooding finds no unresolved P0/P1 visual/interaction defects;
6. documentation is updated to reflect the actual final component/layout contracts.

The key principle is simple: **migrate the product systemically, not cosmetically.**