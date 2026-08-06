# raw-volume-privilege-model Specification

## Purpose
The evidence-driven decision contract for raw volume access: least-privilege selection backed by a reproducible matrix, real-service access validation, transactional grant ordering, a constrained fallback broker, signed deployment with rollback, and closing evidence.

## Requirements
### Requirement: Evidence-driven least-privilege selection

The deployed raw-volume access model SHALL be selected from documented
Windows privilege or service-identity candidates using a reproducible matrix;
it MUST NOT rely on Administrators-group membership or LocalSystem when a
narrower candidate passes the required operations.

#### Scenario: Candidate matrix captures the real token

- **WHEN** a candidate service configuration is evaluated on a clean supported
  Windows host
- **THEN** the evidence SHALL include the service account SID, held/enabled
  privilege state, raw-volume open result, USN control-code results, and exact
  Win32 error codes

#### Scenario: Broad control does not silently become production configuration

- **WHEN** LocalSystem or an Administrators-group service is used as a
  diagnostic control
- **THEN** it SHALL be labeled as a control/last-resort result and SHALL NOT be
  installed as the selected production model without an explicit security
  decision

### Requirement: Real installed service can access the scan volume

The final installed service identity SHALL successfully open each configured
scan volume and perform the required USN journal control operations without
falling back to an unbounded administrator identity.

#### Scenario: Initial raw-volume scan succeeds

- **WHEN** the signed installed service receives a real `StartVolumeScan`
  request for a configured NTFS volume
- **THEN** the raw `CreateFileW` open SHALL succeed and the scan SHALL publish
  its normal completion/batch behavior

#### Scenario: Journal operations succeed under the same identity

- **WHEN** the service opens and reads the USN journal for that volume
- **THEN** `FSCTL_QUERY_USN_JOURNAL` and the required read operation SHALL
  succeed or return a documented non-privilege condition such as an inactive
  journal, with the exact code persisted in evidence

### Requirement: Grant and startup ordering are transactional

Service registration SHALL apply the selected account rights and security
descriptor before starting the service, and the service SHALL start with a
fresh token that is verified in its own startup/scan path.

#### Scenario: Grant failure prevents an invalid service start

- **WHEN** account-right assignment or service security configuration fails
- **THEN** installation SHALL roll back the partial registration and SHALL NOT
  report a usable indexing service

#### Scenario: Restart uses the newly granted token

- **WHEN** a right is added or the service identity changes
- **THEN** the service SHALL be restarted before verification and the diagnostic
  record SHALL describe the token used by the subsequent raw-volume call

### Requirement: Any fallback broker has a constrained command surface

If no documented narrow service identity can perform the required operations,
any privileged broker introduced by the selected design SHALL accept only
authenticated volume enumeration, raw-volume scan/journal, cancellation, and
structured-status commands.

#### Scenario: Unauthorized client cannot use the broker

- **WHEN** a client without the existing signed-image and authorization-group
  checks connects
- **THEN** the broker SHALL reject the connection before executing a privileged
  operation

#### Scenario: Broker does not expose arbitrary privilege

- **WHEN** a valid client sends a command outside the documented volume and
  journal surface
- **THEN** the broker SHALL reject it and SHALL NOT expose arbitrary process,
  file-mutation, or unrestricted-handle operations

### Requirement: Signed, reversible deployment

The final privilege model SHALL be deployed only with signed service and
Engine artifacts whose mutual-authentication pins are configured, and the
installer SHALL provide an atomic rollback path for service binaries,
configuration, and rights.

#### Scenario: Unsigned or unpinned artifacts are rejected

- **WHEN** installation sees an unsigned peer or placeholder thumbprint
- **THEN** it SHALL fail closed before enabling the production privileged path

#### Scenario: Failed upgrade restores the prior service

- **WHEN** post-install startup or real-service verification fails
- **THEN** the installer SHALL restore the previous binary/configuration and
  leave the service in its prior known-good state

### Requirement: Final evidence closes the privilege decision

The change SHALL retain a signed-installation evidence artifact containing the
final account identity, privilege state, raw-volume outcome, USN outcomes,
exact error codes, selected model, and rollback status before upstream scan and
architecture tasks are marked complete.

#### Scenario: Verified candidate produces a handoff artifact

- **WHEN** the final signed service passes real volume and journal validation
- **THEN** the evidence SHALL identify the selected model and support updating
  the owning foundation and scanning tasks

#### Scenario: No candidate passes

- **WHEN** every documented narrow candidate still returns an access denial
- **THEN** the evidence SHALL keep the upstream tasks open and identify the
  constrained-broker/security decision required next
