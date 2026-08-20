# allocated-size-search-filter — search-query-engine delta

## MODIFIED Requirements

### Requirement: Structured Filter Parsing
The query engine SHALL recognize `key:value` tokens for the registered filter keys `ext`, `size`, `ondisk`, `modified`, `kind`, `name`, and `folder`. `ondisk` is the canonical Size-on-Disk/allocated-size numeric filter key; no `allocated` alias is introduced by this change.

#### Scenario: Size on Disk filter is recognized
- **WHEN** a user enters `ondisk:>10MB`
- **THEN** the parser SHALL recognize `ondisk` as a registered structured filter and SHALL NOT report it as an unrecognized key

#### Scenario: Invalid Size on Disk value is reported as invalid
- **WHEN** a user enters `ondisk:banana`
- **THEN** the parser SHALL report the filter value as invalid through the existing invalid-filter mechanism rather than treating `ondisk` as an unknown key or matching the token literally

### Requirement: Comparison and Range Operators for Numeric and Date Filters
The `ondisk:` filter SHALL support the same binary-size comparison grammar as `size:`: `>`, `<`, `>=`, `<=`, `=`, exact values, and inclusive `..` ranges with `B`, `KB`, `MB`, and `GB` units. `size:` continues to compare logical `sizeBytes`; `ondisk:` compares allocated bytes only when that value is known.

#### Scenario: Greater-than Size on Disk comparison
- **WHEN** a candidate has `allocatedSizeKnown=true` and `allocatedSizeBytes=20 MiB`, and the query is `ondisk:>10MB`
- **THEN** the candidate SHALL satisfy the filter

#### Scenario: Inclusive Size on Disk range
- **WHEN** a candidate has a known allocated size exactly equal to either boundary of `ondisk:10MB..100MB`
- **THEN** the candidate SHALL satisfy the filter

#### Scenario: Known zero is a real allocated-size value
- **WHEN** a candidate has `allocatedSizeKnown=true` and `allocatedSizeBytes=0`, and the query is `ondisk:0B`
- **THEN** the candidate SHALL satisfy the filter

## ADDED Requirements

### Requirement: Numeric Size on Disk filters require known allocation
A numeric `ondisk:` predicate SHALL match only candidates whose allocated size is explicitly known. UNKNOWN allocated size SHALL fail every numeric `ondisk:` comparison or range predicate, regardless of logical size or any stray byte value stored alongside a false knownness flag.

#### Scenario: Unknown does not match zero
- **WHEN** a candidate has `allocatedSizeKnown=false` and the query is `ondisk:0B`
- **THEN** the candidate SHALL NOT match

#### Scenario: Unknown does not match a broad nonnegative comparison
- **WHEN** a candidate has `allocatedSizeKnown=false` and the query is `ondisk:>=0B`
- **THEN** the candidate SHALL NOT match

#### Scenario: Unknown never falls back to logical size
- **WHEN** a candidate has logical size 50 MiB, allocated size unknown, and the query is `ondisk:>10MB`
- **THEN** the candidate SHALL NOT match based on its logical size

### Requirement: Logical and allocated-size filters remain independent dimensions
`size:` SHALL continue to evaluate logical `sizeBytes` exactly as before. `ondisk:` SHALL evaluate the explicit allocated-size fields. Adding `ondisk:` SHALL NOT alter existing `size:` parsing, matching, or sorting semantics.

#### Scenario: Logical and allocated predicates select different rows
- **WHEN** two candidates deliberately have different logical-vs-allocated ordering/membership
- **THEN** a `size:` query and the analogous `ondisk:` query SHALL independently select rows according to their respective dimensions

### Requirement: Size on Disk filtering is projection-view compatible
`ondisk:` SHALL be evaluable against both `Candidate` and `CandidateView` using only the existing allocated-size scalar metadata. It SHALL NOT require folder/path reconstruction or a database/filesystem/service lookup.

#### Scenario: Accelerated projection path evaluates on-disk predicate
- **WHEN** indexed Search evaluates an `ondisk:` query through the projection-backed `CandidateView` path
- **THEN** membership SHALL be identical to the owning-candidate path and no full path SHALL be materialized solely because the `ondisk:` predicate exists

### Requirement: Size on Disk composes with existing implicit AND and scope behavior
`ondisk:` SHALL combine with free-text and other structured filters using the existing implicit-AND semantics and SHALL remain subject to the selected Search scope.

#### Scenario: Extension and Size on Disk both required
- **WHEN** a user enters `ext:iso ondisk:>1GB`
- **THEN** only candidates satisfying both the extension predicate and the known allocated-size predicate within the active scope SHALL match
