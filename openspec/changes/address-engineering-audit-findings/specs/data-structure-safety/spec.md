## ADDED Requirements

### Requirement: FlatHashMap grow trigger
FlatHashMap SHALL grow (rehash to a larger bucket array) when the load factor exceeds 75%. Insertion SHALL never enter an infinite loop.

#### Scenario: Insertion triggers grow
- **WHEN** the FlatHashMap load factor reaches 75% and a new key is inserted
- **THEN** the map rehashes to a larger size and the insertion succeeds

#### Scenario: Maximum probe distance
- **WHEN** a probe sequence reaches 64 steps without finding an empty slot
- **THEN** the map aborts the insertion with an error rather than looping indefinitely
