# measured-performance-and-exact-certification

## Purpose

Make a measured, attributable claim about FastFiles' user-visible performance
without weakening any correctness, durability, security, accessibility, or
provenance requirement, and express certification so that an evidence-only
commit cannot silently invalidate it.

## ADDED Requirements

### Requirement: A fresh exact-head baseline precedes any performance claim

A performance campaign SHALL establish its baseline from a build whose source
identity is the actual campaign-start SHA, with a clean tree, before any
optimization is measured or claimed.

The machine-readable baseline record SHALL contain, at minimum: source SHA,
tree cleanliness, build preset, fixture parameters, warm-up count, repetition
count, timestamp, and metric definition.

The campaign SHALL NOT reuse a historical measurement as the primary
"before" row when a current exact-head baseline can be generated.

#### Scenario: baseline is taken at the exact starting SHA
- GIVEN a campaign start SHA with a clean tree
- WHEN the campaign establishes its baseline
- THEN the baseline binaries embed that exact SHA
- AND the baseline record names that SHA, its tree state, its preset, its
  fixture parameters, and its warm-up/repetition counts.

#### Scenario: historical numbers are never the before row
- GIVEN a historical measured value exists for the same metric
- WHEN the campaign records a before/after comparison
- THEN the before row is the freshly measured baseline
- AND the historical value may be cited as a reference only.

### Requirement: A candidate is retained only when its effect exceeds host noise

An optimization SHALL be measured against a baseline in the same session on
the same machine, in interleaved randomized order, with the host quiesced.

A change SHALL be retained only when the targeted meaningful metric improves
beyond the observed natural variation. When metrics disagree, the campaign
SHALL prioritize user-visible end-to-end latency, then tail latency, then
production-path throughput, then CPU/resource utilization, then memory, then
isolated microbenchmarks.

A change whose measured effect is indistinguishable from a no-op rerun of the
same unchanged binary SHALL be reverted. A reverted candidate is an
acceptable campaign result and SHALL be recorded with its evidence.

#### Scenario: a favorable total row inside the noise band is reverted
- GIVEN a candidate whose targeted rows move less than the same-binary
  run-to-run variation on the same host
- WHEN the campaign evaluates the candidate
- THEN the candidate is reverted
- AND the retention record states the observed noise floor and the reason.

#### Scenario: a candidate exceeds the noise floor
- GIVEN a candidate whose targeted user-visible metric improves beyond the
  observed natural variation with no unacceptable regression
- WHEN the campaign evaluates the candidate
- THEN the candidate is retained
- AND its before, after, delta, noise spread, and correctness evidence are
  recorded.

### Requirement: First-generation delivery is measured on the path the product takes

The first-generation delivery benchmark SHALL hand the exported snapshot map
to the UI server the same way production does, so it does not measure a copy
the product never performs.

The benchmark SHALL publish the same directory count, file count, and
generation identity as production for the same fixture, so the measure stays
meaningful.

#### Scenario: the benchmark moves the map
- GIVEN the exported snapshot map is no longer needed by the caller after the
  publish call
- WHEN the benchmark publishes it
- THEN it is passed by move, matching `Main.cpp` and
  `VolumeSessionManager`.

#### Scenario: published content is unchanged
- GIVEN the benchmark is run before and after the move-semantics repair
- WHEN the published directory count, published file count, and last
  generation id are compared
- THEN they are identical.

### Requirement: Wall-clock benchmarks are informational and never brittle gates

Noisy wall-clock benchmarks SHALL stay outside ordinary CTest and SHALL store
their results with provenance.

Deterministic structural or functional invariants that are stable MAY be
registered with CTest.

#### Scenario: a wall-clock benchmark is not a gate
- GIVEN a benchmark whose result varies by tens of percent run to run
- WHEN the suite is executed
- THEN the benchmark is not registered as a pass/fail CTest test.

### Requirement: Noise is characterized before an effect is claimed

A campaign SHALL record the run-to-run noise floor of its host, using a
same-binary control, before attributing a delta to a code change.

#### Scenario: same-binary control
- GIVEN the unchanged baseline binary
- WHEN it is measured repeatedly under the same protocol
- THEN the observed run-to-run spread is recorded
- AND any later claim smaller than that spread is not made.

### Requirement: Certification is a proved tree-equivalence, not a single SHA

A certification record SHALL model at least: `implementation_sha`,
`certified_binary_sha`, `evidence_sha`, and `repository_tip_sha`.

The record SHALL mechanically prove that the executable-impacting tree is
unchanged between the certified binary's SHA and any later evidence tip, by
comparing a canonical set of build inputs rather than by assertion.

The set of executable-impacting inputs SHALL be derived and verified, not
hard-coded from an unproven path list.

Documentation SHALL NOT state that a SHA's binaries were rebuilt when they
were not.

#### Scenario: an evidence-only commit does not invalidate certification
- GIVEN a certified binary built at SHA X
- WHEN a later commit Y changes only documentation and evidence
- THEN the record proves the executable-impacting tree of X equals that of Y
- AND certification stays valid for Y's binaries-without-rebuild.

#### Scenario: an executable-impacting change invalidates certification
- GIVEN a later commit that changes a source under the executable-impacting
  set
- WHEN the equivalence is computed
- THEN it fails
- AND certification is not claimed for that tip.

### Requirement: Local validation is never reported as hosted certification

A campaign SHALL check hosted CI only for a final candidate, record the exact
run, and classify it. Local validation SHALL NOT be presented as hosted
certification.

#### Scenario: hosted CI is blocked
- GIVEN hosted jobs fail before any step runs
- WHEN the campaign records hosted state
- THEN it is classified `HOSTED_CI = BLOCKED_EXTERNAL`
- AND local results are still reported as local results only.
