# Interactive visual catalog — refreshed 2026-08-30

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
| Home | PASS (`home-default`, `home-from-command`, `home-wide`, `home-narrow`, `home-dark`) | Product map, quick actions, degraded badge, and semantic Home activation. The refreshed catalog establishes Home through the real `navigation.home` command before capture. |
| Column View | PASS | Breadcrumb, columns, details pane, degraded badge. |
| Fast Search | PASS | Idle empty state, query field, browsed-snapshot chip. Tab label stays on the last filesystem/Home identity (workspace is not a fake path). |
| Storage Analysis | PASS | Segmented Overview pill, themed status line, and owner-painted SysHeader32 in the refreshed post-fix capture. |
| Settings | PASS | General page, Theme + Density, and the refreshed themed tab strip. |
| Command palette | PASS | AutomationId 6301, visible top overlay, and post-fix z-order over Storage/Search surfaces. |

## Product fixes landed from this catalog

- Settings: page labels and Density combo participate in `SwitchPage`; `Show` re-applies the selected page; `WM_ERASEBKGND` fills the themed background.
- Storage: status STATIC is owner-painted with tokens (no system-white slab).
- List headers: visual styles disabled so NM_CUSTOMDRAW can run; the refreshed catalog verifies the post-fix chrome.

## Still external

- Production signer/pins
- Privileged MFT/USN
- Full live foreground/Explorer UIA journey
- High Contrast / 150% DPI and broader physical-display matrix
