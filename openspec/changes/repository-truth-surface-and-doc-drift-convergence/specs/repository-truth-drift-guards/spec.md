# repository-truth-drift-guards — spec delta

## ADDED Requirements

### Requirement: Current-facing guidance is semantically checkable

The system SHALL validate machine-derived claims in declared current-facing
guidance surfaces against their authoritative executable sources (verifier
command definitions, CMake preset/target declarations, lifecycle manifest,
snapshot publication source seam), deterministically and offline.

#### Scenario: Verifier verb drift is detected

- **WHEN** a current-facing doc enumerates the verifier verb set and omits a
  supported verb or advertises an unsupported one
- **THEN** the drift gate fails with a reason code identifying the authority,
  the surface, and the missing/extra verb, and exits non-zero through CTest

#### Scenario: Stale current-campaign claim is detected

- **WHEN** a current-facing surface asserts a named change/campaign is current,
  active, or current open work while `openspec/lifecycle.json` records that
  change as completed, superseded, external-evidence-only, or unknown
- **THEN** the drift gate fails with a stale-current-claim diagnostic naming
  both the claim and the manifest state

#### Scenario: Snapshot mechanics stay anchored to source

- **WHEN** current-facing guidance describes removed snapshot slot-flip /
  double-buffering mechanics while the publisher source seam proves
  generation-committed immutable mappings
- **THEN** the drift gate fails until the guidance matches the source-derived
  mechanism

### Requirement: Fences fail closed

Machine-checkable claims are enclosed in paired truth-fence markers. An
unclosed, duplicated, or unknown-contract fence, or an unreadable authority
input, SHALL fail the gate with a reason code rather than pass vacuously.

#### Scenario: Orphaned fence after contract rename

- **WHEN** a surface contains a fence whose contract id no longer exists, or a
  fence is left unclosed after an edit
- **THEN** the gate reports `FENCE_UNKNOWN_CONTRACT` / `FENCE_UNCLOSED` for
  that surface and fails

### Requirement: Historical evidence is not rejected

The guard SHALL scope semantic checks to declared current-facing surfaces.
Historical campaign evidence may contain superseded wording without failing.

#### Scenario: Historical wording preserved

- **WHEN** a file outside the current-facing surface set (for example archived
  campaign evidence) contains wording that would be stale as a current claim
- **THEN** the gate passes it untouched

### Requirement: Adversarial fixtures prove both directions

Deterministic fixtures SHALL prove each contract fires on its negative case
(missing/extra verbs, stale claims, stale mechanics, malformed fences,
unreadable authority) and stays silent on positive and historical cases.

#### Scenario: False-positive discipline

- **WHEN** the fixture suite runs a historical document containing legacy
  wording and a correctly reconciled surface set
- **THEN** all checks pass with deterministic ordering and no absolute host
  paths in diagnostics
