# Semantic UIA for Core Navigation/Search Surfaces

## Why

FastFiles' three most important surfaces — Column View, Home / Quick Access
(rendered inside Column View), and the Search result list — are invisible to
assistive technology and to semantic automation. The `zero-touch-autonomous-
engineering` validation campaign records provider-absence SKIPs for exactly
these surfaces:

- `column-items-not-exposed-to-uia` (3.2, 3.7, 3.8, 3.9): "ColumnView rows
  are Direct2D with no UIA/MSAA provider (WM_GETOBJECT absent, chrome-only
  control view)".
- `no-scroll-selection-provider` (3.3): "item surface exposes no
  Scroll/Selection providers (scroll=False selection=False)".
- `result-items-not-exposed-to-uia` (3.11): "result rows are LVS_OWNERDATA
  on D2D with no UIA item provider".
- `error-state-drawn-on-d2d-surface` (3.5): in-column error states are
  painted text with no provider.

The consequence is not just a validation gap: Narrator and other screen
readers cannot navigate, announce, select, or activate any file or folder in
the product's primary surface. This change implements real Microsoft UI
Automation provider semantics for these surfaces through the normal
`WM_GETOBJECT` / `UiaReturnRawElementProvider` mechanism, exposing the same
semantics to Narrator, Accessibility Insights, UIAutomationClient, and
FastFiles' own `verify/uia-driver`.

This is explicitly scoped: **Treemap UIA is a separate follow-up.** The
provider architecture is designed so a future Treemap provider slots in
without rework, but no Treemap provider is implemented here.

## What Changes

- **Provider foundation** (`src/ui/src/UiaProviderBase.*`,
  `src/ui/src/UiaRuntimeIds.h`): COM reference counting / QueryInterface
  helpers, VARIANT and property helpers, deterministic runtime-ID namespaces,
  DIP→physical→screen coordinate conversion, UIA event raising, and a
  zombie-safe owner-lifetime token.
- **Pure projection seam** (`src/ui/include/ffui/UiaSurface.h`): a read-only
  view of the real render/interaction state (panes, columns, items, Home
  sections, search results, selection, focus, scroll, geometry) plus the
  action methods that route back into the exact real mutation logic. The
  provider tree depends only on this seam, which makes every provider
  headless-testable without a window, a renderer, or an engine.
- **Column View provider** (`src/ui/src/ColumnViewUiaProvider.*`): fragment
  root (Window "FastFiles") → Column View group → per-pane Pane (Scroll
  pattern) → per-column List (Selection pattern) → per-item ListItem
  (SelectionItem / Invoke / ScrollItem). Dual-pane correct by construction
  (runtime IDs embed the pane). Items are lightweight, created on demand,
  identity-keyed, and released on structure change.
- **Home provider** (`src/ui/src/HomeUiaProvider.*`): Home group → sections
  (Groups) → items (ListItem + Invoke) and Clear affordances / Quick actions
  (Button + Invoke). Unavailable known folders are `IsEnabled=false` and not
  invokable. Activation reuses the existing `HomeActivation` callback path —
  no third activation implementation.
- **Search provider** (`src/ui/src/SearchUiaProvider.*`): a fragment root on
  the native `SysListView32` result list (via a subclassed `WM_GETOBJECT`
  handler) because the UIA/MSAA proxy cannot enumerate `LVS_OWNERDATA`
  virtual items. The list stays fully native for input and virtualization;
  only the UIA semantics are replaced. Results expose name, containing
  location, type, logical size, modified time, selection, focus, bounds, and
  offscreen state; activation routes through the exact search-to-navigation
  path used by double-click.
- **Events**: focus-changed, selection-invalidated, element-added/removed,
  structure-changed (navigation, Home refresh, result replacement), invoked,
  and property-changed events — all driven by semantic mutations, never by
  paint. Raised after locks are released (no reentrant deadlock).
- **Driver + validation**: extend `verify/uia-driver` with ScrollItem /
  ItemContainer consumption where they are general capabilities; convert the
  provider-absence SKIPs above into real end-to-end scenarios run against the
  live app; keep the NO-PIXEL-FALLBACK rule.

## Success Criteria

- A real UIA provider tree is returned for the custom Column View surface
  through `WM_GETOBJECT`.
- Files/folders expose meaningful Name and ControlType; real focus,
  selection, scroll, bounds, and offscreen state are exposed; UIA selection
  manipulates the real `SelectionModel`; `ScrollIntoView` uses the real
  scroll math.
- Home sections/items/Clear actions are semantically exposed; unavailable
  locations are disabled and non-invokable; activation reuses real logic.
- Search results are discoverable through UIA without destroying
  virtualization; activation uses the real search-to-navigation path.
- Stale providers (after navigation, Home refresh, or query replacement)
  fail cleanly and never retarget the new occupant of the same row index.
- Provider COM lifetime is safe during window destruction.
- The `zero-touch-autonomous-engineering` SKIPs 3.2, 3.3, 3.5 (in-column
  error state), 3.7–3.9, and 3.11 are re-run; provider absence is no longer
  the reason for SKIP on any surface implemented here.
- Performance gates: no per-frame provider work, bounded caches, and a
  measured synthetic large-directory enumeration benchmark.
