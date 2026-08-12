# Interactive visual validation evidence

Recorded 2026-08-11 on `recovery/fast-files-20260809` during the authorized
Computer Use pass against the rebuilt Debug `FastFiles.exe`.

## Host-state transition

- The Windows Display Settings UI reported **150%** at the start of this
  campaign (the earlier system-wide DPI probe returned 96, so the Settings
  UI and per-window DPI were used as the authoritative observations).
- Computer Use observed Settings at 144 DPI and FastFiles at 144 DPI while
  the 150% state was active.
- High Contrast was changed to the Windows `Aquatic` theme for the required
  row, then restored to `None`.
- Animation effects were changed to `Off` for the reduced-motion rows, then
  restored to `On`.
- The Display Settings UI was finally restored to **100%**. A post-restore
  per-window observation reported FastFiles and Settings at 96 DPI.
- Final read-only system checks reported `HighContrast=False` and
  `Animations=True`. No UAC, logout, restart, signing, trust-store, pin, or
  authentication-policy change was made.

## Captured states

All retained screenshots are under the untracked execution-evidence
directory `verify/runs/manual-visual-20260811/`.

| State | Result | Retained artifacts |
| --- | --- | --- |
| Light, 150% baseline | PASS. Menu text, degraded-mode badge, breadcrumb, status text, and column layout were readable with no overlap or mojibake. | `settings-150-0.jpg`, `fastfiles-150-baseline-0.jpg` |
| High Contrast, 150% | PASS. Aquatic high-contrast chrome and system-color menu remained readable; the details region did not occlude the column surface. | `settings-high-contrast-0.jpg`, `fastfiles-high-contrast-150-0.jpg`, `fastfiles-high-contrast-menu-0.jpg`, `fastfiles-high-contrast-menu-1.jpg`, `fastfiles-high-contrast-menu-2.jpg` |
| Animations disabled, 150% | PASS. The normal and High Contrast surfaces remained readable after the Windows animation-effects toggle was off. | `settings-animations-off-high-contrast-0.jpg`, `fastfiles-high-contrast-animations-off-150-0.jpg`, `fastfiles-animations-off-150-0.jpg` |
| Dark theme, 150% | PASS. Dark theme menu, degraded badge, status, and layout were readable with no visual defect; the app theme was returned to Light before cleanup. | `fastfiles-dark-150-0.jpg` |
| Light, 100%, details open/closed | PASS. The reserved right-hand `Properties / No selection` region was visible when open; the closed state returned the column surface to the full width. | `fastfiles-final-100-details-open-0.jpg`, `fastfiles-final-100-normal-recheck-0.jpg`, `fastfiles-final-100-single-details-0.jpg` |
| Light, 100%, dual pane | PASS. Both column panes remained left of the reserved details region; no column content rendered beneath it. | `fastfiles-final-100-dual-details-0.jpg` |
| Light, narrow width | PASS. The Windows Size path reduced the FastFiles window to 724 px; the full details card was removed from the layout and the remaining columns stayed usable without an underlay. | `fastfiles-narrow-details-0.jpg` |
| Settings/menu and final normal chrome | PASS. Settings menu labels, `Last Operation Errors…`, degraded status, and selection status were readable. | `fastfiles-final-100-normal-menu-0.jpg`, `fastfiles-final-100-normal-menu-1.jpg` |

Earlier Computer Use observations in this change also covered normal/wide
window layout, narrow-width collapse behavior, navigation, and the details
pane's reserved geometry. The direct geometry and DPI behavior remains backed
by `ffdetails_layout_tests`.

## Storage percentage row

Computer Use opened the real **Analyze Storage → Drill Down** surface against
the repository folder. The accessibility tree exposed the live `Name`,
`Type`, `Size`, `Subtree Size`, `% of Parent`, and `Modified` columns and real
file/folder rows. In the active degraded/partial-coverage path, the parent
aggregate was not available: file rows displayed `—` and folder rows showed
`Partial`, so a truthful 100%-sum assertion could not be made from this run.
The retained screenshot records this observed state:

- `storage-drilldown-parent-column-0.jpg`

The headless percentage implementation and its mixed file/directory,
rounding, zero, and overflow tests remain passing; they are not substituted
for the requested real aggregate-backed drill-down evidence. Task 5.1 stays
open for this data-dependent row and any remaining matrix combinations that
require a populated privileged index.

## Cleanup

- FastFiles and the exact sibling `FastFilesEngine.exe` were closed and
  verified absent after the pass.
- Final system checks were `HighContrast=False`, `Animations=True`, and the
  Display Settings UI showed 100% before it was closed.
- No product defect was found in the exercised visual states. The remaining
  release blockers are the documented signed-binary/pin, authenticated
  privileged-path, expired/untrusted fixture, and clean-host prerequisites.
