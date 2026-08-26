# Baseline — repository-truth-surface-and-doc-drift-convergence

Recorded 2026-08-26 after live reconciliation.

## Exact pre-change Git facts

- Fetched/pruned `origin`; local `main` fast-forwarded `165f7bc` → `5758bbf`.
- Baseline HEAD: `5758bbfde24da5a0076373abcbc3e3d585ad2895`
  (`planner: queue executable repository truth hardening campaign`), equal to
  `origin/main`, clean worktree, branch `main`, 0 ahead / 0 behind after ff.
- Parent chain: `165f7bc` (`openspec-lifecycle: record final integrity handoff`)
  ← `09bac46` (`ci-fetch-full-history-for-lifecycle-evidence`) ←
  `cd8b0b1` (`openspec-lifecycle-enforce-typed-evidence-integrity`) ←
  `9256808` (planner queue) ← `ff4a6fe`.

## Pre-change status facts

- `openspec/lifecycle.json` schemaVersion 2; lifecycle model PASS.
- Raw unchecked tasks: 24; active-local tasks: 0.
- Prior campaign (`openspec-lifecycle-evidence-integrity-and-semantic-convergence`)
  completed with hosted run `32865014793` = success for `09bac46`;
  final-handoff CI for `165f7bc` recorded by planner as run `32867962170`
  success.
- `.agent/EXECUTION_PROMPT.md` at baseline: ACTIVE for this campaign.

## Pre-change drift-gate state

- `tests/drift/check_doc_drift.ps1`: four banned strings across README/AGENTS/
  CLAUDE/CODE_INDEX plus mixed-line-ending check under src/ and tests/.
- Registered as CTest `ffdoc_drift_tests` (pwsh-gated,
  `tests/drift/CMakeLists.txt`, TIMEOUT 120).
- The gate passed at baseline while the guidance contradictions in
  `evidence/drift-findings.md` were live — demonstrating false-green.

## Toolchain facts (from prior validated sessions, unchanged)

- VS 2022 BuildTools x64 discovered via `vswhere`; presets debug /
  release-test / analyze / asan build through VsDevCmd environment.

## Inventory pointer

Full surface-by-surface inventory: `evidence/truth-surface-inventory.md`.
