# Tasks

## 1. Re-derive the live tip

- [x] 1.1 Re-derive branch, HEAD, `origin/main`, ahead/behind, worktrees, status, OpenSpec lifecycle, PR #12, and the latest hosted run. Record the result in `evidence/explore-findings.md` or a successor ledger if the propose-time snapshot is no longer current. Do not treat `bdd7d7b` or run `36786432149` as permanent truth.
- [x] 1.2 Confirm implementation commits land on `feat/performance-closure-and-multisurface-acceleration-20261001`, created from the actual main tip. Do not reuse `feat/measured-performance-acceleration-20260930` as open development state, force-push, or rewrite history.
- [x] 1.3 Leave `.kilo/worktrees/scythe-cloud` at `0dc3356` untouched. Do not read `verify/runs/`, `verify/baselines/`, or `verify/.signing/`.

## 2. Close predecessor certification debt

- [x] 2.1 Identify and stop only campaign-owned or safely disposable processes that distort measurement. Do not kill arbitrary user work. Wait for the established benchmark idle gate.
- [x] 2.2 Rerun full release-test CTest on that idle host. Keep the existing 30-second assertion in `TestStopReturnsWithoutWaitingForTheSchedulerInterval`. Do not raise it, skip it, or retry until a pass appears. Use at most three idle runs to distinguish a stable pass from a stable failure. If any idle run exceeds 30 seconds, investigate the product or test before any optimization.
- [x] 2.3 Rerun full Debug CTest where the current certification ladder requires it, and record actual passed, failed, and skipped totals.
- [x] 2.4 Run Analyze warning-clean on the pre-optimization candidate and record the result. Do not inherit a historical Analyze pass.
- [x] 2.5 Run the authoritative ASan slice from `verify/Invoke-CiParity.ps1`: `ffprotocol_fuzz_tests`, `ffipc_framing_tests`, `ffipc_listener_lifecycle_tests`, and `ffvolumescanner_terminal_tests`. Record the exact total.
- [x] 2.6 Run E2E, UIA core, UIA full, visual catalog, and focused many-tabs/focus coverage on the same pre-optimization candidate. Record pass, fail, skip, and classification. Do not inherit historical UIA or visual numbers.
- [x] 2.7 Check predecessor tasks 6.3 and 6.4 only after 2.2–2.6 have current passing evidence, or record an honest unverified reason only when the gate is genuinely external with evidence. A locally executable failed or unrun gate stays open and blocks production optimization; do not mark a task complete merely because its failed result was recorded. Mark `measured-performance-acceleration-and-exact-certification` completed only when its open tasks are closed or honestly external, its stale evidence is corrected, repository truth is reconciled, and current-state validation is recorded. Do not set `supersededBy` as a substitute for that closure.

## 3. Repair stale evidence

- [x] 3.1 Add a successor interpretation record for `allocation-probe.json`. Label the short-name probe `CONFLATED ALLOCATION + PAYLOAD-SIZE EXPERIMENT`. Preserve every original measured value. Do not edit the historical numeric fields into agreement with the later report.
- [x] 3.2 Add a current equivalence validation artifact that preserves the historical 228-path, `.cpp`-only negative record as historical. The current record must include a documentation-only pass, a compiled `.cpp` failure that names the source, a header-only failure that names the header, an unresolved quoted-include failure, a CMake-manifest failure, and the claim boundary from design D4.
- [x] 3.3 Run those controls against the current gate. Synthetic commits stay in a temporary worktree and must not remain on the feature branch. An unresolved quoted project include must fail closed.
- [x] 3.4 Document that a passing gate proves git-controlled executable-impacting source and build-input equivalence, not reproducible binaries. Add compile-command hashing only if a control shows the current digest would falsely certify a command-line difference that matters to this campaign.

## 4. Quiet-host baseline

- [x] 4.1 Rebuild `release-test` from a clean pre-optimization tree and verify embedded identity before the baseline.
- [x] 4.2 Measure the first-generation path with the production-faithful move, idle gate, single-core pin, high priority, and enough process-level samples. Record source SHA, tree state, preset, fixture, warm-up, repetitions, and stage medians. Do not reuse 88.403 ms or 129.329 ms as the before row.
- [x] 4.3 Measure the same-binary noise floor in that same session. Later retention decisions use this floor, not the historical 5–7% figure.

## 5. Allocation isolation

- [x] 5.1 Add a benchmark-only isolation arm that keeps 200,000 entries, the same names, the same name lengths, the same serialized byte count, the same directory count, the same file count, and the same generation identity. Do not change the production wire format to obtain the measurement.
- [x] 5.2 Measure control, export-allocation-eliminated, and full-path-eliminated arms under the D5 protocol. Add the parse-eliminated arm if the full-path result cannot otherwise say which side to change. Record payload bytes, latencies, and allocation count or `unmeasured`.
- [x] 5.3 Decide outcome A, B, or C from design D6. A byte-count mismatch invalidates the run. A gain inside the fresh noise floor is outcome C.

## 6. Name-path decision

- [x] 6.1 If outcome A, implement the smallest production change that captures the measured side, preferring direct serialization from interned or generation-owned names over a custom small-string class. Keep snapshot format version 3 unless the measured design cannot represent the same bytes, in which case incompatible peers must fail closed.
- [x] 6.2 If a pooled or zero-copy representation is retained, prove backing storage outlives readers across generation replacement, disconnect, teardown, and concurrent readers, and prove bounded teardown and resource retention: cover stalled readers, repeated generation replacement, disconnect, publisher teardown, and concurrent readers; shutdown must not wait indefinitely for a reader and the engine must not retain every old generation. Preserve the current-mapping / independently-held-client-mapping contract in `src/engine/src/SnapshotPublisher.h`. ASan and focused lifetime tests must pass. Revert an unsafe speedup. This does not authorize claiming the separate privileged raw-I/O completion bound is solved.
- [x] 6.3 Prove Unicode, mixed-case, maximum-length names, copy, move, comparison, and exact fixture output equivalence for any retained name change. Confirm no material search or unjustified memory regression. (Evidence: `ffprotocol_snapshot_pooled_equivalence_tests` covers 12 randomized fixtures with Unicode, mixed-case, empty, and 32,767-char names plus copy/move view remap; `ffengine_pooled_snapshot_lifetime_tests` proves engine-level export equivalence on a seeded Unicode/mixed-case/CJK fixture; the retained search campaign shows selective +1.6% (inside noise), Unicode -10.9%, mixed-case -26.8%; `projection-memory-decomposition.md` records the 8.3 rejection with no unjustified memory regression.)
- [x] 6.4 Retain the change only when the real first-generation path improves by a meaningful margin clearly above the fresh same-binary noise floor (10 percent remains the strong target, not a hard floor), with no correctness regression and no material common selective-search regression (both unconditional) and any memory tradeoff specifically justified and measured; otherwise reject it with the before, after, noise, and payload evidence. Outcome B does not add an inline-string type; if payload size accounts for most of the historical gain, investigate a separate wire-format/payload-reduction candidate as its own measured arm and record a measured retention/rejection decision or a concrete blocker — the investigation is not optional and does not force retaining a format change without evidence. Outcome C leaves production name code unchanged.
- [x] 6.5 Do not repeat packed serialization or in-place row construction unless a new profile shows a different dominant cost.

## 7. Broad search

- [x] 7.1 After the name decision, freshly benchmark selective, moderate, broad, no-result, exact, short-prefix, mixed-case, Unicode, high-limit, and cancellation or replacement queries on the current candidate. Do not use the historical ~95 ms figure as the before row.
- [x] 7.2 Profile candidate acquisition, normalized-field access, matching, result construction, sorting, top-N selection, and publication. Record whether the existing filename index is on the measured broad path.
- [x] 7.3 Implement at most the evidence-backed candidate. Do not revive the fold-free matcher unless this profile shows an end-to-end broad-search win.
- [x] 7.4 Retain only a material broad-query reduction, preferably at least 10% or a meaningful absolute drop, without a material selective, Unicode, mixed-case, cancellation, or memory regression. Otherwise revert and record the rejection.

## 8. Projection memory

- [x] 8.1 Remeasure projection memory on the current candidate. Decompose rows, names, normalized names, arena overhead, vector capacity, maps, secondary indexes, identifiers, path structures, allocation overhead, and padding using working-set or allocation evidence, not `sizeof` alone.
- [x] 8.2 Classify duplication across SQLite, the projection, `NamePool`, the filename index, the snapshot export, the UI generation, and search results as intentional, transient, or accidental.
- [x] 8.3 Retain a memory change only when it saves a material fraction at realistic scale, preferably at least 10%, without materially worsening search, navigation, indexing, startup, or reconciliation. Removing a useful index that makes search materially slower is not by itself a win. Otherwise reject and record the tradeoff.

## 9. User-visible behavior and the search flake

- [x] 9.1 After any retained engine or search change, check navigation-to-ready, search-input-to-visible-results, tab-switch-to-ready, Home-to-ready, and large-directory-to-ready where practical. Do not turn those checks into a brittle wall-clock gate. If a check cannot be run, say so.
- [x] 9.2 Attempt to reproduce `search-deep-result-navigation` (`alpha` not accepted, observed empty on AutomationId 7101) with repeated focused trials and an aggregate run.
- [x] 9.3 Investigate readiness, UIA cache staleness, focus transfer, text and value patterns, foreground state, control recreation, typing versus direct value set, and event synchronization. Instrument the harness only as far as that distinction requires.
- [x] 9.4 Fix a deterministic harness defect in the harness, or a deterministic product race in the product. If the matrix does not reproduce it, record `UNREPRODUCED / OBSERVED FLAKE` and keep the historical failure.

## 10. Adversarial review

- [x] 10.1 Reviewer A tries to prove each claimed speedup is noise, using the fresh same-binary floor and payload-identity precondition. (Reviewer A: broad/extension separations SUPPORTED as not noise; the pooled retention claim was UNPROVEN as attributed because the decision session lacked an in-session same-binary control and carried a 12.4% same-binary artifact session -- resolved by the in-session controls + counterbalanced A/B recorded in evidence/retention-ab-pooled-name-path.md and by correcting the record; fixed-permutation, dirty-provenance, and search-arm-difference notes recorded.)
- [x] 10.2 Reviewer B attacks name ownership, dangling views, generation replacement, disconnect, and teardown. (Reviewer B: no dangling-view defect found in the pooled model; found and fixed a real heap-use-after-free in UiServer::EnforceIndexedWorkingSetCapLocked (key dereferenced after erase), copy-and-swap exception safety, a RemapEntryNames guard, the vacuous equivalence gate, and the missing case-insensitive coverage.)
- [x] 10.3 Reviewer C challenges Unicode, mixed case, result limits, sorting, and cancellation. (Reviewer C: found the empty-primaryTerm tier divergence (descending Relevance admission skipped retained rows) and the incomplete capped-truncation fix in ExecuteSearchCore/ExecuteSearchViews; both fixed with a classifier property test and a top-N content assertion. Unicode/mixed-case/limits/sorting/cancellation otherwise SUPPORTED.)
- [x] 10.4 Reviewer D checks whether a memory win hides a latency or CPU regression. (Reviewer D: confirmed the admission adds no scan-floor cost (it returns before classify) and no production caller uses the owning conversion; corrected the memory-probe wording (owning reference path), the residual attribution (unseparated, not fragmentation), and the search noise-floor labeling.)
- [x] 10.5 Reviewer E attacks the equivalence gate with `.cpp`, header, nested-header, CMake, unresolved-include, and added or deleted source inputs. (Reviewer E: found the configure_file template false PASS (fixed; the new template control now FAILs naming cmake/FastFilesVersion.rc.in) and a fabricated C1 implementationSha in the v2 record (corrected); v3 record carries eight controls plus the recorded bounds. All seven original controls re-proven.)
- [x] 10.6 Reviewer F compares tasks, lifecycle, workstate, GitHub, PR state, tests, and benchmark records. Fix every material reproduced finding. (Reviewer F: dirty tree restored, task 6.3 ticked with evidence, the capped-truncation behavior delta recorded in proposal+spec, WORKSTATE refreshed; benchmark/certification numbers and Git/PR/hosted facts SUPPORTED; temp-worktree cleanup pending before push.)

## 11. Final certification

- [x] 11.1 Commit the last retained production edit, or the last rejection if none is retained. Record `IMPLEMENTATION_SHA`. Rebuild cleanly so identity-bearing binaries embed that SHA. (IMPLEMENTATION_SHA = 63ddc21acf089ad92ddb5783882a74a595a2513c; the review-fix commit is d50c474; all presets rebuilt at the tip before the ladder; identity binding/freshness pass inside the parity CTest legs.)
- [x] 11.2 Run Debug CTest, release-test CTest, Analyze, the authoritative ASan slice, strict OpenSpec, provenance, governance, project status, doc drift, repository hygiene, release-candidate qualification, and local CI parity. Use actual registered totals. No historical reuse. (Local CI parity 11 PASS / 0 FAIL / 0 SKIP at the clean tip: preflight, policy, strict OpenSpec 91/91, debug/release-test/analyze builds, Debug CTest 127/127, release-test CTest 124/124, ASan slice 4/4, hygiene suite PASS, release-candidate PASS. Analyze warning-clean.)
- [x] 11.3 Rerun E2E, UIA core, UIA full, visual catalog, and the performance matrix on that implementation candidate. Record honest skips separately from passes. (E2E 8/0/0 boundToHead=true; UIA core 16/0/0; UIA full 23/6/0; visual catalog 20/20; many-tabs 1/1; performance matrix = the retention/search measurements recorded in evidence.)
- [x] 11.4 Update `docs/agent/WORKSTATE.md` from live reconciliation. Supersede the stale `main = 9e2dddc` and "no local action" checkpoint. Preserve it as history. Do not embed the SHA of the workstate commit itself. (WORKSTATE current checkpoint rewritten twice: review-resolution frontier and the pre-ladder certification checkpoint with IMPLEMENTATION_SHA; stale bullets and corrupted paths fixed.)
- [x] 11.5 Write the final report with sections A–M from the campaign brief: repository truth, certification counts, flake result, equivalence controls, allocation isolation, name decision, search decision, memory decision, user-visible effect, reverted ideas, SHA relationship, hosted CI, and remaining work. (evidence/final-report.md contains sections A-M with actual totals, all SHA identities, the reviewer dispositions, and the external-only remaining work.)
- [x] 11.6 After every tracked documentation, task, and report commit (including 11.4, 11.5, and all checkbox updates) is committed, run the corrected equivalence gate between `IMPLEMENTATION_SHA` and the actual final tracked tip. Record implementation SHA, certified binary-embedded SHA, evidence SHA, repository tip SHA, executable-impacting digest, and verdict. If any later tracked commit occurs — even a checkbox, report, or WORKSTATE edit — run it again. Keep the final live tip and verdict outside the checked commit (generated artifact, PR body, or session response), never as another tracked annotation. Do not open another certify-then-document loop after that relationship is proved.

## 12. Reviewable handoff

- [ ] 12.1 Push the feature branch without force. Open one reviewable pull request. Do not merge it. Set `MERGE_ELIGIBLE` to YES or NO with exact reasons.
- [ ] 12.2 Inspect hosted CI once for the final candidate or evidence tip. If jobs fail before any step, classify `HOSTED_CI = BLOCKED_EXTERNAL` and do not rerun or change billing. If jobs execute and fail for a repository reason, fix that failure.
- [ ] 12.3 Leave production signing, clean-host install, elevated MFT/USN, the raw-volume completion bound, the physical display matrix, and guest/provider work external. Do not check predecessor tasks 5.4 or 2.3. Continue only if another measured, material, locally executable candidate is already concrete; do not invent speculative work to extend the campaign.
