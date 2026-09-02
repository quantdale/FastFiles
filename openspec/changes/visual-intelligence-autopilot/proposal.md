# Proposal: Visual Intelligence Autopilot

## Why

Hosted CI is green but visual quality remains human-judged. The repo already captures DWM screenshots with semantic UIA postconditions, but there is no deterministic visual regression, no structured findings, no provider-neutral vision critic, and no native component-gallery matrix. Visual regressions currently escape until a human notices.

## What Changes

* Add Visual Evidence Schema v2 (baseline identity, environment profile, diff/critic/status).
* Add deterministic perceptual regression engine (baseline lifecycle, diff map, thresholds).
* Add structured Visual Findings schema (objective vs subjective, severity/confidence).
* Add provider-neutral Vision Critic adapter + deterministic FakeCritic for CI.
* Add native debug Component Gallery surface sharing production rendering.
* Expand deterministic Visual Catalog from ~13 to ~20+ scenario matrix.
* Implement before/after visual closure workflow and tiered visual release classifications.
* Add privacy/redaction retention policy and performance isolation.
* Promote core 10/10 UIA smoke into retained verifier.

## Impact

* New: `verify/visual/*` schema, comparison engine, findings, critic adapter, gallery harness.
* Modified: `UiAutomationValidation`, `Invoke-VisualCatalog`, shell/theme seams only where a confirmed before/after defect justifies it.
* No new runtime dependency on external vision provider; CI uses FakeCritic.

## Non-Goals

* Rewrite to WinUI/Electron/Storybook, replace UIA with pixels, make model sole release authority, make Mobbin mandatory, commit large screenshot corpora.
