# Tasks

## 1. Exact-head baseline

- [x] 1.1 Reconcile the live repository: branch, HEAD, `origin/main`, ahead/behind,
  working tree, worktrees, running processes, OpenSpec lifecycle, hosted CI state,
  benchmark artifacts. Recorded: branch `main`, HEAD
  `3323fcb1e1c2a79a57386a6482e932b3189655e8`, 0/0 vs `origin/main`, clean tree,
  one detached worktree `.kilo/worktrees/scythe-cloud` at `0dc3356`, no FastFiles
  processes.
- [x] 1.2 Create the campaign branch `feat/measured-performance-acceleration-20260930`
  from the actual current `main`. No reset to historical SHAs, no history rewrite.
- [x] 1.3 Build `release-test` from a clean tree and verify the five identity-bearing
  executables, embedded SHA, and build identity binding.
- [x] 1.4 Run the first-generation benchmark as the baseline and record it with full
  provenance (source SHA, tree state, preset, fixture, warm-up, reps, processes).
  Result: TOTAL median-of-run-medians 129.329 ms across 7 processes
  (export 54.102, publish+broadcast 53.609, pipe receive 0.007, client parse
  20.505). Written under `build/perf-baseline/`.

## 2. First-generation profile

- [x] 2.1 Instrument export substages and measure: enumeration, filtering,
  reconstruction, name materialization, entry build, map insert. Export substage
  totals recorded in `build/perf-baseline/substage_profile.json`.
- [x] 2.2 Instrument publication substages: size pass, mapping creation, view,
  serialization, previous-mapping teardown, merge, working-set cap, broadcast.
  Recorded in the same file.
- [x] 2.3 Identify the dominant costs and record the evidence table.
- [x] 2.4 Establish the ceiling of the name-representation candidate with an
  interleaved, core-pinned A/B, and record the conflation caveat honestly.
  Result: removing per-entry heap allocation plus payload shrinking is worth
  27.39% TOTAL, but the probe does not isolate the two mechanisms, so 27.39% is an
  upper bound, not a prediction for a small-string type alone.

## 3. Harness fidelity repair

- [x] 3.1 Diagnose that the benchmark passed the exported snapshot map as an
  lvalue, forcing a deep copy of 200,000 entries that no production call site
  performs.
- [x] 3.2 Repair the benchmark to pass by move, matching `Main.cpp` and
  `VolumeSessionManager`.
- [x] 3.3 Measure the repair with an interleaved randomized A/B under core
  pinning: TOTAL 99.696 → 88.403 ms (−11.33%), publication −34.47%, arm spreads
  5.5%–7.5%, same-binary control delta 2.52%. Decide KEEP.
- [x] 3.4 Commit the repair with the measured evidence in the message.

## 4. Statistical discipline

- [x] 4.1 Establish the host noise floor with a same-binary control. Unpinned
  run-to-run spread was 50.0%.
- [x] 4.2 Reduce noise by pinning each repetition to one logical core at high
  priority with an idle gate. Same-binary control spread fell to ~5–7%.
- [x] 4.3 Re-measure both earlier reverted candidates under the improved
  protocol and record the outcome honestly.

## 5. Candidates attempted and decided

- [x] 5.1 Packed-record snapshot serialization (one memcpy per fixed record
  instead of one call per field). Measured TOTAL −0.99% under core pinning.
  Decision REVERT: export +2.49%, total −0.99%, inside noise.
- [x] 5.2 In-place snapshot row construction in reserved vector storage.
  Measured TOTAL −0.70% (export −3.26%, parse −7.26%, publish +2.45%).
  Decision REVERT per the total-improves-beyond-noise rule.
- [x] 5.3 Small-string-optimized `SnapshotDirectoryEntry::name`. Ceiling measured
  at 27.39% but conflated with payload size; the isolated allocation-only gain is
  unquantified and the change has wide wire-format blast radius with real
  overload-ambiguity risk. Decision: not implemented; recorded as the
  highest-value next candidate.
- [x] 5.4 Deferred teardown of the previous generation's mapping. Rejected as a
  design-contract change (`SnapshotPublisher` documents that the engine keeps
  only the current mapping) for an unmeasured ~3.4 ms.

## 6. Correctness regression

- [x] 6.1 Full `release-test` CTest suite: 124/124 passed with the retained
  change in place.
- [x] 6.2 Byte-exact wire-format invariant added and verified: a hand-emitted
  reference buffer, `SerializedSnapshotSize` against the emitted byte count, and a
  full field-for-field round trip including negative `int64_t` volume ids. The
  test caught a real layout defect during development (an entry header that
  double-counted the name-length prefix at 82 bytes instead of 78), which
  validated its value. The production change it was written for was reverted, so
  the format-pinning test is retained standalone as a regression guard.
- [ ] 6.3 Re-run the authoritative ASan slice and Analyze for the final candidate.
- [ ] 6.4 E2E, UIA core, UIA full, visual catalog for the final candidate.

## 7. Certification workflow

- [ ] 7.1 Implement the canonical executable-impacting input set and the
  `implementation_sha` / `certified_binary_sha` / `evidence_sha` /
  `repository_tip_sha` equivalence check.
- [ ] 7.2 Add a deterministic gate that verifies the relationship mechanically.
- [ ] 7.3 Prove the equivalence holds for the campaign's own evidence commits.

## 8. Final validation, review, delivery

- [ ] 8.1 Full local certification sweep on the final candidate.
- [ ] 8.2 Independent adversarial review of the performance claim.
- [ ] 8.3 Push the branch and open a reviewable PR. Do not merge.
- [ ] 8.4 Write the final report.
