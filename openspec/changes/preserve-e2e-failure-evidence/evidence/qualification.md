# E2E evidence-ownership qualification

Base freshly fetched PR31 `0fbaee3574d4f34a6a7e8eeec3f46ad197ce96ef`; isolated linked worktree; root is sole integrator/publisher. Original main checkout's three dirty files untouched.

Combined `b0cf4bb` native E2E returned 7/8 FAIL at cold restore. Its `failure.png` visibly showed `Nested` and `restore-proof.txt`; its UIA Control tree lacked ColumnView. The original screenshot/tree remain in the integration worktree's ignored `build/integration-20261028/phase-palette-ordinary-e2e/cold-restore/` (screenshot SHA-256 `cbfd74f7f96c41ecae62648bbed1bdd67a558fa2b957c82dfb3482db877f4059`). The inner scenario captured those files and a live envelope; the outer wrapper subsequently overwrote that envelope with a no-window note. The underlying UIA failure remains undiagnosed; these observations do not establish its cause or pass.

Headless actual-wrapper baseline fails six ownership assertions: current inner envelope overwritten; missing separate outer record; exception path overwrites prior evidence; pre-existing scenario evidence is trusted and its body executes. Screenshot/tree sentinel files in this regression are plain test data, not screenshots or visual certification.

A separate baseline after the ownership repair fails five expected result-validity checks: null, missing status, unknown status, wrong id and multiple emitted values do not produce `invalid-result` evidence. Every actual scenario uses `New-E2EResult`; explicit PASS/SKIPPED remain supported.

Final headless `pwsh -NoProfile -File tests/e2e/Test-E2EHarness.ps1` PASS, including 24 actual CLI provenance checks, the successful fake-preflight existing-empty-root handoff, actual direct-runner AST headless availability test and ownership/result checks. The fake gate and stub are scoped to unique owned fixture directories, use non-executable text placeholders and never launch the app. Both tests preserve sentinel files and validate their uniquely owned cleanup targets before removal. Initial red logs and final log are compressed alongside this note.

CLI and direct runner now reject non-empty or non-directory artifact roots; existing empty roots are accepted. Each wrapper invocation reserves its own fresh scenario directory. Inner failure.json/tree/image remain untouched and outer boundary result or exception is saved separately with collision-safe naming. Missing inner capture receives a normal outer envelope. Invalid scenario output yields FAIL, preserving any inner evidence. No existing test was weakened and no process/GUI/privileged experiment was run. Combined-source/full native qualification and independent integration review remain pending.

Final boundary extension validates string fields and finite nonnegative numeric duration; extra malformed cases for nonnumeric, negative, NaN, infinite and empty-stage/null-detail results fail safely. Full strict OpenSpec94/94 PASS and documentation/line-ending drift PASS. The complete headless harness passed after this extension; source SHA-256 values are recorded in source-hashes.json. This source-only branch does not claim actual GUI, hosted CI or combined product qualification.

## Zero-work false-green follow-up

Fresh independent review identified empty selectors and nonpositive lifecycle counts. An owned headless probe using the old `007af1c` actual function AST and mocked UIA/process lookup returned `PASS` with `0 passed, 0 failed, 0 skipped` for an empty selector; lifecycle `0` and `-1` each returned `PASS` claiming zero/negative completed lifecycles. No app or process termination was used. The initial six module-boundary assertions and actual CLI zero-count assertion failed before the fix; red and actual-old-result logs are retained.

The actual CLI and module now reject empty/whitespace/delimiter-only selectors before artifacts/UIA; unknown tokens also fail before artifacts/UIA. CLI/direct module/internal lifecycle enforce positive counts. Headless actual-module tests cover four invalid selectors, zero and negative counts, five valid selector forms, positive1/2/5/max and prerequisite SKIPPED. The actual CLI copy with fake gate confirms default2 and explicit1 handoff, and invalid count/empty filter rejection without artifact creation. Final complete headless harness PASS with30 CLI/provenance assertions, strict OpenSpec94/94 and doc/line-ending drift PASS. No screenshot or full native E2E is claimed.

## Reviewer-residual follow-up — unknown selector ordering

Fresh independent re-review (`/root/incomplete_transfer_review`) found no
introduced P1/P2 but recorded one pre-existing boundary inconsistency: an
unknown nonempty filter token was rejected by the module only after the CLI
had created its artifact directory and run the provenance gate. It could not
produce a false pass, but it violated this change's own fail-closed ordering
standard (selector errors before evidence-directory creation).

Red-first: the new `Test-E2EProvenance.ps1` assertions
(`actual CLI rejects unknown selector '<token>' before artifact creation`
plus `rejected before the provenance gate`) failed against the previous
runner exactly as reported — an unknown token created its evidence
directory. The vocabulary now lives in one exported
`Resolve-E2EFilterSelection` used by both the CLI boundary (before artifact
creation and before the provenance gate) and `Invoke-FastFilesE2E`; the
copied-runner stub fixture provides its own compatible resolver.

Final headless state: `Test-E2EHarness.ps1` PASS (ownership + harness
contracts), `Test-E2EProvenance.ps1` PASS with 34 checks, strict OpenSpec
94/94 PASS, doc-drift/line-ending PASS, `git diff --check` clean. The
committed `source-hashes.json` is regenerated over the final source; the
`Test-E2EFailureEvidenceOwnership.ps1` hash is corrected in the same pass
(its content is unchanged since the prior commit; the earlier recorded hash
pre-dated the file's final content). No app, UI, or privileged experiment
was run for this follow-up.

## Integration qualification (task 2.1) — 2026-10-28

Primary integrator merged `c29f4cf` into the disposable combined candidate
(`test/original-vision-integration-20261025`, merge commit `2eab37c`). The
five session-owned conflicts were additive unions: `openspec/lifecycle.json`
keeps both lineages' entries (valid JSON), `Test-E2EHarness.ps1` keeps the
combined close/restore fixtures and delegates to
`Test-E2EFailureEvidenceOwnership.ps1`, `Test-E2EProvenance.ps1` takes this
branch's superset, and README/WORKSTATE union both checkpoint lineages.
`git diff 90e6cdf 2eab37c -- src cmake third_party` is empty: no product
binary changed, so no product behavior claim is made or needed from this merge.

Combined-tree checks at `2eab37c`: strict OpenSpec 104/104 PASS,
doc-drift/line-ending PASS, and the merged headless harness
(`Test-E2EHarness.ps1` incl. provenance 34 checks + ownership suite) PASS.

Combined native E2E through the merged harness (fresh debug-diagnostic build
at `2eab37c`, exact-tree provenance gate): first run 7/8 —
`preview-terminal-state` failed at `preview-ready` ("preview remained
non-terminal; observed='Preview: No selection'"; `restore-proof.txt` visible
with `isSelected=false` at capture). This is a live demonstration of the
change's evidence-ownership contract: the inner `failure.json`/PNG/tree and
diagnostic tails were preserved intact beside the separate outer record.
Focused re-run of `-Filter preview` on the same binaries PASS
(`Preview: Ready -> Preview: Unsupported`, stale preview rejected), and the
full-suite re-run on the same binaries PASS 8/8. The first run's failure is
retained as an observed intermittent UIA result — product C++ is byte-identical
across all three runs and to the earlier passing 8/8, so no root cause is
claimed and no result is overwritten. This is not a product defect claim and
not an environment-limitation claim.

The historical cold-restore UIA failure (b0cf 7/8, `failure.png` SHA-256
`cbfd74f7...` under the integration tree's ignored evidence) remains a
separate unexplained observation and is NOT erased by these passes;
`cold-restore` passed in both current runs, which narrows but does not
diagnose it. No admin, signing, privileged, billing, or restricted experiment
was used. Hosted CI for this branch remains externally blocked (account
payment/spending limit) and was not rerun.

## Publication (task 2.2) — 2026-10-28

Fresh fetch/reconciliation: branch clean at `9884911`, four commits ahead of
its PR31 base (`0fbaee3`, `origin/fix/address-navigation-discovery-20261028`).
Pushed normally (no force, no history rewrite); draft successor
**PR #35** published on the PR31 base with the validation table and stated
limitations. Hosted run `40727077090` for `9884911` (branch push + PR mirror):
six jobs, **zero executed steps**, and the account payment / spending-limit
annotation — inspected once, not rerun, no billing/account change.
`HOSTED_CI = BLOCKED_EXTERNAL` for this branch; local evidence stands separate
and no hosted pass is claimed. Unmerged by design; owner controls merges.
