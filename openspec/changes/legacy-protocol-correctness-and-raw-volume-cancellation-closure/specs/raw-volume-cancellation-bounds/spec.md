## Purpose

Establish production-safe, independently testable guarantees for stopping scanner and journal workers that issue synchronous raw-volume `DeviceIoControl` calls, while treating a cancellation request as a request rather than completed I/O.

## ADDED Requirements

### Requirement: A cancellation request is not completed I/O

Requesting cancellation of a worker's synchronous raw-volume operation MUST NOT be described or tested as proof that the kernel operation finished. A control path that returns within a deadline is not proof that the underlying kernel operation completed. Named-pipe cancellation results MUST NOT be reported as raw-volume cancellation results.

#### Scenario: CancelSynchronousIo is recorded as a request

- **WHEN** the service stops a scanner or journal worker that may be inside a synchronous raw-volume `DeviceIoControl`
- **THEN** certification MUST record that cancellation was requested
- **AND** MUST NOT claim the kernel operation completed solely because cancellation was requested

#### Scenario: Named-pipe cancellation is a separate guarantee

- **WHEN** a cancelable blocked service-pipe read is aborted through the owned worker lifecycle
- **THEN** that bounded pipe result MAY be claimed for the pipe path
- **AND** MUST NOT be claimed as a finite bound for an arbitrary raw-volume IOCTL

### Requirement: Stall behavior is measured with a hermetic fixture

A deterministic, hermetic fault-injection fixture MUST simulate an operation that fails to complete promptly. The fixture MUST NOT deliberately hang a production volume. Tests MUST cover cancellation during active scanning and journal operations. Each of the following MUST be recorded as a separate guarantee: time until cancellation is requested, time until the control path responds, time until the worker stops, whether cleanup requires kernel I/O completion, whether new work can be admitted, whether the service can shut down, and whether a previous worker can affect a replacement session.

#### Scenario: Simulated stall is measured without a production hang

- **WHEN** the hermetic fixture holds a raw-volume-shaped operation past the control-path deadline
- **THEN** the test records each listed timing and admission/shutdown/replacement observation separately
- **AND** does not open or stall a real production volume device to obtain those numbers

### Requirement: Only proven architecture guarantees are implemented

The implementation MUST use the narrowest production-safe architecture whose supported guarantees have been established. It MUST NOT detach a worker that still accesses stack-owned state, close resources the kernel or worker may still reference unsafely, use `TerminateThread`, introduce an unbounded detached-worker population, report a requested cancellation as a completed cancellation, or claim a universal deadline against an arbitrary non-cooperative storage driver. Required invariants: no use-after-free, no unbounded worker accumulation, no stale worker writing to a replacement connection, no authentication bypass, no false scan-completion event, no deadlock introduced by cancellation, no loss of durable index correctness, and no indefinite admission of replacement work while an abandoned operation remains authoritative.

#### Scenario: Cooperative simulated driver does not close the kernel bound

- **WHEN** new tests pass against a cooperative simulated driver that returns after cancellation
- **THEN** residual completion-bound work MUST remain unresolved unless a finite bound is proven for the supported architecture against non-cooperative completion
- **AND** that work MUST NOT be marked complete solely because the cooperative tests passed

#### Scenario: Late completion after cancellation cannot corrupt a replacement

- **WHEN** a cancelled worker's kernel operation completes after a replacement session has started
- **THEN** the late completion MUST NOT write to the replacement connection
- **AND** MUST NOT emit a successful scan terminal for the cancelled work
