## Purpose

Synchronous raw-volume device-control cancellation has no proven finite completion bound. This campaign keeps that bound open and does not redesign the privileged worker to chase it.

## ADDED Requirements

### Requirement: The raw-volume completion bound stays unproven

The product SHALL NOT claim a finite in-process completion bound for an arbitrary synchronous raw-volume device-control call. A cooperative simulated stall SHALL NOT be reported as that bound. This campaign SHALL NOT redesign privileged worker ownership, overlapped device control, cancellation policy, or process isolation in order to close the bound. The inherited open tasks that exist to hold the bound, legacy-protocol task 5.4 and residual-hardening task 2.3, SHALL remain unchecked. The handoff SHALL classify the bound as unproven.

#### Scenario: Simulated stalls do not close the inherited tasks

- **WHEN** a test parks a worker in a simulated stall and the worker later returns
- **THEN** that result SHALL NOT be recorded as a finite completion bound for an arbitrary storage driver
- **AND** legacy-protocol task 5.4 and residual-hardening task 2.3 SHALL stay unchecked

#### Scenario: This campaign does not redesign the worker

- **WHEN** the scan-authority and residual-certification work is implemented
- **THEN** the privileged scan and journal workers SHALL keep their current request-and-join cancellation shape
- **AND** overlapped device control, a helper process, and a new admission quarantine SHALL NOT be introduced here
