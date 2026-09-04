# Tasks: External Certification Execution Engine and Frontier Truthfulness

- [x] 1.1 Create change `external-certification-execution-engine-and-frontier-truthfulness` (proposal, design, tasks)
- [x] 1.2 Re-derive live truth (SHA/parity/worktrees/PRs/lifecycle/frontier) and record starting defect ledger
- [x] 1.3 Run baseline gates (Debug CTest 96/97 on dirty tree; single failure = status live-model tripped by unregistered change files, passes 100% on clean tree; cert tests 10/10; status truthful)

## 2. Clean-host execution semantics
- [x] 2.1 Split audit-only vs installation-certification modes; audit never implies product PASS
- [x] 2.2 Implement real stage→launch→smoke→shutdown→remove journey with per-leg checks and guardrails
- [x] 2.3 Honest `BLOCKED_MISSING_INSTALLABLE_CANDIDATE` when no installer exists; rollback/residue visibility
- [x] 2.4 Regression tests: audit≠install, no-proof≠PASS, installer/runtime/launch/uninstall/residue/cleanup failures

## 3. Generic provider execution state machine
- [x] 3.1 Separate preflight from execution; 14-phase generic lifecycle with per-phase records
- [x] 3.2 Record owner scope directive: vendor-specific VM-provider execution/testing is excluded from FastFiles local completion; retain generic lifecycle and fake-adapter contracts without requiring a specific provider implementation or guest run
- [x] 3.3 Fake-provider fixture covering full failure permutation matrix
- [x] 3.4 Provenance binding and mismatch rejection; regression tests for generic terminal classifications

## 4. Privileged NTFS ground truth + signing audit
- [x] 4.1 Fixture ground-truth comparison leg (normal-FS truth vs privileged/raw observation), read-only rule
- [x] 4.2 Injectable seams + layered tests (A parser, B contract, C elevated, D external clean/elevated execution)
- [x] 4.3 Signing audit: chain, pin, Authenticode, timestamp, negative tests, fail-closed BLOCKED_EXTERNAL
- [x] 4.4 Regression tests: raw-open-alone≠PASS, fixture-mismatch=FAIL, wrong-pin=FAIL

## 5. Frontier capability contract + UIA/soak tightening
- [x] 5.1 Machine-readable execution contracts + capability vocabulary on all 25 tasks; runner capability advertisement
- [x] 5.2 Inventory validation: capability mismatch fails (DPI, contrast, lane semantics)
- [x] 5.3 UIA display-matrix dimensions (requested-vs-observed) + rapid-input Pane.1/Search-Filters assertions + artifact detail
- [x] 5.5 visual-matrix scenario (details reservation dynamics + treemap percent-sum/drill-down path with honest engine-blocked classification); EX-009 CLOSED: degraded treemap fallback (snapshot-aggregate + totals recompute, honest Partial) + scope warmup + debug-diagnostic engine-connected execution proven 2/2 x 14/14 locally; fail-closed debug builds keep the honest BLOCKED skip
- [x] 5.4 Soak private-bytes gate, explicit modes, verified-close transitions, first-class cleanup everywhere

## 6. Status/lifecycle truthfulness
- [x] 6.1 Three-state readiness (NOT_IMPLEMENTED / READY_EXTERNAL / EXECUTED) recorded per in-scope frontier task; owner-excluded vendor-specific VM-provider work must not count as active local completion work
- [x] 6.2 NOT_IMPLEMENTED in-scope task + active change tasks surface as active local work; READY_EXTERNAL tasks project external-only
- [ ] 6.3 Register change in `openspec/lifecycle.json` with commit+hosted-run binding on completion

## 7. Validation, evidence, hosted certification
- [ ] 7.1 Full regression + mutation-minded verification of new tests
- [ ] 7.2 Long validation block (Debug ×3, release-test ×2, UIA 100/50, soak ≥10 min, IPC 100/100/25, exact-tree evidence)
- [ ] 7.3 Logical commits, push, hosted CI green on final SHA, final adversarial review + report

## Scope note — owner directive 2026-09-04

Vendor-specific VM-provider testing is intentionally skipped. Do not add it back
to the validation ladder, remaining-local-work list, or campaign completion gate
unless the owner explicitly reverses this directive.
