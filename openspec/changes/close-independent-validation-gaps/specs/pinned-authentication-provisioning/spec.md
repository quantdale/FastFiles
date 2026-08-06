## ADDED Requirements

### Requirement: Pinned Signature Provisioning Keeps Private Material Out of Source Control
The system SHALL provision real engine and service Authenticode leaf thumbprints for mutual authentication only through a controlled release process, and SHALL NOT commit private certificate material, signing keys, or the signing certificate's private key to source control. The production pins SHALL be populated from the actual signing certificate at release time and SHALL be applied to the pinned-signature source only through a documented, reviewable step.

#### Scenario: Real pins are provisioned at release, not committed by developers
- **WHEN** a controlled release build is performed with a provisioned signing certificate
- **THEN** the engine and service Authenticode leaf thumbprints SHALL be derived from that certificate and applied as the pinned values, with the private key material and certificate kept out of source control, and the applied pins SHALL match the certificate that actually signs the shipped binaries

#### Scenario: No private material is present in the repository
- **WHEN** a repository-wide search is performed for private certificate material or signing keys
- **THEN** no private key, password, or certificate private material SHALL be present in source control

### Requirement: Fail-Closed When Pins Are Absent or Placeholder
When the configured pins are all-zero placeholders or otherwise not configured, mutual authentication SHALL fail closed: the system SHALL reject every peer rather than accept unsigned or unexpectedly-signed binaries, and SHALL NOT weaken this behavior merely to make the privileged connection become active.

#### Scenario: Placeholder pins reject every peer
- **WHEN** the configured pins are placeholders (for example, all-zero) and a peer attempts to authenticate
- **THEN** the system SHALL reject that peer (fail closed), and the privileged connection SHALL NOT be activated

#### Scenario: Partially-populated pins reject the unpinned peer
- **WHEN** one configured pin (for example, the engine thumbprint) is populated from a real certificate and the other (the service thumbprint) remains a placeholder, and the unpinned peer attempts to authenticate
- **THEN** the system SHALL reject that unpinned peer (fail closed for the placeholder side), and SHALL NOT activate the privileged connection even if the other side's pin is valid

#### Scenario: Compile-time diagnostic escape hatch never weakens shipped behavior
- **WHEN** a shipped binary is built
- **THEN** the `FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED` diagnostic escape hatch SHALL be disabled, so mutual authentication continues to fail closed

### Requirement: Constant-Time Thumbprint Comparison
The system SHALL compare fixed-length Authenticode thumbprints in constant time, so that the comparison's execution time does not reveal the number of matching leading bytes to a local timing side channel.

#### Scenario: Thumbprint comparison is non-branching on sensitive bytes
- **WHEN** the system compares an actual signer thumbprint against a pinned expected thumbprint
- **THEN** the comparison SHALL use a constant-time algorithm that produces the same execution profile regardless of how many leading bytes match

### Requirement: Rejection of Unmatched Or Invalid Signer Thumbprints
The system SHALL reject any binary whose Authenticode signature is absent, whose signer thumbprint does not match the pinned value, or which is unsigned, expired, untrusted, or otherwise fails signature verification, so that only a binary signed by the pinned certificate is accepted as a peer.

#### Scenario: Unsigned binary rejected
- **WHEN** an unsigned binary attempts to authenticate as a peer
- **THEN** the system SHALL reject it and SHALL NOT activate the privileged connection

#### Scenario: Mismatched, expired, or untrusted signature rejected
- **WHEN** a binary's signer thumbprint does not match the pinned thumbprint, or its signature is expired or untrusted
- **THEN** the system SHALL reject the binary rather than accept it

### Requirement: Certificates Rotate Without Opening the Boundary
When a signing certificate is rotated, the system SHALL update the pinned thumbprints through the controlled release process and SHALL ensure that a binary signed by the new certificate is the only one accepted after rotation, with stale-self-detection continuing to reject binaries that drift from the loaded/current identity.

#### Scenario: Rotation updates accepted signer only
- **WHEN** the signing certificate is rotated and the pinned thumbprints are updated to the new certificate
- **THEN** binaries signed by the previous certificate SHALL be rejected, and only binaries signed by the new pinned certificate SHALL be accepted

### Requirement: Privileged-Path Release-Gate Evidence
Privileged-path release readiness SHALL require documented evidence for: signed install validation; service startup and connection-state validation (including the active-transition and degraded fallback); end-to-end privileged scanning validation; negative authentication tests; `fftest` privilege-diagnostics coverage; protocol fuzz-test regression coverage; and a clean-host privilege-candidate matrix across the documented supported Windows environments. A release machine SHALL NOT be marked present or passed without this evidence.

#### Scenario: Negative authentication yields required evidence
- **WHEN** a release readiness review requires negative-authentication evidence
- **THEN** the evidence SHALL show that unsigned, mismatched, placeholder-pin, expired, and untrusted peers are all rejected, recorded under the repository's evidence-retention path

#### Scenario: Signed install validation yields required evidence
- **WHEN** a release readiness review requires signed-install evidence
- **THEN** the evidence SHALL show that a service binary signed by the pinned certificate installs under the authorized elevated host, with `ffsetup` registration (group, service, ACL, task) succeeding and an Authenticode-verified binary on disk, recorded under the repository's evidence-retention path

#### Scenario: Service-startup and connection-state evidence covers the active transition
- **WHEN** a release readiness review requires service-startup and connection-state evidence
- **THEN** the evidence SHALL show the privileged connection transitioning from a non-active state to `Active` against the running signed service, AND SHALL show the system falling back to the degraded path (with no privileged scan) when the signed service is unavailable or the handshake fails, so the active-transition and the degraded-fallback are both exercised and recorded under the repository's evidence-retention path

#### Scenario: End-to-end privileged scanning validation yields required evidence
- **WHEN** a release readiness review requires end-to-end privileged-scanning evidence
- **THEN** the evidence SHALL show a real MFT/USN enumeration against a documented supported volume completing through the privileged path with byte-accurate record counts, recorded under the repository's evidence-retention path

#### Scenario: fftest privilege-diagnostics coverage yields required evidence
- **WHEN** a release readiness review requires `fftest` privilege-diagnostics evidence
- **THEN** the evidence SHALL show the `fftest` probe binary running against the deployed signed service and reporting the expected privilege-set / raw-volume-access outcomes, recorded under the repository's evidence-retention path

#### Scenario: Protocol fuzz-test regression coverage yields required evidence
- **WHEN** a release readiness review requires protocol fuzz-test evidence
- **THEN** the evidence SHALL show the `ffprotocol_fuzz_tests` (and any sibling fuzz suites) passing in the regression run for the candidate release build, recorded under the repository's evidence-retention path

#### Scenario: Clean-host matrix is environment-dependent and not assumed complete
- **WHEN** a release readiness review lists clean-host matrix coverage for the documented supported Windows environments
- **THEN** the matrix SHALL be recorded as pending (or marked external / `REQUIRED-BUT-UNAVAILABLE` with machine evidence) until it is actually executed on those hosts, and SHALL NOT be marked complete on the strength of a single development host alone
