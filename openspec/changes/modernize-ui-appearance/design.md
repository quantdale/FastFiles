# Modernize UI Appearance — Design

## Context

`establish-architecture-foundation` D1 chose Win32+COM+Direct2D/DirectComposition and explicitly accepted, as a budgeted cost, that "everything a UI framework normally gives free — per-monitor DPI, UIA/accessibility for custom-rendered controls, Windows 11 Mica/rounded-corner chrome, package-identity — must be hand-built." The six UI-surface pillars (`navigation-and-workspace`, `instant-search`, `storage-analysis`, `file-preview-and-properties`, `settings-and-appearance`, `shell-integration-and-commands`) then implemented every surface to a functional MVP bar: the Column View renders file/folder "icons" as 8×8 flat rectangles (`ColumnView.cpp:633`), selection is a sharp full-width rectangle (`ColumnView.cpp:625`), the navigation chrome/tabs/dialogs are un-themed GDI and system controls, the sidebar derives its background from the theme but hardcodes dark RGB literals for its section header and labels (`NavigationSidebar.cpp:110`), the search edit's custom paint creates a throwaway `ID2D1DCRenderTarget` on every `WM_PAINT` (`PaintSearchEdit` in `SearchPanel.cpp`), the search results ListView stays system-light in dark mode, and there are no animations anywhere.

Two details of the current state matter for scoping, because earlier planning docs describe them aspirationally rather than as shipped:

- `settings-and-appearance` D5 specified "one recreation routine" for theme/DPI/device-loss. What shipped is a fan-out: `WindowShell::ApplyTheme()` flips each component's dirty flag and brushes/text formats are rebuilt lazily in each paint-path `EnsureCreated`; DPI goes through `Renderer::Resize`. Device loss was never handled at all — `Renderer::EndFrame` discards the `EndDraw`/`Present` HRESULTs and `Renderer::Resize` ignores the `ResizeBuffers` result. The theme cross-fade D5 permitted was never implemented either; its task 6.2 shipped the instant swap.
- The `SystemAnimationsEnabled()` gate that D5's animation posture assumed does not exist in the code — settings-and-appearance task 6.2's completion note cites it, but there is no such function (nor any `SystemParametersInfo` call) in `src/ui`. This change adds it.

This change does not re-architect any of the above. It is a presentation-layer overhaul: one design-token system, one shared styling/animation helper layer, and a restyle of every visible surface. Four decisions fix the approach precisely enough that the work stays coherent and the performance contracts hold.

## Goals / Non-Goals

**Goals:**
- A single Fluent-style design-token set (colors, elevation, radii, spacing, typography) consumed by every Direct2D surface and every Win32 chrome surface, in both dark and light themes.
- Real file/folder icons via the system image list, bounded and cached, DPI-correct.
- Tasteful animation (hover, selection, smooth scroll, theme cross-fade) gated on the `SystemAnimationsEnabled()` posture `settings-and-appearance` D5 specified (helper added here; see Context).
- Removal of the known wasteful/incoherent rendering paths: per-paint DC render target in `SearchPanel`, hardcoded sidebar literals, system-light results list in dark mode, window-title-only file-op progress.
- Renderer device-loss recovery (`D2DERR_RECREATE_TARGET`) through the shared recreation pattern.

**Non-Goals:**
- UIA/accessibility providers for custom-drawn surfaces — a pre-existing, separately tracked gap; this change does not make it better or worse.
- List/details view surfaces (a `navigation-and-workspace` Non-Goal, still out of scope), per-pane tab strips (deferred by its D6), custom accent colors or a theme editor (`settings-and-appearance` D-scope limit, unchanged).
- Any change to behavior, state models, IPC, indexing, or the security architecture. No engine/service/protocol code is touched. (One pre-existing behavior bug is noted but deliberately not fixed: the degraded-mode badge's "click to enable" text is not actually clickable — the restyle keeps it non-interactive.)
- DComp visual-tree restructuring (per-region visuals, effects). Animation stays in immediate-mode Direct2D (D2 below).

## Decisions

### D1: One token system extends `UiTheme`; no second palette

`src/ui/src/UITheme.h` already centralizes the palette via `GetUiTheme(bool dark)` and the `UiMetrics` constants namespace. This change extends the `UiTheme` struct with the Fluent vocabulary — elevation surfaces (`surfaceElevated`, `surfaceSubtle`), interaction overlays (`hoverOverlay`, `pressOverlay`, `selectionSoft`), `dividerSubtle`, `focusStroke` — and extends `UiMetrics` with corner radii (`kRadiusSmall=4`, `kRadiusMedium=8`), a spacing scale, a typography ramp, icon size (16 DIP), and Fluent standard-density row height (28 DIP). Dark values stay anchored to the existing `0x202124` base and the accent is refined toward Windows 11 blues (dark ≈ `#4C8DFF` family, light ≈ `#0067C0` family) without a wholesale hue change.

It also consolidates the metric constants `ColumnView` currently duplicates locally (`kRowHeight`/`kColumnWidth`/`kBadgeHeight` in `ColumnView.h`) into `UiMetrics`, so the 24→28 row-height change cannot silently diverge between the namespace and the view.

**Why extend rather than replace:** `GetUiTheme` is already the single recreation-coupled palette source (`settings-and-appearance` D5); every consumer recreates brushes from it on theme/DPI change. A parallel token store would recreate exactly the drift problem D5 was written to prevent. Adding fields keeps one recreation routine and one source of truth.

**Win32/GDI consumption:** the same tokens are converted to `COLORREF` via one helper for the GDI/owner-draw chrome surfaces. This is what finally deletes the sidebar's hardcoded `RGB(0x29,0x2B,0x2F)` header/label literals — not a second dark palette.

### D2: Animation is timer-driven lerp in immediate-mode Direct2D, gated and snap-instant when disabled

A small `UiAnimation` helper provides ~100–180 ms ease-out cubic interpolation of scalar values (opacity, scroll offset), ticking a `SetTimer` only while an animation is active and invalidating through the existing `WindowShell::RequestRepaint()` (or an injected repaint callback for non-shell consumers). The gate is the new `SystemAnimationsEnabled()` helper (querying `SPI_GETCLIENTAREAANIMATION`, the setting behind "Show animations in Windows"): when it returns false, every animated value snaps to its target with zero timers, zero extra frames.

**Why immediate-mode rather than the DComp animation API:** the whole shell is one DComp visual hosting one swap chain; wiring `IDCompositionAnimation` would require restructuring into multiple visuals/effects for transitions that are trivially expressible as lerped draw parameters. The DComp path buys nothing here except complexity, and the timer-only-while-active pattern keeps steady-state cost identical to today (one vsync'd repaint per state change).

**Cost discipline:** no per-frame allocations, no geometry rebuilds per frame; animations only modulate floats consumed by the existing draw calls. Row/column culling in `ColumnView::Render` is untouched, so scroll-animation frames cost the same as a static frame.

**Theme cross-fade:** on theme change, the pre-change frame is captured into an `ID2D1Bitmap1` and drawn fading out over the newly-themed frame for ≈150 ms. This implements — not extends — the animation posture `settings-and-appearance` D5 (and its task 6.2) already permits ("at most a short, non-blocking cross-fade… skipped entirely when 'Show animations' is off"). Input is never blocked. Coverage caveat: the bitmap capture spans the Direct2D canvas (the swap-chain content); the separate Win32 chrome HWNDs (navigation chrome, sidebar, dialogs) cannot participate in a bitmap fade and instead repaint with the new tokens inside the same `ApplyTheme` pass, so the whole window settles within the same ≈150 ms and no surface is left stale-themed.

### D3: Real icons via a bounded, DPI-aware `IconCache`, never blocking the render thread

A new `IconCache` module wraps `SHGetImageList`/`SHGetFileInfo`, converts the returned `HICON` to `ID2D1Bitmap1` via WIC (premultiplied, sized to the DIP grid), and caches by extension/folder key with an LRU bound (~512 entries). Requests are fulfilled on a worker thread; until an icon arrives, surfaces draw the existing themed glyph rectangle as a placeholder. The cache is flushed/rebuilt on DPI change (icons are bitmap-resolution-dependent) but survives theme changes (icons themselves are theme-independent).

**Why the system image list over custom SVG/vector glyphs:** correctness and fidelity per extension for free (including app-associated icons), matching what users see in Explorer; a custom glyph set is a large art-and-maintenance cost for a worse result. The known cost — `SHGetFileInfo` is not cheap per call — is exactly why the cache is bounded, keyed, and off-thread.

**Memory bound:** 512 entries × 16×16 (and 32×32 at 200% DPI) premultiplied bitmaps is low single-digit MB worst case; the LRU evicts least-recently-used keys.

### D4: Win32 chrome is owner-drawn from the token set; stock controls stay only where owner-drawing is disproportionate

The navigation chrome (back/forward buttons, breadcrumb bar, tab strip, drive combo), sidebar, and command palette become owner-drawn surfaces sourcing every color from D1's tokens: rounded breadcrumb pills with chevron separators, Fluent glyph buttons (Segoe Fluent Icons with Segoe MDL2 fallback), rounded top tabs with close-on-hover ×, Fluent nav-item pills in the sidebar. Settings and conflict dialogs get themed backgrounds/statics and the DWM immersive dark title bar so they are never blinding-white in dark mode, but their inner ListViews/tab controls remain stock — owner-drawing those is a disproportionate cost for the visual delta, and this is an explicit, recorded trade-off, not an oversight.

**Why owner-draw rather than a UI framework migration (WinUI 3/XAML Islands):** the foundation already evaluated and rejected framework migration (WPF's Fluent theme judged "fragile under heavy custom styling"; a XAML Islands adoption would reopen the D1 decision wholesale). Owner-draw of a fixed, small control set is the bounded version of the same outcome.

**Windows 11 window chrome:** `DWMWA_WINDOW_CORNER_PREFERENCE` (round) and `DWMWA_SYSTEMBACKDROP_TYPE` (Mica) are applied when runtime-detected as supported; both are no-ops on Windows 10. The existing `DWMWA_USE_IMMERSIVE_DARK_MODE` logic (`WindowShell::ApplyTheme`) is retained.

### D5: Device-loss recovery joins the shared recreation pattern

`Renderer::EndFrame` currently discards the `EndDraw`/`Present` HRESULTs, and `Renderer::Resize` ignores the `ResizeBuffers` result — device loss is unhandled. This change makes device loss a fourth trigger of the recreation pattern `settings-and-appearance` D5 established (the `ApplyTheme`-style fan-out: mark every consumer dirty, recreate lazily in each paint-path `EnsureCreated` on the next frame). On `D2DERR_RECREATE_TARGET` the Renderer recreates its swap chain and target bitmap, then signals consumers to drop device-dependent resources (theme brushes, icon bitmaps, the cross-fade capture), which rebuild on demand on the next frame. `ResizeBuffers` failure is handled through the same path.

**Why now:** a UI that animates and holds more device-dependent resources (icon bitmaps, cross-fade capture) raises the cost of the existing gap from "rare freeze" to "rare freeze with more state lost." Fixing it inside this change is cheaper than after.

## Risks / Trade-offs

- **Animation-driven invalidation regresses idle CPU/battery** → timers run only while an animation is active; steady state is unchanged (one repaint per state change, vsync'd flip swap chain). Manual verification includes an idle-CPU sanity check.
- **Icon cache adds memory and a worker thread** → bounded LRU (~512 entries), placeholder glyphs mean a cache failure degrades visuals, never function.
- **Owner-drawn chrome drifts from native behavior** (keyboard focus rects, high-contrast) → hit-testing and keyboard handling stay in the existing Win32 message paths; only painting changes. High-contrast mode keeps system colors (detected via `GetSysColor` when `HIGHCONTRAST` is active — token overlays are suppressed).
- **Windows 10 divergence** → Win11-only DWM attributes are runtime-gated no-ops; the token restyle itself is version-independent.
- **Scope creep into behavior** → the spec's requirements are phrased as rendering/presentation outcomes; any task that would change state models or message flows is out of scope by construction.

## Migration Plan

Purely additive at the UI layer; no settings schema, protocol, or storage changes. Rollout is by phase (tokens → renderer/animation → column view/icons → search panel → chrome), each phase leaving the app buildable and warning-clean. No rollback concerns beyond normal source control.

One ordering constraint: this change MODIFIES `theming` requirements introduced by `settings-and-appearance`, which is not yet archived (`openspec/specs/` is empty). Archive `settings-and-appearance` first, or reconcile the MODIFIED requirements against its delta at archive time.

## Open Questions

- None blocking. (Possible follow-ups, explicitly not this change: UIA providers for custom surfaces; custom accent-color themes; per-pane tab strips.)
