## Why

The managed verification client silently exposes standard Win32 controls as generic panes and misses genuine title Name events when its installed default proxy assembly cannot be resolved with the expected namespace casing. Separately, the capture observer passes a process ID to a thread-ID API, so previous drag observations cannot establish an operating-system capture limitation.

## What Changes

- Initialize standard managed client proxies from the loaded UIAutomationClient directory, allowing only a case adjustment of the standard assembly name to match its exported entry namespace.
- Cache process-local initialization success or failure, preserve mock-only execution, and fail clearly when the installed provider metadata or registration is unavailable.
- Query capture using the window owner's returned thread ID, preserving any capture on that source thread and fail-closed invalid handles.
- Add portable metadata tests and an explicit, own-window native diagnostic for actual standard patterns, genuine title events, and own-thread capture.
- Repair E2E source-provenance preflight: resolve the preset build directory above src/ui and pass all five required binaries, including missing siblings, to the existing fail-closed gate. Execute the actual runner in an owned headless preflight contract test.

## Capabilities

### New Capabilities

- `native-uia-verification`: Trustworthy native-control and source-thread capture observations in the verification client.

### Modified Capabilities

None.

## Impact

Only the PowerShell UIA driver, its tests, and change/evidence records. No product executable, protocol, process ownership, installer, signing, configuration, registry, or trust-store changes. Actual FastFiles acceptance remains open until the integrator validates the combined safe-cleanup source.
