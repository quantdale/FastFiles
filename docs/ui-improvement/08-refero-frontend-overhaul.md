# 08 — Refero-Grounded Frontend Overhaul

**Status:** active implementation direction  
**Scope:** native FastFiles frontend/UI only; file-management behavior, engine boundaries, virtualization, UIA semantics, and operation semantics remain unchanged.

## Executive direction

FastFiles should feel like a precise desktop instrument rather than a collection of separately modernized surfaces. The existing UI-v3 architecture is strong: semantic tokens, shared state painters, accessibility providers, native rendering, density profiles, and deterministic visual capture already exist. The next wave is therefore an experience-architecture pass, not a framework rewrite.

The target is a calm, compact, information-first Windows file manager with:

- content visually stronger than chrome;
- one disciplined blue interaction accent;
- neutral surface hierarchy instead of card/border proliferation;
- consistent geometry for every repeated control;
- adaptive rather than compressed narrow-width behavior;
- explicit focused/unfocused selection and active-pane cues;
- progressive disclosure for secondary controls and metadata;
- high-contrast, keyboard, UIA, DPI, and reduced-motion behavior preserved as product contracts.

## Refero research used

The implementation direction was checked against real product references through the connected Refero MCP rather than inferred from generic design trends.

### Cal.com — monochrome utility and hierarchy

Useful patterns:

- strict neutral hierarchy;
- restrained single accent;
- lightweight elevation;
- clear distinction between primary and secondary actions;
- compact, predictable geometry.

FastFiles adaptation: retain native small radii rather than copying Cal.com's pill-heavy marketing controls. Use its restraint, not its shape language.

### Perplexity — quiet productivity workspace

Useful patterns:

- off-white canvas with white interactive surfaces;
- low visual noise;
- strong search prominence;
- compact navigation;
- hierarchy mostly through tone and typography rather than shadows.

FastFiles adaptation: light mode now uses a subtly tinted canvas and white work surfaces, with lowered border contrast.

### shadcn/ui — precise neutral component geometry

Useful patterns:

- small coherent radius set;
- subtle 1px structural separation;
- compact 8px rhythm;
- restrained typography weights;
- minimal decorative treatment.

FastFiles adaptation: keep the existing 4/6/8-DIP native geometry contract and avoid turning every control into a card or pill.

### Linear — layered dark workspace

Useful patterns:

- deep neutral canvas;
- closely spaced surface levels;
- strong text hierarchy;
- compact control density;
- elevation expressed through tone more than diffuse shadow.

FastFiles adaptation: dark mode adopts calmer charcoal layers while keeping blue as the product interaction color instead of copying Linear's marketing accent.

### Dropbox / document-management references — file-centric navigation

Useful patterns:

- persistent orientation sidebar;
- file content as the dominant visual field;
- search and navigation remain globally discoverable;
- secondary metadata is visually subordinate.

FastFiles adaptation: details and secondary surfaces must yield space before the primary browse workspace becomes cramped.

### Command-palette references — Runey, fal.ai, Relume

Useful patterns:

- focused elevated search surface;
- dimmed or quiet surrounding context;
- keyboard shortcut visibility;
- clear result hierarchy.

FastFiles adaptation: command/search workspaces should reuse shared row states and keep query/results dominant over filters and chrome.

## Current frontend architecture

FastFiles is a native C++20 Win32 application. The UI target includes:

- `WindowShell` — composition, workspace switching, details/preview, global input and status;
- `NavigationChrome` — tabs, breadcrumb/address mode, back/forward/up, Fast Search, settings;
- `NavigationSidebar` — Home, search, storage, known folders, drives, bookmarks, collapsed rail;
- `ColumnView` — primary Finder-style browsing surface;
- `SearchPanel` — virtualized search workspace;
- `CommandPalette` / `CommandSystem` / `QuickActions` — power-user command surfaces;
- `StorageAnalysis` / `TreemapView` — storage visualization;
- `SettingsDialog`, `ConflictDialog` — secondary configuration/trust surfaces;
- `UITheme`, `UiStyle`, `UiComponents`, `UiAnimation` — visual foundation;
- UIA providers for browse, search, treemap and shell-host semantics.

Rendering uses Direct2D/DirectWrite and DirectComposition-era native presentation, with Win32 child controls where appropriate. Dependencies include D3D11, DXGI, D2D1, DWrite, DComp, WIC, Shell32, OLE, common controls, common dialogs, and UI Automation Core.

## Key UX problems to solve

### 1. Surface cohesion

The major surfaces already share tokens but still have enough independent layout/painting responsibility that visual drift can occur. The solution is stricter component/state contracts, not additional one-off styling.

**Files:** `UITheme.h`, `UiComponents.h`, `NavigationChrome.cpp`, `NavigationSidebar.cpp`, `ColumnView.cpp`, `SearchPanel.cpp`, `WindowShell.cpp`.

### 2. Secondary UI can compete with primary file content

Details, filters, tabs, chrome, side navigation, and status affordances must not collectively reduce the useful browse area below a productive threshold.

**Implemented in this branch:** adaptive details presentation now chooses full / disclosure rail / hidden states and applies a stricter dual-pane width contract.

**Files:** `ffui/DetailsLayout.h`, `WindowShell.cpp`, `tests/ui/test_details_layout.cpp`.

### 3. Visual palette was slightly more Windows-default than product-specific

The previous palette was sound and accessible but could still read as a collection of Windows-modernized surfaces rather than one deliberate FastFiles workspace.

**Implemented in this branch:** restrained neutral palette with quieter borders/hover overlays, more coherent dark layers, subtly tinted light canvas, and one blue interaction accent.

**Files:** `UITheme.h`, all token-consuming surfaces automatically inherit it.

### 4. Responsive shell behavior is still threshold-based in several surface files

Examples include a fixed 620-DIP search-collapse breakpoint and fixed sidebar widths. The end state should make narrow/normal/wide behavior an explicit shared policy rather than scattered `if (width < ...)` branches.

**Next files:** `NavigationChrome.cpp/.h`, `NavigationSidebar.cpp/.h`, `ffui/ShellLayout.h`, new pure layout policy helpers and matching tests.

### 5. Search can expose too much form chrome

Search should read as query + results first. Filters and sorting should collapse into progressive disclosure at constrained widths.

**Next files:** `SearchPanel.cpp/.h`, `SearchUiaProvider.*`, shared chip/state-panel primitives.

### 6. Home must prioritize launching work

Pinned and recent locations should dominate; known folders and utility actions should be visually quieter. Long paths must always be bounded.

**Next files:** `ColumnView.cpp/.h`, `HomeData.h`, display-path helpers and visual-catalog scenarios.

### 7. Visual correctness needs release-level enforcement

The repository already captures deterministic screenshots and has a planned visual-intelligence pipeline. The remaining work is to make before/after visual review and perceptual/geometry regression part of the frontend acceptance loop.

**Next files:** `verify/visual/*`, visual catalog scenarios, component gallery.

## Complete overhaul plan

### Wave 1 — Foundation and global visual language

**Status: in progress / partially implemented**

1. Refine semantic palette while preserving contrast guarantees.
2. Keep native 4/6/8-DIP geometry; reserve pills for chips.
3. Maintain compact typography and regular/semibold hierarchy.
4. Keep hover subtle, pressed immediate, keyboard focus independent.
5. Preserve High Contrast system-color fallbacks.
6. Extend tests around token contrast and layout policies.

### Wave 2 — Shell and navigation

**Priority: P0**

1. Introduce a shared chrome layout policy with narrow/normal/wide states.
2. Reduce permanent chrome at constrained widths by moving lower-priority commands to overflow/palette.
3. Keep breadcrumb/address as the primary location signal.
4. Normalize tabs, icon buttons, search affordance, and focus treatment.
5. Auto-collapse the sidebar only through an explicit product rule, never incidental clipping.
6. Preserve tab/history/address/shortcut semantics exactly.

Acceptance:

- current location, active tab, active pane, and active workspace readable at a glance;
- no overlap at supported window sizes;
- keyboard focus visible in every state;
- UIA bounds updated with every layout transition.

### Wave 3 — Browse / Home / dual-pane

**Priority: P0**

1. Apply one canonical FileRow state grammar everywhere.
2. Make active-pane identity clear without duplicating toolbars.
3. Keep separators low-contrast until interaction needs emphasis.
4. Rework Home to pinned → recents → recent searches → utilities.
5. Ensure path/name ellipsis and tooltip/UIA full-value access.
6. Make secondary details yield space before file content.

Acceptance:

- active vs inactive selection is unmistakable without color alone;
- long names/paths/localization do not escape bounds;
- dual pane never leaves two unusably narrow primary panes merely to preserve a secondary metadata card;
- large-directory culling and scrolling performance remain intact.

### Wave 4 — Search and command surfaces

**Priority: P0/P1**

1. Query field is the dominant control.
2. Active filters become concise chips/summary; inactive filters move behind progressive disclosure when constrained.
3. Results reuse canonical row selection/focus behavior.
4. Searching/no-results/error/degraded states use the common StatePanel grammar.
5. Command palette exposes shortcut/category metadata without becoming a menu dump.

Acceptance:

- search state understandable visually without logs;
- filters never crowd the query at narrow width;
- thousands of results remain virtualized;
- keyboard-only usage remains first-class.

### Wave 5 — Details, operations, conflicts

**Priority: P1**

1. Preview + highest-value 4–6 metadata fields first.
2. Advanced metadata uses progressive disclosure.
3. Loading/unsupported/too-large/permission states are intentional StatePanels.
4. Progress surfaces distinguish queued/active/completed/cancelled/failed.
5. Conflict actions use explicit verbs and predictable defaults.

Acceptance:

- no stale details during selection changes;
- operation state is always understandable;
- destructive meaning never depends on red alone.

### Wave 6 — Storage, settings, dialogs

**Priority: P2**

1. Storage view emphasizes scope, selected item, total/selected size, and drill navigation.
2. Treemap hover/focus/selection have separate non-color cues.
3. Settings migrates to reusable section/row geometry.
4. Secondary dialogs inherit the same action-row, typography, spacing, and focus rules.

### Wave 7 — Visual acceptance and release hardening

1. Component Gallery coverage: light/dark/high-contrast, compact/comfortable, focus/hover/pressed/disabled, 100/125/150/200% DPI.
2. Visual catalog: narrow/normal/wide, tabs, dual pane, Home, search states, details states, storage, settings, palette, conflict dialog.
3. Perceptual comparison + geometry assertions.
4. Vision-based critique classifies objective defects separately from subjective polish suggestions.
5. Every visual repair closes with the same scenario captured before/after.

## Implementation file map

| Area | Primary files | Intent |
| --- | --- | --- |
| Global tokens | `src/ui/src/UITheme.h` | palette, typography, radius, spacing, density |
| Shared visual states | `src/ui/src/UiComponents.h`, `UiStyle.*` | canonical component rendering/state resolution |
| Shell geometry | `src/ui/include/ffui/ShellLayout.h`, `DetailsLayout.h`, `WindowShell.*` | responsive workspace allocation |
| Top chrome/tabs | `NavigationChrome.*` | navigation hierarchy, tabs, search affordance, overflow |
| Sidebar | `NavigationSidebar.*` | orientation, collapse behavior, sections, focus |
| Browse/Home | `ColumnView.*`, `HomeData.h` | primary file workspace and launch surface |
| Search | `SearchPanel.*`, `SearchUiaProvider.*` | query/results-first workspace |
| Commands | `CommandPalette.*`, `CommandSystem.*`, `QuickActions.*` | fast keyboard-centric actions |
| Details/preview | `WindowShell::RenderDetails`, `Preview.*` | selection information with progressive disclosure |
| Operations | `FileOperations.*`, `ConflictDialog.*` | trust, progress, conflicts, failure feedback |
| Storage | `StorageAnalysis.*`, `TreemapView.*` | hierarchy, scope, selection, accessibility |
| Settings | `SettingsDialog.*` | reusable sections/rows and responsive alignment |
| Visual validation | `verify/visual/*`, `Gallery.*`, UI tests | pixel/geometry acceptance alongside UIA |

## Non-negotiable preservation constraints

- no framework rewrite during UI v3;
- no behavioral changes to file operations merely for visual polish;
- preserve virtualization/culling and avoid synchronous shell metadata work in paint/input paths;
- preserve UI Automation identity unless a semantic change is intentional and tested;
- preserve High Contrast and system animation settings;
- no raw per-file theme colors where a semantic token exists;
- no decorative animation that increases input latency;
- no pixel-only test may replace filesystem, semantic UIA, or functional truth.

## Changes already implemented by this overhaul branch

1. **Global visual language** — `UITheme.h` now carries the Refero-grounded neutral palette and reference-lock rationale. All token-consuming Direct2D and owner-drawn Win32 surfaces inherit the update.
2. **Adaptive details presentation** — `DetailsLayout.h` now emits `Hidden`, `DisclosureRail`, or `Full` presentations, bounds the full details width to 280–320 DIP, and protects a 720-DIP primary workspace contract in dual-pane mode.
3. **Responsive contract tests** — `test_details_layout.cpp` now covers presentation classification, constrained dual-pane behavior, bounded wide-layout details width, and DPI-invariant presentation.

These changes deliberately alter presentation and space allocation only; navigation, indexing, search semantics, file operations, UIA identity, and engine behavior remain untouched.