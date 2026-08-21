## Purpose

Provides a deterministic, auditable path from a clean FastFiles checkout to an explicitly classified release candidate without weakening production signing or presenting unsigned validation artifacts as production releases.

## ADDED Requirements

### Requirement: Release-candidate generation is deterministic and fail-closed
The release-candidate entry point SHALL validate repository cleanliness, canonical version state, supported architecture/configuration, required build and test gates, and packaging inputs before emitting artifacts; a production release request MUST stop when authorized signing material or pins are absent, malformed, placeholder, or mismatched.

#### Scenario: Unsigned local validation candidate
- **WHEN** a tester requests a local unsigned validation candidate without production signing inputs
- **THEN** the command emits only explicitly labeled development/test artifacts or reports a clear non-production result, and never labels them as a production release

#### Scenario: Production signing prerequisites are missing
- **WHEN** a production release is requested without an authorized certificate, private-key access, or non-placeholder engine/service pins
- **THEN** the command fails before publishing a production artifact and identifies the missing prerequisite without modifying trust stores or inventing credentials

### Requirement: Release artifacts carry coherent provenance and integrity metadata
Every distributable artifact emitted by the candidate pipeline SHALL have a stable filename, canonical product version, architecture, configuration, commit identity, signed/unsigned classification, and SHA-256 digest; the pipeline SHALL emit a machine-readable manifest and a checksum file covering the exact staged bytes.

#### Scenario: Manifest matches staged artifacts
- **WHEN** candidate staging completes
- **THEN** the manifest lists every artifact once, the checksum file hashes the same bytes, and a fresh verification of those files reproduces the recorded digests

#### Scenario: Version metadata disagrees
- **WHEN** `VERSION`, installer metadata, binary version resources, release filenames, or release notes disagree
- **THEN** candidate generation fails with the mismatched sources identified and does not publish a candidate manifest

### Requirement: Candidate packaging is reproducible from a clean checkout
The documented release procedure SHALL identify the exact command, inputs, prerequisites, staging layout, artifact names, and validation gates needed to produce a candidate from a clean checkout; generated evidence and secrets SHALL remain outside tracked source unless deliberately sanitized.

#### Scenario: Clean checkout produces a candidate
- **WHEN** a tester follows the documented release command with available non-secret prerequisites
- **THEN** the pipeline builds/tests the requested configuration, stages the installer and required binaries, emits manifest/checksum/provenance files, and records the command outcome

#### Scenario: Packaging prerequisite is unavailable
- **WHEN** an installer, certificate provider, Windows SDK, clean-host, or optional SBOM prerequisite is unavailable
- **THEN** the command reports the exact unavailable prerequisite, performs safe staging cleanup, and distinguishes skipped external validation from a failed product gate

### Requirement: Release documentation distinguishes readiness states
Release documentation SHALL separate unsigned development/test validation, release-candidate packaging, production signing, signed installation, privileged MFT/USN validation, and clean-host acceptance; release notes SHALL include version, features, important fixes, known limitations, system requirements, upgrade notes, and security-relevant changes.

#### Scenario: Reviewer inspects an unsigned candidate
- **WHEN** a reviewer opens candidate metadata or release notes for an unsigned build
- **THEN** the files state that the artifact is non-production, identify the commit and build configuration, and list the signing/privileged validation blockers

#### Scenario: Future tester prepares a clean host
- **WHEN** a tester follows the clean-host procedure
- **THEN** it states supported Windows versions/architectures, prerequisites, install/validation/cleanup steps, expected results, log collection, and which evidence remains external until executed
