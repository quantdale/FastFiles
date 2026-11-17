## ADDED Requirements

### Requirement: Generated-file exemption is bound to a configured binary root

The executable-tree guard MUST support internal and external configured CMake builds without classifying arbitrary missing or external project inputs as generated. Existing tracked inputs in either compared commit MUST remain content-hashed regardless of build location.

#### Scenario: External configured build
- **WHEN** the actual compile database includes existing generated RC/header files under a binary root with matching unambiguous source/cache-directory metadata
- **THEN** a documentation-only comparison MUST prove coverage and source equivalence with the same digest as the corresponding internal build

#### Scenario: Unproven or missing input
- **WHEN** cache provenance is missing, foreign or ambiguous, a required generated file does not exist, or an untracked TU lies outside the proven root
- **THEN** the guard MUST fail closed rather than drop the input and assert equivalence

### Requirement: Relative compilation paths follow the recorded working directory

The guard MUST resolve relative directory entries against the compile-database location and relative source/include paths against the recorded compilation directory.

#### Scenario: Relative TU and project include
- **WHEN** a valid compile database uses relative source and include paths from a separate binary directory
- **THEN** the source/header closure MUST be equivalent to the absolute-path form

### Requirement: Content and dependency changes still invalidate certification

The guard MUST retain recursive header, RC resource, generated-template and symmetric product-addition coverage with content-sensitive digests.

#### Scenario: Nested, resource or generated-recipe input changes
- **WHEN** either compared commit changes a nested project header, embedded manifest, generator template or tracked input inside a binary root
- **THEN** the guard MUST reject equivalence and identify the changed input
