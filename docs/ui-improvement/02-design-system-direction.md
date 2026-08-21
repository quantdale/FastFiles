# 02 — FastFiles UI v3 Design-System Direction

## Design thesis

FastFiles should not chase “Fluent” as a collection of effects. It should use Windows conventions to produce a **calm, compact, high-performance file-management workspace**.

The visual identity should come from:

- exceptional alignment and spacing;
- strong information hierarchy;
- restrained use of accent color;
- clear selection/focus states;
- excellent file and folder presentation;
- consistent command placement;
- deliberate empty/error/loading states;
- native typography and icons;
- smooth but minimal motion;
- coherent behavior across light, dark, and contrast themes.

The existing `UITheme`, `UiStyle`, `UiAnimation`, and `IconCache` are the implementation foundation. UI v3 adds a stricter product contract on top.

## 1. Core visual principles

### Content before chrome

Files, folders, paths, results, and selection should be the strongest visual elements. Sidebars, cards, separators, and backgrounds should support orientation without competing for attention.

### One accent, many neutrals

Use the accent primarily for:

- active selection;
- keyboard focus when appropriate;
- primary action emphasis;
- meaningful progress/active state;
- links or active navigation markers.

Do not paint large portions of the shell in accent simply to make the product feel branded.

### Elevation only when it explains structure

Not every region needs a card. Use surface/elevation differences only where they establish a real layer:

- flyouts and menus;
- command palette;
- modal dialogs;
- preview/detail overlays where applicable;
- temporary status/progress surfaces.

Primary workspace areas should usually be separated by spacing, subtle dividers, or background tone rather than nested cards.

### Geometry is systematic

The existing 4-DIP and 8-DIP radii are suitable. Define exactly which class uses which radius instead of selecting a radius per file.

Recommended contract:

| Element | Radius direction |
| --- | --- |
| file/search rows | 4 DIP or visually equivalent small radius |
| icon/text buttons | 4–6 DIP |
| search/address fields | 6–8 DIP |
| cards/panels/flyouts | 8 DIP |
| chips/badges | half-height/pill only when the semantic form is a chip |

Avoid turning every control into a pill.

## 2. Spacing and layout grid

Microsoft's Windows design guidance uses consistent spacing and a multiple-of-four layout rhythm. FastFiles already has `2/4/8/16/24` DIP tokens. Keep that foundation but reduce arbitrary local spacing.

Recommended semantic mapping:

- `2 DIP` — hairline/micro optical correction only; not a normal layout gap.
- `4 DIP` — tightly related icon/text or compact internal spacing.
- `8 DIP` — standard control-to-control gap.
- `12 DIP` — useful intermediate label/control separation; add as a token if repeated.
- `16 DIP` — section/internal panel padding.
- `24 DIP` — separation between major content groups.
- `32 DIP` — optional major page-level separation, used sparingly.

All primary shell geometry should be expressible through named metrics rather than raw numbers inside surface files.

## 3. Typography

Microsoft's current Windows guidance recommends Segoe UI Variable and a hierarchy based around 12, 14, 18, 20, 28 DIP/effective-pixel sizes. FastFiles' existing 12/14/16/20 ramp is close but can be made more semantic.

Recommended FastFiles roles:

| Role | Suggested treatment | Use |
| --- | --- | --- |
| Caption | 12 regular | secondary metadata, helper text, timestamps |
| Body | 14 regular | file names, standard labels, settings descriptions |
| Body strong | 14 semibold | selected/high-value labels, section labels where needed |
| Subtitle | 16–18 semibold | Home group headings, dialog subheads |
| Page title | 20 semibold | Settings/Storage/large secondary workspace titles |
| Large title | 28 semibold | rare; only where the surface truly needs a page title |

Rules:

- Use sentence case for UI labels.
- Prefer regular and semibold; avoid heavy bold as the default hierarchy tool.
- Left-align almost all functional text.
- Use ellipsis for constrained names/paths, with the full value available via tooltip/details/UIA where useful.
- Never solve hierarchy only by reducing secondary text to unreadably small sizes.
- File names should remain the most readable text in the core browsing surface.

## 4. Density model

A file manager benefits from more than one density, but the design must be coherent before exposing it as a user setting.

### Compact profile

Designed for keyboard/mouse power users and large directories.

- file/search row: roughly 28 DIP;
- command targets: no smaller than a deliberate usable target area;
- icon: 16 DIP;
- reduced vertical gaps;
- metadata remains readable.

### Comfortable profile

Designed for general use, high-DPI screens, touchpad/touch comfort, and reduced precision.

- file/search row: roughly 32–36 DIP;
- larger icon/command hit targets where appropriate;
- more vertical breathing room;
- same information architecture, not a different app.

The exact numbers should be validated in the native component gallery and on actual Windows scale factors. WCAG 2.2's 24×24 target-size minimum is a useful accessibility benchmark for pointer targets even though FastFiles is a native desktop app rather than web content.

## 5. Color system

The existing theme object should evolve toward fully semantic roles.

### Recommended semantic groups

**Surfaces**

- app background;
- primary workspace;
- secondary workspace/sidebar;
- elevated surface;
- transient/flyout surface;
- subtle hover surface;
- divider/border.

**Text**

- primary;
- secondary;
- tertiary/disabled;
- on-accent;
- link/action.

**Interaction**

- accent;
- hover overlay;
- pressed overlay;
- focused selection;
- unfocused selection;
- focus stroke.

**Status**

- success;
- warning/degraded;
- error/destructive;
- informational;
- progress.

Status colors require text/icon/shape redundancy. For example, engine-degraded state should never be communicated only through amber.

### Contrast requirements

Use Microsoft's Windows accessibility guidance as the native baseline and WCAG 2.2 ratios as a measurable cross-check:

- normal text: target at least 4.5:1 against its background;
- large text: at least 3:1;
- important non-text component boundaries/focus indicators should remain clearly distinguishable;
- contrast themes must use Windows/system-aware colors instead of forcing normal-theme tokens.

## 6. Iconography

Use two distinct icon sources intentionally:

1. **File/folder/type identity:** shell/system file icons through the existing icon cache.
2. **Commands/navigation:** Segoe Fluent Icons when available, with the existing compatibility fallback where necessary.

Rules:

- Do not mix arbitrary Unicode symbols with Fluent glyphs for primary commands.
- Use icons with labels for unfamiliar or destructive commands.
- Tooltips/accessibility names remain required for icon-only commands.
- Keep command icon sizing on known crisp sizes, typically 16 or 20 DIP for the shell.
- Use the same glyph for the same command everywhere unless context genuinely changes meaning.

## 7. Component vocabulary

UI v3 should formalize a small native component library. It does not need a new framework; it needs shared rendering/measurement/state contracts.

### IconButton

States: rest, hover, pressed, keyboard focus, disabled, toggled/active.

Contract:

- consistent target size;
- centered Fluent glyph;
- tooltip/accessibility name;
- no persistent border at rest unless needed for grouping;
- visible focus independent of hover.

### TextButton / PrimaryButton

Use text buttons for dialog/action surfaces. Reserve a filled primary treatment for the single highest-priority action in a modal/empty state.

### NavItem

For Home/known folders/bookmarks/storage/settings navigation:

- icon + text;
- clear active state;
- optional disclosure/section behavior;
- hover must not look identical to selected;
- keyboard focus must not depend on selected state.

### Tab

Contract should define:

- active/inactive hierarchy;
- hover;
- close affordance only when discoverable/hovered or focused;
- dirty/busy state only if such semantics are ever introduced;
- truncated path/name behavior;
- keyboard focus and Ctrl+Tab ordering.

### BreadcrumbSegment

- current segment has reduced “button” appearance unless interactive state requires it;
- separators are visually quiet;
- path editing mode is an intentional transformation, not a separate-looking subsystem;
- long paths collapse intelligently without losing current location.

### SearchField / AddressField

Shared text-field geometry and focus style, but distinct purpose/placeholder/iconography.

### FileRow / SearchResultRow

One state model should govern both:

- rest;
- hover;
- focused;
- selected focused;
- selected unfocused;
- cut/ghosted where relevant;
- drop target;
- error/unavailable where relevant.

The search result may add location/match metadata, but selection semantics should still feel like the browse surface.

### Section / Card

Use cards only for discrete grouped content. A Home screen with every item inside a heavy card grid should be avoided unless usability testing proves it superior.

### StatePanel

Reusable presentation for empty, loading, error, permission, degraded, and unavailable states:

- optional icon;
- short title;
- concise explanation;
- one primary recovery action when possible;
- optional secondary action/details;
- UIA/live announcement policy.

### StatusChip

For compact persistent state such as degraded engine mode. Chips should be rare and semantic.

### ProgressSurface

For file operations or long-running storage analysis:

- clear operation name/status;
- progress when meaningful;
- cancel action only while valid;
- completion/failure feedback;
- no misleading progress animation when progress is indeterminate.

## 8. Interaction-state matrix

Every reusable component specification should include the following matrix where applicable:

| State | Visual requirement |
| --- | --- |
| Rest | lowest necessary chrome |
| Hover | subtle surface/opacity change; never the only indication of interactability |
| Pressed | immediate stronger feedback than hover |
| Keyboard focus | explicit high-contrast focus indicator |
| Selected + focused | primary selected treatment plus focus cue when needed |
| Selected + unfocused | distinguishable softer selected treatment |
| Disabled | reduced prominence while retaining readable label if visible |
| Busy | progress/busy cue without blocking unrelated commands |
| Destructive | warning semantics near the action, not red everywhere |
| Drop target | strong but unambiguous destination affordance |
| High Contrast | system-aware colors/shapes; no translucent-only state |

## 9. Motion

Microsoft's Windows guidance describes motion as reactive, direct, and context-appropriate. FastFiles already respects the Windows client-area animation setting. Keep that behavior.

Recommended use:

- hover/press transitions: extremely short;
- selection/navigation transitions: short and direct;
- panel reveal/collapse: only when it improves spatial understanding;
- theme transition: optional/subtle;
- scrolling: smooth only if it preserves precise control and low latency.

Do not animate:

- every row entering large directories;
- search results continuously as the result set streams;
- destructive confirmation simply for spectacle;
- progress in ways that make the UI feel slower.

Reduced/system-disabled animation must snap immediately to final state.

## 10. Responsive shell states

Design for **available window width**, not physical display size.

Suggested conceptual states; exact breakpoints must be derived from content minima and validated in FastFiles rather than blindly copying XAML breakpoints.

### Narrow

- compact/collapsed sidebar;
- details pane hidden or transient;
- lower-priority toolbar commands in overflow;
- breadcrumb collapses intermediate segments;
- Home becomes one-column/list-oriented;
- search filters move into a flyout/secondary row on demand;
- dual-pane disabled or requires a larger minimum width.

### Normal

- standard sidebar;
- primary commands visible;
- details pane optional;
- Home can use a restrained two-column arrangement where content warrants it.

### Wide

- persistent sidebar;
- details/preview visible without crowding;
- richer metadata columns/sections where useful;
- dual-pane comfortable.

### Dual-pane

Dual-pane is its own layout state, not merely two normal panes squeezed in half. It should reduce secondary chrome per pane and make the active pane unmistakable.

## 11. Component gallery before broad implementation

Build a **native FastFiles Component Gallery** during the future implementation phase rather than adding Storybook or a web runtime dependency.

The gallery should display every component and state in:

- light;
- dark;
- contrast theme where automatable;
- compact and comfortable density;
- multiple DPI scales;
- enabled/disabled animation.

It can be a debug-only native surface or deterministic rendering harness. Its purpose is to make visual decisions reviewable without navigating the full app.

## 12. Design-system acceptance gate

Do not begin broad surface migration until the following are approved in the gallery or mockups:

- shell spacing and typography;
- icon-button family;
- nav item;
- tab;
- breadcrumb/address field;
- file/search row;
- focus treatment;
- selected focused/unfocused treatment;
- StatePanel;
- progress/status surface;
- dialog action row;
- narrow/normal/wide shell diagrams.

A polished component system will eliminate more visible inconsistency than another file-by-file styling pass.