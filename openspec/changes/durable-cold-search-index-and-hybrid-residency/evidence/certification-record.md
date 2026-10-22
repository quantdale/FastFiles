# Certification record — durable-cold-search-index-and-hybrid-residency

Candidate: `feat/durable-cold-search-and-hybrid-residency` at `e6b9c51c1bb25ff425a5c121a9b6de9a219d1f57`
(clean tree; identity binding verified against HEAD by
`ffbuild_identity_binding_tests`). Predecessor totals are NOT inherited: the
numbers below are this candidate's own runs (2026-10-22).

## Full test suites (authoritative counts)

| Gate | Result |
|---|---|
| release-test full CTest | **128/128 (100%)**, 255 s (identity-binding correctly skipped: Debug binaries required) |
| Debug full CTest | **128/128 (100%)**, 237 s (identity binding PASSED vs clean HEAD) |

Executable totals equal the predecessor's 128 because the new gates live
inside existing suites; the internal assertion counts grew materially:
`ffindexstore_store_tests` now runs **809 checks** (7 new candidate-index
gates incl. kill-during-commit + v1-idempotency),
`ffindexstore_read_session_tests` adds the superset differential + cap +
side-set mechanism gates.

## Durable-index correctness matrix (this candidate)

- Canonical superset differential (adversarial corpus, injected fold-exceptional
  seam, mid-name placement, exact-set assertions): PASS
- Candidate cap / canonical fallback (tiny-cap seam): PASS
- v2->v3 migration (drop-simulation, version bump, rebuilt coverage): PASS
- v1->v3 idempotent ALTER (reviewer-B P1 regression): PASS
- Graceful fault rollback (all-or-nothing batch): PASS
- REAL kill-during-commit (child ExitProcess mid-transaction + WAL recovery): PASS
- Tamper -> open-time rebuild (state/mapped-rows/fold-signature): PASS
- Hot/cold identity (engine pipeline suites): PASS
- Certified-accelerator regression (projection suite): PASS

## Benchmark / resource matrix

- 3-scale production matrix (200k/1M/2M x 12 classes hot+cold): recorded;
  target classes 2M cold selective 3.1 ms / no_match 1.4 ms / exact_name 2.7 ms
  (1123-1528x vs reproduced baseline); cost cap ensures no broad-class
  regression (200k confirmation: all broad <= baseline).
- Resource A/B (disk 124 MB/1M, WAL 3.4 MB under production policy, RAM flat,
  ingestion e2e 1.49x): `evidence/resource-measurements.md`.

## Policy gates

- strict OpenSpec: **93/93**
- lifecycle/status model: **PASS** (3 active-local tasks = successor's open
  3.3/4.x, honestly unchecked)
- doc drift + line endings + truth contracts: PASS
- source markers: PASS (238 production files clean)
- `git diff --check`: clean

## Local CI parity / Analyze / ASan

(parity bundle running at record time — result recorded in the final report
and the PR update; this record is committed with the certification commit.)

## Preserved environmental limitations

- `HOSTED_CI = BLOCKED_EXTERNAL` (to be re-checked once post-push).
- `EXPLORER_TO_FASTFILES_E2E = UNQUALIFIED` — PR #14's separate interactive
  limitation; untouched by this campaign (no drag-and-drop code changed).
- 5M-scale matrix + per-shadow-table dbstat breakdown + 2M/5M migration
  timing: disclosed gaps (staged plan).
