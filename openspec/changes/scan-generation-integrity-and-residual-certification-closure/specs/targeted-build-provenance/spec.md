## Purpose

Certification binaries must embed the source revision of the build that produced them, including when only that target is rebuilt, and an explicit identity override must stay fail-closed.

## ADDED Requirements

### Requirement: Targeted builds embed the revision they were built from

Every certification-relevant binary that embeds a source revision SHALL embed the revision current when that binary is produced. The minimum set is `FastFiles.exe`, `FastFilesEngine.exe`, `FastFilesIndexSvc.exe`, and `FastFilesSetup.exe`. Diagnostic and test binaries that embed a source revision and are built by the targeted local or hosted CI commands SHALL be checked separately from those four. A binary produced at revision A SHALL NOT still report A after a targeted build once the source identity is revision B. The check SHALL NOT pass because some other target already refreshed a shared artifact that this binary did not rebuild. An explicit identity override that is not 40 hexadecimal characters SHALL fail the build. A well-formed override SHALL be embedded as given and SHALL NOT be replaced with a different revision in order to pass. When the existing build edges already refresh every such binary, the campaign SHALL record that proof and SHALL NOT change the build definition.

#### Scenario: A targeted rebuild picks up revision B

- **WHEN** the certification binaries are built at revision A, the source identity advances to revision B, and the targeted build commands used by local and hosted CI rebuild each binary under test
- **THEN** each rebuilt binary SHALL embed revision B
- **AND** SHALL NOT embed revision A

#### Scenario: A stale artifact cannot satisfy the check

- **WHEN** the proof inspects a binary that was not relinked after the identity advanced and instead reads an output another target already refreshed
- **THEN** the check SHALL fail
- **AND** SHALL NOT report that binary as fresh

#### Scenario: A malformed override fails closed

- **WHEN** the build is given an identity override that is empty or not 40 hexadecimal characters
- **THEN** the build SHALL fail
- **AND** SHALL NOT write a fallback identity and continue

#### Scenario: A well-formed override is kept

- **WHEN** the build is given a 40-character hexadecimal identity override
- **THEN** the embedded identity SHALL be that override
- **AND** the build SHALL NOT rewrite it to the checkout revision to force a match

#### Scenario: No code change when the current edges are already sufficient

- **WHEN** the revision A then revision B targeted sequence shows every certification-relevant binary embedding B without a build-file edit
- **THEN** the campaign SHALL record that result as a no-code closure
- **AND** SHALL NOT modify the identity target or its dependency edges
