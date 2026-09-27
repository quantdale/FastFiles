# Legacy scan-failure reproducer — red baseline, root cause, and safe correction

Change: `legacy-protocol-correctness-and-raw-volume-cancellation-closure`
Tasks: 2.1–2.4, 3.1–3.4
Test: `tests/engine/test_legacy_scan_failure_binding.cpp` (`ffengine_legacy_scan_failure_binding_tests`)

## What the reproducer drives

The test compiles the real service scanner and the real engine dispatch/session/pipeline
sources into one executable and binds them over an in-process named pipe pair:

* **Service half** — `ffindexsvc::RunVolumeScan` on an unmounted drive letter (the real
  privilege-enable / raw-volume-open / geometry-failure paths) and
  `ffindexsvc::RunSyntheticVolumeScanForTesting` with `enumerationFailed=true|false`
  (the real `ScanOutput::Finish` decision used by a mid-enumeration I/O fault and by a
  genuine end of enumeration), both run through the production
  `RunScanWorkerBoundary`.
* **Engine half** — the bytes the service actually wrote are read with the production
  framing (`ffipc::ReadFrame`) and dispatched through
  `PrivilegedConnection::DispatchFrameForTesting`, which calls the production
  `DispatchReceivedFrame` (exact-length payload validation included) into the real
  `VolumeSessionManager` callbacks and `IndexPipeline` durable store.

For every negotiation `{1,5}`, `{2,0}`, `{2,1}`, `{2,2}` the test covers a setup failure
and a mid-enumeration failure separately from a genuine success, and records the terminal
frame actually written, whether the connection was ended, the engine's durable
`scanComplete` flag, whether an in-flight reconciliation pass survived, whether the
cadence clock advanced, whether a seeded durable entry survived, and whether
connection-scoped engine state was cleared.

Command (Debug):

```powershell
cmake --build --preset debug --target ffengine_legacy_scan_failure_binding_tests
./build/debug/tests/engine/ffengine_legacy_scan_failure_binding_tests.exe
```

## Red baseline — the defect is real (task 2.3)

Against the uncorrected implementation (service `SendScanTerminal` falling through to
`SendScanComplete` for a pre-`{2,2}` peer; engine `MarkScanComplete`/`FinishReconciliationPass`
unchanged) the reproducer exits `1` with **36 failures / 84 passes**, and every failure is
on a pre-`{2,2}` negotiation — 12 failures for each of `{1,5}`, `{2,0}`, `{2,1}`; the
`{2,2}` control case has none:

```text
FAIL: {1,5} setup failure writes no success terminal
FAIL: {1,5} setup failure never marks the volume scan complete
FAIL: {1,5} setup failure deletes no durable records from incomplete coverage
FAIL: {1,5} setup failure leaves the reconciliation cadence clock untouched
FAIL: {1,5} setup failure ends the attempt by aborting the connection
FAIL: {1,5} setup failure clears connection-scoped engine state
... (identical set for the mid-enumeration failure, and for {2,0} and {2,1})
```

Each line is a distinct durable consequence, not a duplicate: a failed scan delivered the
legacy `ScanComplete`, `VolumeSessionManager::OnScanComplete` found a valid in-flight scan
and called `IndexPipeline::MarkScanComplete` plus `FinishReconciliationPass`, the pass's
partial seen-set removed a seeded durable entry (`seed.txt`), the reconciliation cadence
clock advanced, and the connection stayed up with the stale session intact.

Conclusion (task 2.3): **a pre-`{2,2}` engine would mark the volume indexed, advance
durable completion state, and finalize an incomplete reconciliation pass.** The reproducer
fails against the original behavior exactly as required.

## Intentional compatibility rule vs. safe implementation (task 2.4)

`index-durability-false-success-closure` D1 introduced an *intentional* compatibility
rule: never send the additive `ScanFailed` message to a peer that negotiated below
`{2,2}`, and keep every successful-scan byte unchanged for those peers. The existing
`index/scan-completion-truthfulness` spec only required those two things — it never
required a `ScanComplete` terminal on failure.

The implementation went further and reused `SendScanComplete` as the legacy failure
terminal. That is the unsafe part: the protocol contract for `ScanComplete` is "exactly
once per scan that runs to completion" (`src/protocol/include/ffprotocol/Commands.h`), and
the engine's authority decision is made from the message type alone —
`VolumeSessionManager::OnScanComplete` has no negotiated-version gate and no way to
distinguish "the MFT ran out" from "the peer had no failure terminal to send". With a
pre-`{2,2}` peer, the compatibility fallback therefore converted a failed scan into
authoritative durable success, which is the exact invariant the campaign must preserve:

> A scan that did not complete must never be represented as successfully completed merely
> because the peer cannot understand `ScanFailed`.

The correction (task 3.1) keeps the *intent* of the compatibility rule and removes only
the false-success path: a failed scan is not representable to a pre-`{2,2}` peer, so the
service writes **no terminal at all** and ends that connection. That reuses the engine's
already-safe silent-cancellation shape (disconnect never marks completion), sends no
message the peer cannot parse, and leaves successful-scan bytes byte-for-byte unchanged.
A protocol bump is not required: `{2,2}` `ScanFailed`, the wire layouts, negotiation,
authentication, and connection-scoped identities are untouched.

## Green result (task 3.4)

After the correction the same test exits `0`: **120 passes / 0 failures**, including

* `{1,5}`, `{2,0}`, `{2,1}` failure: no `ScanComplete`, no `ScanFailed`, connection abort
  requested, engine never marks completion, pass discarded, seeded entry preserved,
  cadence clock untouched, connection-scoped state cleared;
* `{2,2}` failure: exactly one `ScanFailed`, connection stays up, engine never marks
  completion, in-flight scan state cleared;
* every negotiation on genuine success: exactly one `ScanComplete`, volume marked indexed,
  connection stays up.

Supporting focused tests on the same candidate: `ffvolumescanner_terminal_tests`,
`ffengine_volume_session_manager_tests` (including
`TestConnectionLossAbortsReconciliationPassWithoutAuthority`), `ffprotocol_tests`,
`ffprotocol_fuzz_tests`, `ffcommand_surface_tests`, `ffengine_index_pipeline_tests`,
`ffindexstore_*`, `ffengine_seeded_indexed_mode_tests` — all pass.
