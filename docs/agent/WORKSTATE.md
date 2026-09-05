# Fast Files — Agent Workstate

## index-durability-false-success-closure — ACTIVE 2026-09-05 (post-certification audit PA-001..PA-006; local validation green, commit/push/CI pending)

- Provenance: `docs/agent/POST_CERTIFICATION_PRODUCT_AUDIT_2026-09-05.md` (PA-001..PA-006); OpenSpec change `openspec/changes/index-durability-false-success-closure/` (proposal/design/spec/tasks, D1–D6); lifecycle entry `active` in `openspec/lifecycle.json`.
- Implementation COMPLETE: protocol `ScanFailed = 22` + `{2,2}` gate; service `SendScanTerminal` + EOF-vs-fault enumeration truth; engine completion trust model (stale/paused/batch-failed terminals non-authoritative, `OnScanFailed` never marks); reconciliation identity (`AbortReconciliationPass`, deferred-safe `TriggerReconciliation`, USN seen-set, journal-failure reconciliation request); store `IntegrityVerdict` (prepare-failure classification via `sqlite3_extended_errcode`) + fail-closed open without deletion license on inconclusive; `Main.cpp` bounded retry (3 attempts/500 ms, delete only on Corrupt); 42–46 diagnostics.
- Tests: 5 VSM + 3 pipeline + 3 store + protocol gates + command-surface rejection + new `ffvolumescanner_terminal_tests` (legacy `{2,1}` fallback proof). Fixture lessons: WAL readers ignore `BEGIN EXCLUSIVE` (use `locking_mode=EXCLUSIVE` + active read); page-1 b-tree damage fails integrity-check *prepare* (classify the connection code); pure-garbage bytes read as empty DB; `/analyze` C6326 on constant enum compare (volatile reads); `#define private public` cannot call cross-TU private methods on MSVC (mangled `Q` vs `A`).
- Local validation green: Debug CTest 100/100 ×2 consecutive (passes 2–3; pass 1 drift failure from LF-in-CRLF edits, fixed via normalization), release-test 100/100 ×1, `/analyze` warning-clean, ASAN 4/4 (fuzz/mft/usn/terminal-scan), OpenSpec strict 82/82, tasks 1.1–7.4 checked (7.5 pending).
- NEXT EXACT ACTION: commit in waves (protocol+service → engine completion → reconciliation → store verdict → tests/docs) without staging `.opencode/*`; push `main`; watch hosted CI green on final tip; then lifecycle `completed` + 7.5 + push + CI.

## Current authoritative state — 2026-09-05 (final provenance + documentation-truth closure COMPLETE)

Terminal classification: `CERTIFICATION READINESS COMPLETE — EXTERNAL EXECUTION PENDING`.

## NTFS observer + visual-matrix closure + soak-gate repair campaign — COMPLETE 2026-09-04 (tip CI since verified green)

- Closure tip: `630960bec2051649fe511d02eebb133b846e41ad` (commit A: exact-tree evidence + docs scrub + guard), pushed normally; HEAD == origin/main. Primary worktree still dirty ONLY with the 2 pre-existing unrelated `.opencode/package.json[.lock]` edits (preserved untouched: not staged, not committed, not reverted, not overwritten).
- Hosted CI on the closure tip verified live: run `33932827458` (push/main/`630960bec2051649fe511d02eebb133b846e41ad`) completed SUCCESS 6/6 (debug, release-test, analyze, asan-protocol-ipc-lifecycle, repository-hygiene-and-contracts, unsigned-release-candidate-qualification). Lifecycle record now binds this run. Prior tip run `33899848739`/`5dbe234` and implementation run `33897389042`/`227902eaecfdcaccd733d328ec6e63bb56c57d82` retained below as history.
- Campaign `external-certification-execution-engine-and-frontier-truthfulness`: `completed` in `openspec/lifecycle.json`, all tasks checked. No active planner prompt (`.agent/EXECUTION_PROMPT.md` ARCHIVED/FROZEN) — this session runs only the narrow terminal closure: exact-tree evidence, docs scrub + guard, WORKSTATE convergence, final-tip CI.
- Defect ledger: `docs/agent/NTFS_VISUAL_CERTIFICATION_DEFECT_LEDGER_2026-09-04.md` (NV-001..NV-011 + FC resolutions, all forensically established against the live tree).
- NTFS observer (FC-003/FC-004, NV-002/005/006): `fftest ntfs-observe` (semantics/find/usn via production ioctls+parsers, read-only, 0 mutations) + runner semantics checks + observer-backed fixture confirmation + `mft/usn-observer-semantics` vocabulary (schema/manifest/frontier 4.6/4.7). Live non-admin proof: real open attempted, error 5, BLOCKED exit 2. Commit `98375b0`.
- Visual EX-009 (FC-007, NV-007): treemap degraded fallback + totals recompute + partial-known gating + matrix scope warmup + `-Preset debug-diagnostic`. Live: visual-matrix 2/2 x 14/14 PASS (10/20/30/40, drill 100%). Frontier 5.1 -> READY_EXTERNAL. Commit `343d22e`.
- Soak gate (NV-011): settled-step allowance + Test 7 warmup; 600 s interactive soak PASS (12/12, drift=2, graceful, zero orphans); 20-cycle + 45-cycle probes prove bounded caches, no leak. Commit `a8662a0`.
- FC-001: post-fix rapid-input 100/100 PASS (1100 assertions). FC-002/005: no vendor-specific VM-provider build per owner scope directive (fail-closed + declared credentials verified). NV-001/004/009 not reproduced; NV-003/010 no-change-needed/documented.
- Exact-tree evidence status: retained UIA/IPC/soak JSON artifacts record `exactTreeBound=true` but bind older implementation SHAs (e.g. `39b873a`, `1a29ef5`); the post-fix rapid-input 100/100 and interactive-soak 600 s passes live only in untracked run trees plus NV-010 dirty-tree prose. Authoritative regen COMPLETE in clean cert worktree `C:/ff-cert` (branch `cert/final-exact-tree` at `5dbe234`, `status --porcelain` empty throughout; tree `8f648afcbcd04a0a211c921fd168a37cc1d523c4`). Committed as `openspec/changes/external-certification-execution-engine-and-frontier-truthfulness/evidence/exact-tree-5dbe234/` (SUPERSESSION.md + manifest + sidecars; consistency `-RequireExactTree` PASS). Product `src/` and `verify/` are byte-identical between `5dbe234` and the closure tip; the only test-tree delta is the new drift-guard script, itself validated (Debug 99/99 re-verified in-primary on final content). Environment finding: a detached HEAD breaks `ffproject_status_ps_tests` (empty Branch parameter) — cert branch used instead; commit/tree binding unaffected.
- Docs scrub 2026-09-05: excluded vendor-specific VM technology name removed from all tracked `.md` prose (14 files across docs/openspec/.agent/CODE_INDEX/codebase-audit); implementation symbols (`.ps1`/`.psm1`/`.json` provider ids, adapter paths, executed commands incl. historical `external-frontier-summary.json`) intentionally preserved per scope. Durable guard `Test-ExcludedProviderTerminology` added to `tests/drift/check_doc_drift.ps1` (`ffdoc_drift_tests`); green after this rewrite. Prior broad `verify/README.md` rewrite attempts failed hosted CI (runs `33865833834`/`33865755337`/`33865780253`); this closure uses minimal token-level replacements only and validates all gates locally before push.
- Remaining frontier is external-only (elevated NTFS, physical display/DPI, external environment provider, production signing, clean-host).
- NEXT EXACT ACTION: No deterministic local certification engineering remains. Execute the retained external frontier only when the required authorized external capabilities become available.
- NOTE: entries below (including the IN PROGRESS FC-001..FC-009 header) are superseded checkpoint evidence; the head entry above is current.

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
- `external-certification-execution-engine-and-frontier-truthfulness`
  exact-tree closure: hosted run `33932827458` on exact SHA
  `630960bec2051649fe511d02eebb133b846e41ad` (6/6 SUCCESS).
  Supersedes tip run `33899848739` on
  `5dbe234248f630e58aecc621ac3c7a6cacb8e449` (6/6 SUCCESS) and
  implementation run `33897389042` on
  `227902eaecfdcaccd733d328ec6e63bb56c57d82` (6/6 SUCCESS).

## Next exact action

No deterministic local certification engineering remains. Execute the retained external frontier only when the required authorized external capabilities become available. Vendor-specific VM-provider testing remains excluded by owner directive.
