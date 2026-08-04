# Modernize UI Appearance

## Why

The FastFiles UI is functionally complete (all six UI pillars are spec'd and implemented to MVP bar) but visually stuck at "stock Win32 + flat rectangles": file/folder "icons" are 8×8 colored squares, selection is a sharp full-width rectangle, the top chrome/sidebar/tabs/dialogs are un-themed GDI and system controls (the sidebar derives its background from the theme but hardcodes dark RGB literals for its section header and labels), there are no hover states, no animations, no real file icons, and search results render as a system-light ListView even in dark mode. The result reads as a 2005-era utility, not a modern Windows app — a credibility problem for a tool whose pitch is being a best-in-class native file manager.

## What Changes

- Introduce a Fluent-style design-token system (elevation surfaces, corner radii, spacing scale, typography ramp, hover/press/selection overlays) extending the existing `UiTheme` palette in `src/ui/src/UITheme.h`, so every surface — Direct2D and Win32 chrome — renders from one coherent token set.
- Restyle the Direct2D surfaces: Column View (real file/folder icons from the system image list via a bounded icon cache, rounded selection pill, animated hover states, smooth animated scrolling, refined degraded-mode badge), treemap (rounded tiles, hover lift), and the details/preview pane (elevated card, refined status bar with a real file-operation progress indicator replacing window-title progress text).
- Modernize the search panel: eliminate the per-`WM_PAINT` throwaway `ID2D1DCRenderTarget`, Fluent text-box styling for the query edit, themed owner-drawn result rows with icons and match-span highlighting (per `instant-search` design's surrogate-safe ranges), and proper in-progress/no-results visual states.
- Re-skin the Win32 chrome — navigation chrome (breadcrumb, back/forward, tab strip, drive combo), navigation sidebar, command palette, settings dialog, conflict dialog — to follow the active theme via owner-draw, eliminating hardcoded system colors and ad hoc RGB literals.
- Add tasteful, minimal animation: ~100–180 ms ease-out transitions for hover, selection, scroll, and a short theme cross-fade (implementing the animation posture `settings-and-appearance` D5 permitted but shipped as instant-only in its task 6.2). Every animation is gated on a `SystemAnimationsEnabled()` helper added by this change — the gate D5 assumed but which was never actually implemented — and snaps instantly when Windows "Show animations" is off.
- Apply Windows 11 window chrome where supported (rounded window corners, Mica backdrop), runtime-detected and gracefully inert on Windows 10.
- Harden the renderer against device loss (`D2DERR_RECREATE_TARGET` recovery through the shared resource-recreation path), closing a known gap.

## Capabilities

### New Capabilities
- `ui-appearance`: The cross-surface Fluent-style visual language — design tokens, shared styling/animation helpers, icon cache, and the restyled Direct2D and Win32 surfaces — with performance and accessibility constraints (animation gating, virtualization preservation, no per-frame allocations).

### Modified Capabilities
- `theming` (from `settings-and-appearance`): extends the palette with the new token set and implements the previously spec-permitted-but-unimplemented theme-change cross-fade. Theme switching remains a trigger of the single shared resource-recreation routine (D5); no new recreation path is introduced.

## Impact

- **Code**: `src/ui/` only — `UITheme.h`, new `UiStyle.*`, `UiAnimation.*`, `IconCache.*`, and restyle work in `ColumnView`, `TreemapView`, `SearchPanel`, `NavigationChrome`, `NavigationSidebar`, `CommandPalette`, `SettingsDialog`, `ConflictDialog`, `WindowShell`, `Renderer`. No changes to engine, service, protocol, IPC, index store, or the security model.
- **Specs**: modifies `theming` (token set + cross-fade); adds `ui-appearance`. Does not alter any requirement in `instant-search`, `navigation-and-workspace`, `storage-analysis`, or `file-preview-and-properties` — those surfaces' *behavior* is unchanged; only their rendering is restyled. **Archive order**: the modified `theming` requirements are introduced by `settings-and-appearance`, which is not yet archived (`openspec/specs/` is empty) — this change must archive after it, or the MODIFIED requirements have no base to apply against.
- **Performance**: virtualization contracts (`LVS_OWNERDATA` results list, Column View row/column culling, vsync'd flip swap chain) are preserved; new costs (icon cache, animation invalidation) are bounded and documented in the design. The per-paint DC-render-target waste in `SearchPanel` is removed.
- **Compatibility**: Windows 11-only DWM features are runtime-gated; Windows 10 behavior is unchanged except the token restyle.
- **Out of scope**: UIA/accessibility providers for the custom-drawn surfaces (existing known gap, unchanged), list/details view surfaces (a `navigation-and-workspace` Non-Goal), per-pane tab strips (deferred by `navigation-and-workspace` D6), custom accent-color themes.
