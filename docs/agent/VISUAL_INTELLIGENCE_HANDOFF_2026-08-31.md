# FastFiles Visual Intelligence — Next-Session Handoff

**Captured:** 2026-08-31  
**Status:** concept preserved; implementation intentionally deferred  
**Primary planning authority:** docs/ui-improvement/07-visual-intelligence-autonomous-design.md

## Objective

Resume FastFiles with a dedicated campaign that turns the existing screenshot/visual-validation infrastructure into a closed-loop visual-intelligence and autonomous UI/UX design system.

The desired outcome is not merely more screenshots. The desired outcome is that an AI agent can inspect actual FastFiles screenshots, identify visual defects and design weaknesses, use the repository's design system plus high-quality references to choose improvements, implement bounded fixes, and prove before/after visual closure without weakening functional, UIA, accessibility, performance, or filesystem-truth guarantees.

## Important discovery from the prior session

FastFiles already has real visual capture.

Relevant current paths include:

- verify/capabilities/windows-ui-visual-validation/capability.json
- verify/capabilities/windows-ui-visual-validation/VisualValidation.psm1
- verify/visual/VisualCapture.psm1
- verify/visual/Invoke-VisualCatalog.ps1
- verify/uia-driver/
- tests/ui/
- tests/uia-driver/
- openspec/changes/final-product-completion-and-production-readiness/evidence/visual-catalog/
- historical visual evidence under codebase-audit/sessions/

The current screenshot system uses real DWM window bounds and screen capture, associates images with semantic UIA assertions, records metadata/hashes, detects unexpected duplicate captures, and performs privacy redaction.

Therefore do NOT start by inventing another screenshot framework.

The missing layer is primarily:

1. perceptual visual regression and baseline lifecycle;
2. machine-vision interpretation of screenshots;
3. structured visual findings;
4. before/after visual closure;
5. a native Component Gallery;
6. an agent design-review workflow;
7. optional reference-provider/Mobbin integration;
8. separate visual release classifications.

## Design authority

FastFiles already has a design-system direction at docs/ui-improvement/02-design-system-direction.md.

The owner does not want to be required to invent UI layouts or aesthetics. The implementation should make design decisions through an authority hierarchy:

1. FastFiles product/design contract;
2. official Windows/Fluent guidance;
3. official Windows component/design-kit guidance;
4. proven Windows file-management patterns;
5. strong external product-pattern references;
6. Mobbin or an equivalent reference database if available;
7. model-generated novel ideas.

Do not replace product judgment with random model taste.

## Mobbin decision

Mobbin is an optional accelerator.

The owner is willing to consider paying for it if it materially improves the app, but it is viewed as expensive enough that the purchase should be justified.

Next session should:

- verify current Mobbin product/pricing/licensing/MCP details before relying on them;
- determine how it can integrate with the development agent actually being used;
- run or design a bounded value spike around real FastFiles surfaces;
- keep all FastFiles build/test/release workflows functional without Mobbin;
- recommend purchase only if the reference access measurably improves design quality or reduces iteration.

Do not block the OpenSpec plan on Mobbin availability.

## First tasks for the next agent

1. Sync and inspect current main; do not assume repository state is unchanged.
2. Read the full UI-improvement documentation series, especially document 07.
3. Reinspect the latest visual-capture capability and current visual catalog.
4. Determine whether any visual-intelligence work landed after this handoff.
5. Create a new implementation/planning branch for this campaign.
6. Create a dedicated OpenSpec change with proposal, design, tasks, and relevant spec deltas.
7. Define the visual evidence schema changes before touching product UI.
8. Define perceptual-diff/baseline policy.
9. Define a provider-agnostic vision-critic interface and structured finding schema.
10. Define privacy/redaction and screenshot-retention policy for model review.
11. Plan and implement the native Component Gallery in a bounded phase.
12. Define the agent design-review/reference workflow.
13. Evaluate optional Mobbin integration independently of core functionality.
14. Add before/after closure and tiered visual release gates.
15. Validate that UIA/accessibility/performance/functionality remain independent blocking evidence.

## Recommended campaign name

Visual Intelligence + Autonomous UI/UX Design Pipeline

Suggested branch naming examples:

- plan/visual-intelligence-autonomous-design
- feature/visual-intelligence-pipeline

Use the repository's current branch conventions if they differ.

## Core invariant

**Semantic success is not visual success, and visual success is not functional success.**

The future verifier should report these independently. Never let a good screenshot substitute for semantic/product correctness, and never let passing UIA/log tests substitute for inspection of the rendered product.

## Expected pipeline

UIA/dogfood scenario  
→ semantic postcondition  
→ real screenshot  
→ deterministic/perceptual visual diff  
→ vision-model critic  
→ structured findings  
→ design/reference review  
→ bounded implementation  
→ exact-scenario recapture  
→ before/after/reference review  
→ UIA + functional + accessibility + performance regression  
→ explicit visual and functional classifications

## High-value initial scenarios

Start with a small representative baseline rather than every possible state:

- Home: light, dark, narrow, normal, wide
- Browse/column view
- single pane and dual pane
- selection/focus
- Fast Search idle/results/no-results/filters
- Settings
- Storage Analysis
- command palette
- details/preview open and closed
- long-name and Unicode fixtures

Expand only after the pipeline is trustworthy.

## What not to do

- Do not perform another vague "make it prettier" pass.
- Do not replace semantic UIA with coordinate/pixel automation.
- Do not make a vision model the sole release judge.
- Do not make Mobbin mandatory.
- Do not commit arbitrary large screenshot collections.
- Do not upload personal dogfood captures to external vision services without an explicit privacy policy.
- Do not modify visual-diff thresholds merely to force a pass.
- Do not rewrite the UI framework as part of this work unless a separate evidence-backed decision authorizes it.
- Do not ask the owner to supply artistic choices that can be resolved from the design system, references, and measured review.

## Completion target for the next planning session

The next planning session should end with a detailed, repository-grounded OpenSpec implementation master plan that is executable by an autonomous engineering agent and that explicitly maps:

- existing files to reuse;
- new files/modules;
- schemas;
- interfaces;
- scenario/baseline policy;
- vision-provider abstraction;
- Component Gallery work;
- agent instructions;
- privacy rules;
- optional Mobbin integration;
- tests;
- rollout phases;
- failure classifications;
- acceptance gates;
- rollback.

No feature implementation is required merely to satisfy this handoff; the immediate purpose is to make the implementation plan precise enough that a subsequent long-running campaign can execute without losing the original intent.
