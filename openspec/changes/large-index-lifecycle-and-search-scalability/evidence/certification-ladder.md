# Certification ladder (10.1) + identity (10.2) — 2026-10-21

Candidate tree: the review-remediation series ending at `43fc3c1`
(executable-impacting) plus documentation/evidence commits. All runs on the
campaign host from a clean worktree.

## Ladder results (actual counts)

| Gate | Command | Result |
|---|---|---|
| Debug build | `cmake --build build/debug` | PASS (0 errors) |
| Debug CTest | `ctest --preset debug` | **128/128 PASS** (255 s) |
| release-test build | `cmake --build build/release-test` | PASS |
| release-test CTest | `ctest --preset release-test` | **128/128 PASS** (219 s) |
| Analyze build | `cmake --build build/analyze` (via parity) | PASS, 0 warnings |
| ASan slice | parity `asan-slice` (4 targets + 4 tests) | PASS (76 s) |
| strict OpenSpec | parity `openspec validate --all --strict` | PASS (91 items at certification; **93/93** at closure tip incl. the successor change) |
| Source markers + diagnostic policy + doc drift + `git diff --check` | parity `policy` | PASS |
| Status/contracts/visual-fixtures/exact-tree | parity `hygiene-suite` | PASS |
| Unsigned release qualification | parity `release-candidate` | PASS |
| **Local CI parity (11 checks)** | `pwsh ./verify/Invoke-CiParity.ps1` | **11 PASS / 0 FAIL / 0 SKIP** (`LOCAL-PARITY-NOT-HOSTED-CI`) |
| E2E (real product, UIA-bound) | `pwsh ./dev.ps1 test-e2e` | **8 passed / 0 failed / 0 skipped, boundToHead=true** |
| Visual catalog | `verify.ps1 run -Capability windows-ui-visual-validation` | **PASS: 26 captures, 27 skipped/blocked, 0 failed** (honest limitations per harness) |
| UIA | `verify.ps1 run -Capability windows-ui-automation-validation` | **1 scenario FAILED (`drag-in-from-explorer`), 2/2 deterministic; all FastFiles-surface scenarios green** — see honest classification below |
| Startup (7.1) | `build/campaign/startup-probe.ps1` ×3 (empty sandbox, degraded) | shell window ready **928 / 810 / 381 ms** cold; engine-readiness chain measured separately (below) |
| Rebuild benchmark revalidation | `--memory-report --rebuild-cycles 10` | 28-32x typical / >=17.7x worst (see lifecycle evidence) |
| Large-index search + cold matrix | search bench 200k/1M hot+cold | recorded (memory-cold-decision.md) |
| Unicode differential suite | `ffindexstore_projection_tests` + `ffengine_index_pipeline_tests` (hot/cold identity+ordering) | PASS |
| Clipboard negative controls | `ffclipboard_classifier_tests` (16 checks incl. 10 cases + counterexamples) | PASS |

## Honest classifications (skips/failures)

- **UIA `drag-in-from-explorer`: FAIL, environment-localized, not a product
  pass.** The failing operation is `SelectionItemPattern.Select` on a Windows
  **Explorer** list item inside the test-owned Explorer source window --
  before any FastFiles drop handling runs (the destination file is never
  attempted). Deterministic 2/2 this session; the scenario's own source
  documents this host's Explorer item interaction as view-mode/foreground
  dependent and previously flaky. FastFiles-surface scenarios (core, search,
  navigation, other drag scenarios' FastFiles halves) all passed in the same
  runs. This is recorded as a FAIL, not a skip or a pass.
- **UIA startup probe product stamps not captured** (sandbox diagnostics-log
  path did not resolve within the settle window); window-ready timings are
  valid, engine-side readiness is measured from the bench captures instead.
- **Visual 27 skipped/blocked captures** are the harness's
  REQUIRED-BUT-UNAVAILABLE classes (live DPI / High-Contrast / privileged
  surfaces on this headless-capable host), as in prior campaigns.

## Startup / readiness (7.1 closure, scoped)

- User-visible half (real FastFiles.exe, debug-diagnostic, empty sandbox,
  degraded mode): process start -> main window **381-928 ms** (3 cold
  repeats; 250 ms poll granularity).
- Engine half (real product components, bench captures): durable index open
  326 ms (200k) / 1,929 ms (1M, cold); projection hydration (RebuildAll/
  re-stream) 857-990 ms (200k) / 4.9-5.7 s (1M); first snapshot export+publish
  66 ms / 291 ms. **Projection hydration dominates perceived readiness at 1M+
  rows** (~5 s vs <1 s shell). Fixture-scale UI startup (200k/1M seeded
  sandbox DBs) not measured this campaign -- engine numbers carry the scale
  dimension.

## Identity / equivalence (10.2)

- `implementation_sha` = `43fc3c1` (last executable-impacting commit: the
  six-lane review remediation).
- Subsequent commits are documentation/evidence/tasks only
  (`01f5427`, `bdf2664`, the tasks/WORKSTATE commits, and this evidence
  commit) -- proven by the executable-tree equivalence gate:
  `pwsh ./verify/qualification/Test-ExecutableTreeEquivalence.ps1
  -ImplementationSha 43fc3c1 -EvidenceSha <final tracked tip>` (verdict kept
  outside the checked tree to avoid self-reference; reported in the PR and
  the final report).
- `certified_binary_sha`: the identity-bearing executables embed
  `43fc3c1`-era tree state; per the campaign's identity discipline the exact
  embedded identity is re-derived by `ffbuild_identity_binding_tests` (green
  in both CTest runs above) and is NOT claimed as bit-for-bit reproducible.
- Historical measurement artifacts (`evidence/rebuild/*`, `evidence/search/*`)
  carry their own embedded identities as noted in their provenance blocks;
  the cold-profile artifacts are asserted provenance (disclosed, lane E E-8).

## Equivalence gate verdict (10.2, recorded outside the checked tree per
## the non-self-reference rule)

Run: `verify/qualification/Test-ExecutableTreeEquivalence.ps1
-ImplementationSha 43fc3c175d4a940a094e1261b1c96aa8bf0668d1 -EvidenceSha <tip>`

- **PASS** after two harness quoting repairs (both the hook-guard class from
  `c261da4`: `Start-Process -ArgumentList` joined `-C <path-with-space>`
  unquoted, and the `-I` capture regex truncated quoted include roots at the
  first space; both made the verifier falsely report missing inputs on any
  checkout under a space-containing path). Repairs in
  `verify/qualification/Test-ExecutableTreeEquivalence.ps1`.
- Verdict: `product_tree(43fc3c17) == product_tree(<tip>)`,
  digests `cc97f4708d6eefef49ceec9d8589c73ce002e3505bc7731f600bbc775e0eb7a3`
  identical, 393 paths covered (313 TUs + build inputs), derivation coverage
  verified. A binary certified at `implementation_sha` is not invalidated by
  the documentation/evidence commits that follow.
- **SUPERSEDED DIGEST (closure campaign, 2026-10-22):** the `cc97f470…`/393
  path verdict above was taken with a derivation that had one real gap
  (RC-embedded `FastFiles.manifest` invisible to the `#include` closure). The
  corrected derivation covers 394 paths with digest
  `6e7642640a5a00783e0acd1761c1eafebe2bd612812b2ea9583775c50946a1a6`;
  the equivalence **conclusion is unchanged** (re-proven at the fixed gate).
  Authoritative record: `equivalence-gate-adversarial-matrix.md`.
- No bit-for-bit binary reproducibility is claimed; this is source-tree
  equivalence only.
