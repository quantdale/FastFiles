# Certification Integrity and Residual Closure

## ADDED Requirements

### Requirement: Transport-fatal scan writes fail closed

When a service batch frame write fails after any bytes may have reached the named-pipe byte stream, the service MUST NOT append another frame to that stream. It MUST record a metadata-only diagnostic containing the service volume identity and captured error, request connection abort, and allow the existing disconnect/reconnect path to recreate the transport. A cancellation MUST remain silent, and a scan failure that occurs before a frame write MAY use the capability-gated failure terminal only when that terminal write succeeds.

#### Scenario: Partial frame write

- **WHEN** an overlapped batch write returns failure after a partial write
- **THEN** no `ScanFailed` or `ScanComplete` frame is appended to the same pipe
- **AND** the diagnostic and connection-abort path make the failure observable without claiming scan completion

### Requirement: Delivered scan failures are retried safely

When the engine receives `ScanFailed` for an in-flight initial or reconciliation scan, it MUST clear that scan's authority without finalizing its pass, preserve the durable cursor, and schedule a bounded retry. The retry MUST recheck active connection, session identity, and pause state. A later clean completion MAY mark the volume complete.

#### Scenario: Delivered failure retries safely

- **WHEN** a delivered `ScanFailed` leaves an initial or reconciliation scan incomplete
- **THEN** the engine preserves the cursor, schedules a bounded retry, and does not finalize the partial pass

### Requirement: Local CI parity is fail-closed

A required local-parity step MUST be `PASS` only when its child exits zero. A missing required tool MUST be classified `SKIPPED` with machine evidence, a validator failure MUST be `FAIL`, and an aggregate containing a required skipped step MUST NOT be `PASS`. OpenSpec resolution and validation MUST use the same pinned command in hosted and local workflows.

#### Scenario: Required OpenSpec tool is missing

- **WHEN** the OpenSpec child cannot resolve `npm` or `npx`
- **THEN** local parity records an explicit unavailable result and never reports aggregate `PASS`

### Requirement: Visual catalog coverage is explicit

The visual runner MUST define one canonical 16-scenario core registry and one conditional four-scenario gallery registry. Every normal or early-failure run MUST reject missing, duplicate, or unexpected scenario IDs and MUST retain explicit lifecycle diagnostics. Historical counts MUST identify whether they mean core scenarios alone or core plus gallery variants.

#### Scenario: Early catalog failure omits scenarios

- **WHEN** lifecycle setup aborts before all expected scenarios are recorded
- **THEN** the coverage guard fails and lists every missing scenario instead of certifying a smaller catalog

### Requirement: UIA skips require observed evidence

An unavailable foreground query MUST remain `FAIL`/harness-unknown and MUST NOT be represented as a proven ownerless environment. A skip requires a proven foreign owner or explicitly blocked input point. Native input helpers MUST expose whether required `SendInput` stages were accepted and MUST release held buttons/modifiers on failure. Geometry timeouts and evidence-copy failures MUST remain visible in the result.

#### Scenario: Foreign foreground is proven

- **WHEN** a bounded precondition observation captures a non-FastFiles foreground owner
- **THEN** the scenario may be classified as an evidence-backed environment skip
- **AND** an unavailable observation remains a failure

### Requirement: Shared-session navigation failures stay local

An exception thrown by the address-bar navigation helper MUST be converted to the normal `{ Pass, Detail }` result contract before dependent Search scenarios run. A failed navigation precondition MUST classify both dependent scenarios and MUST stop downstream input. Ordinary product failures and FastFiles-owned failures MUST remain `FAIL`.

#### Scenario: Foreign navigation helper failure

- **WHEN** navigation throws after a proven foreign foreground is captured
- **THEN** both dependent Search scenarios are recorded as evidence-backed `SKIPPED`
- **AND** the shared session continues to teardown exactly once

#### Scenario: Owned navigation helper failure

- **WHEN** navigation throws while FastFiles owns the foreground
- **THEN** both dependent Search scenarios are recorded as `FAIL`
- **AND** no input is sent by their dependent bodies
