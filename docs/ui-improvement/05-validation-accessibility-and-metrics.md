# 05 — Validation, Accessibility, and UX Metrics

## Principle

FastFiles' next UI overhaul must be **measured visually and behaviorally**. The repository already proves that functional/UIA success can coexist with a visible layout defect: the 2026-08-21 dogfood campaign caught Home path text overflowing into adjacent cards after other automated checks passed.

UI v3 therefore needs four independent evidence layers:

1. functional correctness;
2. semantic/accessibility correctness;
3. visual correctness;
4. perceived/task usability.

None can substitute for the others.

---

# 1. Deterministic visual-state catalog

Create a repeatable screenshot suite driven through existing test/UIA capability.

## Mandatory states

### Shell/navigation

- first launch;
- Home;
- This PC;
- standard folder;
- deep path;
- editable address mode;
- multiple tabs;
- many tabs/overflow;
- dual pane, left active;
- dual pane, right active;
- collapsed sidebar;
- narrow window;
- wide window.

### Browsing

- no selection;
- hover;
- selected/focused;
- selected/unfocused;
- multi-selection;
- empty directory;
- access denied;
- missing/unavailable path;
- long names;
- long paths;
- Unicode/emoji/RTL stress content where supported;
- drag target;
- inline rename.

### Search

- empty query;
- typing/query focus;
- searching/in progress;
- populated results;
- selected result;
- active filters;
- no results;
- engine degraded/unavailable;
- error.

### Preview/details

- image preview;
- text/metadata-only preview;
- unsupported preview;
- oversized/disabled preview;
- loading;
- single selection;
- multi-selection.

### Operations

- queued/starting;
- progress;
- cancellable;
- cancelled;
- completed;
- partial failure;
- conflict dialog;
- permanent-delete confirmation if present.

### Secondary workspaces

- command palette;
- storage analysis calculating;
- storage analysis populated;
- treemap hover/focus/selection;
- settings categories/long page;
- reset/destructive settings section.

## Screenshot policy

Use a controlled Windows environment for golden/reference images:

- fixed OS build or explicitly versioned image;
- fixed font availability;
- deterministic fixture data;
- fixed window dimensions;
- deterministic theme;
- animations disabled or captured after settled state;
- no personal paths/user data in committed artifacts.

Do not fail CI on raw whole-screen pixel equality across arbitrary hosts. Native text rasterization, OS patches, GPU drivers, and theme details can create benign pixel noise.

Prefer:

- deterministic geometry assertions for strict layout invariants;
- region-based/image-diff thresholds on a pinned validation environment;
- human review for broad visual acceptance;
- automatic detection of overlap/clipping/out-of-bounds rectangles where possible.

---

# 2. Theme and display matrix

The current dogfood checklist already identifies this as unfinished evidence. UI v3 should make it a blocking gate.

## Minimum matrix

| Dimension | Required coverage |
| --- | --- |
| Theme | Light, Dark, Follow System |
| Contrast | at least one Windows contrast theme plus system-color correctness checks |
| Display scale | 100%, 125%, 150%, 200% |
| Motion | Windows animations enabled and disabled |
| Window width | narrow, normal, wide |
| Pane mode | single and dual pane |
| Input | mouse, keyboard-only; touch/pen where explicitly supported/testable |

Add 175%, 225%+, multi-monitor mixed-DPI, and large text accessibility settings to extended compatibility runs.

## Per-monitor transition test

Because FastFiles declares `PerMonitorV2`, test the actual transition, not just cold launch at different scales:

1. open FastFiles on monitor A;
2. record window/client/UIA geometry;
3. move to monitor B with a different scale factor;
4. allow `WM_DPICHANGED` handling to settle;
5. verify text, controls, icons, hit targets, D2D surfaces, Win32 child controls, flyouts/dialogs, and UIA bounds all agree;
6. move back and repeat.

This test should specifically validate whether the current system-DPI-based helper remains correct in the per-monitor path.

---

# 3. Accessibility baseline

FastFiles is a native desktop application, so do not casually claim WCAG certification. Use current Microsoft Windows accessibility guidance as the implementation baseline and WCAG 2.2 criteria as useful measurable cross-checks for visual/input requirements.

## Programmatic access

Every meaningful interactive/custom-drawn element must expose appropriate:

- accessible name;
- control type/role;
- state;
- value where applicable;
- selection semantics;
- focus semantics;
- invoke/toggle/expand/collapse/scroll patterns as appropriate;
- screen bounds;
- relationships/grouping where needed.

FastFiles already has dedicated providers for core custom surfaces. UI v3 must update provider geometry when layouts change rather than treating UIA as a post-redesign patch.

## Keyboard access

Every primary scenario must be completable with the keyboard:

- reach and operate shell controls;
- navigate sidebar;
- use tabs and switch panes;
- browse columns/files;
- invoke context/global commands;
- search and edit filters;
- use preview/details controls if interactive;
- complete/cancel dialogs;
- use storage analysis;
- use settings.

### Focus requirements

- Focus must always be visible.
- Hover must never be used as the keyboard focus indicator.
- Focused selection and unfocused selection must remain distinguishable.
- Focus must not disappear into the accent background.
- Modal dialogs/flyouts must place focus intentionally and restore it sensibly when closed.
- No keyboard traps.

WCAG 2.2 Focus Visible is a good minimum reference; the stronger Focus Appearance guidance is a useful design target even where it is not a native-app conformance requirement.

## Contrast and color

Use automated token-level contrast calculations plus real screenshot inspection.

Targets:

- normal text: at least 4.5:1 in ordinary themes;
- large text: at least 3:1;
- actionable state/focus boundaries should have strong non-text contrast;
- selection/error/degraded/progress meaning must not rely only on hue;
- Windows contrast themes must be usable with system-aware colors.

## Text/display scaling

Verify:

- Windows display scaling;
- Windows text-size accessibility settings where supported by the implementation;
- no clipped labels or controls;
- no overlapping settings rows/dialog actions;
- sensible ellipsis/wrapping;
- primary functionality remains reachable.

For custom DirectWrite text, explicitly test behavior rather than assuming stock-control scaling behavior applies.

## Narrator/manual AT pass

Automation is necessary but insufficient. For each release-candidate UI wave, manually validate with Narrator at least:

- Home → open a location;
- folder browse → select/open item;
- search → result selection;
- dual-pane active-pane transition;
- file operation/conflict;
- storage analysis semantic navigation;
- settings.

Confirm spoken order, labels, state changes, and whether transient messages are announced appropriately without becoming noisy.

---

# 4. Interaction quality gates

## Latency

The design must feel immediate.

Measure at minimum:

- launch → first meaningful shell content;
- navigation input → destination presentation;
- keypress in search → query accepted/rendered;
- query change → first visible result update;
- selection change → details/preview placeholder or updated state;
- command invocation → visible acknowledgement;
- file operation start → progress/status feedback;
- window resize → settled layout;
- DPI transition → settled layout.

Do not invent hard pass/fail numbers before baseline measurement. Capture the current distribution first, then set budgets that prevent regression and target meaningful improvement.

## Frame/scroll quality

For large directories and animated scrolling:

- sample frame times under bounded workloads;
- record long frames/jank events;
- verify icon arrivals do not cause pathological relayout;
- ensure idle animation timers stop;
- ensure no continuous repaint while the UI is static.

## Visual stability

Measure or assert:

- no text outside assigned bounds;
- no overlapping control rectangles;
- minimum hit-target rectangles where specified;
- no negative/zero content areas after responsive collapse;
- active/focused item remains visible after keyboard navigation;
- dialogs remain within usable work area at supported scaling.

---

# 5. Task-based usability validation

A UI can be visually attractive and still be inefficient. Run small repeatable task studies during the redesign.

## Core task script

1. Open a recently used folder from Home.
2. Navigate several levels using columns and Back/Forward.
3. Open a second tab and switch back.
4. Enable dual pane and copy/move a file between panes.
5. Find a file using search and a filter.
6. Preview/select the result and locate its parent folder.
7. Resolve a synthetic filename conflict.
8. Inspect storage usage and drill into a large category.
9. Change theme/appearance setting and return to browsing.

## Observe

- time to first correct action;
- hesitation or misclicks;
- commands users search for;
- confusion between hover/selection/focus;
- whether active pane is clear;
- whether users understand search scope/filter state;
- whether progress/failure states are trusted;
- whether users can predict where Back will go;
- whether Home helps or slows them down.

Even 3–5 representative testers per major design iteration can expose problems that a code audit cannot. For a personal project, structured self-dogfooding with recorded tasks is still better than unstructured “looks good to me” review.

---

# 6. UX metric set

Do not collect invasive telemetry by default. Metrics can come from local test instrumentation and opt-in diagnostic runs.

## Objective metrics

### Navigation

- task completion rate;
- Back/Forward correctness rate;
- active-pane error rate;
- tab/pane command mis-target rate.

### Search

- time to first result;
- time to stable result set where measurable;
- search cancellation/replacement responsiveness;
- no-results recovery success.

### Operations

- time from command to acknowledgement;
- percentage of failed operations with actionable explanation;
- conflict resolution completion;
- cancel-state correctness.

### Accessibility

- percentage of interactive controls with expected accessible name/role/pattern;
- keyboard-only core-task completion rate;
- unresolved critical accessibility findings;
- contrast token violations;
- focus-order/focus-visibility failures.

### Visual quality

- overlap/clipping regressions;
- out-of-bounds text defects;
- unsupported-theme islands;
- DPI-specific layout defects;
- screenshot-review defects by severity.

### Performance

- input-to-visible-response latency;
- scroll frame-time distribution;
- idle CPU/repaint activity;
- memory growth across repeated navigation/search/theme cycles.

## Subjective metrics

Use a tiny post-task scorecard rather than a vague “is it nice?” question:

- I always knew where I was.
- I always knew what was selected.
- I could find the command I needed.
- Search state/results were clear.
- File operations felt trustworthy.
- The interface felt fast.
- The interface felt visually coherent.
- The interface felt comfortably dense.

Track changes between baseline and UI-v3 iterations.

---

# 7. Severity model for UI findings

### P0 — Release blocker

- core workflow impossible/inaccessible;
- destructive action ambiguity capable of causing data loss;
- major UI unusable at a supported scale/theme;
- complete keyboard/UIA loss for a primary workflow.

### P1 — Must fix before UI-v3 completion

- persistent overlap/clipping on normal content;
- active pane/selection ambiguity;
- unreadable contrast;
- major search/navigation state ambiguity;
- serious visual inconsistency affecting operation;
- significant performance regression caused by new UI.

### P2 — Important polish/usability

- inconsistent spacing/type/state treatment;
- secondary layout problems;
- awkward but recoverable workflow;
- low-frequency dialog inconsistency.

### P3 — Cosmetic

- minor optical alignment;
- low-impact animation/elevation nuance;
- tiny non-blocking visual differences.

Never classify a functional accessibility failure as merely cosmetic.

---

# 8. Pull-request acceptance template for future UI changes

Every UI implementation PR should answer:

- Which approved component/surface spec is being implemented?
- Which screenshots changed intentionally?
- What light/dark/contrast evidence exists?
- What DPI/window sizes were tested?
- What keyboard/UIA scenarios were run?
- Did performance-sensitive paths change?
- Are there new raw color/size literals? Why?
- Are any old one-off style paths now removable?
- What regressions were explicitly checked?

A screenshot plus “looks better” is insufficient, and a green unit-test run without visual evidence is also insufficient.

---

# 9. UI-v3 final acceptance matrix

The program may be considered complete when:

- all primary workflows pass functional tests;
- UIA automated scenarios pass;
- keyboard-only task script passes;
- manual Narrator pass has no unresolved critical issue;
- normal-theme text contrast meets defined targets;
- contrast-theme presentation is usable;
- supported DPI/window states have no P0/P1 layout defect;
- deterministic screenshot set is reviewed and approved;
- large-directory/search responsiveness is no worse than the accepted performance budget;
- no idle UI animation/repaint leak is present;
- real dogfooding closes with no unresolved P0/P1 UI finding.

The design is done only when the evidence says the product is coherent—not when the styling code is finished.