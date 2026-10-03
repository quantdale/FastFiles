## Why

`origin/main` is `bdd7d7b5fb91586df957ce43b348c7fb95cbeae3`, and that tip is not a closed performance campaign. PR #12 merged `measured-performance-acceleration-and-exact-certification` even though its body said "not for merge yet." Tasks 6.3 and 6.4 are still open, the durable equivalence record still describes the pre-header-fix 228-path gate, and `allocation-probe.json` still says the short-name probe isolates allocation. The only retained edit made the benchmark match production; it did not make FastFiles faster. Closing the ledger without a measured product decision would repeat that failure.

## What Changes

- Finish the predecessor's open certification debt on a quiet host before any production optimization. Do not raise the 30-second Start/Stop assertion, convert it to a skip, or retry it until a lucky pass appears. A locally executable failed or unrun gate stays open and blocks production optimization; recording its failed result does not close it. Investigation and necessary defect repair may proceed; a performance change that bypasses the gate may not.
- Correct stale machine-readable evidence without rewriting measured numbers. Mark the short-name probe as a conflated allocation-plus-payload experiment, and replace the pre-fix equivalence record with a versioned current validation that preserves the historical row.
- Re-prove `Test-ExecutableTreeEquivalence.ps1` with documentation, compiled `.cpp`, header-only, unresolved quoted-include, and CMake controls. The claim is git-controlled executable-impacting source and build-input equivalence, not a reproducible binary.
- Run a benchmark-only allocation-isolation experiment that keeps names, name lengths, serialized bytes, counts, and generation identity constant. Enter a production name-representation design only if that isolated gain clearly exceeds the freshly measured noise floor. If payload size accounts for most of the historical gain, investigate a separate wire-format/payload-reduction candidate and record a measured retention/rejection decision or a concrete blocker — that investigation is not optional, adds no inline-string class, and forces no format change without evidence. Any retained pooled or zero-copy name representation must also prove bounded teardown and resource retention (stalled readers, repeated generation replacement, disconnect, teardown, concurrent readers) under the existing `SnapshotPublisher` current-mapping ownership contract.
- After that decision, freshly profile broad search and projection memory. Retain a change only when it improves the targeted metric without a material regression; otherwise record an evidence-backed rejection. Do not repeat packed serialization, in-place row construction, or the fold-free matcher without new end-to-end evidence.
- Attempt the `search-deep-result-navigation` flake with a defensible trial matrix. Fix a deterministic harness or product defect; otherwise keep `UNREPRODUCED / OBSERVED FLAKE`.
- Land the work on `feat/performance-closure-and-multisurface-acceleration-20261001` and, at apply time, a reviewable pull request. Do not merge it from this campaign.

## Capabilities

### New Capabilities

- `performance-closure-and-multisurface-acceleration`: close the current certification debt truthfully, isolate allocation from payload size, and retain a name-path, broad-search, or projection-memory improvement only when a fresh measurement justifies it.

### Modified Capabilities

- `indexed search result truncation`: the bounded result set is corrected to
  retain the documented top-N by the requested sort and direction. A
  comparator-direction defect in the bounded heap made capped searches retain
  the worst N and discard the best result; the fix is recorded as an explicit
  delta in this change's spec because it changes observable results for capped
  searches. All other existing product requirements are unchanged. The
  predecessor change is now `completed`: its open certification tasks were
  closed with current evidence by this change's task 2.7, and it is not
  superseded.

## Impact

- Certification and evidence: predecessor tasks 6.3 and 6.4, `allocation-probe.json`, `equivalence-gate-validation.json`, `docs/agent/WORKSTATE.md`, and `openspec/lifecycle.json`. No product source is changed by the proposal itself.
- Measurement and, only if justified, product code: `tests/engine/bench_first_generation_publish.cpp`, `IndexPipeline::ExportDirectorySnapshot` (`ToWString(projection_.Names().Get(...))` at the two export sites), `ffprotocol::SnapshotDirectoryEntry`, `ParseSnapshot`, the filename-index search path in `IndexPipeline`, and `Projection::EstimatedMemoryBytes` / `NamePool`.
- UI validation: `verify/capabilities/windows-ui-automation-validation` scenario `search-deep-result-navigation`, plus E2E, UIA core/full, and the visual catalog on the candidate actually under test.
- No new privileged command, no open-path primitive, and no change to foundation decisions D1–D6. The snapshot format stays version 3 unless a measured candidate proves a format change is necessary. Wall-clock benches stay outside ordinary CTest.
- External boundaries stay external: hosted CI if jobs still fail before steps, production signing, clean-host install, elevated MFT/USN, the raw-volume completion bound, the physical display matrix, and authorized guest/provider work. Predecessor tasks 5.4 and 2.3 are not checked by supersession.

## Goals

- A reader can distinguish the merged-but-unfinished predecessor from this successor, with live SHA, PR, hosted-run, and open-task evidence named.
- Certification debt is closed with current counts, or left honestly external with evidence. Stale evidence is corrected without fabricating agreement between numbers. A locally executable failed or unrun gate remains open.
- At least one real product-performance improvement is retained, or each concrete high-value candidate is measured and legitimately rejected.
- The final pull request is reviewable and not auto-merged.

## Non-goals

- Implementing product code, running the certification ladder, or opening the pull request in the propose session.
- Raising, skipping, or luck-retrying the 30-second volume-session assertion.
- Claiming the historical 27.39% short-name result as an allocation-only speedup.
- Expanding the equivalence gate into a full reproducible-build system.
- Changing billing, branch protection, signing pins, or trust stores.
- Creating speculative follow-on changes before the measured successor loop stops.

## Open questions

- Whether allocation-only cost, with payload held constant, exceeds the remeasured same-binary noise floor. The experiment answers this; the proposal does not.
- Whether `ffengine_volume_session_manager_tests` still exceeds 30 seconds on an idle host. If it does, that is a product or test defect to investigate, not a threshold edit.
- Whether `search-deep-result-navigation` reproduces. Closure is not assumed.
- Whether hosted jobs for a future evidence tip still fail before any step. The annotations API did not return a billing message for run `36786432149` in this session; apply inspects the live run once and does not inherit that label.
