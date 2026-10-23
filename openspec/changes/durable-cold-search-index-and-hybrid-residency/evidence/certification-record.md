> HISTORICAL RECORD (628de6d-era). Current acceptance is documented in
> [final-acceptance-owner-report.md](final-acceptance-owner-report.md).
> Old count-only equivalence and diff-only Gallery attribution are superseded;
> this file is not current-source certification.

# Certification record — durable-cold-search-index-and-hybrid-residency

Candidate: `feat/durable-cold-search-and-hybrid-residency` at
`628de6d9dcfc3838d88e58d451c7a9b63535939d` (product tree) / tip `80a6f11`
(evidence + bench-harness commits on top; `git diff 628de6d..80a6f11 -- src/`
is empty). Identity binding via `ffbuild_identity_binding_tests` (Debug
binaries rebuilt at the committed tree) and the executable-tree equivalence
gate from `628de6d` to the final tip (verdict recorded in the PR body and the
final owner report, kept outside the checked tree per the non-self-reference
rule). Predecessor totals are NOT inherited: every number below is this
candidate's own run (2026-10-23).

This record replaces the previous version, which bound to `e6b9c51` with
provisional "parity running" language and headline wall-clock claims without
their methodology caveats (review lanes D/F P1s). Caveats now travel with the
claims.

## Full test suites (authoritative counts)

| Gate | Result |
|---|---|
| Debug full CTest | **128/128 (100%)**, 297 s (identity binding PASS at the committed tree) |
| release-test full CTest | **128/128 (100%)**, 241 s (identity-binding correctly skipped: Debug binaries required) |
| Analyze (`/analyze`) | **clean** — 0 warnings, 0 errors |
| ASan slice | **PASS** — 4 targets + 4 tests (parity `asan-slice` step) |
| strict OpenSpec | **93/93** |
| lifecycle/status model | PASS (open tasks = this change's own honest ledger) |
| doc drift + line endings + truth contracts + source markers + exact-tree hygiene | PASS (parity `policy` + `hygiene-suite` steps) |
| `git diff --check` | clean |
| unsigned release qualification | PASS (parity `release-candidate` step) |
| **Local CI parity** | **11 PASS / 0 FAIL / 0 SKIP** (`verify/Invoke-CiParity.ps1`) |

Executable totals equal the predecessor's 128 because the new gates live
inside existing suites; internal counts grew materially:
`ffindexstore_read_session_tests` now runs **140 checks** (multi-volume
admission A-E, atomic-failure red-first, readiness-gate red-first), store
suite 809+ checks.

## Durable-index correctness matrix (this candidate)

- Canonical superset differential (adversarial corpus, injected fold-exceptional
  seam, exact-set assertions): PASS
- Candidate cap / canonical fallback + volume-aware admission (cases A-E,
  red-first both ways): PASS
- Atomic candidate delivery under mid-stream SQLite failure (deterministic
  fault injection, legacy-arm red proof): PASS
- Query-time readiness gate (mid-rebuild window + host-fold mismatch,
  red-first truncation proof): PASS
- v2->v3 migration at 1M/2M/5M measured + v1 idempotent ALTER: PASS
- Graceful fault rollback + REAL kill-during-commit + WAL recovery: PASS
- Fail-closed downgrade (v2 app refuses v3; entries preserved): PASS
- Hot/cold identity (engine suites + 5M 12/12 class equivalence): PASS

## Performance claims (with methodology caveats attached)

- 2M production path, final candidate, repeated fresh-process runs (cold n=2,
  warm n=10): selective **3.23 ms** (median cold), no_match **0.97 ms**,
  exact_name **3.50 ms** — ~1,100-2,150x against the reproduced baseline
  (whose cold cells are n=1; conservative claim: three orders of magnitude
  on eligible queries). Broad classes at parity (cap fallback). Artifacts:
  `build/campaign/bench-run{4,5}-2m.json` (identity-embedded) + analysis in
  `evidence/performance-repeatability.md`.
- "cold" = first iteration of a measurement block (no OS cache flush);
  >=2M "warm" labels are cold-stream under the 500 MiB budget. See the
  provenance manifest for the full identity/temperature semantics.
- 5M staged acceptance: 12/12 hot/cold result-count equivalence; target
  classes 3.1/5.1/1.5 ms at 5M rows (`evidence/5m-staged-acceptance.md`).
- Startup: fold signature ~41-42 ms constant/process (uncached by decision);
  integrity_check is 99%+ of reopen (`evidence/startup-decomposition.md`).

## UI surfaces (this candidate; zero UI code changed — `git diff 1c427d7..HEAD
-- src/ui` empty)

| Surface | Result |
|---|---|
| E2E (`dev.ps1 test-e2e`, real UI + marker-bound FS results) | **PASS 8/8** (0 fail, 0 skip) |
| UIA core smoke (uia-core-smoke + keyboard-nav + dual-pane, 25 iterations) | **PASS** (0 failures, cleanup PASS, exact-tree bound) |
| UIA full visual-matrix (25 iterations, themes/viewport/DPI matrix) | **PASS 25/25** (0 fail, 0 skip) |
| Visual catalog (core 16 scenarios: Home/ColumnView/Search/Settings/...) | **PASS 16/16** |
| Visual catalog Gallery variants | **1/4** — `gallery-light` PASS; `gallery-dark`, `gallery-narrow`, `gallery-wide` **FAIL (reproducible 2/2 runs)** |

Gallery failure detail: the debug Gallery window renders empty at 800 px and
1600 px widths (capture mean 28-32) and the F9 dark toggle does not repaint
it (lightMean == darkMean == 234.8) at this host's 125% DPI. Classification:
**P2 finding, DEFERRED** — it is impossible from this campaign's diff (store/
query layer only), it is the debug visual-test Gallery surface rather than a
product surface (all 17 product-surface scenarios pass), and root-causing the
viewport/theme capture needs a UI-focused change. Reproduction artifacts:
`build/campaign/visual-catalog-gallery{,-2}/`. Owner decision required on
whether this blocks merge; it is not silently downgraded to PASS.

`EXPLORER_TO_FASTFILES_E2E = UNQUALIFIED` unchanged (PR #14-era separate
interactive limitation; no drag-and-drop code touched).

## Preserved environmental limitations

- `HOSTED_CI = BLOCKED_EXTERNAL` — re-checked once post-push (run recorded in
  the PR body / final report).
- Physical display matrix, clean-host install, elevated MFT/USN, production
  signing: external, unchanged.
