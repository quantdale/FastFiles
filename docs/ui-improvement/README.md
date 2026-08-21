# FastFiles UI/UX Improvement Program

**Status:** research and planning only  
**Branch:** `docs/ui-improvement-roadmap`  
**Baseline:** `main` at `1d4fb45cfc4029911a4626c6fbd35e5f0499f161`  
**Implementation status:** **NO UI code changes are part of this branch.**

## Purpose

This documentation defines a repository-specific plan for making FastFiles look and feel like a polished modern Windows file manager rather than incrementally adding more styling to individual custom-drawn surfaces.

FastFiles already completed a substantial `modernize-ui-appearance` change. The current tree has a centralized `UITheme`, spacing/type metrics, `UiStyle`, animation infrastructure, real file icons, themed search, styled Win32 chrome, dark-mode handling, High Contrast fallbacks, UI Automation providers, and Windows 11 window treatment. Therefore the next UI effort must **not** simply repeat “make it Fluent.” The remaining problem is broader: visual hierarchy, composition, density, interaction consistency, information architecture, responsive behavior, cross-surface state design, and objective visual validation.

The goal is a coherent **FastFiles UI v3** built on the existing native architecture.

## Product direction

FastFiles should feel:

- **Native:** unmistakably Windows, using familiar Windows interaction conventions without mechanically cloning File Explorer.
- **Content-first:** file names, location, selection, search results, and operations dominate; decorative chrome recedes.
- **Fast:** visual polish must preserve the performance advantage of the existing Direct2D/DirectWrite pipeline.
- **Dense but calm:** efficient for keyboard/mouse power users without looking cramped or noisy.
- **Predictable:** the same command, selection, focus, hover, error, loading, and empty-state semantics across every surface.
- **Accessible by construction:** keyboard, UI Automation, contrast themes, visible focus, scaling, and reduced-motion behavior are release gates, not cleanup work.
- **Incrementally migratable:** no full framework rewrite is required to obtain a dramatically better experience.

## What this program changes conceptually

The previous modernization primarily established visual primitives. UI v3 should establish a **product-level design system** and apply it as a system:

1. A single shell silhouette and hierarchy.
2. A small, explicit component vocabulary.
3. A defined density model rather than ad hoc dimensions.
4. Standard state contracts for controls and content surfaces.
5. Responsive rules for narrow, normal, wide, and dual-pane layouts.
6. A first-class visual QA process with deterministic screenshots and accessibility evidence.
7. A staged migration that keeps existing behavior and performance contracts intact.

## Non-goals

This plan does **not** recommend:

- rewriting FastFiles in WinUI 3, WPF, Electron, Qt, or another framework as the first step;
- changing the engine/index/search architecture merely to improve appearance;
- replacing working UI Automation semantics with visual-only custom controls;
- adding heavy animation, glass effects, or visual novelty that harms clarity or latency;
- copying every current File Explorer layout decision;
- changing file-operation semantics, search semantics, or persistence behavior unless the UX requires a separately specified behavior change.

A framework migration may be evaluated later only if measured UI engineering costs remain unacceptable after the design system and surface architecture are cleaned up.

## Documentation map

| Document | Purpose |
| --- | --- |
| [`01-current-state-audit.md`](01-current-state-audit.md) | Repository audit, existing strengths, current UX risks, and the gaps the earlier modernization did not solve. |
| [`02-design-system-direction.md`](02-design-system-direction.md) | Target visual language, component/state model, typography, spacing, density, color, iconography, motion, and responsive rules. |
| [`03-surface-by-surface-roadmap.md`](03-surface-by-surface-roadmap.md) | Concrete recommendations for Home, navigation, columns, tabs/dual pane, search, preview/details, settings, commands, file operations, and storage analysis. |
| [`04-implementation-and-migration-plan.md`](04-implementation-and-migration-plan.md) | Phased future implementation sequence, file ownership map, dependencies, risk controls, and acceptance gates. |
| [`05-validation-accessibility-and-metrics.md`](05-validation-accessibility-and-metrics.md) | Visual QA, keyboard/UIA/Narrator validation, DPI/theme matrix, usability studies, and measurable success criteria. |
| [`06-research-references.md`](06-research-references.md) | Primary-source Windows/W3C research and repository evidence used to shape the plan. |

## Executive priorities

### P0 — Establish the visual truth

Before changing styling again, create a deterministic baseline of the current app at representative states and dimensions. The repository already has strong UIA infrastructure; use it to drive screenshots for Home, browsing, selection, dual pane, search, empty/error states, preview, storage analysis, settings, conflict dialogs, and file-operation progress.

This matters because the prior appearance work is marked complete, yet the product is still perceived as visually poor. That means implementation checkboxes are no longer an adequate proxy for design quality.

### P0 — Fix the shell hierarchy

The top chrome, tabs/address/navigation, sidebar, primary file surface, and details pane must read as one composition. Do this before polishing secondary dialogs. A good shell makes the whole product feel intentional; a poor shell makes every feature look bolted on.

### P0 — Standardize states and components

Create explicit reusable specifications for:

- icon button;
- text button;
- navigation item;
- tab;
- breadcrumb segment;
- text/search field;
- file row;
- selected file row;
- card/section;
- status chip;
- progress surface;
- empty/error/loading state;
- dialog action row;
- focus treatment.

Every component must define rest, hover, pressed, focused, selected, disabled, busy, destructive, and High Contrast behavior where applicable.

### P1 — Redesign the primary workflows

Apply the system to the workflows that dominate perceived quality:

1. first launch/Home;
2. folder navigation and selection;
3. tabs and dual-pane;
4. search and filtering;
5. preview/details;
6. copy/move/delete/conflict/progress feedback.

### P1 — Make adaptive layout a contract

FastFiles already handles a details-pane disclosure behavior and Per-Monitor-V2 awareness. UI v3 should define explicit layout states for narrow, normal, wide, and dual-pane windows so controls do not merely shrink until they become crowded.

### P1 — Make accessibility and visual QA blocking gates

The current dogfood ledger leaves the light/dark/High Contrast/reduced-motion/DPI matrix pending. UI v3 should turn that matrix into a repeatable gate and combine it with keyboard-only and UIA verification.

## Definition of success

UI v3 is successful when all of the following are true:

- The app has one recognizable shell hierarchy across Home, browsing, search, and storage analysis.
- Visual state behavior is consistent across custom Direct2D and hosted Win32 controls.
- No primary action depends on color alone.
- Keyboard focus is always visible and predictable.
- Light, dark, Follow System, and Windows contrast themes are usable.
- The app survives representative DPI and window-size transitions without clipping, overlap, stale scale, or inaccessible controls.
- Long file names/paths, Unicode, empty results, permission failures, degraded engine states, and file-operation failures have deliberate presentation states.
- Scroll/search/navigation responsiveness does not regress from the current native implementation.
- Visual acceptance is based on reproducible screenshots and task-based review, not only unit-test completion.
- The work can be shipped incrementally without a big-bang rewrite.

## Recommended program sequence

**Research baseline → Design system contract → Shell redesign → Browse/Home → Search/commands → Preview/file operations → Storage/settings/dialogs → accessibility/performance polish → full acceptance matrix.**

Do not start by repainting every control. Establish the shell and component rules first, then migrate surfaces in priority order.