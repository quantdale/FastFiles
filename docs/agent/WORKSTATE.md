# Fast Files — Agent Workstate

## Current authoritative state — 2026-09-04

## NTFS observer + visual-matrix closure + soak-gate repair campaign — COMPLETE 2026-09-04 (pending hosted CI on final tip)

- Frontier: `external-certification-execution-engine-and-frontier-truthfulness` (tasks 7.1/7.2 checked this campaign; 6.3/7.3 close with the hosted-CI binding). HEAD `a8662a0` lineage (rebased onto upstream docs; code/harness byte-identical; origin/main after push; clean except pre-existing unrelated `M .opencode/package.json[.lock]`, preserved untouched).
- Defect ledger: `docs/agent/NTFS_VISUAL_CERTIFICATION_DEFECT_LEDGER_2026-09-04.md` (NV-001..NV-011 + FC resolutions, all forensically established against the live tree).
- NTFS observer (FC-003/FC-004, NV-002/005/006): `fftest ntfs-observe` (semantics/find/usn via production ioctls+parsers, read-only, 0 mutations) + runner semantics checks + observer-backed fixture confirmation + `mft/usn-observer-semantics` vocabulary (schema/manifest/frontier 4.6/4.7). Live non-admin proof: real open attempted, error 5, BLOCKED exit 2. Commit `98375b0`.
- Visual EX-009 (FC-007, NV-007): treemap degraded fallback + totals recompute + partial-known gating + matrix scope warmup + `-Preset debug-diagnostic`. Live: visual-matrix 2/2 x 14/14 PASS (10/20/30/40, drill 100%). Frontier 5.1 -> READY_EXTERNAL. Commit `343d22e`.
- Soak gate (NV-011): settled-step allowance + Test 7 warmup; 600 s interactive soak PASS (12/12, drift=2, graceful, zero orphans); 20-cycle + 45-cycle probes prove bounded caches, no leak. Commit `a8662a0`.
- FC-001: post-fix rapid-input 100/100 PASS (1100 assertions). FC-002/005: no Hyper-V build per §17 (fail-closed + declared credentials verified). NV-001/004/009 not reproduced; NV-003/010 no-change-needed/documented.
- Validation on final content: Debug 99/99 x3; release-test 99/99 x2; Analyze PASS; ASAN 6/6; OpenSpec strict 81/81; markers/policy PASS; cert suites 10/10; handshake/framing 100/100 + UiServer 25/25; lifecycle 100/100; rapid 100/100; soak 600 s PASS. Artifacts exactTreeBound=false SOLELY from the 2 unrelated .opencode files (NV-010).
- NEXT EXACT ACTION: push final tip, verify hosted CI green on the exact SHA, then register lifecycle completion (6.3) + close 7.3, push again, verify CI on the ultimate tip. Remaining frontier is external-only (elevated NTFS, physical display/DPI, provider/guest, production signing, clean-host).
- NOTE: entries below (including the IN PROGRESS FC-001..FC-009 header) are superseded checkpoint evidence until lifecycle completion lands.

## Final local certification closure (FC-001..FC-009) — IN PROGRESS 2026-09-04

Current campaign: `external-certification-execution-engine-and-frontier-truthfulness` — **ACTIVE**.

Owner scope directive: vendor-specific VM-provider execution and testing are explicitly **out of scope** for FastFiles local completion and future campaigns unless the owner explicitly reverses this directive. Generic provider lifecycle, provenance, cleanup, and fake-adapter contract tests may remain as repository infrastructure; they are not a completion gate for any specific VM provider.

### FC-001 focus race

- Root-caused and product-fixed in `b3dffc5a94ad09529016364dd5a6974d494742a6`.
- `CommandPalette::Show()` now verifies edit-focus acquisition via `EnsureEditFocus()`; `ffui/PaletteFocus.h` provides a pure readiness seam; harness retry counts were not increased.
- Regression target `ffpalette_focus_tests` added as the 98th Debug CTest target.
- Full Debug CTest: 98/98 PASS ×1.
- Post-fix authoritative rapid-input stress: CLOSED 2026-09-04 by the NTFS/visual/soak campaign -- rapid-input 100/100 PASS (1100/1100 assertions) on the palette-fix tree; see the campaign head entry above and `docs/agent/NTFS_VISUAL_CERTIFICATION_DEFECT_LEDGER_2026-09-04.md` (FC-001).

### Remaining LOCAL work

1. ~~Run post-fix rapid-input 100× minimum~~ DONE 2026-09-04: 100/100 PASS (see campaign head entry; FC-001 closed).
2. ~~Implement real default MFT and USN observation backends~~ DONE 2026-09-04: `fftest ntfs-observe` + runner wiring (commit `98375b0`; see ledger NV-002).
3. ~~Resolve visual-matrix task 5.1~~ DONE 2026-09-04: degraded treemap fallback + scope warmup + diagnostic preset; matrix 2/2 x 14/14 PASS; frontier 5.1 READY_EXTERNAL (see ledger NV-007).
4. ~~Complete the validation ladder~~ DONE 2026-09-04: Debug 99/99 x3, release-test 99/99 x2, Analyze, ASAN 6/6, cert suites 10/10, IPC 100/100/25, UIA 100/100, soak 600 s PASS, OpenSpec 81/81, markers/policy green.
5. Complete campaign tasks 6.3/7.3, reconcile lifecycle/current documentation, push normally, and bind successful hosted CI on the exact final tip. (7.1/7.2 checked; 6.3/7.3 close with CI.)

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

## Historical hosted-run bindings (lifecycle record corroboration)

Retained because `openspec/lifecycle.json` hosted-run records point at
this file and the project-status gate requires the run ID and exact SHA
to appear in the record text:

- `final-product-completion-and-production-readiness`: hosted run
  `33294518606` on exact SHA
  `0ad79a8caeab30f6c129c0f285279d1508b62c48` (6/6 SUCCESS); ledger tip
  `94a4830cb799a459844e7af421257fd525b82116` (hosted run
  `33295510209`, 6/6 SUCCESS).
- `completion-integrity-hardening-and-release-certification`: hosted run
  `33294518606` on exact SHA
  `0ad79a8caeab30f6c129c0f285279d1508b62c48` (6/6 SUCCESS).

## Next exact action

Continue the active closure campaign with post-fix rapid-input stress, real MFT/USN backends, visual-matrix context/reclassification, then full validation/lifecycle/final-tip hosted convergence. Vendor-specific VM-provider testing is intentionally skipped.
