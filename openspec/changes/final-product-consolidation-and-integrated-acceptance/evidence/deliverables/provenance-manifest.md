# Source provenance manifest — integrated candidate (task 11.3)

Non-self-referential: live SHAs (final tip, main, publication/merge) are
derived from Git/GitHub at publication and recorded in `owner-handoff.md`;
this manifest records identities that are stable inputs to the tested build.

## Build inputs (the certified tree)

- **Starting main (intake):** `0dfd4d5533663c8f2110108e11789910b179a3f5`
  (PR #37 merge; planning baseline for `integration/fastfiles-final-product-20261029`).
- **Candidate:** `integration/fastfiles-final-product-20261029` — stage-merge
  sequence recorded in the merge commits (intake ledger → Core #14/#17 →
  Nav/storage #19/#20/#29 → Operations #24/#36/#34 → Verification #35 →
  Theme #16 → reconciliation/analyze fixes). Every one of the 23 PR head
  SHAs pinned in `evidence/integration-ledger.json` is an ancestor of the
  candidate tip (verified 23/23); the two accepted local inputs
  (`evidence/local-input-reconciliation.md`) add one test registration and
  two PowerShell probes.
- **Accepted PR inputs:** all 23 (number/title/head SHA/unique commits in
  `evidence/integration-ledger.json`).
- **Resolution commits:** the merge commits above plus
  `final-product-consolidation: close fit-native-command-palette-to-results
  lifecycle state`, `final-product-consolidation: close
  preserve-e2e-failure-evidence lifecycle state`,
  `final-product-consolidation: reconcile local-only and simulation inputs`,
  `final-product-consolidation: keep the /analyze gate warning-clean on the
  integrated test union`.
- **Rejected input content (explicit):** the flat 512 bytes/row
  empty-projection prior from `test/mixed-residency-oracle-20261026` (and the
  simulation lineage) — disproven by PR #14's post-completion audit and
  replaced by the measured-growth guard; recorded in
  `evidence/local-input-reconciliation.md`.

## Executed-test identities

- **Executed binaries:** the Debug, release-test, debug-diagnostic, analyze and
  asan preset build trees at the candidate tip (per-worktree
  `build/<preset>/`), compiled by MSVC 14.50 /VC/Tools/MSVC/14.50.357 with the
  repository policy (`/W4 /WX /permissive- /sdl /guard:cf /Zc:__cplusplus`,
  link `/DYNAMICBASE /NXCOMPAT /guard:cf`), Ninja generator, x64.
- **Build identity (debug-diagnostic E2E set):** `06abcfe0274ee35016fa60faaccd34cb9be188cc`
  (set-consistent, boundToHead; tree binding NOT_EMBEDDED — source-equality is
  not a bit-for-bit reproducibility claim).
- **Bench identity (perf run):** `600ef86efcc3d11622981dfc82b94b9f05ee3c3a`
  (Release config; report embeds command line, preset and layout revision).
- **SHA-256 of executed product/test binaries** are captured in the E2E
  identity artifact (`build/e2e-artifacts/<run>/summary.json` per executable)
  and the bench report; they are regenerated per run and deliberately not
  re-embedded here (run artifacts are execution evidence, untracked).
- **Full CTest selections:** 143 configured tests, 143 selected, 143 passed,
  0 failed, 0 skipped in both Debug and release-test; the only unexecuted
  configured test remains the restricted live-reaper fixture (policy
  exclusion, never a silent pass) and the opt-in `privileged-trust` label
  family (not configured in this build).

## Toolchain / environment

VS 2022 Community 17.14.36127.28 (VC tools 14.50.357), CMake/Ninja from the
VS bundle, pwsh 7.6.9, Windows 11 host. Hosted CI for this repository is
`BLOCKED_EXTERNAL` (pre-step payment/spending failure; six jobs, zero executed
steps — historical runs documented per input PR); one post-publication
inspection of the exact pushed SHA is performed at task 12.3 and recorded in
`owner-handoff.md`.

## Equivalence boundary

Documentation-only commits after the last executable-impacting commit are
proven tree-equivalent over `src/ tests/ cmake/ verify/ CMakeLists.txt
CMakePresets.json third_party/ VERSION` by content diff at publication
(task 11.5); the verdict is recorded in `owner-handoff.md`, derived
externally — no self-referential containing SHA is embedded in tracked state.
