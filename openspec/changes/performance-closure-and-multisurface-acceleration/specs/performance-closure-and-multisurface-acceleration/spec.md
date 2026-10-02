# performance-closure-and-multisurface-acceleration

## Purpose

Close the unfinished measured-performance certification truthfully, then keep a name-path, search, or projection-memory improvement only when a fresh measurement separates it from noise.

## ADDED Requirements

### Requirement: Predecessor certification debt is closed before product optimization

The campaign SHALL NOT change production file-manager behavior until the predecessor campaign's open certification tasks have current successful results on the candidate under test, or an evidenced, genuinely external classification with the exact command, count, and reason. A locally executable gate that has failed or has not been run remains open: it blocks production optimization, and recording its failed result does not close it. Investigation and necessary defect repair may proceed while such a gate is open; a performance change that bypasses the gate may not. The predecessor change MAY be marked completed only after its open debt is closed or honestly external, its stale evidence is corrected, repository truth is reconciled, and current-state validation is recorded.

The rerun SHALL use the existing assertions. The campaign MUST NOT raise a fixed wall-clock limit, convert a failing lifecycle test to a skip, or repeat a run until a pass appears.

#### Scenario: Quiet-host certification precedes optimization

- **WHEN** the successor campaign begins product-performance work
- **THEN** the predecessor's open analysis, sanitizer, end-to-end, accessibility, and visual tasks have current passing results recorded against the candidate, or an evidenced genuinely-external classification
- **AND** a load-sensitive lifecycle test has been rerun on a host that meets the established idle gate, or its idle-host failure is recorded as a real defect under investigation

#### Scenario: A wall-clock assertion is not relaxed to obtain green

- **WHEN** a lifecycle test exceeds its existing fixed time bound on an idle host
- **THEN** the bound remains unchanged
- **AND** the result is investigated as a product or test defect rather than retried into a pass

### Requirement: Stale evidence is corrected without rewriting measurements

The campaign SHALL preserve original measured values when it corrects an interpretation. A record that describes a conflated experiment as an isolated cause MUST be marked as conflated, and the historical numbers MUST remain recoverable.

A superseded validation record MUST be versioned or explicitly preserved. The campaign MUST NOT delete the historical row in order to make the current gate look as if it had always included the missing control.

#### Scenario: A conflated probe keeps its numbers

- **WHEN** a prior probe changed both allocation behavior and serialized payload size
- **THEN** the durable record identifies it as a conflated allocation-plus-payload experiment
- **AND** the original measured latencies remain present and are not edited into agreement with a later conclusion

#### Scenario: A pre-fix gate record is not silently replaced

- **WHEN** the current equivalence gate covers more inputs than an older durable validation
- **THEN** the new validation is stored as current evidence
- **AND** the older path count and control set remain identifiable as historical

### Requirement: Executable-tree equivalence is adversarially proved and bounded

The certification gate SHALL prove git-controlled executable-impacting source and build-input equivalence between an implementation revision and a later evidence revision. It MUST fail closed for a compiled translation-unit change, a header-only change reachable through the compile graph, an unresolved quoted project include, and a build-affecting build-manifest change. A documentation-only pair MUST pass.

The durable current validation SHALL name each control, the expected verdict, the observed verdict, and the changed input when the verdict is a failure. The campaign MUST NOT claim reproducible-binary equivalence. The record SHALL state that compiler version, SDK version, environment-derived preprocessor definitions, unmodeled command-line flags, and external toolchain binaries are outside the claim unless a later control actually includes them.

#### Scenario: Documentation-only evidence does not invalidate certification

- **WHEN** two revisions differ only in documentation or evidence and their executable-impacting inputs are unchanged
- **THEN** the gate reports equivalence
- **AND** the durable record identifies the pair as the positive control

#### Scenario: A header-only product change fails the gate

- **WHEN** two revisions differ only in a product header reachable from the compile graph
- **THEN** the gate fails
- **AND** the record names that header

#### Scenario: An unresolved quoted project include fails closed

- **WHEN** a quoted project include cannot be resolved from the derived compile graph
- **THEN** the gate fails rather than reporting full coverage
- **AND** the failure identifies the unresolved include

#### Scenario: The claim boundary is explicit

- **WHEN** the campaign records a passing self-application of the gate
- **THEN** the record states that the pass covers derived source and build inputs
- **AND** it does not state that the binaries are bit-for-bit reproducible

### Requirement: Allocation cost is isolated before a name-representation change

Before any production name-representation change, the campaign SHALL measure how much first-generation delivery latency is caused by per-entry owning-string allocation when logical names, name lengths, serialized byte count, directory count, file count, and generation identity are held constant.

The experiment MAY be benchmark-only. It MUST NOT change the production wire format merely to obtain the measurement. The machine-readable result SHALL record payload byte count, export latency, publication latency, client-parse latency, and total latency for both arms, plus allocation count and allocated bytes when those are measurable. If an allocation counter cannot be attached without changing the measured path, the record SHALL say the counter is unmeasured rather than inventing a count.

Both arms SHALL be compared with the campaign's idle-gated, core-pinned, high-priority, interleaved protocol. A gain inside the freshly measured same-binary noise floor MUST NOT be treated as an allocation speedup.

#### Scenario: Payload identity is a precondition

- **WHEN** the isolation experiment reports an allocation-only comparison
- **THEN** both arms have the same logical names, name lengths, serialized byte count, directory count, file count, and generation identity
- **AND** a difference in any of those fields invalidates the allocation-only conclusion

#### Scenario: A small isolated gain rejects the representation change

- **WHEN** the allocation-only total improvement does not clearly exceed the same-binary noise floor
- **THEN** no production name-representation change is retained from that experiment
- **AND** the rejection records the before, after, noise floor, and payload byte counts

#### Scenario: Outcome B triggers a mandatory measured payload investigation

- **WHEN** the isolated experiment shows payload size accounts for most of the historical gain
- **THEN** the campaign investigates a separate wire-format or payload-reduction candidate and records a measured retention or rejection decision, or a concrete blocker
- **AND** the investigation is not dropped as optional
- **AND** no inline-string class is added and no format change is retained without evidence

### Requirement: A production performance change is retained only when it beats noise

The campaign SHALL retain a production change only when the targeted user-visible metric improves beyond the freshly measured same-binary variation, the output remains equivalent for the same fixture, the change introduces no correctness regression, and the change introduces no material regression in common selective search. Correctness and common selective-search protections are unconditional: no measured win, however large, may justify a correctness regression or a material common-selective-search regression. Only a specifically justified, measured memory tradeoff may use an exception to the general regression rule, and that tradeoff must still satisfy the memory surface's acceptance rule.

A strong success target for the real first-generation path is at least a 10 percent improvement with a clear margin above same-binary variation. That target MUST NOT be replaced by a historical upper bound from a conflated probe. A candidate inside the noise floor SHALL be reverted and recorded.

Packed snapshot serialization and in-place row construction MUST NOT be repeated unless new evidence shows a different cost than the recorded noise-floor rejections. A matcher microbenchmark win MUST NOT be retained unless the broad-search end-to-end metric improves.

#### Scenario: An output-inequivalent speedup is rejected

- **WHEN** a candidate changes published directory count, file count, generation identity, or serialized payload semantics for the same fixture
- **THEN** it is not retained as a performance improvement
- **AND** the mismatch is recorded

#### Scenario: A noise-floor candidate is reverted

- **WHEN** a candidate's targeted metric moves by less than the same-binary run-to-run spread
- **THEN** the candidate is reverted
- **AND** the record states the spread and the decision

### Requirement: Zero-copy name storage cannot outlive its backing generation

If a retained name optimization avoids per-entry owning strings by referencing pooled or mapped storage, that storage SHALL outlive every reader. Publication of a replacement generation, client disconnect, and teardown MUST NOT leave a reader with a dangling name view. Concurrent readers MUST NOT observe freed storage. The campaign SHALL prove those bounds with sanitizer and focused lifetime tests before retaining the optimization.

Teardown and resource retention SHALL also be bounded. The focused lifetime tests MUST cover stalled readers, repeated generation replacement, client disconnect, publisher teardown, and concurrent readers. Shutdown MUST NOT wait indefinitely for a reader, and the engine MUST NOT retain every old generation. The existing ownership contract in `src/engine/src/SnapshotPublisher.h` — the engine keeps only the current mapping while a client that still holds an independently-held mapping can finish reading it safely — MUST be preserved. These guarantees do not authorize claiming that the separate privileged raw-I/O completion bound is solved.

A name representation SHALL accept Unicode names, mixed-case comparison, and pathological maximum-length names. It MUST NOT change wire-format compatibility unless the measured design requires it and both peers reject an incompatible snapshot rather than parsing it as a different layout.

#### Scenario: Generation replacement does not dangle a reader

- **WHEN** a client is reading names from a published generation and a newer generation replaces it
- **THEN** the client either finishes against storage that remains valid or observes a defined unavailable result
- **AND** it does not read freed name storage

#### Scenario: A maximum-length name still round-trips

- **WHEN** a fixture contains a pathological maximum-length Unicode name
- **THEN** export, publication, and parse preserve that name
- **AND** search comparison remains case-correct for mixed-case and non-ASCII names

#### Scenario: Teardown is bounded and old generations are not retained

- **WHEN** a stalled reader holds an old generation while generations are repeatedly replaced, or the publisher shuts down or a client disconnects
- **THEN** teardown completes without waiting indefinitely for the reader and the engine does not retain every replaced generation
- **AND** a stalled or concurrent reader observes valid storage or a defined unavailable result, never freed storage

### Requirement: Broad search is freshly profiled before a matcher change

After the name-path candidate is retained or rejected, the campaign SHALL measure selective, moderate, broad, no-result, exact, short-prefix, mixed-case, Unicode, high-limit, and cancellation or replacement queries on the current candidate. It MUST profile candidate acquisition, normalized-field access, matching, result construction, sorting, top-N selection, and publication before selecting a search optimization.

A broad-search change SHALL be retained only when broad-query latency has a material reduction, preferably at least 10 percent or a meaningful absolute decrease, without materially degrading common selective queries, Unicode correctness, mixed-case correctness, cancellation, or memory. Historical broad-search latency MUST NOT be the before row when a current measurement can be taken.

#### Scenario: Broad search is not improved by slowing selective search

- **WHEN** a search candidate reduces broad-query latency
- **AND** common selective-query latency regresses materially
- **THEN** the candidate is not retained, regardless of the broad-query speedup
- **AND** the selective before and after are recorded

#### Scenario: Cancellation remains bounded

- **WHEN** a broad query is cancelled or replaced before completion
- **THEN** the replacement observes the new query rather than a stale result set
- **AND** the cancelled query does not continue to publish as the current result

### Requirement: Projection memory is decomposed before a representation change

The campaign SHALL remeasure current projection memory and decompose the contributors that are actually resident, including rows, names, normalized names, arena overhead, vector capacity, maps, secondary indexes, identifiers, path structures, allocation overhead, and padding. A `sizeof` estimate alone MUST NOT justify a representation change.

A memory reduction SHALL be retained only when it saves a material fraction at realistic scale, preferably at least 10 percent, without materially worsening search, navigation, indexing throughput, startup, or reconciliation. Removing a useful accelerator that makes search materially slower is not by itself a win. Duplication across the durable store, projection, name arena, filename index, snapshot, UI generation, and search results SHALL be classified as intentional, transient, or accidental before a copy is removed.

#### Scenario: An index removal that slows search is not an automatic win

- **WHEN** a memory candidate removes a secondary filename index
- **AND** broad or selective search becomes materially slower
- **THEN** the candidate is retained only if the recorded tradeoff still meets the acceptance rule
- **AND** both the byte delta and the latency delta are present

#### Scenario: Current memory is not inherited

- **WHEN** the campaign records a projection-memory before row
- **THEN** the row comes from the current candidate
- **AND** a historical bytes-per-entry figure is cited only as a reference

### Requirement: User-visible responsiveness is checked against retained engine changes

After a retained engine or search change, the campaign SHALL check, where practical, whether navigation to a usable directory, search input to visible results, tab switch to ready content, Home to ready, and large-directory ready time move in the same direction as the engine measurement. These checks MUST NOT be turned into a brittle wall-clock pass/fail gate. If a check cannot be run, the record SHALL say so instead of inferring a user-visible win from an engine timer alone.

#### Scenario: An engine win is not automatically a UI win

- **WHEN** an engine benchmark improves and the corresponding interactive path is not measured
- **THEN** the report separates the engine result from user-visible responsiveness
- **AND** it does not claim the user will notice the change

### Requirement: The search-input flake is reproduced or left unreproduced

The campaign SHALL attempt to reproduce the previously observed search-input failure, where a search query was not accepted and the observed value was empty, using repeated focused trials and an aggregate run. It SHALL investigate control readiness, cache staleness, focus transfer, text and value patterns, foreground state, control recreation, typing versus direct value setting, and event synchronization.

A deterministic harness defect SHALL be fixed in the harness. A deterministic product race SHALL be fixed in the product. If the failure remains unreproduced after a defensible trial matrix, the classification SHALL remain unreproduced observed flake. The campaign MUST NOT fabricate closure.

#### Scenario: An unreproduced flake stays visible

- **WHEN** the focused trials and the aggregate run do not reproduce the empty search-query failure
- **THEN** the result is recorded as unreproduced
- **AND** the historical failure is retained rather than deleted from the record

### Requirement: Final certification names the implementation and evidence tips separately

After the last retained production edit, the campaign SHALL establish an implementation revision, rebuild cleanly, and run the local certification ladder against that revision using actual registered totals. Historical pass counts MUST NOT be reused.

If a later evidence revision differs from the implementation revision, the campaign SHALL run the corrected equivalence gate and record the implementation revision, the binary-embedded revision, the evidence revision, the repository tip revision, the executable-impacting digest, and the verdict. The gate SHALL run after the final tracked documentation, task, and report commit so the checked relationship ends at the actual final tracked tip; any subsequent tracked commit, including a checkbox, report, or workstate edit, requires another gate run. The campaign MUST NOT write a tracked annotation merely to embed the final gate invocation: the final live tip and verdict belong in a generated artifact, the pull request body, or the session response outside the checked commit. The campaign MUST NOT create an unbounded certify-then-document loop. A final report MAY name a certified implementation revision and a separate evidence tip when that relationship is mechanically established.

Local validation MUST NOT be reported as hosted certification. Hosted CI for the final candidate or evidence tip SHALL be inspected once. If jobs fail before executing any step, the classification SHALL be externally blocked, and the campaign MUST NOT repeatedly rerun them or change billing. External signing, clean-host, elevated volume, physical-display, and guest boundaries MUST remain open when their evidence is absent.

#### Scenario: Evidence does not impersonate the certified binary

- **WHEN** evidence commits move the repository tip after the certified implementation revision
- **AND** the equivalence gate proves the executable-impacting trees match
- **THEN** the report names both revisions and the verdict
- **AND** it does not say the evidence tip itself was rebuilt

#### Scenario: A zero-step hosted failure stays external

- **WHEN** hosted jobs for the inspected tip fail with no executed steps
- **THEN** hosted CI is classified as externally blocked
- **AND** local results remain labeled as local results

### Requirement: The successor is reviewable and not self-merged

The campaign SHALL leave its implementation on a branch created from the actual main tip, with coherent commits and no history rewrite or force-push. It SHALL open a reviewable pull request and MUST NOT merge that request from the campaign. The merge-eligibility statement SHALL give the exact reasons it is or is not eligible. A predecessor record that said "not for merge yet" and was merged anyway MUST NOT be treated as permission to merge this successor.

#### Scenario: The campaign does not merge itself

- **WHEN** the final handoff is written
- **THEN** the pull request is open or explicitly unmerged
- **AND** the handoff states merge eligibility without performing the merge
