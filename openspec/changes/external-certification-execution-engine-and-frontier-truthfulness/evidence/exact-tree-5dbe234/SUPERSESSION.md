# Exact-Tree Evidence Supersession Record — 2026-09-05

- **Authoritative set**: every `long-run-*.json` and `resource-soak-summary.json`
  in this directory records `commit 5dbe234248f630e58aecc621ac3c7a6cacb8e449`,
  `tree 8f648afcbcd04a0a211c921fd168a37cc1d523c4`, `exactTreeBound: true`.
- **Provenance**: generated in clean certification worktree `cert/final-exact-tree`
  at `5dbe234` (`git status --porcelain` empty; `-RequireExactTree` asserted on
  every lane), with binaries rebuilt from scratch in that worktree. Executable
  hashes are recorded per artifact.
- **Supersedes** (preserved, not rewritten): older dirty-tree runs described in
  `docs/agent/WORKSTATE.md` / `docs/agent/NTFS_VISUAL_CERTIFICATION_DEFECT_LEDGER_2026-09-04.md`
  (NV-010 caveat, `exactTreeBound=false` from unrelated `.opencode` edits), the
  `external-certification-readiness-and-evidence-convergence` evidence set
  (`exactTreeBound=false`), and the older-SHA `exactTreeBound=true` sets bound to
  `67f06b5`, `39b873a`, `1a29ef5`, etc. Those remain legitimate historical records.
- **Lanes**: handshake 100/100, framing 100/100, UiServer lifecycle 25/25,
  concurrency 20/20, dialog focus 50/50, UIA application-lifecycle 100/100,
  UIA rapid-input 100/100 (1100/1100 assertions, post-fix), resource soak
  interactive 600 s PASS (12/12 activities, all stability legs PASS, 0 orphans).
  Validation alongside: Debug CTest 99/99, release-test CTest 99/99, Analyze
  build warning-clean — all on this exact tree.
- **Manifest**: `evidence-manifest.json` (SHA-256 per file); sidecar
  `*.json.sha256` per artifact; `Test-EvidenceConsistency -RequireExactTree` PASS.
