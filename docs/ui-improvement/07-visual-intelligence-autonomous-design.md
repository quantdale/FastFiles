# 07 — Visual Intelligence and Autonomous UI/UX Design Pipeline

**Status:** planning authority for a future implementation campaign  
**Date captured:** 2026-08-31  
**Scope:** visual observability, visual regression, vision-model review, autonomous design guidance, reference-driven UI/UX improvement, and visual release gates  
**Implementation in this document:** none

## Why this document exists

FastFiles already has rigorous functional, UI Automation, dogfood, accessibility, and release verification. The remaining gap is not that the application has no visual evidence. The repository already captures real screenshots. The gap is that screenshots are mostly treated as evidence for a human reviewer rather than as a first-class machine-readable input to an autonomous repair and design loop.

A functional test can prove that a button exists, that it can be invoked, that selection changed, and that the expected UIA state is present. None of those assertions prove that the result is visually good. The application can be semantically correct while still containing clipping, misalignment, awkward spacing, hierarchy problems, stale paint, unintended overlap, poor density, inconsistent component styling, or an obviously unattractive composition.

The intended next step is therefore not another generic "make the UI better" campaign. The target is a **Visual Intelligence + Autonomous UI/UX Design Pipeline** that can observe the rendered application, compare it with known-good states and design rules, ask a vision-capable model to critique the actual pixels, generate structured defects or design proposals, implement bounded corrections, recapture the exact state, and verify the result without weakening functional or accessibility gates.

This document also addresses a second product-development constraint: the product owner should not have to provide artistic direction for every screen. FastFiles already has a design-system direction. Future agents should derive decisions from an explicit hierarchy of product rules, Windows conventions, proven reference patterns, and measurable visual review rather than requiring the owner to invent layouts or aesthetics.

## What already exists

The future campaign MUST preserve and build on the current repository instead of replacing it.

### Deterministic visual-validation capability

verify/capabilities/windows-ui-visual-validation/capability.json defines the Windows deterministic visual-validation capability. Its contract is semantic UIA postconditions followed by DWM-bounds screen-region capture, manifest generation, and duplicate-image enforcement.

verify/capabilities/windows-ui-visual-validation/VisualValidation.psm1 contains the scenario-driven visual validation implementation. It creates disposable fixtures, launches FastFiles, uses semantic UIA state to establish the intended product state, handles window/foreground behavior, sanitizes sensitive path presentation, and captures only after the semantic precondition is established.

### Real screen capture

verify/visual/VisualCapture.psm1 captures the live top-level FastFiles window from DWM extended frame bounds using screen copying. The module records scenario identity, fixture, theme, window width, pane mode, DPI, application SHA, screenshot path, image hash, semantic assertion evidence, timestamp, status, duplicate relationships, and privacy redactions.

This means FastFiles already has actual pixel capture. The missing capability is higher-level interpretation of those pixels.

### Visual catalog driver

verify/visual/Invoke-VisualCatalog.ps1 already launches the diagnostic UI, drives representative product states through the UIA helpers, sizes the application, switches states/themes, performs privacy redaction discovery, and emits a deterministic visual catalog.

The committed catalog under openspec/changes/final-product-completion-and-production-readiness/evidence/visual-catalog contains representative captures including Home, browse/column view, wide and narrow layouts, dual pane, search, expanded filters, Storage Analysis, Settings, command palette, and dark mode.

There is also substantial historical visual evidence under codebase-audit/sessions, including light/dark states, narrow/wide layouts, deep navigation, search, many tabs, long names, Unicode/emoji data, empty directories, storage views, and earlier PrintWindow-versus-screen investigation.

### Existing UI automation and headless contracts

tests/ui, tests/uia-driver, verify/uia-driver, and verify/capabilities/windows-ui-automation-validation already provide semantic test infrastructure. Future visual intelligence MUST remain layered on top of these contracts. Pixel analysis must not replace semantic UIA, filesystem truth, accessibility, or deterministic state checks.

### Existing product design direction

docs/ui-improvement/02-design-system-direction.md already defines the FastFiles UI v3 thesis: a calm, compact, high-performance Windows file-management workspace. It includes spacing, typography, density, semantic color roles, iconography, component vocabulary, interaction states, motion, responsive states, and the proposal for a native Component Gallery.

Therefore the design problem is not "invent a style from nothing." The task is to operationalize the existing design system, make it reviewable, connect it to reference sources, and enforce it through visual evidence.

## The gap

The current pipeline is strong at answering questions such as:

- Did the expected semantic element exist?
- Did the click or keyboard action work?
- Did UIA expose the correct state?
- Did the application reach the intended page?
- Was a screenshot successfully captured?
- Was the image unexpectedly identical to another scenario?

It is not yet strong at answering:

- Is anything visibly clipped, overlapping, misaligned, or incorrectly scaled?
- Is the hierarchy visually coherent?
- Does a selected row actually look selected?
- Is a toolbar excessively dense or visually unbalanced?
- Did one control drift from the rest of the component family?
- Does a dark-mode surface look unfinished even if contrast ratios technically pass?
- Is an empty/error/loading state understandable and visually intentional?
- Did a fix make the screen better rather than merely different?
- Does a new design remain consistent with the FastFiles design language?
- Can an autonomous agent identify and repair these problems without the product owner supplying artistic instructions?

The new system should answer those questions while remaining deterministic enough to audit.

## Target architecture: FastFiles UI Autopilot

The conceptual pipeline is:

1. Real-user-like UIA/dogfood scenario establishes a known semantic state.
2. The harness captures the real FastFiles window at a meaningful checkpoint.
3. Deterministic visual regression compares the capture with the approved reference for the same scenario and environment class.
4. A vision-capable critic receives the screenshot plus structured semantic context and the FastFiles design contract.
5. The critic emits machine-readable findings rather than free-form aesthetic commentary.
6. The design/review agent consults the repository design system and approved external references.
7. A bounded implementation agent changes only the relevant presentation seams.
8. The exact scenario is replayed and recaptured.
9. Before/after/reference images are reviewed together.
10. Existing UIA, accessibility, functional, performance, and filesystem-truth gates rerun.
11. The campaign records separate functional and visual classifications.

The key principle is **semantic truth first, visual truth second, repair third**. A screenshot is never accepted as proof that product behavior is correct, and semantic success is never accepted as proof that the product looks correct.

## Capture policy

Do not continuously take screenshots on a timer. That creates noise, storage cost, privacy exposure, and meaningless duplicates.

Capture at deterministic checkpoints:

- first stable frame after launch;
- after navigation to a new major surface;
- after opening or closing a major panel;
- after meaningful selection/focus state transitions;
- before and after a visual repair;
- on a failed UIA postcondition when the window is still capturable;
- on geometry anomalies;
- on visual-regression suspicion;
- at explicitly defined release-catalog states.

For long dogfood sessions, use bounded milestone sampling rather than every interaction. A scenario manifest should explain why each capture exists.

Every accepted screenshot should be tied to:

- scenario ID;
- product/build identity;
- semantic state assertion;
- theme;
- density;
- window dimensions;
- DPI class;
- pane mode;
- relevant fixture;
- timestamp;
- privacy-redaction record;
- optional baseline/reference identity.

## Deterministic visual regression

Image hashing is useful for duplicate detection but is not a sufficient regression system. The future implementation should add perceptual comparison.

The comparator should support:

- exact diff where deterministic rendering permits it;
- perceptual similarity for anti-aliasing and small rasterization changes;
- structural-region comparison for known dynamic areas;
- configurable masks for intentionally volatile pixels;
- geometry-derived regions so the test is not based on hard-coded desktop coordinates;
- difference images or heatmaps for diagnostics;
- thresholds calibrated per scenario/environment class;
- a strict policy that threshold changes require evidence rather than being used to make a failure disappear.

A visual baseline must be associated with an explicit environment profile. Do not compare 100% DPI light-mode output against a 150% DPI dark-mode baseline.

Baselines should be reviewable artifacts, but large transient run captures should not automatically be committed to Git.

## Vision-model critic

A vision-capable model should review selected captures. It should not be given a vague instruction such as "does this look good?" The input must include:

- scenario description;
- expected task/state;
- current screenshot;
- optional approved baseline;
- optional before image;
- relevant UIA geometry/element identities;
- current design-system rules;
- known intended deviations;
- target platform: modern Windows desktop;
- severity rubric.

The output should be structured. A recommended finding shape is:

- findingId
- severity: blocker / major / moderate / minor / suggestion
- confidence
- surface
- component
- region or approximate geometry
- category
- observedProblem
- expectedDesignRule
- userImpact
- suggestedCorrection
- evidence
- requiresHumanTasteDecision
- regressionCandidate

Useful categories include:

- clipping
- overlap
- alignment
- spacing
- density
- hierarchy
- typography
- iconography
- contrast/presentation
- selection/focus clarity
- state inconsistency
- responsive-layout failure
- stale/incomplete paint
- visual noise
- affordance/discoverability
- empty/error/loading state quality
- cross-surface inconsistency
- subjective-polish suggestion

The critic must distinguish objective defects from taste suggestions. A low-confidence aesthetic preference must never block a release by itself.

## Before/after visual closure

Any repair originating from visual analysis should be closed with the same scenario whenever feasible.

The closure packet should include:

- before screenshot;
- after screenshot;
- approved baseline/reference if one exists;
- deterministic diff metrics;
- vision-critic before findings;
- vision-critic after findings;
- semantic UIA result;
- relevant functional regression results;
- accessibility result where applicable;
- explicit statement of any remaining accepted deviation.

A repair is not complete merely because the code changed or the screenshot changed.

## Native Component Gallery

Implement the native Component Gallery proposed in docs/ui-improvement/02-design-system-direction.md.

The gallery should be a debug-only native surface or deterministic rendering harness, not a web Storybook dependency.

It should render the reusable FastFiles vocabulary in controlled states:

- IconButton
- TextButton
- PrimaryButton
- NavItem
- Tab
- BreadcrumbSegment
- AddressField
- SearchField
- FileRow
- SearchResultRow
- Section/Card
- StatePanel
- StatusChip
- ProgressSurface
- dialog action rows
- focus treatments
- selection focused/unfocused
- drop targets
- disabled/busy/destructive states

The gallery should support:

- light
- dark
- Follow System where meaningful
- High Contrast/contrast-theme behavior where automatable
- compact and comfortable density
- representative DPI scales
- reduced/disabled animation
- narrow/normal/wide geometry

The Component Gallery is the fastest way to turn design work from "change a screen and hunt for the state" into "inspect every component state in one deterministic place."

It should become a primary input to screenshot baselines and vision review.

## Autonomous design workflow

The product owner should not be the visual-design bottleneck. Future agents should follow a reference-driven decision process.

When a UI/UX improvement is requested, the agent should:

1. Identify the user task and information hierarchy before styling.
2. Read the relevant FastFiles design-system and surface-roadmap sections.
3. Capture and inspect the existing FastFiles state.
4. Identify objective defects separately from opportunities for redesign.
5. Consult the approved reference hierarchy below.
6. Produce a small number of candidate approaches only when the design decision is genuinely ambiguous.
7. Score candidates against product fit, native Windows conventions, consistency, accessibility, density, performance, implementation risk, and discoverability.
8. Select the strongest option based on those criteria rather than arbitrary novelty.
9. Implement through shared component/design seams whenever possible.
10. Recapture and run visual plus semantic closure.
11. Record any new reusable design rule discovered during the work.

This process removes the need for the owner to invent spacing, component shapes, layouts, states, icon treatment, or visual hierarchy from scratch.

## Reference authority hierarchy

Future design agents should use the following authority order:

1. FastFiles product behavior and repository design contract.
2. Official Microsoft Windows/Fluent/WinUI design guidance and Windows accessibility conventions.
3. Official Windows Design Kit/component guidance when applicable.
4. Proven Windows file-management patterns, including current File Explorer and strong Windows-native file-manager applications when legally and technically appropriate as references.
5. High-quality cross-platform product references and pattern libraries.
6. Mobbin or equivalent product-pattern databases when available.
7. Model-generated novel ideas.

Lower levels may inspire a solution but must not silently override higher-level platform or FastFiles constraints.

Do not blindly clone another product. References are evidence for interaction patterns, hierarchy, density, command placement, state design, and composition.

## Mobbin integration: optional accelerator, not a blocker

Mobbin is a potentially valuable addition because it can give an agent direct access to a large library of real shipped product screens and established UI patterns. This is especially useful when the product owner does not want to generate visual concepts manually.

However:

- FastFiles MUST work without Mobbin.
- A Mobbin subscription MUST NOT become a build, test, or release dependency.
- Current pricing, licensing, MCP availability, supported clients, rate limits, and allowed use MUST be re-verified when the integration campaign begins.
- The product owner should not purchase Mobbin solely because this document mentions it.

Recommended purchase decision:

Run a bounded integration/design spike first if access is available, or evaluate current product documentation and examples. Demonstrate that Mobbin materially improves at least a few real FastFiles decisions compared with the existing Windows guidance and reference workflow.

Good trial cases:

- Home/first-launch composition;
- search filters and no-results state;
- file-operation progress/conflict surfaces;
- Settings information hierarchy;
- Storage Analysis presentation;
- command-palette behavior;
- compact desktop navigation patterns.

Measure whether it reduces design iteration, improves reviewer scores, or produces stronger patterns. If the improvement is material, a subscription is justified as a development tool. If the agent can reach the same quality using official Windows references plus existing design guidance, do not make Mobbin mandatory.

## Visual reference ingestion

If external reference screenshots are used, store only what is legally and operationally appropriate. Prefer links, metadata, derived design observations, and short-lived local/reference caches over committing large third-party image collections.

For every reference-derived decision, record the principle rather than copying pixels:

- "reduce competing toolbar emphasis"
- "group search filters by task"
- "persistent selection has stronger hierarchy than hover"
- "secondary metadata uses a quieter typography role"

The design system should accumulate reusable rules, not a collage of copied screens.

## Privacy and evidence retention

The existing visual-capture system already redacts host-user filesystem paths from committed evidence. Preserve and extend that posture.

The future vision pipeline must assume screenshots can contain:

- usernames
- file/folder names
- private paths
- recent searches
- mounted volumes
- application state
- potentially sensitive preview content

Rules:

- deterministic fixtures are preferred for committed visual evidence;
- redact known sensitive regions before persistence or remote model analysis where practical;
- do not upload arbitrary real-user dogfood screenshots to an external vision provider without an explicit data-handling policy;
- transient screenshots should have bounded retention;
- large raw run dumps remain outside Git by default;
- retained findings should contain the minimum necessary visual evidence;
- model/provider identity should be recorded when a vision judgment becomes release evidence.

## Release-gate model

FastFiles should eventually report visual quality separately from functional correctness.

Recommended top-level classifications:

- FUNCTIONAL_PASS / FUNCTIONAL_FAIL / FUNCTIONAL_BLOCKED
- ACCESSIBILITY_PASS / ACCESSIBILITY_FAIL / ACCESSIBILITY_BLOCKED
- VISUAL_REGRESSION_PASS / VISUAL_REGRESSION_FAIL / VISUAL_REGRESSION_BLOCKED
- VISUAL_REVIEW_PASS / VISUAL_REVIEW_FAIL / VISUAL_REVIEW_ADVISORY / VISUAL_REVIEW_BLOCKED

A campaign must not collapse these into a single optimistic "PASS."

Objective visual regressions can be blocking. Subjective polish suggestions should normally be advisory unless the repository has promoted the relevant rule into an explicit acceptance requirement.

## Visual acceptance matrix

The future implementation should cover, as applicable:

- Light
- Dark
- Follow System
- Windows contrast/High Contrast
- compact density
- comfortable density
- narrow window
- normal window
- wide window
- single pane
- dual pane
- 100% DPI
- 125% DPI
- 150% DPI
- representative higher DPI if the host permits
- empty states
- populated states
- loading/progress
- errors
- permission/unavailable/degraded states
- keyboard focus
- selection focused/unfocused
- hover/pressed where deterministic capture is practical
- long names/paths
- Unicode/emoji/RTL-sensitive content where applicable
- many tabs
- details/preview open and closed
- search idle/results/no-results/filters
- Home
- Settings
- Storage Analysis
- command palette
- file-operation progress/conflict/closure surfaces

The matrix must be prioritized so routine CI does not become impractically expensive. Use tiers: fast deterministic checks, broader local interactive certification, and release visual certification.

## Performance constraints

Visual QA must not make FastFiles development prohibitively slow.

- Do not invoke a vision model for every unit test.
- Run cheap deterministic visual diff first.
- Use model review on changed/suspicious/high-value captures.
- Cache design rules and reference summaries, not arbitrary model judgments.
- Batch related gallery states when the provider supports multi-image review.
- Keep release-grade visual review broader than routine developer checks.
- Preserve current native rendering performance; do not redesign around screenshot-test convenience.

## Proposed implementation phases

### Phase 0 — Current-state audit and OpenSpec authority

Create a dedicated OpenSpec change for visual intelligence and autonomous design. Re-audit current main before implementation because the exact verification harness may have evolved.

### Phase 1 — Visual evidence schema v2

Extend the visual manifest with baseline identity, environment profile, diff metadata, critic status, and closure relationships without breaking existing consumers.

### Phase 2 — Perceptual regression engine

Add deterministic comparison, masks/regions, diagnostics, and baseline lifecycle rules. Establish a small high-value baseline set first.

### Phase 3 — Vision critic adapter

Add a provider-agnostic interface that can package sanitized screenshots plus semantic/design context and parse structured findings. Keep provider credentials/configuration outside the repository.

### Phase 4 — Component Gallery

Implement the native debug-only gallery and capture matrix. Use it to stabilize shared components before broad redesign.

### Phase 5 — Agent design-review workflow

Create repository instructions/skills/prompts so agents automatically consult design rules, visual evidence, reference hierarchy, and critic findings before editing UI.

### Phase 6 — Optional Mobbin/reference MCP integration

If chosen by the owner, integrate Mobbin as a reference source for development agents. Treat it as optional infrastructure with graceful absence.

### Phase 7 — Before/after repair loop

Connect critic findings to bounded repair campaigns and require same-scenario recapture plus semantic regression.

### Phase 8 — Release visual gates

Add tiered visual regression/review classifications to certification and release reporting.

## Acceptance criteria for the future program

The program is successful when:

- a real FastFiles UI state can be deterministically captured and associated with semantic truth;
- an unintended visible change in a protected scenario is detected without relying on a human noticing it;
- a vision-capable reviewer can consume a screenshot and emit structured, auditable findings;
- objective visual defects and subjective design suggestions are clearly separated;
- a visual repair can demonstrate before/after closure on the same scenario;
- the Component Gallery makes shared component states reviewable in one place;
- agents can improve common UI/UX problems without needing the owner to invent the design;
- reference use follows the authority hierarchy and does not devolve into random visual imitation;
- Mobbin, if purchased, improves the design workflow but remains optional;
- privacy controls prevent accidental persistence or external analysis of sensitive real-user content;
- release reports distinguish functional, accessibility, visual-regression, and visual-review outcomes;
- existing UIA, functional, filesystem-truth, performance, and accessibility guarantees are not weakened.

## Non-goals

This program does not authorize:

- a rewrite to WinUI, WPF, Electron, Qt, or another framework;
- replacing UIA with pixel automation;
- making vision-model opinion the sole release authority;
- sending arbitrary personal screenshots to external services;
- committing huge visual datasets to Git;
- making Mobbin mandatory;
- copying another application's visual identity wholesale;
- accepting visual polish that damages responsiveness, accessibility, keyboard behavior, or file-management correctness;
- hiding failed visual checks behind threshold changes.

## Key risks

### Model subjectivity

Mitigate with a structured rubric, confidence, explicit distinction between objective defects and suggestions, and deterministic diff/UIA evidence.

### Screenshot nondeterminism

Mitigate with environment profiles, fixture control, window geometry control, DPI/theme-specific baselines, masks, and calibrated perceptual thresholds.

### Privacy

Mitigate with deterministic fixtures, redaction, bounded retention, provider policy, and explicit remote-analysis rules.

### Design drift

Mitigate by making the repository design system the highest product-design authority and updating reusable rules when a decision is accepted.

### Cost and latency

Mitigate with staged checks: cheap diff first, model review only where valuable, batching, and separate developer versus release tiers.

### Overfitting to screenshots

Mitigate by preserving task-based dogfood, UIA, accessibility, performance, and filesystem truth as independent gates.

## Decision record from the 2026-08-31 discussion

The following intentions are authoritative for the next campaign:

- The owner specifically wants FastFiles to be able to **look at its own rendered UI**, not merely infer visual quality from logs or semantic automation.
- Existing screenshot infrastructure should be extended rather than discarded.
- Vision-model review is desired so the agent itself can inspect screenshots and judge visual defects.
- The owner does not want lack of personal UI/UX design expertise to block product quality.
- Future design should therefore become system-driven, reference-driven, and agent-reviewable.
- Mobbin is considered potentially worth purchasing if it materially improves FastFiles, but the purchase is not yet a dependency or commitment.
- The entire concept should remain in the repository so it can be resumed in a later development session.

## Next-session instruction

Do not begin by changing random UI code.

First read this document together with:

- docs/ui-improvement/README.md
- docs/ui-improvement/01-current-state-audit.md
- docs/ui-improvement/02-design-system-direction.md
- docs/ui-improvement/03-surface-by-surface-roadmap.md
- docs/ui-improvement/04-implementation-and-migration-plan.md
- docs/ui-improvement/05-validation-accessibility-and-metrics.md
- docs/ui-improvement/06-research-references.md
- verify/visual/VisualCapture.psm1
- verify/visual/Invoke-VisualCatalog.ps1
- verify/capabilities/windows-ui-visual-validation/VisualValidation.psm1
- the latest visual-catalog evidence
- docs/agent/VISUAL_INTELLIGENCE_HANDOFF_2026-08-31.md

Then re-audit current main and create the dedicated OpenSpec proposal/design/tasks/spec deltas for the implementation campaign. Treat the current repository as authority over any stale path or assumption recorded here.
