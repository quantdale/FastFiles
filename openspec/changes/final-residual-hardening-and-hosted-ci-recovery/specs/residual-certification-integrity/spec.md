## Purpose

Defines the evidence required to certify the remaining transport failure boundary and the final executable candidate without overstating source provenance or hosted execution.

## ADDED Requirements

### Requirement: Partial-frame transport failures have deterministic proof

The certification fixture MUST transmit through the production frame serialization and transport path and inject failure at known byte boundaries. It MUST cover failure before the first byte, within the header, immediately after the header, within the payload, and immediately before frame completion, where those boundaries are representable by the transport. For every failed frame, certification MUST establish that the scan cannot be reported successful, the invalidated connection emits no later terminal, and the receiver cannot accept incomplete bytes as a valid frame. It MUST also establish bounded teardown, terminal-write failure behavior, behavioral cancellation and worker-exception outcomes, and a fresh connection's recovery without stale state or authentication bypass. A portable synthetic scan or verifier seam MUST be isolated from the production service build and MUST NOT be reported as live signed-peer or elevated MFT/USN validation. Repeated `ScanFailed` deliveries MUST have bounded retry rate and outstanding state, with disconnect clearing pending retry work.

#### Scenario: Partial header or payload is delivered

- **WHEN** the transport delivers only a selected prefix of a scan frame and the next write fails
- **THEN** the production receiver rejects the incomplete frame, the scan cannot complete successfully, and no terminal is appended to that connection

#### Scenario: Failure occurs at a frame boundary

- **WHEN** a write fails before the first byte, immediately after the header, or immediately before the final byte
- **THEN** the observed bytes and scan outcome match the selected boundary and the invalidated connection is torn down deterministically

#### Scenario: Terminal write fails and connection recovers

- **WHEN** a failure interrupts terminal delivery and a second connection reaches the authorization gate
- **THEN** the first connection yields no duplicate or successful terminal, the gate runs again and rejects disallowed peers, and an allowed test peer starts with fresh connection-scoped state

#### Scenario: Cancellation or worker exception interrupts active scan work

- **WHEN** cancellation occurs during active output or a controlled worker exception is raised
- **THEN** cancellation remains silent, an exception cannot yield a successful terminal, and owned workers and handles are released within the deadline

### Requirement: Additional provenance is adopted only for a demonstrated gap

The provenance review MUST distinguish source-tree identity, binary-content integrity, build-input coverage, and proof that a binary was compiled from claimed sources. It MUST challenge the existing controls with clean and dirty source states, stale or mixed binaries, copied artifacts, generated inputs, and manifest mismatches. Strict certification MUST reject required missing, unhashable, or unreadable executable records and MUST either enforce embedded build-time cleanliness or cease claiming that it has verified it. Confirmed fail-open verifier defects MUST be repaired with regression tests independently of the decision on an additional digest. After those repairs, the review MUST record exactly one digest decision: `IMPLEMENT`, `ALREADY COVERED`, or `NOT JUSTIFIED`. An added digest MUST have deterministic input and verification rules and MUST reject every mismatch in its documented policy. Certification MUST NOT claim that a source digest or a manifest generated from selected binaries alone proves how an executable was built.

#### Scenario: A required binary cannot be verified

- **WHEN** a required executable is absent, cannot be hashed or read, or carries an incompatible build-time dirty-state claim
- **THEN** strict binary-to-source binding fails with a specific reason and cannot issue a valid certification record

#### Scenario: Existing controls reject the threat

- **WHEN** every concrete candidate mismatch in the defined threat model is already rejected or the proposed digest cannot close the demonstrated gap
- **THEN** the decision records `ALREADY COVERED` or `NOT JUSTIFIED` with evidence, and no digest is added

#### Scenario: A material gap is demonstrated

- **WHEN** a reproducible inappropriate artifact passes the current authoritative gate and a deterministic digest would reject it
- **THEN** the decision is `IMPLEMENT`, and modified, missing, malformed, stale, mixed, and dirty-state cases are rejected according to its documented scope

### Requirement: Hosted and local certification remain separately attributable

The final certification record MUST bind every result to a candidate commit and executable tree, identify each required local gate and its result, and distinguish scenario-level success from process-level execution or classified skips. A later documentation-only HEAD MUST be distinguished from the executable candidate and MUST NOT inherit a strict binary-to-HEAD binding claim without rebuilding and checking that HEAD. Hosted certification MUST name the workflow, run, trigger, exact SHA, required jobs, results, skips, and artifacts. A billing or availability rejection before jobs run MUST be recorded as `HOSTED_CI = BLOCKED_EXTERNAL`; a local parity report MUST NOT embed an unverified, fixed hosted status or be called hosted certification. The agent MUST avoid repeated known-unstartable hosted runs and MUST preserve the repository's validation and security gates.

#### Scenario: Hosted jobs execute

- **WHEN** GitHub Actions accepts a run for the final pushed SHA
- **THEN** certification inspects every required job and artifact, and reports hosted success only if the complete required matrix succeeds for that SHA

#### Scenario: Hosted jobs are rejected before execution

- **WHEN** the current run annotation shows an account-level billing or availability block
- **THEN** the report records the actual run and annotation as `BLOCKED_EXTERNAL`, provides the owner unlock action, and keeps local results separate

#### Scenario: Required local gate is unavailable or skipped

- **WHEN** a required local validation step cannot run or a scenario is classified skipped
- **THEN** the report records its explicit status and evidence without converting it to `PASS`

### Requirement: Closure is adversarially reviewed and handed off precisely

Before local residual closure, independent review MUST try to falsify transport failure behavior, provenance binding, certification claims, and change scope with reproducible or source-backed findings. After the planned phases, a focused final residual review MUST check the modified subsystems once more. Confirmed material findings MUST be corrected and affected evidence refreshed. The handoff MUST classify each remaining item as locally actionable, externally blocked, speculative, or future product work and MUST identify each external prerequisite, canonical validation path, expected evidence, and success criterion. No externally unavailable scenario may be reported as passed.

#### Scenario: Reviewer finds a material reproducible defect

- **WHEN** an independent review demonstrates a transport, provenance, or certification failure within this change's scope
- **THEN** the defect is corrected and the affected validation is rerun before closure

#### Scenario: Only external requirements remain

- **WHEN** all material local tasks are complete or proven unnecessary but hosted, signing, clean-host, elevated scan, guest, or display evidence remains unavailable
- **THEN** the report distinguishes local closure from external certification and gives one exact unlock action per remaining dependency
