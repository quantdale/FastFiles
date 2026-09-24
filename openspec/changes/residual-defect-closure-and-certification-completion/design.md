# Design

## D1 — Transport-fatal scan writes

`RunVolumeScan` returns an explicit outcome. A failure while writing a batch is treated as a transport failure: it records a metadata-only diagnostic containing the opaque volume identity and captured Win32 error, requests connection abort through the service worker boundary, and never appends `ScanFailed` to the same pipe. Setup and enumeration failures that occur before a frame write may still use the capability-gated terminal; their terminal write is itself checked and converts to transport failure when it cannot be delivered. Cancellation remains silent.

The service worker cancels the connection's pending overlapped I/O after a transport-fatal outcome. Ordered worker/connection teardown remains the sole owner of handle closure. The existing H1 revalidation and H2 exception boundaries remain intact.

## D2 — Bounded scan-failure retry

`VolumeSessionManager` records failed initial/reconciliation scans in a wakeable, bounded retry queue. The existing scheduler waits for the earlier of its normal poll interval and the next retry deadline, then reissues the initial scan from the durable cursor or starts a fresh reconciliation pass. A failed retry is requeued with the same delay; disconnect/reconnect still clears ephemeral IDs and re-derives durable sessions. No terminal can mark a failed pass complete.

## D3 — Explicit CI-parity classification

A small pure PowerShell module owns step-status and aggregate-status decisions. Exit code 2 is `SKIPPED` only for a step explicitly declared tool-unavailable; all other nonzero exits are `FAIL`. The OpenSpec child first proves `npm` and `npx` resolution, invokes the pinned package with the npm separator, and classifies only proven npm/npx bootstrap failures as unavailable. Validator exit code 2 and arbitrary `ENOENT` text remain failures. Any required skipped step makes the aggregate `SKIPPED`, never `PASS`. Hosted and local commands use the same pinned invocation.

## D4 — Visual catalog registry

The runner owns one canonical list of 16 core scenario IDs and four gallery IDs. A pure coverage comparator rejects missing, duplicate, or unexpected IDs while allowing explicit lifecycle diagnostics. The comparator runs in all paths, including an early lifecycle exception, so a failed run cannot silently look like a smaller catalog. Historical 20-record evidence is documented as 16 core plus four conditional gallery scenarios; older shell-only states receive an explicit disposition rather than being silently counted as current core scenarios.

## D5 — UIA evidence and input truth

Foreground state records whether observation succeeded separately from the observed title/HWND. The converter skips only proven foreign ownership or an explicitly blocked input point; unavailable observations fail closed. Native input helpers propagate `SendInput` acceptance and release held modifiers/buttons on failure. Geometry waits are honored. Explorer-source drag classification requires repeated fresh attempts with accepted input. Screenshots use virtual-screen bounds, and diagnostics-copy failures are cleanup/evidence failures rather than silently discarded state.

## D6 — Navigation boundary

The shared Search fixture navigation uses a total-result wrapper that converts helper exceptions into the existing `{ Pass, Detail }` contract. A failed navigation precondition is classified for both dependent scenarios and stops their downstream input immediately. The two local catch blocks remain the final classification boundary, with behavioral tests for foreign, owned, ordinary, and helper-thrown details.

## Risks and controls

- Retry scheduling can race disconnect: all retry actions re-check `running_`, `active_`, session identity, and pause state.
- Cancelling overlapped I/O can race teardown: only the owning service worker requests abort; the existing ordered teardown still closes the handle.
- Input helpers can throw in new places: all scenario runners already have bounded outer boundaries; failures become explicit `FAIL`/harness results, never skips.
- Visual guard may reject a legitimate conditional scenario: core and gallery expectations are explicit and separately tested.
- PowerShell child-process classification can vary by host: pure functions and controlled fixtures guard the truth table independently of npm network state.
