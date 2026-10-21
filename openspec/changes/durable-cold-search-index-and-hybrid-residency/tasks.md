# Tasks

Proposal-stage ledger. **Nothing here is started.** The first action of the
future campaign is task 1.1; no production implementation task may begin
before the measurement phase (group 1) has produced its decision record.

## 1. Measurement phase (mandatory before any implementation)

- [ ] 1.1 Reproduce the 2M-5M cold-regime baseline on the landed PR #14 tree (same harness discipline: idle gating, embedded source identity, machine-readable artifacts) at 200k / 1M / 2M / 5M, all 12 query classes, hot and cold.
- [ ] 1.2 Measure alternatives A-E from `design.md` as prototypes behind test-only seams (never production): candidate-generation latency, completeness status, ingestion overhead, index size, memory bounds.
- [ ] 1.3 Prove or disprove candidate completeness under Windows ordinal folding for each alternative (fold-exceptional + surrogate classes included); record red-first differential evidence.

## 2. Decision

- [ ] 2.1 Select or reject each alternative with measured rationale in the `design.md` decision log; state the schema/migration consequence explicitly.
- [ ] 2.2 If no alternative meets the targets with acceptable risk, record the measured floor and stop — an honest documented floor is an acceptable outcome.

## 3. Implementation (only after 2.x selection and owner authorization)

- [ ] 3.1 Implement the selected candidate generator incrementally (additive schema, versioned), with crash-consistent updates and rebuild-on-open fallback.
- [ ] 3.2 Wire cost-based planner selection (candidate cap + fallback to canonical scan preserved).
- [ ] 3.3 Migration + rollback tooling with measured v2->v3 cost at 2M-5M rows and crash-recovery tests.

## 4. Acceptance

- [ ] 4.1 Cold/hot result identity + ordering equivalence suites green for all query classes at 200k/1M/2M/5M.
- [ ] 4.2 Resource bounds proven (planner caches, disk bytes per 1M rows, default 500 MiB projection budget unchanged).
- [ ] 4.3 Full certification ladder at the final candidate (Debug/release-test CTest, Analyze, ASan slice, strict OpenSpec, local CI parity, E2E, UIA, visual), with the same honest classification discipline as the predecessor campaign.
