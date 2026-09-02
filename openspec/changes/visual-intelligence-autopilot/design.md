# Design: Visual Intelligence Autopilot

## Context

FastFiles has real DWM captures (VisualCapture.psm1), UIA semantic postconditions, and a 13-state catalog. Missing layer is machine-readable visual intelligence closing the loop: semantic → screenshot → regression → analysis → fix → recapture → classification.

## Decisions

* **D1 Evidence schema v2**: extend manifest with `schemaVersion:2`, build/commit identity, environment profile (theme/density/DPI/viewport/monitor), capture method, redaction, baseline/reference identity, diff/critic/status. Retain backward compat.
* **D2 Regression engine**: deterministic perceptual diff (exact + perceptual SSIM/ΔE + region masks). Baseline per environment profile, no threshold laundering. Difference map + stats as artifacts, manifest-driven lifecycle.
* **D3 Findings**: structured record (`findingId`, severity, confidence, surface/component/region, category, observedProblem, expectedRule, userImpact, suggestedCorrection, evidence, requiresHumanTasteDecision, regressionCandidate). Categories cover clipping/overlap/alignment/spacing/hierarchy/density/typography/contrast/truncation/focus/selection/layout.
* **D4 Critic adapter**: provider-neutral `Invoke-VisualCritic` interface (image+context → findings). Separate deterministic `FakeCritic` that emits canned findings for fixture images. External provider optional; CI never requires network.
* **D5 Gallery**: native debug-only surface (`FastFiles --gallery`) rendering shared components (buttons/nav/tabs/breadcrumbs/search/rows/cards/status) in theme/density/DPI geometry matrix. No web dependency.
* **D6 Catalog**: risk-based pairwise matrix (themes light/dark, density compact/comfortable, width narrow/normal/wide, panes, DPI 100/125/150, states empty/populated/loading/error/degraded/focus/selection, content unicode/emoji). Document supported vs blocked.
* **D7 Closure**: before/after packet (images, diff, findings before/after, UIA/accessibility/perf). Both surfaces required.
* **D8 Classification**: separate `FUNCTIONAL_*`, `ACCESSIBILITY_*`, `VISUAL_REGRESSION_*`, `VISUAL_REVIEW_*` PASS/FAIL/BLOCKED/ADVISORY. Never collapsed.
* **D9 Privacy**: deterministic fixtures preferred, host-user path redaction, bounded retention outside Git, committed baselines sanitized.

## Architecture

```
UIA scenario → VisualCapture (schema v2) → Baseline compare → Findings
                                                      ↘ Critic adapter (Fake/Real) → bounded fix → recapture → classification
```

## Risks

* Model subjectivity → rubric + objective vs suggestion separation + deterministic diff first.
* Nondeterminism → environment profiles + fixture control + masks.
* Privacy → redaction + retention policy.
* Cost → cheap diff first, batched critic, no per-unit-test model calls.
