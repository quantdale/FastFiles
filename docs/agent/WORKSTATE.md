# Fast Files — Agent Workstate

## Current authoritative state — 2026-09-04

Current campaign: `external-certification-execution-engine-and-frontier-truthfulness` — **ACTIVE**.

Owner scope directive: vendor-specific VM-provider execution and testing are explicitly **out of scope** for FastFiles local completion and future campaigns unless the owner explicitly reverses this directive. Generic provider lifecycle, provenance, cleanup, and fake-adapter contract tests may remain as repository infrastructure; they are not a completion gate for any specific VM provider.

### FC-001 focus race

- Root-caused and product-fixed in `b3dffc5a94ad09529016364dd5a6974d494742a6`.
- `CommandPalette::Show()` now verifies edit-focus acquisition via `EnsureEditFocus()`; `ffui/PaletteFocus.h` provides a pure readiness seam; harness retry counts were not increased.
- Regression target `ffpalette_focus_tests` added as the 98th Debug CTest target.
- Full Debug CTest: 98/98 PASS ×1.
- Post-fix authoritative rapid-input stress of at least 100 consecutive iterations is still required.

### Remaining LOCAL work

1. Run post-fix rapid-input 100× minimum (200× preferred if runtime remains useful), stopping on the first failure and preserving evidence.
2. Implement real default MFT and USN observation backends for privileged certification; keep raw-volume access read-only and retain fixture-to-filesystem ground-truth comparison.
3. Resolve visual-matrix task 5.1 by providing an engine-connected UIA/indexed context or proving the runner is locally complete and truthfully reclassifying the remaining requirement as external.
4. Complete the validation ladder: Debug ×3 total, release-test ×2, Analyze, ASAN, certification tests, generic provider-contract tests, NTFS tests, visual contracts, OpenSpec/status, and exact-tree qualification.
5. Complete campaign tasks 6.3/7.1/7.2/7.3, reconcile lifecycle/current documentation, push normally, and bind successful hosted CI on the exact final tip.

### Explicitly excluded from local completion

- Vendor-specific VM-provider guest execution/testing.
- Provider-specific guest images, guest credentials, guest-channel execution, and provider-specific rollback qualification.

Do not reintroduce those items into the remaining-local-work list, validation ladder, or future FastFiles campaign prompts unless the owner explicitly changes scope.

### Genuinely external prerequisites still relevant to in-scope certification

- production signer/pins;
- elevated disposable NTFS environment for real privileged execution;
- clean disposable host where required;
- physical DPI/contrast/display matrix;
- real vision provider if still required by the visual-review lane.

### Historical state

Prior campaign reports and historical evidence remain available in their OpenSpec change directories and Git history. They are historical records, not current instructions. When historical prose disagrees with this section or generated project status, current lifecycle/project-status truth wins.

## Next exact action

Continue the active closure campaign with post-fix rapid-input stress, real MFT/USN backends, visual-matrix context/reclassification, then full validation/lifecycle/final-tip hosted convergence. Vendor-specific VM-provider testing is intentionally skipped.
