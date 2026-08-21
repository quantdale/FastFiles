# 01 — Current-State UI/UX Audit

## Audit scope and confidence

This audit is based on the current `main` tree, including the native UI implementation under `src/ui/`, the current UI design tokens, the completed `modernize-ui-appearance` OpenSpec change, navigation/workspace structures, the Per-Monitor-V2 manifest, UI Automation infrastructure, and the 2026-08-21 dogfood ledger.

This is primarily a **source-informed UX audit**. Where a point is not backed by executed visual evidence, it is labelled as a risk or hypothesis rather than a confirmed visual defect.

## Architecture in one paragraph

FastFiles is a native Win32 C++ desktop application whose main shell combines Direct2D/DirectWrite rendered surfaces with Win32 child controls. `WindowShell` coordinates navigation, `ColumnView`, search, command palette, file operations, preview/details, storage analysis, settings, sidebar/chrome, icon caching, animation, theming, and UIA geometry. The design deliberately avoids IPC on every keystroke by consuming engine snapshots and preserves virtualization/culling in high-volume surfaces.

That architecture is capable of excellent UI. The problem is not that FastFiles lacks modern rendering technology. The harder issue is that a large amount of product interaction and presentation responsibility is spread across custom surface implementations.

## What is already good and should be preserved

### 1. A real design-token foundation exists

`src/ui/src/UITheme.h` is already the intended single source of truth for colors, typography metrics, spacing, radii, and interaction overlays. It contains semantic colors such as `surfaceElevated`, `surfaceSubtle`, `hoverOverlay`, `pressOverlay`, `selectionSoft`, `dividerSubtle`, and `focusStroke`, plus a spacing and typography ramp.

**Preserve:** the semantic-token model. UI v3 should extend or rationalize it, not return to file-local color/size constants.

### 2. The previous modernization was substantial

The completed `openspec/changes/modernize-ui-appearance/tasks.md` records implementation of:

- theme/design tokens;
- shared style helpers;
- renderer device-loss handling;
- Windows 11 window treatment;
- animation/reduced-animation gating;
- real system file icons;
- rounded selection and hover states;
- smooth scrolling;
- themed treemap;
- themed/virtualized search results;
- themed navigation/sidebar/command palette/dialogs;
- details-card and file-operation progress treatment;
- High Contrast fallback behavior.

This is important: **UI v3 must not waste effort re-implementing completed primitives.**

### 3. Accessibility infrastructure is far ahead of many custom-drawn apps

The repository has dedicated providers for column view, search, and treemap plus shared UIA support infrastructure. The current dogfood campaign reports successful semantic UIA runs for core navigation/search/selection scenarios.

**Preserve:** UI Automation as a first-class contract. Visual redesigns should keep semantic identity stable unless intentionally changed.

### 4. Performance-sensitive decisions are already sound

The code uses Direct2D/DirectWrite, bounded/off-thread icon loading, result virtualization, row/column culling, immutable preview handoff, and shared engine snapshots. These are valuable constraints.

**Preserve:** visual improvements must not introduce per-frame allocations, synchronous shell metadata work on paint/input paths, or unbounded animation invalidation.

### 5. Navigation capability is mature

`NavigationWorkspace` already supports:

- Home and filesystem locations;
- Back/Forward history;
- breadcrumbs and editable address mode;
- tabs and reopen-closed-tab;
- dual pane;
- active-pane state;
- bookmarks;
- known folders;
- recent folders;
- persisted workspace state.

The UI redesign should make these capabilities clearer rather than inventing a different navigation model.

## Why the UI can still feel bad despite the completed modernization

The prior change solved many **styling primitives**, but visual quality is an emergent property of the entire composition. A product can have rounded corners, Mica, correct icons, hover states, and dark mode and still look weak if hierarchy, density, spacing, content grouping, alignment, control prominence, and state transitions are inconsistent.

The next effort should therefore move from **surface styling** to **experience architecture**.

## Main current-state problems and risks

### P0 — No durable visual acceptance system

The strongest repository evidence that the UI still has problems is the recent dogfood finding `UX-001`: long recent-folder paths on Home visibly overflowed into adjacent cards. It was fixed, but the failure demonstrates that semantic/UIA correctness does not guarantee visual correctness.

The same dogfood matrix still lists light/dark/High Contrast/reduced-motion and 100/125/150/200% DPI visual coverage as pending.

**Problem:** visual regressions can survive even when code, unit tests, and UIA semantics pass.

**Required direction:** a deterministic screenshot/state catalog must become a release gate.

### P0 — Shell composition is not governed by a single design contract

`WindowShell` coordinates a large number of independent presentation surfaces: navigation chrome, sidebar, columns, details, search, storage analysis, command palette, file operations, and dialogs. Although they share tokens, they do not automatically share hierarchy rules.

Symptoms to inspect and normalize:

- competing surfaces with similar visual weight;
- too many borders/dividers/elevated regions;
- inconsistent control heights and alignment baselines;
- top chrome consuming excessive vertical attention;
- sidebar, tabs, breadcrumb, address controls, and details pane all appearing as separate widgets rather than one shell;
- inconsistent use of accent color.

These are **design-system risks**, not claims that every item is currently broken.

### P0 — Component consistency is implicit rather than explicit

FastFiles has style helpers, but the product lacks a documented component catalog that says exactly how a FastFiles icon button, breadcrumb, nav item, tab, file row, empty state, error state, or status chip behaves in every visual state.

When each custom-drawn file implements interaction presentation independently, small differences accumulate.

**Required direction:** define components before another broad repainting wave.

### P0 — Information hierarchy needs a task-first redesign

FastFiles is feature-rich: Home, filesystem browsing, bookmarks/recents, tabs, dual pane, search/filter/sort, details/preview, command palette, storage analysis, settings, and engine/degraded status. The design must clearly answer at every moment:

1. Where am I?
2. What is selected?
3. What can I do next?
4. Is the app busy, degraded, or in an error state?
5. What changed as a result of my last action?

Any surface that makes these answers visually ambiguous should be simplified, even if it is technically “modern.”

### P1 — Density is fixed rather than productized

`UiMetrics` currently exposes fixed values such as a 28-DIP row height, 24-DIP control height, 16-DIP icon size, and a 72-DIP chrome height.

A desktop file manager has competing needs: high information density and comfortable click targets. Treating density as a coherent mode is stronger than tuning isolated constants.

**Recommended direction:** define at least two density profiles at the design-contract level:

- **Compact:** optimized for keyboard/mouse and large file sets.
- **Comfortable:** larger rows/targets for general use and higher accessibility comfort.

Do not add a setting until the layouts can actually support both cleanly.

### P1 — Responsive behavior needs explicit state transitions

The current shell already has layout logic and a details-pane disclosure behavior. However, responsive UX should be specified at the product level rather than emerging from minimum widths.

Define what happens when the window becomes narrow:

- sidebar becomes compact/collapsed;
- lower-priority commands move to overflow;
- address/breadcrumb treatment changes deliberately;
- details pane becomes overlay/disclosure or closes;
- dual pane either enforces a sensible minimum or changes presentation;
- Home cards reflow from multiple columns to one;
- search filters collapse into a filter flyout rather than crowding the query row.

### P1 — Per-monitor DPI handling deserves a focused audit

The application manifest declares `PerMonitorV2`, and `WindowShell` handles `WM_DPICHANGED`. That is correct directionally.

However, `UITheme.h::UiDpiScale()` currently derives scale via `GetDpiForSystem()`, while the app is per-monitor aware. `WindowShell` uses `UiScale()` for multiple layout calculations.

This is a **technical risk to verify**, not a confirmed bug: after moving the app between monitors with different scale factors, ensure every Win32 and Direct2D surface derives layout from the current window/monitor DPI rather than the process/system DPI. If a mismatch is observed, the future implementation should introduce a per-window DPI source instead of relying on the system value.

### P1 — Mixed custom-drawn and stock Win32 controls can expose visual seams

The previous modernization deliberately retained stock inner controls in some dialogs. That was a reasonable incremental trade-off, but UI v3 should inspect whether those controls now visibly conflict with the rest of the app, particularly in dark mode, focus states, spacing, typography, and DPI transitions.

Do not automatically replace stock controls. Replace or owner-draw only where the visual or accessibility benefit outweighs maintenance cost.

### P1 — Error/loading/empty/degraded states need one grammar

FastFiles contains many stateful surfaces:

- no search results;
- search in progress;
- unavailable/permission-denied path;
- engine degraded/unavailable;
- preview unavailable/too large;
- storage calculation in progress;
- empty Home sections;
- file-operation failures/conflicts/cancellation;
- unavailable volumes.

These states should share a common presentation model:

- concise title;
- optional explanatory copy;
- relevant recovery action;
- optional secondary detail;
- icon/status semantics;
- consistent live-region/UIA notification behavior when state changes.

### P1 — The Home screen needs to earn its existence

Home currently aggregates pinned locations, recent folders, recent searches, known folders, and actions. That can become visually busy quickly.

The recent path-overflow defect is a symptom of a broader requirement: Home must prioritize **launching work**, not merely display every available shortcut source.

Recommended hierarchy:

1. Quick access/pinned locations.
2. Recent folders.
3. Recent searches only when useful/history exists.
4. Secondary actions/known locations with reduced prominence.

Avoid a dashboard aesthetic with too many equally weighted cards.

### P1 — Search should be a workspace, not a form

Search is one of FastFiles' differentiators. The visual model should emphasize query and results, while filters/sort become progressive disclosure rather than permanent chrome when space is constrained.

Key risks to review:

- filter controls competing with the query box;
- weak distinction between result name, location, metadata, and match highlight;
- state changes that do not clearly indicate searching vs completed vs stale results;
- selection/focus visuals differing from file browsing.

### P2 — Settings is likely carrying too much custom layout responsibility

`SettingsDialog.cpp` is a large custom implementation. Large settings surfaces commonly degrade through accumulated one-off rows, hardcoded alignment, and insufficient scaling/reflow.

UI v3 should treat Settings as a small navigation/content system with reusable setting rows, descriptions, validation/error placement, section headings, and consistent control alignment.

### P2 — Storage analysis needs semantic visual hierarchy beyond colorful rectangles

Treemaps are inherently information-dense. The treemap should not depend on color alone and should clearly expose:

- current scope/path;
- total/selected size;
- selected category/item;
- hover/focus/selection distinction;
- drill-in/back affordance;
- calculating/partial/error states;
- accessible semantic equivalent through UIA.

### P2 — Visual polish should not create latency debt

Any future use of backdrop, shadows, hover animation, cross-fades, preview effects, or richer icons must stay subordinate to input responsiveness. A file manager that looks excellent but feels delayed is a regression.

## What not to do next

1. **Do not launch another repository-wide “round every rectangle” pass.** The earlier modernization already did that class of work.
2. **Do not rewrite the UI framework first.** It would risk search/navigation/file-operation behavior and performance before the desired design is even validated.
3. **Do not judge success by task checkboxes.** Judge it with visual baselines, task completion, accessibility evidence, and perceived coherence.
4. **Do not polish settings and secondary dialogs before fixing the shell.** The shell dominates perceived quality.
5. **Do not use color as the primary state carrier.** Selection, focus, degraded status, errors, and progress need redundant cues.

## Highest-value next design questions

Before implementation begins, resolve these in mockups or a native component gallery:

- What is the exact FastFiles shell silhouette at 1280×800, 1920×1080, and a narrow snapped width?
- What is the default density profile?
- How much top chrome is truly necessary?
- How do tabs, breadcrumb/address, Back/Forward, search, and global commands share the top region?
- When does the sidebar collapse?
- When does the details pane disappear, overlay, or become a disclosure surface?
- What is the canonical file-row selection/focus treatment shared by browsing and search?
- What are the canonical empty/error/loading components?
- Which information belongs on Home versus the sidebar?
- Which command surfaces are persistent, contextual, or overflow-only?

UI v3 should not enter broad implementation until these questions have concrete visual answers.