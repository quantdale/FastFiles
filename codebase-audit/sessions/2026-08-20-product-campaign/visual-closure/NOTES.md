# Visual closure — bottom-edge "transparent strip" investigation (2026-08-21)

## Root cause of the originally reported ~15px strip

**Not a renderer/layout defect.** The strip visible in the campaign's
`visual/08-fresh-single.png` evidence is the DWM **invisible resize border**
that `GetWindowRect` reports but does not draw. The campaign's capture script
(`build/_session/visual_matrix.ps1`) grabbed the `GetWindowRect` region, which
extends 7 px past the visible window on left/right/bottom at 100% DPI, so the
desktop behind the window bled into every capture — reading as a "transparent
strip" at the bottom edge (and thinner bands at the sides).

Evidence:

- Live geometry of the current build (`build/_session/probe_strip_live.ps1`,
  commit 397e391 + WM_DPICHANGED fix): `WindowRect` 1024x640 vs
  `DWMWA_EXTENDED_FRAME_BOUNDS` 1010x633 → invisible borders L=7 R=7 B=7,
  top=0; client bottom sits 1 px above EFB bottom (the OS frame line).
- Pixel measurement of the original `08-fresh-single.png`: dark band ≈8 px at
  bottom plus ≈4–8 px side bands — exactly the border geometry, matching no
  layout constant.
- Code audit: `ColumnView::Render` opens every frame with an opaque
  `Clear(theme.background)`; the shell layout height chain
  (`ComputeMainShellLayout` → `ComputeDetailsLayout`) is exact in float with no
  double subtraction; `WM_SIZE` resizes the composition swap chain to the full
  client rect.

## What this matrix shows

Captures below use the **visible frame** (`DWMWA_EXTENDED_FRAME_BOUNDS`) as
the source rectangle, so they show exactly what a user sees. All nine states
were inspected visually and pixel-sampled: content color reaches the last
client row in light and dark themes; only the OS's own 1 px frame border
follows. No desktop bleed-through, no seam, no clipping.

| Shot | State |
| --- | --- |
| 01-light-normal-single | Light / 1024x680 / single pane / This PC |
| 02-dark-normal-single | Dark / same geometry |
| 03-dark-narrow | Dark / 720x520 |
| 04-dark-wide-dualpane | Dark / 1600x950 / dual pane |
| 05-dark-details-open | Dark / wide / dual pane / details open |
| 06-dark-details-closed | Dark / wide / dual pane / details closed |
| 07-light-wide-dualpane | Light / wide / dual pane |
| 08-dark-home | Dark / Home surface, single pane |
| 09-dark-search | Dark / search panel open, query "txt" |

`geometry.log` records per-shot WindowRect / EFB / client rect / DPI.

## Product change shipped with this closure

While instrumenting the resize/DPI chain, one real latent defect was found and
fixed: `WM_DPICHANGED` sized the composition swap chain from the suggested
**window** rect (includes non-client chrome) instead of the **client** rect,
which would clip the bottom of the scene after any DPI transition.
(`src/ui/src/WindowShell.cpp`). Regression coverage:
`TestFullClientCoverageInvariant` in `tests/ui/test_shell_layout.cpp` pins the
"layout tiles the whole client with zero remainder" invariant across DPI scales
x pane states x dual-pane.

## Environment limits

- Host runs 100% DPI (96). 150% was not exercised: switching the host display
  scale programmatically is an invasive host-level change; the DPI-change code
  path is covered by the fix above and the layout DPI-matrix unit tests.
- Captures were taken on a live interactive desktop; the capture script raises
  the app `HWND_TOPMOST` before each shot to prevent overlap contamination.
