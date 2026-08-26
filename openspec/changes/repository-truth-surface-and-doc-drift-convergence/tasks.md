# Tasks: repository-truth-surface-and-doc-drift-convergence

## 1. Baseline and audit

- [x] 1.1 Record exact pre-change Git/status facts and the complete truth-surface inventory. (Evidence: `evidence/baseline.md`, `evidence/truth-surface-inventory.md`.)
- [x] 1.2 Map every machine-derived claim to its authoritative executable seam and classify drift findings, including negative-space hits beyond the planner examples. (Evidence: `evidence/authority-map.md`, `evidence/drift-findings.md`.)

## 2. Drift contract engine

- [x] 2.1 Implement `tests/drift/DocTruth.psm1` with fail-closed fence parsing and authority-derived contracts (verbs, exit codes, presets, targets, lifecycle pointer). (Evidence: `tests/drift/DocTruth.psm1`; fixture coverage.)
- [x] 2.2 Add lifecycle-aware anti-staleness rules for stale current-change/current-open-work claims resolved against `openspec/lifecycle.json`. (Evidence: fixture cases `stale-current-change`, `stale-current-open-work`, historical pass-through.)
- [x] 2.3 Anchor snapshot-publication guidance to the source seam (`GenerationSectionName` + commit-before-handoff) without asserting implementation wording. (Evidence: `tests/drift/DocTruth.psm1` snapshot contract; reconciled AGENTS/CODE_INDEX wording.)
- [x] 2.4 Compose the engine into `check_doc_drift.ps1`, preserving banned-phrase and line-ending checks with reason-coded diagnostics and no absolute-path leakage. (Evidence: focused run output in `evidence/implementation-and-validation.md`.)

## 3. Adversarial fixtures

- [x] 3.1 Implement `tests/drift/test_doc_truth_fixtures.ps1` covering missing verb, extra verb, stale current-campaign claim, lifecycle/prompt disagreement, stale snapshot mechanics, malformed fence, unreadable authority input, historical-document pass-through, and deterministic ordering/redaction. (Evidence: `evidence/negative-fixture-matrix.md`.)
- [x] 3.2 Register the fixture suite as CTest target `ffdoc_truth_fixture_tests` and keep `ffdoc_drift_tests` as the single fast gate; both execute in CI matrices. (Evidence: `tests/drift/CMakeLists.txt`; ctest run.)

## 4. Guidance reconciliation

- [x] 4.1 README: restore full verifier verb enumeration behind a truth fence, replace stale current-change claims with lifecycle-aware stable wording, mark preset list as explicit subset. (Evidence: README diff; `ffdoc_drift_tests` PASS.)
- [x] 4.2 AGENTS/CODE_INDEX: reconcile snapshot publication wording to immutable per-generation committed mappings; add truth fences for verbs/presets/targets where enumerations are claimed. (Evidence: diffs; source-seam contract PASS.)
- [x] 4.3 CLAUDE/AUTONOMOUS: remove stale "current open work" pointer, anchor current-work authority to `openspec/lifecycle.json`; fix `UiProtocol.h` section-name comment accuracy. (Evidence: diffs.)

## 5. Validation and handoff

- [x] 5.1 Run focused drift gates, PowerShell parser checks, strict OpenSpec validation, and policy/doc-drift consumers. (Evidence: implementation-and-validation record.)
- [x] 5.2 Run Debug/release-test builds + full CTest suites, Analyze warning-clean gate, and applicable ASAN slice. (Evidence: implementation-and-validation record.)
- [x] 5.3 Update evidence ledgers, progress/workstate/lifecycle/prompt durable state; commit, push, reconcile parity, and inspect hosted CI for the exact implementation SHA. (Evidence: final session report commit.)
