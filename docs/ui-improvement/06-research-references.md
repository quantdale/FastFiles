# 06 — Research References and Rationale

## Research posture

This plan deliberately prioritizes **primary sources** and current repository evidence.

Windows App SDK/WinUI documentation is used here primarily as **Windows design guidance**, not as a recommendation to rewrite FastFiles in XAML. FastFiles is a native Win32 + Direct2D/DirectWrite application, so the visual/interaction principles are translated into the existing architecture.

WCAG 2.2 is used as a measurable accessibility reference for contrast, keyboard, focus, target size, and reflow-related thinking. FastFiles should not claim web/WCAG conformance merely because those criteria are used as engineering targets.

---

# A. FastFiles repository evidence

## Current UI architecture

- [`CODE_INDEX.md`](../../CODE_INDEX.md) — module map and UI ownership.
- [`src/ui/src/WindowShell.h`](../../src/ui/src/WindowShell.h) — shell ownership of navigation, Column View, search, command palette, file operations, preview/details, storage analysis, settings, icon cache, animation, and UI state.
- [`src/ui/src/NavigationWorkspace.h`](../../src/ui/src/NavigationWorkspace.h) — Home/filesystem locations, history, breadcrumbs, tabs, dual pane, bookmarks, known folders, recent folders, and persistence.

### Why this matters

The redesign must work with the real product structure rather than inventing generic “screens.” It also shows why shell-level consistency has disproportionate value: many features converge in `WindowShell`.

## Existing design system

- [`src/ui/src/UITheme.h`](../../src/ui/src/UITheme.h)
- [`src/ui/src/UiStyle.h`](../../src/ui/src/UiStyle.h)
- [`src/ui/src/UiAnimation.h`](../../src/ui/src/UiAnimation.h)
- [`src/ui/src/IconCache.h`](../../src/ui/src/IconCache.h)

### Why this matters

FastFiles already has semantic color tokens, spacing/radius/type metrics, shared styling, reduced-animation handling, and system icons. UI v3 should consolidate and productize these rather than starting over.

## Previous UI modernization

- [`openspec/changes/modernize-ui-appearance/proposal.md`](../../openspec/changes/modernize-ui-appearance/proposal.md)
- [`openspec/changes/modernize-ui-appearance/design.md`](../../openspec/changes/modernize-ui-appearance/design.md)
- [`openspec/changes/modernize-ui-appearance/tasks.md`](../../openspec/changes/modernize-ui-appearance/tasks.md)

### Key conclusion

The previous modernization is marked complete and already added the class of improvements normally suggested by a shallow UI audit: tokens, rounded geometry, hover, icons, animation, themed search, themed chrome, Windows 11 treatment, and High Contrast handling.

Therefore the next program focuses on **hierarchy, layout, density, component contracts, responsive behavior, and visual validation**, not another generic Fluent restyle.

## Current accessibility / UIA work

Relevant current changes include:

- [`openspec/changes/semantic-uia-core-surfaces/`](../../openspec/changes/semantic-uia-core-surfaces/)
- [`openspec/changes/semantic-treemap-uia/`](../../openspec/changes/semantic-treemap-uia/)
- [`src/ui/src/ColumnViewUiaProvider.cpp`](../../src/ui/src/ColumnViewUiaProvider.cpp)
- [`src/ui/src/SearchUiaProvider.cpp`](../../src/ui/src/SearchUiaProvider.cpp)
- [`src/ui/src/TreemapUiaProvider.cpp`](../../src/ui/src/TreemapUiaProvider.cpp)

### Why this matters

The UI is custom drawn, so a redesign that ignores provider geometry, state, and patterns could visually improve the app while making it less accessible. UIA is part of the component contract.

## Per-monitor DPI

- [`src/ui/src/FastFiles.manifest`](../../src/ui/src/FastFiles.manifest) declares `PerMonitorV2`.
- [`src/ui/src/UITheme.h`](../../src/ui/src/UITheme.h) currently contains the `UiDpiScale()` helper.
- `WindowShell` handles `WM_DPICHANGED`.

### Research question created by the audit

Because `UiDpiScale()` reads system DPI, validate whether all per-window layout remains correct after cross-monitor DPI transitions. This is intentionally recorded as a validation target rather than asserted as a confirmed defect.

## Real-user dogfood evidence

- [`codebase-audit/sessions/2026-08-21-dogfood/README.md`](../../codebase-audit/sessions/2026-08-21-dogfood/README.md)
- [`codebase-audit/sessions/2026-08-21-dogfood/findings.md`](../../codebase-audit/sessions/2026-08-21-dogfood/findings.md)
- [`docs/quality/USER_ACCEPTANCE.md`](../quality/USER_ACCEPTANCE.md)

### Key conclusion

The Home path-overflow finding demonstrates why visual validation is required in addition to semantic/UIA tests. The same acceptance work also identifies theme/DPI/reduced-motion visual coverage as an area requiring further executed evidence.

---

# B. Microsoft Windows design research

All links below point to official Microsoft documentation current at the time of this research (2026-08-21).

## 1. Windows design overview

**Microsoft — Design Windows apps overview**  
https://learn.microsoft.com/en-us/windows/apps/design/

### Applied guidance

- Treat Windows design as a system of navigation, layout, input, typography, motion, and accessibility rather than isolated styling.
- Use platform conventions to reduce learning cost.
- Apply guidance to FastFiles' native renderer without requiring framework migration.

## 2. Windows design principles

**Microsoft — Design principles**  
https://learn.microsoft.com/en-us/windows/apps/design/design-principles

### Applied guidance

The principles emphasize intuitive, coherent Windows experiences and the use of color/elevation to establish hierarchy. For FastFiles this supports restrained accents, deliberate layering, and a shell that feels like one product rather than several styled surfaces.

## 3. Design guidelines overview

**Microsoft — Design guidelines**  
https://learn.microsoft.com/en-us/windows/apps/design/guidelines-overview

### Applied guidance

Current Windows guidance treats color, commanding, elevation, geometry, iconography, layout, motion, navigation, typography, usability, and writing as related foundations. This is the model used for UI v3's cross-surface design system.

## 4. Navigation basics

**Microsoft — Navigation design basics for Windows apps**  
https://learn.microsoft.com/en-us/windows/apps/design/basics/navigation-basics

### Applied guidance

Microsoft highlights consistency, simplicity, and clarity, recommends avoiding unnecessary navigation depth, and expects familiar navigation placement. This directly supports simplifying FastFiles' sidebar/top chrome and keeping breadcrumb/history behavior obvious.

## 5. Commanding basics

**Microsoft — Commanding basics**  
https://learn.microsoft.com/en-us/windows/apps/design/basics/commanding-basics

**Microsoft — Commanding in Windows apps**  
https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/commanding

### Applied guidance

Commands should be exposed on surfaces appropriate to task frequency and context. This supports auditing duplication across top chrome, context menus, quick actions, shortcuts, and command palette rather than placing every command everywhere.

## 6. Layout and spacing

**Microsoft — Content layout and spacing**  
https://learn.microsoft.com/en-us/windows/apps/design/basics/content-basics

**Microsoft — Layout overview**  
https://learn.microsoft.com/en-us/windows/apps/design/layout/

**Microsoft — Responsive design techniques**  
https://learn.microsoft.com/en-us/windows/apps/design/layout/responsive-design

### Applied guidance

- Use consistent spacing to communicate grouping.
- Prefer a multiple-of-four layout rhythm.
- Design around the available app window rather than physical screen dimensions.
- Responsive behavior can reposition, resize, reflow, show/hide, or re-architect content at meaningful breakpoints.

FastFiles should translate this into explicit narrow/normal/wide/dual-pane layout states rather than merely scaling static rectangles.

## 7. Typography

**Microsoft — Typography in Windows**  
https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/typography

### Current guidance relevant to FastFiles

- Segoe UI Variable is the current Windows system UI font direction.
- Regular and semibold are the primary weights.
- Sentence case is recommended.
- 12 DIP/effective-pixel regular and 14 semibold are practical lower typography bounds in the Windows guidance.
- Ellipsis is generally preferred for truncation.
- The Windows type ramp uses clear hierarchy rather than many arbitrary sizes.

FastFiles' existing 12/14/16/20 metrics are close enough to evolve rather than replace wholesale.

## 8. Iconography

**Microsoft — Icons in Windows apps**  
https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/icons

**Microsoft — Segoe Fluent Icons font**  
https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-fluent-icons-font

### Applied guidance

Segoe Fluent Icons is the current Windows system command-icon direction. FastFiles should use Fluent icons for commands/navigation while continuing to use shell file/type icons for filesystem identity.

## 9. Motion

**Microsoft — Motion in Windows**  
https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/motion

### Applied guidance

Windows motion is described as reactive, direct, and context-appropriate. FastFiles should use short feedback/spatial transitions and preserve its existing system-animation gate. Motion should never obscure result streaming or make the file manager feel slower.

---

# C. Microsoft Windows accessibility research

## 1. Accessibility overview

**Microsoft — Accessibility overview**  
https://learn.microsoft.com/en-us/windows/apps/design/accessibility/accessibility-overview

### Applied guidance

- text must remain legible at scale;
- semantic roles matter to assistive technology;
- sufficient default-theme contrast is required rather than relying on High Contrast as a fallback;
- contrast themes should be respected rather than overridden.

## 2. Develop accessible Windows apps

**Microsoft — Develop accessible Windows apps**  
https://learn.microsoft.com/en-us/windows/apps/develop/accessibility

### Three pillars applied to FastFiles

1. programmatic access through names/roles/values/patterns;
2. complete keyboard access;
3. color/contrast support including contrast themes.

These map directly onto the existing custom UIA provider architecture.

## 3. Accessible text requirements

**Microsoft — Accessible text requirements**  
https://learn.microsoft.com/en-us/windows/apps/design/accessibility/accessible-text-requirements

### Applied guidance

Microsoft aligns ordinary visible text with a 4.5:1 contrast target and emphasizes correct semantic exposure and system scaling. FastFiles should validate token combinations rather than assuming visually subtle secondary text is acceptable.

## 4. Keyboard interactions and keyboard accessibility

**Microsoft — Keyboard interactions**  
https://learn.microsoft.com/en-us/windows/apps/design/input/keyboard-interactions

**Microsoft — Keyboard accessibility**  
https://learn.microsoft.com/en-us/windows/apps/design/accessibility/keyboard-accessibility

### Applied guidance

A desktop file manager is a keyboard-heavy productivity tool. Every core action should be available without a pointer, with logical Tab/arrow navigation and visible focus. This is both accessibility and power-user UX.

## 5. Contrast themes

**Microsoft — Contrast themes**  
https://learn.microsoft.com/en-us/windows/apps/design/accessibility/high-contrast-themes

### Applied guidance

Contrast themes are not just dark themes. Custom-drawn FastFiles surfaces must use system-aware colors/semantics and suppress normal translucent overlay assumptions when contrast mode is active.

## 6. Accessibility testing

**Microsoft — Accessibility testing**  
https://learn.microsoft.com/en-us/windows/apps/design/accessibility/accessibility-testing

**Microsoft — Accessibility checklist**  
https://learn.microsoft.com/en-us/windows/apps/design/accessibility/accessibility-checklist

### Applied guidance

Microsoft recommends combining automated accessibility gates with targeted manual assistive-technology testing. This supports keeping UIA tests in CI while adding deliberate keyboard/Narrator passes for high-risk custom surfaces.

---

# D. Microsoft Win32 DPI research

## High-DPI desktop development

**Microsoft — High DPI Desktop Application Development on Windows**  
https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows

**Microsoft — DPI awareness contexts**  
https://learn.microsoft.com/en-us/windows/win32/hidpi/dpi-awareness-context

### Applied guidance

Per-Monitor-V2 applications are responsible for correctly adjusting/rendering when DPI changes. FastFiles already declares PerMonitorV2 and handles `WM_DPICHANGED`; UI v3 should test full cross-monitor transitions and ensure all layout sources use the correct current DPI.

---

# E. W3C WCAG 2.2 references

## WCAG 2.2 standard

**W3C — Web Content Accessibility Guidelines (WCAG) 2.2**  
https://www.w3.org/TR/WCAG22/

**W3C — Understanding WCAG 2.2**  
https://www.w3.org/WAI/WCAG22/understanding/

### Use in FastFiles

WCAG is not the native Windows API specification, but its measurable criteria are useful cross-checks for custom-rendered desktop UI.

## Contrast

WCAG 2.2 1.4.3 specifies 4.5:1 for ordinary text and 3:1 for qualifying large text. This aligns with Microsoft's Windows accessibility guidance and is therefore a sensible token-validation target.

## Keyboard

WCAG 2.2 2.1.1 requires functionality to be operable through a keyboard interface except for inherently path-dependent input. This aligns strongly with Windows desktop expectations.

## Focus visible / focus appearance

**W3C — Understanding Focus Visible**  
https://www.w3.org/WAI/WCAG22/Understanding/focus-visible

**W3C — Understanding Focus Appearance**  
https://www.w3.org/WAI/WCAG22/Understanding/focus-appearance

### Applied guidance

FastFiles should always show keyboard focus distinctly from hover and selection. The stronger Focus Appearance criterion is useful as a design target for robust custom focus rings.

## Target size

**W3C — Understanding Target Size (Minimum)**  
https://www.w3.org/WAI/WCAG22/Understanding/target-size-minimum

### Applied guidance

WCAG 2.2 defines a 24×24 CSS-pixel minimum/spacing model for pointer targets. The units and conformance model are web-specific, but the usability principle is applicable to compact native toolbar controls. FastFiles should use this as a lower-bound design reference while validating actual Windows DIP hit targets.

---

# F. Deliberate conclusions from the research

## Adopt

- Windows-native typography/icon conventions.
- Multiple-of-four spacing discipline.
- Clear navigation hierarchy.
- Context-appropriate command placement.
- Responsive states based on available window width.
- restrained, meaningful motion.
- UIA + keyboard + contrast as first-class design constraints.
- objective screenshot/state validation.
- per-monitor DPI testing.

## Adapt rather than copy

- WinUI control visuals: use as design references, not mandatory implementation technology.
- Microsoft breakpoints: use the responsive methodology, but derive FastFiles breakpoints from its own content minima.
- WCAG target sizes/reflow: use as accessibility/usability benchmarks appropriate to native desktop behavior.

## Avoid

- framework migration solely to obtain fashionable controls;
- excessive Mica/Acrylic/elevation;
- copying File Explorer's exact information architecture without validating FastFiles' own differentiators;
- tiny compact controls that technically fit but are difficult to focus/click;
- animations that compete with search/file-navigation speed;
- relying on UIA success as proof that pixels are correct;
- relying on screenshots alone as proof that semantics are correct.

---

# Research takeaway

FastFiles already owns most of the low-level technology required to look excellent. The missing leverage is **design discipline plus validation discipline**.

The strongest path is therefore:

> **keep the native engine and rendering advantages, formalize the product design system, redesign the shell and primary workflows as one composition, then prove the result across visual, accessibility, DPI, keyboard, and performance evidence.**