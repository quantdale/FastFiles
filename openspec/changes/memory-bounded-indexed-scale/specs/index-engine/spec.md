# Memory-Bounded Indexed Scale

## index-engine Delta

## MODIFIED Requirements

### Requirement: Filesystem Snapshot Publication

Snapshot publication SHALL remain immutable, generation-named, version-checked
(fail-closed), and bounded by the explicit 256 MiB payload cap. In addition,
bulk indexed publication (startup rebuild, scan completion) SHALL export a
bounded top-of-tree working set of directories rather than the complete volume
map, and the engine's retained indexed-directory map SHALL be capped by a
recency-ordered working set. A cold indexed directory SHALL transparently
re-materialize through the existing request/watch path; deleted or renamed
paths SHALL NOT survive as stale keys; offline volumes SHALL remain honestly
absent; and degraded enumeration precedence SHALL be unchanged.

#### Scenario: Bulk publication is bounded by the working set

- **Given** an indexed volume with far more directories than the publication working-set bound
- **When** startup rebuild or scan-complete publishes the volume
- **Then** the exported directory count is bounded by the configured limit, top-of-tree-first in deterministic path order
- **And** the published payload size scales with the working set, not the total tree

#### Scenario: Retained indexed directories are capped

- **Given** more distinct indexed directories merged than the retention cap
- **When** a new generation is published
- **Then** retained indexed entries never exceed the cap
- **And** least-recently-observed indexed entries are dropped first while actively browsed (degraded) directories are always retained

#### Scenario: Cold indexed directory re-materializes

- **Given** a directory not present in the current published snapshot whose volume is online
- **When** a UI requests it through the normal control pipe
- **Then** the existing degraded request path enumerates, watches, publishes a new immutable generation, and announces it

#### Scenario: Deleted paths still cannot linger

- **Given** a previously published indexed directory that was deleted on disk
- **When** a new complete per-volume indexed export merges
- **Then** the stale key is removed exactly as before the working-set cap existed

## ADDED Requirements

### Requirement: Indexed Search Is Complete Under Eviction

Indexed search SHALL return results whose match identities, ordering,
tie-breaking, truncation flag, and result cap are identical whether the
matching rows are projection-resident or evicted (cold). Cold volumes SHALL be
enumerated by streaming the durable store per volume; a searchable set
containing any cold volume SHALL use canonical matching over both sources, and
the filename-trigram accelerator SHALL be used only when every searched volume
is resident. A failed cold store stream SHALL fail the query closed
(`Unavailable`), never return silent partials.

#### Scenario: Hot/cold equivalence across query classes

- **Given** the same durable index queried twice — once fully resident, once with forced eviction under a small budget
- **When** selective, no-match, broad/capped, extension, structured-filter, Unicode, path-scoped, and incremental-typing queries run
- **Then** candidate identity lists and ordering are identical between the two runs for every query class

#### Scenario: Cancellation during cold work

- **Given** a search enumerating cold store rows
- **When** cancellation is signalled mid-stream
- **Then** streaming stops promptly and the response reports `Cancelled`

#### Scenario: Repeated cold searches do not thrash memory

- **Given** repeated alternating queries after eviction
- **When** each search completes
- **Then** the projection's estimated memory stays within its budget and no reload-then-evict cycle occurs because of searching

### Requirement: Seeded Indexed-Mode Acceptance Path

A deterministic, test-only integration suite SHALL populate the real durable
store and index pipeline with synthetic MFT/USN-equivalent records and verify
population, restart-from-SQLite, indexed navigation data, indexed and
trigram-accelerated search, canonical fallback queries, folder aggregates,
allocated-size propagation, rename/move/delete updates, reconciliation
deletion, unavailable-volume behavior, cold/evicted data, cancellation, and
snapshot serialize→publish→parse round-trips — without disabling Authenticode
verification, adding an unsigned production mode, or fabricating production
MFT/USN evidence.

#### Scenario: Seeded end-to-end pipeline acceptance

- **Given** the seeded suite runs against a fresh disposable database
- **When** the full population → mutation → eviction → restart sequence executes
- **Then** every stage's observable state matches the deterministic expectation and the final state equals the reference model
