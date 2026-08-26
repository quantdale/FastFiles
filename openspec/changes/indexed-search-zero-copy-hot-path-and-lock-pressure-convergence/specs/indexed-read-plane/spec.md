## Purpose

Tightens the indexed-read-plane contract so resident projection search can avoid unnecessary owned candidate/name materialization without weakening bounded nonblocking access or generation safety.

## MODIFIED Requirements

### Requirement: Indexed Read Work Does Not Hold the Mutation Critical Section

Indexed search SHALL perform long-running enumeration, matching, path reconstruction, sorting, and response preparation outside the engine's mutation/lifecycle critical section. Any live projection state held by the query SHALL be captured in bounded lifetime-safe form, and the critical section SHALL be reacquired only for bounded capture or generation validation. A borrowed pointer or string view SHALL NOT survive lock release unless an explicit owner/lease guarantees the referenced storage remains alive and immutable for that use.

#### Scenario: View-compatible hot row is rejected

- **WHEN** a resident projection row can be rejected by view-compatible name/metadata predicates
- **THEN** the engine SHALL be able to reject it without first constructing the full owned result candidate, subject to the selected bounded lifetime-safe capture design

#### Scenario: Interned-name storage mutates

- **WHEN** indexing, compaction, eviction, reload, or rebuild can move or replace interned-name storage while a search is active
- **THEN** no search SHALL dereference a stale borrowed name; it SHALL use storage whose lifetime is explicitly protected, restart from a new coherent context, fall back durably, cancel, or fail closed

#### Scenario: Broad search overlaps ingestion

- **WHEN** a broad indexed search is deliberately held in matching or sorting
- **THEN** a real MFT or USN ingestion batch SHALL commit and apply without waiting for the search to finish

### Requirement: Indexed Read Memory and Work Fan-Out Are Bounded

The read plane SHALL use a fixed maximum number of active query executions, a fixed maximum queued work count, a fixed hot-row capture chunk bound, and the existing indexed result cap. Any name-storage generation, slab, lease, compact capture buffer, or equivalent mechanism retained for search SHALL have bounded lifetime and memory accounting. The design SHALL NOT create an owning O(N) candidate/name snapshot or pin an unbounded historical projection.

#### Scenario: Large resident query rejects most rows

- **WHEN** a view-compatible query scans a large resident index and only a small fraction of rows survive the cheap predicates
- **THEN** transient owned candidate/name materialization SHALL scale primarily with surviving/materialized rows rather than requiring one owned candidate/name copy for every rejected row on the optimized path

#### Scenario: Search overlaps projection compaction

- **WHEN** projection memory pressure requires compaction or eviction while a search holds any lifetime-safe read storage
- **THEN** memory remains bounded, the mutation path makes progress, and retired storage is reclaimed after its bounded readers release it

### Requirement: Read-Plane Diagnostics Are Bounded and Testable

The engine SHALL expose structured diagnostics sufficient to distinguish rows captured lightly, rows rejected before owned materialization, owned candidate/name materializations, view-path fallback, state-lock wait/hold observations, and any retained read-storage lifetime/memory where applicable. Existing generation, cancellation, hot/cold, restart, and durable-fallback diagnostics remain authoritative. Diagnostics SHALL be bounded and SHALL NOT include arbitrary personal filenames or paths.

#### Scenario: Optimization benchmark reports materialization

- **WHEN** the deterministic large-index benchmark executes an eligible resident query
- **THEN** diagnostics SHALL show how many rows were examined, rejected before ownership, materialized as owned candidates/names, how long bounded capture and matching took, and the observed state-lock pressure

## ADDED Requirements

### Requirement: Optimized and Canonical Search Semantics Are Equivalent

Any optimized resident projection path SHALL preserve the canonical query parser, predicate semantics, scope rules, result classification, total ordering, result cap/truncation behavior, cancellation, and cold/durable fallback behavior. Unsupported or non-view-compatible queries SHALL use an explicit canonical fallback rather than approximate semantics.

#### Scenario: Optimized path and reference path evaluate the same corpus

- **WHEN** a deterministic corpus covering ASCII, Unicode, filters, scopes, sorts, selectivity, hot/cold mixes, and generation churn is executed against both paths
- **THEN** completed responses SHALL contain the same candidates in the same order with the same truncation/status semantics

#### Scenario: Optimization cannot establish safe lifetime

- **WHEN** the engine cannot establish a valid bounded lifetime for borrowed resident data
- **THEN** it SHALL fall back to a safe owned/durable path or report the existing fail-closed status; it SHALL NOT use a dangling view or relax generation validation
