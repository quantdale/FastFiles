## Why

`main` is at `3323fcb1e1c2a79a57386a6482e932b3189655e8`, and the first-generation
delivery benchmark (`ffengine_bench_first_generation_publish`) measures a path
that production does not take. Every production call site
(`Main.cpp`'s rebuild callback and `VolumeSessionManager`'s snapshot-ready
callback) hands the exported snapshot map to `UiServer::MergeIndexDirectories`
as a prvalue or `std::move`, so the product path transfers ownership. The
benchmark passed an lvalue, so it deep-copied 200,000
`SnapshotDirectoryEntry` objects and their per-entry `std::wstring` names on
every repetition, adding roughly 14 ms of measured latency that does not exist
in the product.

The result is a benchmark that overstates first-generation latency by 11-14%
and carries a 39% run-to-run noise spread, which makes any smaller real
optimization unmeasurable. Separately, the repository repeatedly re-declares
itself current right after an evidence commit changes HEAD, because
certification is bound to a single tree SHA with no way to express that an
evidence-only commit does not alter executable inputs.

## What Changes

- Fix `ffengine_bench_first_generation_publish` to pass the exported snapshot
  map by move, matching every production call site. Product source, wire
  format, published directory count, published file count, and generation ids
  are all unchanged.
- Add an idle-gated, core-pinned A/B measurement protocol (idle host gate,
  single-core affinity, high priority, interleaved randomized order,
  same-session baseline and candidate) so a change can be told apart from
  machine noise. Establish the noise floor of the current host with a
  same-binary control run.
- Establish a fresh exact-head baseline at the campaign-start SHA before any
  product claim.
- Add a mechanical source-equivalence model that distinguishes
  `implementation_sha`, `certified_binary_sha`, `evidence_sha`, and
  `repository_tip_sha`, and proves that the executable-impacting tree is
  unchanged between them, so a later evidence-only commit no longer
  invalidates certification.
- Add deterministic CTest-safe performance invariants (no duplicate
  serialization, single retained generation, byte-identical wire format) and
  keep noisy wall-clock benchmarks outside ordinary CTest with provenance.

## Capabilities

### New Capabilities

- `measured-performance-and-exact-certification`: establish a fresh exact-head
  baseline, measure a candidate only when it can be separated from host noise,
  retain only a change whose targeted metric improves without unacceptable
  regressions, and express certification as a proved tree-equivalence relation
  rather than a single SHA.

### Modified Capabilities

- None. No existing product requirement is changed. The benchmark repair is a
  measurement-fidelity fix and changes no product behaviour.

## Impact

- `src/` and `cmake/` are untouched by the retained change; only the
  benchmark harness under `tests/` changes.
- The certification model is additive: existing provenance semantics are not
  weakened, and no evidence commit may claim its own tip was rebuilt unless it
  truly was.
- Hosted CI is checked once for the final candidate and classified, never
  treated as local certification.
- Product performance work is reported honestly: the retained change is a
  measurement-fidelity repair, and the highest-value remaining candidate
  (a small-string-optimized `SnapshotDirectoryEntry::name`) is recorded as
  measured-but-unimplemented with the reason it was not retained.
