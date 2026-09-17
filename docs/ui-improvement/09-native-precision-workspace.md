# Native Precision Workspace

FastFiles visual direction for the follow-up native UI modernization, locked
before implementation on 2026-09-17.

## Reference lock

**Primary direction:** Things / Cultured Code (`0796cd74-edc2-4e12-9c71-25fac02a1cb2`).
FastFiles keeps its Segoe UI and native Win32/Direct2D character, with a calm
cool-neutral workspace, readable hierarchy, compact controls, and restrained
blue interaction states.

**Craft layer:** shadcn/ui (`c14c0a94-1037-449e-bf5b-4cb972656ac7`) contributes
1px border discipline, compact spacing, and minimal elevation. Desktop.fm
(`5b70f172-2ba2-4f26-9956-251d4cfc0329`) contributes quiet OS-utility restraint.

**Product composition:** Skiff Drive
(`20e7902b-e1ae-45e8-a135-33a4a9047776`), Dropbox light and dark
(`decaaa94-8f17-463f-a450-7ed4ab0a3e18`,
`563c187a-da65-466b-9d84-397bbcfa976d`), and MonoDesk files
(`76832364-5187-4d25-853f-266a5b0cb5e5`) supply narrow file-browser patterns:
separated navigation/search/actions, filename-first rows, compact metadata,
contextual bulk actions, and tonal dark surfaces. Linear settings
(`7eb1131d-c52e-4678-bcff-3ebb90f90bd2`,
`cf84ce2f-7463-41a4-8f07-d9ad30110b02`) supplies grouped settings navigation
and aligned label/description/control rows.

**Flow authority:** Skiff move files (flow `1919`) and Dropbox move file (flow
`13049`) establish selection -> contextual action -> destination picker ->
truthful progress/success -> refreshed listing. Existing native FastFiles
semantics remain authoritative wherever they are stronger.

### Preserve

1. Segoe UI, native keyboard conventions, UIA identity, and DPI-aware geometry.
2. Cool neutral surfaces with a small number of hierarchy levels.
3. Restrained blue for active, selected, focused, and important actions.
4. Thin borders and tonal separation; shadows only for transient elevation.
5. Compact desktop density with readable file names and metadata.

### Borrow only

- From shadcn/ui: 1px borders, 4/8/12/16 spacing rhythm, and minimal elevation.
- From Skiff/Dropbox/MonoDesk: search/action separation, scanable file rows,
  contextual actions, and filename-first metadata hierarchy.
- From Linear: grouped settings navigation and aligned setting rows.

### Role rules

- Blue means active, selected, focused, or important action.
- Red is destructive/error; green is success; amber is warning/attention.
- Neutral surfaces provide structure; shadows indicate transient elevation only.
- File-type color is used only when it improves recognition.
- Every color status also has text, icon, shape, or position as a non-color cue.

### Reject

Glassmorphism, gradients, giant cards, 18-25px radius everywhere, pill
controls everywhere, neon accent overload, arbitrary shadows, decorative icon
circles, oversized headings, disconnected floating panels, web-first gestures,
and Dropbox branding.

### Token commitments

Light uses a `#F7F8FA` canvas, `#F1F3F5` navigation surface, white primary
surface, `#E1E5EA` border, `#202428` primary text, `#66717D` secondary text,
and approximately `#2F78E9` accent. Dark uses `#111315` canvas, `#15181B`
navigation, `#1A1E22` primary surface, `#2A3036` border, `#F3F5F7` primary
text, `#A7B0BA` secondary text, and approximately `#4C8DFF` accent.

Functional controls target 30-36 DIP heights and 5-7 DIP radii; contained
panels use 8-10 DIP radii; file rows target 38-44 DIP. The existing density
profile and per-window DPI conversion remain the geometry authority.

## Baseline evidence

The pre-change semantic visual catalog is in the ignored build evidence tree:
`build/visual/baseline-20260917-fa53847/`. It recorded 16/16 scenarios across
Home, browse, details, dual pane, search, Storage Analysis, Settings, palette,
light/dark, and 620/800/1280/1600 widths. The host is 125% / 120 DPI; the
catalog truthfully reports 100/150/200% and High Contrast as unavailable here.

Observed follow-up targets are shared rather than screen-specific: light mode
needs stronger tonal separation than the current nearly-flat gray canvas,
Home needs less dead space and clearer grouping, controls are smaller than the
target daily-driver comfort range, and loading/empty/degraded surfaces need a
single calm state language instead of large unowned blocks.
