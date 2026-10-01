# performance-closure-and-multisurface-acceleration

## Context

See `proposal.md` for why this change exists. Live inspection at propose time, not the stale workstate, found:

- `origin/main` and the merged feature branch `feat/measured-performance-acceleration-20260930` are both `bdd7d7b5fb91586df957ce43b348c7fb95cbeae3`. Local `main` was behind at `3323fcb`. The primary worktree was clean. `.kilo/worktrees/scythe-cloud` remains detached at `0dc3356` and is not part of this change.
- PR #12 is `MERGED`. Its body still says "not for merge yet."
- `measured-performance-acceleration-and-exact-certification` is lifecycle-`active`, 30/32 tasks checked. Open: 6.3 Analyze plus the authoritative ASan slice, and 6.4 E2E, UIA core/full, and visual catalog. The change has no `design.md`; this successor does not invent one for it.
- Hosted run `36786432149` for that SHA completed with six jobs, zero steps, and null check output. The annotations API returned 404, so this design does not copy a billing sentence onto that run.
- The equivalence implementation in `verify/qualification/Test-ExecutableTreeEquivalence.ps1` already matches glued and spaced `-I`/`/I`, normalizes include roots, and fails closed on unresolved quoted includes. The durable JSON still records 228 paths and only the `.cpp` negative control.
- `allocation-probe.json` still says the short-name probe "isolates per-entry `std::wstring` heap allocation." The later pinned ceiling at 91.027 ms to 66.097 ms total, and the final report, correctly call that number an upper bound. The numbers disagree across files because they are different runs; they are not to be averaged into one story.
- Export still materializes an owning name at both `IndexPipeline` snapshot sites via `ToWString(projection_.Names().Get(child.nameId))`. `SnapshotDirectoryEntry::name` is a `std::wstring`. The UI parses the mapped generation into another owning map. `NamePool` is already an interned UTF-16 arena. Projection memory accounting already sums entry capacity, maps, the name pool, and filename-trigram postings; that estimator is not a working-set proof.

```text
projection NamePool (interned)
        |
        | ToWString  --> owning SnapshotDirectoryEntry::name
        v
export map --> serialize length-prefixed bytes --> mapped generation
                                                      |
                                                      v
                                              ParseSnapshot owning map
                                                      |
                                                      v
                                              ColumnView / search UI
```

The first-generation benchmark must keep measuring that production path. A benchmark-only arm may replace the owning-string step only when the serialized bytes stay identical.

## Goals / Non-Goals

**Goals:**

- Make the predecessor's open certification results current, or explicitly unverified, before any production edit.
- Make the equivalence and allocation records match what the code and later report already concluded.
- Answer the allocation question with payload held constant, then keep or reject each concrete successor from a fresh profile.

**Non-Goals:**

- A second planning system, a protocol bump, or a new privileged command.
- Bit-for-bit reproducible builds.
- Treating the historical 5–7% spread, 27.39% ceiling, 95 ms broad-search figure, or 329.2 bytes/entry as this campaign's before row.
- Closing raw-volume tasks 5.4 or 2.3.

## Decisions

### D1: One successor, predecessor stays active until its debt is closed

**Why:** The prompt forbids hiding product work inside the certification change and forbids marking that change completed before its debt is closed. Schema v2 allows more than one `active` entry. This change is registered beside `measured-performance-acceleration-and-exact-certification` with `supersededBy` left null.

**Alternatives:** Supersede the predecessor now and restate 6.3/6.4 here. Rejected: that hides an unfinished ledger. Extend the predecessor with the product work. Rejected: the prompt forbids hiding the optimization campaign inside the certification change.

**Trade-off:** Frontier reports show two active changes until apply closes the predecessor honestly. This change's tasks perform that closure; they do not check the predecessor's boxes at propose time.

### D2: Recertify on a quiet host without relaxing the lifecycle bound

**Why:** `ffengine_volume_session_manager_tests` asserts `TestStopReturnsWithoutWaitingForTheSchedulerInterval` finishes 2000 Start/Stop iterations in under 30 seconds. The same binary has ranged from about 9 seconds to about 41 seconds under host load. That makes the 123/124 release-test result unverified, not green.

**Alternatives:** Raise the bound, skip the test, or rerun until one pass. Rejected by the spec.

**Trade-off:** Apply waits for the established idle gate and may stop optimization work if the idle run still exceeds 30 seconds. A small controlled set, not more than three idle runs, distinguishes a stable pass from a stable failure. Any idle exceedance starts a product/test investigation. The historical loaded failure stays in the record.

The authoritative ASan slice is the hosted parity slice in `verify/Invoke-CiParity.ps1`: `ffprotocol_fuzz_tests`, `ffipc_framing_tests`, `ffipc_listener_lifecycle_tests`, and `ffvolumescanner_terminal_tests`. A retained name, search, or lifetime change also runs focused ASan tests for the code it touches. That extra slice does not redefine the hosted four.

### D3: Correct interpretation in a new record; do not edit historical numbers

**Why:** `allocation-probe.json` and `equivalence-gate-validation.json` are the historical artifacts. Rewriting them would destroy the false claim and the pre-fix path count that reviewers need to see.

**Alternatives:** Edit the old JSON in place. Rejected: a reviewer cannot then prove the correction preserved the numbers. Delete the old files. Rejected: that is the silent replacement the spec forbids.

**Trade-off:** Readers must open the successor record. The old files stay byte-identical. The new allocation record cites the original values, labels the probe `CONFLATED ALLOCATION + PAYLOAD-SIZE EXPERIMENT`, and does not compute a new "agreed" latency. The new equivalence record embeds the historical 228-path summary and adds the current controls.

### D4: The equivalence claim stops at derived source and build inputs

**Why:** The gate already derives translation units, the recursive quoted/angle include closure, and executable CMake inputs, and hashes `<path>\0<blob id>`. That is the relationship this campaign needs between an implementation SHA and an evidence tip. Compiler version, Windows SDK, environment preprocessor definitions, unmodeled flags, and toolchain binaries are outside that digest.

**Alternatives:** Hash full `compile_commands.json` command lines now. Deferred, not rejected: add that only if a control shows two revisions with the same derived source set would be falsely certified while their compile commands differ in a way that matters to this campaign. A full reproducible-build project is out of scope.

**Trade-off:** A passing gate does not mean the binaries match byte for byte. The evidence file must say that.

Required current controls, each identified in the machine-readable file:

1. Documentation-only pair passes.
2. Compiled `.cpp` pair fails and names the translation unit.
3. Header-only pair, equivalent in importance to `SnapshotFormat.h`, fails and names the header.
4. Unresolved quoted project include fails closed.
5. Build-affecting CMake change fails and names the manifest.
6. Final implementation SHA versus evidence tip passes when, and only when, the executable-impacting trees are actually unchanged.

Synthetic controls use a temporary commit or worktree and are not left on the branch. No force-push and no history rewrite of `main`.

### D5: Isolate allocation in the benchmark, with payload identity as a precondition

**Why:** The short-name probe shortened names, so it reduced heap allocation, serialized bytes, page faults, and parse work together. The production question is the owning-string cost at constant payload.

**Alternatives:** Change `SnapshotDirectoryEntry` to SSO first and measure. Rejected: that mutates the production model to obtain the measurement and repeats the overload risk the predecessor refused. Use the short-name result as the decision. Rejected by the spec.

**Approach:** Add a benchmark-only arm beside `ffengine_bench_first_generation_publish`. Both arms use the same 200,000-entry fixture, the same logical names, the same name lengths, the same directory and file counts, and the same generation identity. The treatment stores those names in pre-sized inline or arena storage and serializes the same length-prefixed bytes the production serializer would emit. It does not clear names and does not shorten them.

The comparison is a matrix, not one number:

- Control: production owning-string export and production parse.
- Export-eliminated: no per-entry heap allocation while building the bytes; production parser consumes those bytes.
- Parse-eliminated, only if needed: production bytes parsed without per-entry heap allocation.
- Full-path-eliminated: both sides avoid per-entry heap allocation while bytes stay identical.

A production design requires the split. A full-path win alone does not say which side to change.

Allocation count and bytes come from the benchmark-only string/arena counter, or from a bench-scoped allocator, on the release-test latency binaries. Do not move the latency comparison to Debug to obtain a CRT count. If the production arm's allocator cannot be counted without changing its cost, record `allocation_count: unmeasured` and still record the latency and byte-count identity. Payload byte count is mandatory and must match.

Protocol: idle gate, one logical core, high priority, interleaved randomized order, enough process-level samples to compare with a same-binary control measured in the same session. The historical 5–7% spread is a reference, not the threshold.

### D6: Production name work follows the isolation outcome

**Why:** The export sites already copy interned `NamePool` views into owning `std::wstring` values, and the client parses those bytes into another owning map. The best fix may remove one of those copies without a custom small-string class.

**Outcome A, allocation-only gain clearly above the fresh noise floor:** design the smallest production change that captures the measured side.

Prefer, in order:

1. Serialize length-prefixed names from the existing `NamePool` view, or from generation-owned offset/length entries, without an intermediate owning `wstring`, if that is the export cost the split measured.
2. A generation-owned character pool whose entries hold offset and length, with the pool lifetime tied to the snapshot generation.
3. An inline-plus-heap name type only if the first two cannot capture the measured win without a larger semantic break.

Do not start with "replace `std::wstring` with a custom SSO class." Avoid a wire-format version bump. Snapshot format version stays 3, and `IsSnapshotFormatCompatible` still fails closed on a mismatch, unless the measured design cannot represent the same bytes. Both peers ship together; there is no cross-version reader to preserve beyond that fail-closed check. Foundation D1–D6 are unchanged: no new command, no open-path primitive, no `$DATA` forwarding.

**Outcome B, the conflated gain disappears once payload size is constant:** do not add an inline-string type. A separate payload-reduction candidate is allowed inside this change only as another measured arm, with the same retain/revert rule. It is not a required product edit.

**Outcome C, isolated gain inside noise:** reject the representation change and continue to broad search.

**Lifetime, if any non-owning or pooled view is retained:** backing storage outlives readers; generation replacement either keeps the old storage alive until readers finish or returns a defined unavailable result; disconnect and teardown do not leave a view; concurrent readers do not observe freed storage. Prove it with ASan and focused lifetime tests. An unsafe zero-copy result is reverted even if it is faster.

**Acceptance if a production change is kept:** same method, same payload semantics, exact output equivalence for the fixture, no correctness regression, no material search regression, and no memory regression unless explicitly justified. The strong target is at least 10% on the real first-generation path, with margin over the fresh same-binary spread. The 27.39% ceiling is not the expected result.

**Trade-off:** The campaign can reject the largest historical opportunity. That rejection is a successful outcome.

### D7: Do not revive a rejected idea without new evidence

**Why:** Packed-record serialization was about -0.99% total, and in-place row construction was about -0.70% total, both inside the noise floor under the pinned protocol. The fold-free matcher improved a microbenchmark and did not establish an end-to-end win.

**Alternatives:** Retry them because the host is quieter now. Rejected unless a new profile shows a different dominant cost than the one those patches targeted.

**Trade-off:** Apply does not spend a slice rediscovering a recorded negative.

### D8: Broad search and projection memory are sequential measured campaigns

**Why:** The prompt's loop is verify, profile, isolate, implement, measure, retain or revert. Name work comes first because it is the only candidate with a large measured ceiling. Search and memory are the next concrete surfaces, not speculative extras.

**Search:** Use the current candidate and the existing informational benches (`ffengine_bench_large_index_scalability`, `ffengine_bench_secondary_filename_index`, and the literal-matcher bench) before adding a harness. Cover selective, moderate, broad, no-result, exact, short-prefix, mixed-case, Unicode, high result limit, and cancellation/replacement. Profile acquisition, normalized-field access, matching, result construction, sorting, top-N, and publication. The filename index already exists and is used for a sufficiently long simple literal when no cold volume is in the query; a broad or short query may not take that path. Do not add another index until the profile says acquisition or repeated normalized-name materialization dominates.

Retain a search change only for a material broad-query reduction, preferably at least 10% or a meaningful absolute drop, without a material selective, Unicode, mixed-case, cancellation, or memory regression.

**Memory:** Remeasure with the existing projection-memory bench. Decompose `Projection::EstimatedMemoryBytes` and a realistic working set. Classify SQLite, projection rows, `NamePool`, the filename index, the snapshot export map, the UI generation, and search-result structures as intentional, transient, or accidental. A `sizeof(ProjectionEntry)` estimate cannot authorize a layout change. Prefer at least 10% saved at realistic scale without a material search, navigation, indexing, startup, or reconciliation regression. Removing the filename index to save its posting memory is not a win if search becomes materially slower.

**Trade-off:** These phases can end in rejection. The successor loop stops when the next idea is speculative rather than when the task list is empty. It does not stop while a measured, material, locally executable candidate remains.

### D9: The search-input flake is a reproduction matrix, not a presumed fix

**Why:** A prior full UIA run failed `search-deep-result-navigation` because query `alpha` was not accepted on AutomationId 7101 and the observed value was empty. Later isolated and aggregate runs passed. The scenario lives in the UIA capability and already has an isolated runner.

**Approach:** Repeated focused trials plus an aggregate run. Instrument the harness only far enough to distinguish readiness, cache staleness, focus, text/value pattern, foreground, control recreation, typing versus direct value set, and event synchronization. Fix the harness or the product only for a deterministic reproduced defect. Otherwise record `UNREPRODUCED / OBSERVED FLAKE` and keep the historical failure.

**Trade-off:** The final UIA result may contain an honest skip or an unreproduced flake. That is not a pass fabricated by deletion.

### D10: Branch from the actual main tip; do not self-merge

**Why:** The prompt's last campaign was merged against its own no-merge wording. This change was proposed on `feat/performance-closure-and-multisurface-acceleration-20261001`, created from `bdd7d7b`. Apply re-derives the tip before adding implementation commits and does not reuse the merged PR branch as open development state.

**Alternatives:** Commit the campaign directly to `main`. Rejected: the user prompt explicitly overrides the generic session handoff for this work.

**Trade-off:** `main` will not contain the implementation until a later human merge. Apply opens one reviewable pull request and does not merge it. `MERGE_ELIGIBLE` is a statement with reasons, not an action.

### D11: Hosted CI is inspected once per final tip

**Why:** Run `36786432149` already shows the current main tip failed before steps. Each later push is inspected once. A repeated zero-step failure stays `BLOCKED_EXTERNAL`. No billing, protection, signing, or trust-store mutation is in scope.

**Trade-off:** The branch can be locally certified and still not be hosted-certified. The report must say so.

### D12: Workstate is a resume checkpoint, not a second results database

**Why:** The current `docs/agent/WORKSTATE.md` checkpoint still says `main` is `9e2dddc`, PR #11 is the live story, and no local action exists. Those claims are historical. Apply and this propose session replace the current checkpoint after live reconciliation. Git, source, and tests remain authoritative. The checkpoint must not require the SHA of the commit that contains the workstate itself.

**Trade-off:** Older checkpoints stay in the file as history. They are not deleted to make the top read cleaner.

## Risks / Trade-offs

- [Quiet-host gate never arrives because other agents saturate the machine] -> Record the blocker, do not kill unrelated user work, and do not certify the loaded 123/124 as green. Stop only campaign-owned or safely disposable measurement processes.
- [Isolation arm accidentally shortens names or changes byte count] -> The run is invalid. Do not interpret it as outcome A, B, or C.
- [Zero-copy parse is faster and use-after-free] -> ASan failure reverts it. Speed does not override lifetime.
- [Equivalence synthetic commits escape onto the pushed branch] -> Controls run in a temporary worktree and the feature branch history is inspected before push.
- [Two active changes confuse the frontier] -> The predecessor entry stays active only while 6.3/6.4 are open. This change's first apply slice closes or explicitly unverifies them and only then marks the predecessor completed, with evidence.
- [A memory win deletes a useful filename index] -> The search before/after is part of the retention rule, not a follow-up.
- [Hosted jobs start executing on a later push and fail for a repository reason] -> Fix the repository failure. Do not classify an executed failure as the old zero-step block.

## Migration Plan

1. Propose on the fresh branch. No product edit in that step.
2. Apply starts by re-deriving Git, OpenSpec, and hosted state, then runs the quiet-host certification slice.
3. Evidence corrections land before production edits, in their own commit.
4. The allocation experiment lands before any name-representation production commit.
5. Each retained optimization is its own commit. Each rejection is an evidence commit with the candidate absent from the tree.
6. After the implementation SHA, rebuild and certify. Evidence commits after that SHA require a passing equivalence result and then stop.
7. Push the feature branch without force, open one PR, inspect hosted CI once, and do not merge.
8. Rollback of a retained optimization is `git revert` of that slice, not a reset. A failed measurement is reverted before the next candidate.

## Open Questions

None that change the spec, the approach, or the task breakdown. The isolation outcome, the idle-host lifecycle result, flake reproduction, and the next hosted run are measurements this design already routes. They are not undecided approaches.
