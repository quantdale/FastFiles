# Interactive visual catalog

Captured on an interactive Windows desktop with
`pwsh ./verify/visual/Invoke-VisualCatalog.ps1` against the debug-diagnostic
UI (`FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED`). Capture method is
`DwmExtendedFrameBounds+CopyFromScreen` after a UIA post-condition.
PrintWindow is not used.

Production signing was **not** attempted. `PinnedSignatures.h` still has
all-zero placeholder thumbprints; Release remains fail-closed.

## Results

See `summary.md` and `manifest.json` for the machine record.

| Surface | Outcome | Notes |
| --- | --- | --- |
| Home | PASS (`home-from-command`, `home-narrow`, `home-dark`) | Product map, quick actions, degraded badge. `home-default` UIA Name lookup at first paint is racy; the later Home captures are the evidence. |
| Column View | PASS | Breadcrumb, columns, details pane, degraded badge. |
| Fast Search | PASS | Idle empty state, query field, browsed-snapshot chip. Tab label stays on the last filesystem/Home identity (workspace is not a fake path). |
| Storage Analysis | PASS with remaining header chrome at capture time | Segmented Overview pill and themed status line. Capture-time SysHeader32 used unthemed item glyphs; a later product fix owner-paints headers (recapture still pending). |
| Settings | PASS after product fix | General page only; Theme + Density; dark client. Capture-time tab strip had a native light remainder; a later product fix owner-paints the strip (recapture still pending). |
| Command palette | Semantic PASS, visual miss over Storage at capture time | AutomationId 6301 appears. Capture-time overlay sat under Storage listviews; a later product fix raises a dimmer + HWND_TOP overlay and stops Search/Storage layout from stealing z-order (recapture still pending). |

## Product fixes landed from this catalog

- Settings: page labels and Density combo participate in `SwitchPage`; `Show` re-applies the selected page; `WM_ERASEBKGND` fills the themed background.
- Storage: status STATIC is owner-painted with tokens (no system-white slab).
- List headers: visual styles disabled so NM_CUSTOMDRAW can run.

## Still external

- Production signer/pins
- Privileged MFT/USN
- Live Filters UIA journey
- High Contrast / 150% DPI matrix
- Physical-display recapture of the post-fix header/tab/palette chrome
