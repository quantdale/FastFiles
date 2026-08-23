# FastFiles Ship-Ready Stabilization Audit — 2026-08-23

## Scope and branch discipline

This campaign audits the current product state for ship-readiness and applies fixes only on:

`stabilization/ship-ready-2026-08-23`

Baseline: `main` at `90fa560a99217cabcf456b1198c83351c46513ed`.

`main` is intentionally untouched. The branch must remain reviewable and must not be merged until the Windows validation gates below pass.

## Executive finding

FastFiles is not an unimplemented prototype. The current baseline already contains a substantial amount of functional, security, performance, accessibility, UI Automation, storage-analysis, indexed-search, file-operation, and recovery hardening. Several old audit findings are therefore stale and were re-checked against current source before any code was changed.

The remaining ship-risk is concentrated in three categories:

1. **Product polish / local UX defects** — inconsistent native-light Storage Analysis controls, 8.3 short-name leakage in user-facing navigation text, and residual visual consistency issues.
2. **Validation / environment evidence** — live Windows UIA repeatability, clean-host installer/service validation, and final branch build/test evidence.
3. **Release trust material** — production Authenticode signer pins and a production signing certificate are external release inputs. The product correctly fails closed when those are absent. They must never be bypassed to make development look green.

## Repository-wide review map

### `src/indexsvc` — privileged raw-volume/MFT/USN broker

Reviewed against current OpenSpec and previous audit claims.

- MFT parsing includes logical and allocated-size semantics.
- USN delta enrichment and negotiated protocol downgrade paths exist.
- Privileged work remains intentionally isolated from the user-facing UI process.
- Production release capability depends on a correctly signed/pinned service binary.
- No change in this campaign weakens raw-volume authorization, signer validation, or service trust.

**Ship disposition:** code path is mature; production signing/clean elevated-host evidence remains a release prerequisite.

### `src/engine` — per-session indexing/search engine

Current code already contains fixes for several previously reported critical issues:

- engine discovery is canonicalized and signer-verified;
- snapshot publication uses immutable generation-specific mappings rather than overwriting a live mapping;
- indexed Search carries allocated-size metadata and cancellation/supersession semantics;
- volume/session configuration, include/exclude rules, reconciliation, and persistence are covered by dedicated tests/specs.

**Ship disposition:** no new critical engine defect was identified in this pass. Preserve existing security and protocol compatibility gates.

### `src/indexstore` — durable index and projections

Current implementation includes schema migration, allocated-size knownness, aggregate completeness, projection rebuild/reconciliation, and bounded hash-table behavior.

The former unbounded-probe concern in the old engineering audit is stale: the current flat hash map has bounded probing/growth behavior.

**Ship disposition:** no new release-blocking store defect identified in this pass.

### `src/protocol` / `src/ipc`

The current protocol has additive versioned representations for newer metadata while retaining pinned legacy byte layouts. Malformed-frame handling, downgrade behavior, known-vs-zero semantics, and fuzz coverage are represented in the existing test/spec set.

**Ship disposition:** preserve byte compatibility and fail-closed parsing. No protocol rewrite is justified by this UX campaign.

### `src/search`

Current Search already supports:

- indexed and degraded search modes;
- cancellation/query supersession;
- logical-size and distinct Size-on-Disk sorting;
- allocated-size known/unknown semantics;
- structured filters;
- semantic UIA exposure for virtualized results.

**Ship disposition:** no new core-search blocker identified from current source. Live branch regression remains part of acceptance.

### `src/ui` — shell, navigation, Search, Storage, settings, details, commands

This is where the current campaign found actionable local defects.

#### Navigation display hygiene — fixed on branch

Problem: canonical Windows paths can contain 8.3 aliases such as `MICHAEL~1`. Those paths are valid identity/navigation inputs, but surfacing them in breadcrumbs/tabs makes the application look broken and exposes an implementation detail.

Fix:

- Added a display-only local-drive 8.3 expansion seam using `GetLongPathNameW`.
- Breadcrumb labels resolve the long display leaf while keeping the exact canonical target path.
- Tab labels use the same display normalization.
- UNC paths are never resolved through this seam, preventing accidental network I/O in a paint/display path.
- Resolution failure is non-fatal and preserves the original label.
- Added a Windows navigation test that creates a real long-name folder and verifies the label/path split when 8.3 naming is available.

Files:

- `src/ui/src/NavigationWorkspace.h`
- `src/ui/src/NavigationChrome.h`
- `tests/navigation/test_home_location.cpp`

#### Theme system — consolidated on branch

Problem: the baseline had a centralized token system, but the palette itself still carried an older mixed neutral/accent language. This made otherwise-correct surfaces feel inconsistent.

Fix:

- Consolidated the central light/dark token palettes around a Windows-11-aligned blue accent and a deliberate neutral surface hierarchy.
- Kept all color decisions in `UITheme.h`; no per-view hard-coded replacement campaign was introduced.
- Preserved High Contrast fallbacks and existing density/typography contracts.

File:

- `src/ui/src/UITheme.h`

#### Storage Analysis native-light chrome — fixed on branch

Problem: Storage Analysis was a visual outlier. Its Back/Up and view-mode controls were native `BUTTON` / `BS_AUTORADIOBUTTON` controls and stayed system-light while adjacent report lists, headers, treemap, Search, navigation and command surfaces already used FastFiles theme tokens.

Fix:

- Kept the underlying controls as real Win32 buttons/radios, preserving native command routing, radio grouping, focus semantics, keyboard activation and UI Automation behavior.
- Added a lightweight subclass paint layer that uses FastFiles theme tokens for background, border, hover, press, disabled, selection and focus states.
- Added a non-color selected-mode cue (accent marker) instead of relying on color alone.
- High Contrast falls straight through to native system rendering.
- Back/Up and the radio-group entry point are now tab-reachable while the radio group retains native arrow-key traversal.

File:

- `src/ui/src/StorageAnalysis.h`

#### Existing UI-v3 work revalidated rather than duplicated

Current source already contains owner-drawn Search controls, two-line Search rows, semantic Search UIA, themed list headers, responsive Storage layout, treemap UIA, quick-access Home, themed navigation chrome, focus states, drag/drop feedback, status panels and details-pane token usage. Re-implementing those systems would add regression risk with no product benefit.

### `src/setup` / installer and package trust

The release signing pins are intentionally fail-closed. All-zero/unconfigured production pins are a blocker for a real privileged release package, not a defect to bypass.

**Non-negotiable:** do not add an unsigned production escape hatch, weaken `WinVerifyTrust`, relax pinned signer verification, or make release mode accept diagnostic test certificates.

### `verify/` and `.github/workflows/ci.yml`

The repository already defines a Windows CI matrix for:

- `debug` configure/build/test;
- `release-test` configure/build/test;
- `analyze` warning/static-analysis build;
- ASAN protocol + IPC fuzz targets;
- source-marker and diagnostic-policy enforcement.

The branch must use those gates as authoritative compile/test evidence. A review must not infer build success merely because the diff looks valid.

## Findings explicitly rejected as stale

The following older findings were checked against current code and are not active defects on the campaign baseline:

- engine executable discovery / planting risk — current discovery canonicalizes candidates and verifies pinned signatures;
- snapshot publication overwrite/race — current publisher uses immutable per-generation mappings and atomic generation publication;
- unbounded flat-hash probing — current map bounds probes and grows/fails appropriately;
- missing allocated-size propagation — current MFT/store/projection/protocol/Search/Storage stack already implements knownness and compatibility semantics;
- missing treemap semantic accessibility — current treemap has an explicit UIA semantic surface/provider and dedicated tests;
- Search virtualized rows inaccessible to UIA — current Search has a custom semantic fragment provider.

Any future audit must compare findings to the exact current SHA before applying recommendations from historical reports.

## Acceptance matrix for this branch

A merge is acceptable only after all applicable items below are green on the exact branch head.

### Compile / unit / static analysis

- [ ] Windows CI `debug` configure + build + all CTest PASS.
- [ ] Windows CI `release-test` configure + build + all CTest PASS.
- [ ] Windows CI `analyze` configure + build PASS with warning policy intact.
- [ ] ASAN protocol/IPC fuzz job PASS, or an infrastructure failure is clearly distinguished from a product failure.
- [ ] `check_source_markers.ps1` PASS.
- [ ] `check_diagnostic_policy.ps1` PASS.
- [ ] OpenSpec strict validation PASS.

### Focused functional regression

- [ ] Navigation tests verify canonical path identity is unchanged while 8.3 aliases are display-normalized.
- [ ] Breadcrumb ancestor clicks still navigate to the exact intended canonical path.
- [ ] Tab switching/history/reopen/dual-pane navigation remain unchanged.
- [ ] Storage Back/Up work by mouse and keyboard.
- [ ] Storage Overview/Drill Down/Largest Folders/Largest Files/By Category/Treemap remain mutually exclusive and keyboard operable.
- [ ] Storage category filter, list sorting, context menu, aggregate completion and treemap activation remain functional.
- [ ] Light -> Dark -> Follow System theme changes repaint Storage controls consistently.
- [ ] High Contrast keeps native system rendering and focus visibility.

### Live UX / UIA

- [ ] Run the existing `windows-ui-automation-validation` capability on the freshly built branch executable.
- [ ] Re-run Search result, selection/scroll, Storage, treemap, drag/drop and navigation scenario groups without converting a failure into a SKIP.
- [ ] Reproduce/bound any remaining intermittent scenario before calling the branch ship-ready.
- [ ] Capture screenshots at the repository's normal DPI/theme matrix and verify no native-light Storage chrome remains in dark mode.

### Release environment

- [ ] Authorized production signer pins are provisioned into the release build.
- [ ] Release artifacts are Authenticode signed by the expected production certificate.
- [ ] Clean elevated Windows host validates install, service configuration, raw-volume/MFT/USN path, uninstall/rollback and package integrity.
- [ ] No diagnostic unsigned bypass exists in the shipped release configuration.

## Current branch changes

1. `fix(ui): hide local 8.3 names in navigation labels`
2. `fix(ui): expand 8.3 names in tab labels`
3. `refactor(ui): unify production theme palette`
4. `fix(ui): theme Storage Analysis native controls`
5. `test(ui): cover display-only 8.3 expansion`
6. `fix(a11y): make Storage chrome keyboard reachable`
7. `test(ui): make short-name test includes explicit`

## Remaining truth at handoff

This branch fixes concrete local product defects found by the current audit, but it must not be described as production-certified until the exact branch head completes the Windows CI/live-UI validation matrix and the authorized production signing environment supplies real release trust material.

That distinction is intentional: product quality improves only when evidence is real, not when security or validation gates are weakened to manufacture a green status.
