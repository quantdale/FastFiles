# Benchmark/source identity reconciliation (campaign section 20)

Three historically cited SHAs on `feat/durable-cold-search-and-hybrid-residency`:

| SHA | Role | Executable-affecting content |
|---|---|---|
| `628de6d` | reported certified product implementation | production tree (`src/`) + tests/benches of that tree |
| `80a6f11` | later evidence commit | **`src/` identical to `628de6d`** (verified: `git diff 628de6d..80a6f11 -- src/` is EMPTY); executable delta is exactly `tests/indexstore/bench_store_open.cpp` (+16 lines: the `--maintenance-off` entries-only fixture mode) plus evidence markdown |
| `b68767a` | final documentation/evidence tip | `src/` and all tests/benches identical to `80a6f11` (verified: `git diff 80a6f11..b68767a` touches only `docs/agent/WORKSTATE.md`, evidence markdown, `tasks.md`) |

## Consequences for the artifacts

- **Production executables** (FastFiles.exe / FastFilesEngine.exe /
  FastFilesIndexSvc.exe / FastFilesSetup.exe / fftest.exe and the store
  library): `628de6d` == `80a6f11` == `b68767a` at the source level, which permits scoped source-equality inheritance for those paths. This
  does not prove the measured binaries byte-identical; the actual
  executable-tree-equivalence gate scope/verdict must be recorded separately.
- **`ffindexstore_bench_store_open`**: source differs between `628de6d` and
  `80a6f11`. The 5M/2M/1M open-matrix artifacts (including
  `evidence/artifacts/5m-migration-v2.json`) were produced by a binary built
  from **a dirty working tree reported as `628de6d+dirty-harness`**. The
  committed delta at `80a6f11` is the relevant maintenance-off addition, but
  no saved complete dirty-tree snapshot proves that it was the ONLY build
  delta. Do not call it byte-equivalent to `80a6f11`; clean-tip remeasurement
  is required for final benchmark certification. Each artifact additionally
  embeds its own runtime-computed executable SHA-256 (the strongest
  binding). The label discloses a real source-provenance limitation, not a
  clean-commit certification.
- **Other bench executables** (`ffengine_bench_large_index_scalability`,
  `ffindexstore_bench_candidate_resources`,
  `ffengine_bench_fts5_candidate_probe`): identical sources across the
  three SHAs; their artifacts' `628de6d` identities hold through the tip.
- **This campaign's measurements** (`build/campaign/analyze-ab/`,
  `build/campaign/wal-matrix/`, the identity/ordering oracle) are bound to
  the campaign commits (`3e081f5`..`0019b37`), which DO change `src/` (the
  analyze-maintenance-scheduling and plan-stability fixes) — a new
  implementation SHA superseding `628de6d`. Final-sha re-measurements
  (200k/1M repeated cells, 5M migration re-run) are final-ladder items and
  supersede the `628de6d+dirty-harness` figures where they overlap.

## Distinctions kept in the evidence

- production executable vs test executable vs benchmark executable vs
  evidence-only files vs generated binaries are labeled per artifact in
  `artifact-provenance-manifest.md`; this note reconciles the SHA claims
  those labels were made under.
- Historical dirty-harness artifacts remain exploratory/mechanism evidence.
  Their labels and executable hashes are preserved; this note does not
  retroactively establish an exact clean source tree for them.
