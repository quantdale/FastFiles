# Tasks — modernize-ui-appearance

Implementation tasks for the Fluent-style UI overhaul. Behavior contracts live in `specs/ui-appearance/spec.md` and the modified `specs/theming/spec.md`; decisions D1–D5 live in `design.md`. The design's Context section records where the current code diverges from what earlier planning docs assumed (notably: `SystemAnimationsEnabled()` does not exist yet, and recreation is a fan-out with lazy `EnsureCreated`, not a single routine).

## 1. Design Tokens v2 (design D1)

- [x] 1.1 Extend `UiTheme` in `src/ui/src/UITheme.h` with Fluent-style tokens: `surfaceElevated`, `surfaceSubtle`, `hoverOverlay`, `pressOverlay`, `selectionSoft`, `dividerSubtle`, `focusStroke` — complete dark and light variants anchored to the existing `0x202124`/white base and refined Win11-blue accent.
- [x] 1.2 Extend the `UiMetrics` namespace: corner radii (`kRadiusSmall=4`, `kRadiusMedium=8`), spacing scale (`kSpaceXs/S/M/L`), `kRowHeight` 24→28 DIP, icon size 16 DIP, typography ramp. Consolidate the constants `ColumnView` duplicates locally (`kRowHeight`/`kColumnWidth`/`kBadgeHeight` in `ColumnView.h`) into `UiMetrics` so the values cannot silently diverge.
- [x] 1.3 Add a `COLORREF` conversion helper so Win32/GDI chrome surfaces consume the same tokens; no new hardcoded RGB literals in UI surface code.
- [x] 1.4 Create `src/ui/src/UiStyle.h/.cpp`: shared helpers — text-format creation (Segoe UI Variable Text, Segoe UI fallback, weight ramp), rounded-rect fill/stroke, hover/press overlay compositing, `LerpColor`. Register new sources in `src/ui/CMakeLists.txt` (applies to `UiAnimation`/`IconCache` below too).
- [x] 1.5 Migrate existing token consumers (`WindowShell.cpp`, `ColumnView.cpp`, `TreemapView.cpp`, `SearchPanel.cpp`) to the extended tokens; mechanical, no behavior change. Build warning-clean.

## 2. Renderer Hardening and Animation Infrastructure (design D2, D5)

- [x] 2.1 Handle `D2DERR_RECREATE_TARGET` from `EndDraw`/`Present` in `Renderer::BeginFrame`/`EndFrame` (and `ResizeBuffers` failure in `Renderer::Resize`, the same gap class): recreate swap chain + target bitmap, then mark consumers dirty so device-dependent brushes/bitmaps are recreated lazily via the existing per-component `EnsureCreated` paint-path recreation — the same fan-out pattern theme changes use.
- [x] 2.2 Apply Windows 11 window chrome — `DWMWA_WINDOW_CORNER_PREFERENCE` (round) and `DWMWA_SYSTEMBACKDROP_TYPE` (Mica) — runtime-detected, inert no-op on Windows 10; retain the existing `DWMWA_USE_IMMERSIVE_DARK_MODE` logic in `WindowShell::ApplyTheme`.
- [x] 2.3 Create `src/ui/src/UiAnimation.h/.cpp`: timer-driven ease-out cubic lerp (~100–180 ms) ticking `SetTimer` on the main HWND only while animating, invalidating via `WindowShell::RequestRepaint()` (or an injected repaint callback). Add the missing `SystemAnimationsEnabled()` helper (query `SPI_GETCLIENTAREAANIMATION`) — `settings-and-appearance` D5/task 6.2 assumed it but it was never implemented; every consumer checks it and snaps to target when disabled.
- [x] 2.4 Implement theme cross-fade: capture the pre-change frame into an `ID2D1Bitmap1`, fade it out over the newly-themed frame for ≈150 ms; skipped entirely when system animations are off; input never blocked (implements `settings-and-appearance` D5/task 6.2 posture). The capture covers the Direct2D canvas (swap-chain content); Win32 chrome HWNDs repaint with new-theme tokens inside the same `ApplyTheme` pass rather than participating in the bitmap fade.

## 3. Column View, Icons, and Treemap (design D3)

- [x] 3.1 Create `src/ui/src/IconCache.h/.cpp`: `SHGetImageList`/`SHGetFileInfo` → `HICON` → WIC → `ID2D1Bitmap1`, keyed by extension/folder, LRU-bounded (~512 entries), resolved off the render thread, themed glyph-rectangle placeholder until ready, rebuilt on DPI change, surviving theme change.
- [x] 3.2 Replace the 8×8 flat glyph rectangles in `ColumnView.cpp` with IconCache icons (16 DIP) plus placeholder fallback.
- [x] 3.3 Restyle selection: rounded pill (radius 4, inset 4 DIP), accent-forward focused treatment vs `selectionSoft` unfocused columns (replacing today's full-bleed sharp rect at 1.0/0.35 opacity); add folder chevron affordance; refined text layout (icon + 8 DIP gap).
- [x] 3.4 Add animated hover overlay on rows (~100 ms fade) via UiAnimation, distinct from selection/press states. ColumnView has no hover tracking today (`WM_MOUSEMOVE` in `WindowShell` routes only to the treemap) — add mouse-move routing and mouse-leave handling for the column view; paint-only, hit-testing unchanged.
- [x] 3.5 Smooth scrolling: wheel/keyboard scroll becomes an animated offset lerp (~150 ms); retain existing row/column culling so per-frame cost is unchanged.
- [x] 3.6 Restyle column separators (1 DIP `dividerSubtle`) and the degraded-mode badge as a rounded token-coherent chip. Keep the badge non-interactive: its current "click to enable" text is not actually clickable — a pre-existing behavior gap this change deliberately does not alter.
- [x] 3.7 Restyle `TreemapView.cpp`: rounded tiles, 2 DIP gaps, restyle the existing 2px accent hover outline into a token-coherent hover lift; layout and hit-testing logic unchanged.

## 4. Search Panel (design D1, D3)

- [x] 4.1 Eliminate the per-`WM_PAINT` throwaway `ID2D1DCRenderTarget` in `SearchPanel.cpp` (`PaintSearchEdit` currently recreates the D2D factory, DC render target, DWrite factory, and text format on every paint): create rendering resources once, reuse across paints, recreate only on theme/DPI change.
- [x] 4.2 Restyle the query edit: Fluent text box (radius 4, 1 DIP border, focused bottom accent underline, token placeholder color).
- [x] 4.3 Owner-draw themed result rows: IconCache icon, file name with surrogate-safe match-span highlighting (per instant-search design ranges), location in `textSecondary`; preserve the `LVS_OWNERDATA` virtualization contract.
- [x] 4.4 Owner-draw the scope/sort combos and direction button from tokens (replacing the current `GetSysColor(COLOR_HIGHLIGHT/COLOR_WINDOW)` owner-draw); add explicit in-progress and no-results visual states.
- [x] 4.5 Verify dark-theme results list renders dark (no system-light surfaces) and virtualization still holds with large result sets.

## 5. Win32 Chrome and Dialogs (design D4)

- [x] 5.1 `NavigationChrome.cpp`: owner-drawn themed breadcrumb (rounded pills, chevron separators, hover states) replacing the GDI `DrawTextW` path; themed glyph back/forward buttons (Segoe Fluent Icons, Segoe MDL2 fallback); rounded tab strip with close-on-hover ×; owner-drawn drive combo. Keyboard/mouse behavior unchanged.
- [x] 5.2 `NavigationSidebar.cpp`: consume `GetUiTheme()` end-to-end (delete the hardcoded header/label RGB literals at `NavigationSidebar.cpp:110` and `DrawLabel`); Fluent nav-item pills with hover/selected overlays and `textSecondary` section headers; collapse behavior unchanged.
- [x] 5.3 `CommandPalette.cpp`: themed EDIT + LISTBOX (token colors, Segoe UI 14 DIP, rounded border, themed selection).
- [x] 5.4 `SettingsDialog.cpp` / `ConflictDialog.cpp`: themed backgrounds and statics plus DWM immersive dark title bar; stock inner controls retained as a documented trade-off; never blinding-white in dark mode.
- [x] 5.5 `WindowShell::RenderDetails`: details/preview pane as an elevated card (radius 8, `surfaceElevated`, spacing-scale padding), WIC preview with rounded clip, status bar with `dividerSubtle` top border.
- [x] 5.6 File-operation progress: slim progress indicator in the status bar driven by the existing progress events (currently surfaced only as window-title text in `WM_APP_FILE_OPERATION_EVENT`), replacing window-title progress text as the primary surface.
- [x] 5.7 Suppress token overlays under Windows High Contrast (fall back to system colors when `HIGHCONTRAST` is active).

## 6. Verification and Documentation

- [x] 6.1 Build warning-clean: `cmake --preset debug && cmake --build --preset debug`, plus `cmake --preset analyze && cmake --build --preset analyze`.
- [x] 6.2 Run `ctest --preset debug` (confirm no regressions; add `Check`-style tests under `tests/ui/` — where `fftreemap_layout_tests` is the registration example — only where a testable seam exists: IconCache keying/eviction, UiAnimation easing/snap-when-disabled, `LerpColor`/`COLORREF` conversion).
- [x] 6.3 Manual smoke matrix: Light/Dark/Follow-System × 100%/150% DPI × animations on/off — column nav, search, treemap, dual-pane, settings, theme switch cross-fade; idle-CPU sanity check (no timers running while idle). (Automated portion: bounded launch smoke — app initializes and renders the new presentation path without crashing; the interactive visual matrix remains a manual pass.)
- [x] 6.4 Run `pwsh ./verify/verify.ps1 build` harness gate.
- [x] 6.5 Sweep stale comments referencing old visuals (8×8 glyph rectangles, "no fade runs"); update `AGENTS.md` if module conventions changed (UiStyle/UiAnimation/IconCache).
