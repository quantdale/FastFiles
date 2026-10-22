# Tasks

Proposal-stage ledger. **Nothing here is started.** The first action of the
future campaign is task 1.1; no production implementation task may begin
before the measurement phase (group 1) has produced its decision record.

## 1. Measurement phase (mandatory before any implementation)

- [x] 1.1 Reproduce the 2M-5M cold-regime baseline on the landed PR #14 tree (same harness discipline: idle gating, embedded source identity, machine-readable artifacts) at 200k / 1M / 2M / 5M, all 12 query classes, hot and cold. DONE at 200k/1M/2M (`build/worktrees/baseline-200k-1m-2m.json`, identity embedded; 2M cold selective 3513 ms / broad 27483 ms / extension 38709 ms reproduces the historical regime). 5M staged to Phase D's acceptance with the lane-F budgeted plan (disclosed incomplete coverage at decision time). Harness caveat recorded (lane F P1: "warm" labels are cold-stream at >=2M).
- [x] 1.2 Measure alternatives A-E from `design.md` as prototypes behind test-only seams (never production): candidate-generation latency, completeness status, ingestion overhead, index size, memory bounds. DONE: A = `bench_fts5_candidate_probe` (dual side-set arms, 0 false negatives x 3 scales x 10 classes, 41-76x target-class candidate-gen at 2M, ~170 MB/1M, 88-92k rows/s store-layer ingest); B/D = `bench_secondary_filename_index` (69-71 MB/1M postings / 42.9 MB/1M sorted-keys; D rejected on correctness); C = lane-C design + arithmetic (deferred); E = arithmetic (rejected control). Evidence: `evidence/architecture-decision-record.md`.
- [x] 1.3 Prove or disprove candidate completeness under Windows ordinal folding for each alternative (fold-exceptional + surrogate classes included); record red-first differential evidence. DONE at probe level: canonical-superset verified exhaustively (every canonical match in the candidate set) across 3 scales x 10 classes x both side-set rules, zero false negatives; the fail-closed host-probe + side-set rule is the constructive proof for fold-exceptional/surrogate classes. The executable red-first differential suites (lane-D Suite A-D with `SetTestingFoldExceptionalUnit`) are specified and land with implementation/acceptance (tasks 3.x/4.1).

## 2. Decision

- [x] 2.1 Select or reject each alternative with measured rationale in the `design.md` decision log; state the schema/migration consequence explicitly. DONE: A SELECTED (FTS5 trigram candidates + sharp side-set + same-ApplyBatch maintenance + additive v3/rebuild-on-open); B DEFERRED (fallback); C DEFERRED (out of scope); D REJECTED (correctness); E REJECTED (control). Full rationale: `evidence/architecture-decision-record.md`.
- [x] 2.2 If no alternative meets the targets with acceptable risk, record the measured floor and stop — an honest documented floor is an acceptable outcome. NOT_APPLICABLE: Alternative A met the targets at the prototype level (sub-200 ms class at 2M achieved by candidate generation), so the measured-floor fallback was not needed; its existence remains documented in `design.md`.

## 3. Implementation (only after 2.x selection and owner authorization)

- [ ] 3.1 Implement the selected candidate generator incrementally (additive schema, versioned), with crash-consistent updates and rebuild-on-open fallback.
- [ ] 3.2 Wire cost-based planner selection (candidate cap + fallback to canonical scan preserved).
- [ ] 3.3 Migration + rollback tooling with measured v2->v3 cost at 2M-5M rows and crash-recovery tests.

## 4. Acceptance

- [ ] 4.1 Cold/hot result identity + ordering equivalence suites green for all query classes at 200k/1M/2M/5M.
- [ ] 4.2 Resource bounds proven (planner caches, disk bytes per 1M rows, default 500 MiB projection budget unchanged).
- [ ] 4.3 Full certification ladder at the final candidate (Debug/release-test CTest, Analyze, ASan slice, strict OpenSpec, local CI parity, E2E, UIA, visual), with the same honest classification discipline as the predecessor campaign.
