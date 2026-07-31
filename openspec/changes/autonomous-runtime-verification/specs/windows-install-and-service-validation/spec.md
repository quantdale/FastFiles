## ADDED Requirements

### Requirement: Installer Lifecycle Automation
The harness SHALL drive the FastFiles installer through install, upgrade (over an existing installation), repair, and uninstall, each non-interactively, and SHALL capture the installer's own logs into the run's diagnostics.

#### Scenario: Fresh install and clean uninstall
- **WHEN** the harness installs FastFiles on a host with no prior installation and then uninstalls
- **THEN** both operations SHALL complete non-interactively and the installer logs SHALL be captured into the run diagnostics

#### Scenario: Upgrade over an existing installation
- **WHEN** an installation already exists and the harness runs the upgrade path
- **THEN** the upgrade SHALL complete and the result SHALL record that an upgrade (not a fresh install) was performed

#### Scenario: Repair restores a tampered installation
- **WHEN** an installed file or setting is altered and the harness runs the repair path
- **THEN** the repair SHALL restore the expected installed state

### Requirement: Post-Install Integrity Verification
After an install, upgrade, or repair, the harness SHALL verify installation integrity against expectations: installed files present, install-directory ACLs correct, registry entries present, scheduled tasks registered, and services installed.

#### Scenario: Install-directory ACLs match the security model
- **WHEN** the harness verifies a completed installation
- **THEN** it SHALL confirm the install directory grants write only to the intended principals (e.g. Administrators/TrustedInstaller) and SHALL flag any deviation

#### Scenario: Registry, scheduled task, and service presence are verified
- **WHEN** integrity verification runs
- **THEN** the harness SHALL confirm the expected registry entries, the per-user scheduled task, and the `FastFilesIndexSvc` service are present and correctly configured, reporting any missing or misconfigured item

### Requirement: Teardown Always Restores A Clean Host
After any Tier-1 run that installed or mutated system state, the harness SHALL run an idempotent teardown that uninstalls FastFiles and removes accounts, tasks, and ACL changes it created, even when the run failed.

#### Scenario: Failed run still tears down
- **WHEN** a Tier-1 run fails partway through
- **THEN** the harness SHALL still execute teardown so the host is left without a leftover installation, service, task, or created account

### Requirement: Service Registration And SCM Configuration Validation
The harness SHALL verify that `FastFilesIndexSvc` is registered with the intended SCM configuration: start type, service account (including virtual service account), delayed-start setting, and the SCM security descriptor that denies the client group start/stop/reconfigure rights.

#### Scenario: Service account and start type are as specified
- **WHEN** the service is installed
- **THEN** the harness SHALL confirm its SCM start type and service account match the design (e.g. virtual service account) and SHALL flag any mismatch

#### Scenario: Client group cannot control the service
- **WHEN** the harness attempts service start/stop/reconfigure as a member of the authorized client group
- **THEN** the attempt SHALL be denied by the SCM security descriptor, and the harness SHALL record that denial as the expected pass condition

### Requirement: Service Lifecycle Validation
The harness SHALL validate service startup, shutdown, restart, delayed start, and configured recovery actions via SCM, confirming the service reaches the expected state at each transition.

#### Scenario: Start, stop, and restart reach expected states
- **WHEN** the harness starts, stops, and restarts the service through SCM
- **THEN** the service SHALL reach the running and stopped states as expected at each step, within a bounded timeout, and the outcomes SHALL be recorded

#### Scenario: Recovery action fires on unexpected termination
- **WHEN** the service process is terminated unexpectedly and a recovery action is configured
- **THEN** the harness SHALL observe the configured recovery behavior (e.g. restart) and record whether it occurred

### Requirement: Service Logging And Event Viewer Validation
The harness SHALL confirm the service emits its expected service logs and Event Viewer entries for key lifecycle events, and SHALL collect those entries into the run diagnostics.

#### Scenario: Lifecycle events are logged
- **WHEN** the service starts and stops
- **THEN** the harness SHALL find the corresponding service-log and Event Viewer entries and SHALL capture them into the run diagnostics
