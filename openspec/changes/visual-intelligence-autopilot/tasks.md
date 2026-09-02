# Tasks: Visual Intelligence Autopilot

## 1. OpenSpec authority
- [x] 1.1 Create change `visual-intelligence-autopilot` with proposal/design/tasks
- [x] 1.2 Register in `openspec/lifecycle.json` as `active`
- [ ] 1.3 Validate `openspec validate --all --strict` remains green

## 2. Visual evidence schema v2
- [x] 2.1 Define JSON schema `verify/visual/schemas/visual-evidence-v2.schema.json`
- [x] 2.2 Extend `VisualCapture.psm1` / `Invoke-VisualCatalog` to emit v2 fields (baseline/diff/critic)
- [ ] 2.3 Add tests for schema validation

## 3. Deterministic regression engine
- [x] 3.1 Implement `verify/visual/Compare-VisualBaseline.ps1` (dimensions, exact diff, perceptual threshold, diff map, stats)
- [x] 3.2 Define baseline manifest lifecycle (`verify/visual/BaselineManifest.psm1`)
- [ ] 3.3 Add fixture comparison tests (FakeCritic bypass)

## 4. Structured findings
- [x] 4.1 Define schema `verify/visual/schemas/visual-finding.schema.json`
- [x] 4.2 Implement `verify/visual/VisualFindings.psm1` (emit/validate)
- [ ] 4.3 Keep objective vs subjective separation in adapter

## 5. Vision critic adapter
- [x] 5.1 Implement `verify/visual/CriticAdapter.psm1` (provider-neutral, FakeCritic, real-provider seam)
- [x] 5.2 Add deterministic fake critic test
- [ ] 5.3 Document provider opt-in (no required network)

## 6. Native component gallery
- [ ] 6.1 Add debug surface `FastFiles --gallery` (shared components, theme/density geometry)
- [ ] 6.2 Capture gallery matrix via `Invoke-VisualCatalog -Gallery`

## 7. Visual catalog expansion
- [ ] 7.1 Audit existing 13-state catalog and expand to ~20+ states (dual-pane, focus, Unicode/emoji, DPI)
- [ ] 7.2 Track supported/blocked/external per design matrix

## 8. Before/after closure
- [ ] 8.1 Implement packet generation (`verify/visual/New-VisualClosure.ps1`)
- [ ] 8.2 Require before/after on shipped visual fix

## 9. Release classifications
- [ ] 9.1 Implement `verify/visual/VisualReleaseClassification.psm1`
- [ ] 9.2 Wire into `verify.ps1` report (separate functional/accessibility/visual legs)

## 10. Privacy + performance
- [ ] 10.1 Enforce redaction before external critic, bounded retention, no per-frame model calls

## 11. Smoke promotion
- [ ] 11.1 Promote `build/_core_smoke.ps1` 10/10 scenarios into `verify/capabilities/windows-ui-automation-validation/`
## 12. CI integration
- [ ] 12.1 Add schema/fixture/fake-critic gates (no flaky screenshot gate)
- [ ] 12.2 Keep BLOCKED_ENVIRONMENT truthful where desktop not available
