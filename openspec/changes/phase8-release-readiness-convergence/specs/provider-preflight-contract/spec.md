## Purpose

Make environment-provider validation deterministic and plug-in-ready while requiring honest evidence before any real disposable Windows guest is treated as usable.

## ADDED Requirements

### Requirement: Deterministic provider preflight

Every provider SHALL expose a validated descriptor and a preflight result with stable reason codes for unavailable features, missing or unusable images, unsupported VM generation, missing checkpoints, guest boot/reachability failures, absent credentials/toolchains, resource shortages, unsupported restore, log collection failure, and incomplete cleanup.

#### Scenario: An empty VHDX is not a guest

- **WHEN** a provider is given a VHD/VHDX without a detectable Windows installation
- **THEN** preflight returns `REQUIRED-BUT-UNAVAILABLE` with `image-no-usable-windows-installation` and the provider refuses activation

#### Scenario: A valid user-supplied source is accepted defensively

- **WHEN** an operator supplies an existing ISO, VHDX, or template path at runtime
- **THEN** the provider validates confinement/metadata and records the source identity without storing credentials or changing source files

### Requirement: Provider lifecycle contract

The provider orchestrator SHALL execute provision, activate, capability execution, log collection, snapshot restore when declared, and idempotent cleanup; every failure, timeout, cancellation, and repeated cleanup SHALL produce structured evidence.

#### Scenario: Failure still cleans up

- **WHEN** provisioning, activation, capability execution, log collection, or restore fails
- **THEN** cleanup runs at most once per owned resource, repeated cleanup is safe, and the run records the original failure plus cleanup outcome

#### Scenario: Fake provider proves orchestration only

- **WHEN** the contract suite runs against a fake provider
- **THEN** success and each failure mode pass the lifecycle assertions without being counted as evidence from a real Windows guest

### Requirement: Reproducible guest bootstrap plan

The provider SHALL be able to generate disposable bootstrap/unattended artifacts from explicit inputs while keeping secrets runtime-injected and clearly marking generated artifacts as not executed until guest evidence exists.

#### Scenario: Bootstrap output contains no secret material

- **WHEN** a bootstrap plan is generated
- **THEN** output is reproducible for the same non-secret inputs, contains placeholders for credentials, and is safe to delete after the run
