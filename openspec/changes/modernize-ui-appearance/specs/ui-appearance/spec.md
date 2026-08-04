## ADDED Requirements

### Requirement: Unified Fluent-Style Design Tokens
The system SHALL source every rendered color, corner radius, spacing value, and text style across all UI surfaces — Direct2D-rendered and Win32/GDI chrome alike — from a single design-token set extending the existing `UiTheme` palette, with complete dark and light variants, and SHALL NOT introduce a second palette or hardcoded color literals in UI surface code.

#### Scenario: Dark and light themes are coherent across all surfaces
- **WHEN** the application is running in Dark theme (or Light theme)
- **THEN** the column view, treemap, details pane, status bar, navigation chrome, sidebar, tab strip, search panel, command palette, and dialogs SHALL all render with colors derived from the same active token set, with no surface rendering in stale, system-default, or hardcoded off-theme colors

#### Scenario: Tokens are applied through the existing resource-recreation path
- **WHEN** the active theme or DPI changes
- **THEN** token-derived resources (brushes, text formats, GDI color conversions) SHALL be recreated through the shared recreation pattern established for theme changes (mark-consumers-dirty fan-out with lazy paint-path recreation) — the same pattern this change extends to device loss — not through a parallel swap mechanism

#### Scenario: Token overlays are suppressed under High Contrast
- **WHEN** Windows High Contrast is active
- **THEN** surfaces SHALL fall back to system colors and the token interaction overlays (hover, press, selection-soft) SHALL be suppressed, rather than overriding the user's accessibility color choices

### Requirement: Real File and Folder Icons
The system SHALL display real file-type and folder icons obtained from the Windows system image list in the column view and search results, rendered at the correct DPI scale, in place of generic flat placeholder rectangles.

#### Scenario: Icons render per file type
- **WHEN** the column view or search results display items of differing file types
- **THEN** each item SHALL render with the system icon associated with its file type (or folder icon), at the correct resolution for the window's current DPI

#### Scenario: Icon retrieval never blocks rendering
- **WHEN** an icon for a file type is not yet cached
- **THEN** the system SHALL render a themed placeholder glyph immediately and SHALL resolve the real icon off the render thread, updating the display when it arrives, without stalling input or repaints

#### Scenario: Icon memory is bounded
- **WHEN** the user browses many distinct file types over a long session
- **THEN** the icon cache SHALL remain within its documented bound (on the order of hundreds of entries, low single-digit MB), evicting least-recently-used entries rather than growing without limit

#### Scenario: Icons are re-resolved on DPI change
- **WHEN** the window moves to a monitor with a different DPI scale
- **THEN** cached icons SHALL be re-resolved at the new scale so icons remain crisp rather than bitmap-stretched

### Requirement: Gated, Non-Blocking UI Animation
The system SHALL animate hover states, selection changes, scrolling, and theme changes with short (approximately 100–180 ms), ease-out transitions; SHALL remain fully responsive to input during any animation; SHALL run no animation timers while idle; and SHALL snap all animated values instantly to their targets whenever the Windows "Show animations" setting is disabled.

#### Scenario: Animations disabled by accessibility setting
- **WHEN** the Windows "Show animations in Windows" setting is disabled
- **THEN** the application SHALL apply all visual state changes (hover, selection, scroll, theme change) instantly with no transition effects and no active animation timers

#### Scenario: Theme change cross-fade does not block input
- **WHEN** the active theme changes while animations are enabled
- **THEN** the application MAY cross-fade the previous frame over the new theme for no more than approximately 150 ms and SHALL accept and process user input normally throughout

#### Scenario: Smooth scrolling preserves virtualization cost
- **WHEN** the user scrolls a column with the mouse wheel or keyboard with animations enabled
- **THEN** the scroll offset SHALL transition smoothly over approximately 150 ms, and each animated frame SHALL retain the existing row/column culling so per-frame rendering cost does not grow with directory size

### Requirement: Restyled Column View Interaction Visuals
The column view SHALL render selection as a rounded pill distinct between focused and unfocused columns, SHALL render a hover indication on pointer-over rows, and SHALL render folder items with a navigation affordance, all using the unified token set.

#### Scenario: Focused vs unfocused selection is visually distinct
- **WHEN** a selection exists in both a focused column and one or more unfocused columns
- **THEN** the focused column's selection SHALL render with the accent-forward treatment and unfocused columns' selections SHALL render with a muted treatment, both using rounded pill geometry rather than full-bleed sharp rectangles

#### Scenario: Hover feedback on rows
- **WHEN** the pointer moves over a selectable row
- **THEN** the row SHALL display a subtle hover overlay (animated when animations are enabled) that is visually distinct from both selection and press states

#### Scenario: Folder rows show a navigation affordance
- **WHEN** a row in the column view represents a folder
- **THEN** the row SHALL render a chevron (or equivalent affordance) indicating that activating it navigates deeper

### Requirement: Modernized Search Surface
The search panel SHALL render its query edit, scope/sort controls, and results list with the active theme's tokens in both dark and light themes; results rows SHALL show real icons, the matched file name with the match span visually highlighted, and secondary location text; and the panel SHALL present explicit visual states for in-progress and no-results searches.

#### Scenario: Results list follows the active theme
- **WHEN** the application is in Dark theme and search results are displayed
- **THEN** the results list SHALL render with dark-theme token colors for background, text, selection, and hover — not system-default light colors

#### Scenario: Match span is highlighted in result names
- **WHEN** a result row displays a file name matched by the query
- **THEN** the matched span SHALL be visually emphasized (accent-colored or emphasized weight), using surrogate-safe match ranges, while virtualization of the results list is preserved

#### Scenario: Query edit painting does not recreate render targets per frame
- **WHEN** the search query edit repaints
- **THEN** its rendering resources SHALL be created once and reused across paints, recreated only on theme or DPI change

### Requirement: Theme-Coherent Win32 Chrome and Dialogs
The navigation chrome (back/forward, breadcrumb, address, tab strip, drive selector), navigation sidebar, and command palette SHALL render via owner-draw from the unified token set; settings and conflict dialogs SHALL at minimum render themed backgrounds, themed static text, and a theme-matching title bar, so no window appears in mismatched system-default colors under either theme.

#### Scenario: Breadcrumb and tabs are themed and interactive
- **WHEN** the navigation chrome is displayed under Dark or Light theme
- **THEN** breadcrumb segments, navigation buttons, and tabs SHALL render with token colors, rounded geometry, and hover feedback, and SHALL retain their existing keyboard and mouse behavior

#### Scenario: Dialogs do not flash system-light in dark mode
- **WHEN** a settings or conflict dialog opens while Dark theme is active
- **THEN** the dialog's background, static text, and title bar SHALL render dark-theme-coherent; stock inner controls that are not owner-drawn are an accepted, documented trade-off

### Requirement: Modern Windows Window Chrome
On Windows 11 (or any OS build where the required DWM attributes are supported), the main window SHALL use rounded window corners and a Mica-family system backdrop, detected at runtime; on systems without support, these attributes SHALL be inert no-ops and all other modernization behavior SHALL be unchanged.

#### Scenario: Windows 11 window renders with rounded corners and Mica
- **WHEN** the application runs on a Windows 11 build supporting `DWMWA_WINDOW_CORNER_PREFERENCE` and `DWMWA_SYSTEMBACKDROP_TYPE`
- **THEN** the main window SHALL present rounded corners and the Mica backdrop behind the DirectComposition content

#### Scenario: Windows 10 behavior is unchanged
- **WHEN** the application runs on an OS without support for those DWM attributes
- **THEN** the window SHALL render with the token-restyled content and standard system chrome, with no errors or rendering artifacts from the unsupported attributes

### Requirement: Device-Loss Recovery
The renderer SHALL recover from Direct2D/DXGI device loss by recreating device-dependent resources through the same shared recreation routine used for theme and DPI changes, without crashing or leaving surfaces permanently blank.

#### Scenario: Device loss is recovered on the next frame
- **WHEN** the render device is lost (e.g., `D2DERR_RECREATE_TARGET` from EndDraw/Present, GPU reset, or driver update)
- **THEN** the application SHALL recreate the device-dependent resource set and resume rendering subsequent frames correctly, including theme brushes and cached icon bitmaps

### Requirement: File-Operation Progress in the Status Bar
File-operation progress SHALL be presented as a visual progress indicator in the status bar area of the main window, replacing window-title-text as the primary progress surface.

#### Scenario: Progress is visible during a file operation
- **WHEN** a file operation is in progress
- **THEN** the status bar SHALL display a progress indicator reflecting the operation's reported completion fraction, and the window title SHALL no longer be the sole progress surface
