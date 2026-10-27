## Context

See proposal.md for motivation. A disposable raw Win32 checkbox and combo reproduced the same generic Pane/no-pattern behavior as the prior FastFiles observations. Installed PowerShell 7.6.9 ships UIAutomationClientSideProviders 10.0.0.0 whose entry type namespace is UIAutomationClientsideProviders. Registering that namespace spelling with the public API changes the same HWNDs to real standard roles/patterns and changes a genuine title-event observation from zero to one.

## Goals / Non-Goals

**Goals:** Preserve installed assembly identity and path; allow only the known standard-name case adjustment; make initialization repeatable across module imports without repeated registration; retain headless mock tests; accurately observe source-thread capture.

**Non-Goals:** Product accessibility changes, custom provider substitutes, global proxy configuration, file/assembly installation, input behavior changes, foreign capture changes, Explorer drag certification, or actual FastFiles execution from this main-based worktree.

## Decisions

### D1: Resolve the installed standard provider beside the loaded client

Use the loaded AutomationElement assembly's directory, inspect the installed standard provider's identity and exact exported entry type, and clone its AssemblyName before adjusting Name to the exported namespace. Require both names to equal the known standard name with OrdinalIgnoreCase and the entry class name to match exactly. Validate the loaded provider path equals the derived path. Why: the public API builds a case-sensitive type name from the requested assembly spelling; a general assembly search or substitute proxy would broaden the dependency surface. Version, token, culture and path remain installed values.

First read the public AutomationElement.RootElement to initialize the normal UIA runtime. Cold registration without any prior element read throws NullReferenceException on the installed client, whereas the original fixture's before-query warmed that runtime. Do not infer cold-start coverage from a warm before/after fixture; test both the direct fresh-client initializer and explicit fixture `-ColdStart` mode. This read does not require standard-control patterns and does not mark initialization successful before registration finishes.

### D2: Register once per installed client/provider module identity

Store a process/AppDomain record under the client module MVID, including provider MVID and status. Register once on the first managed driver initialization, then reuse this record on later imports. Cache failure clearly and mark script initialization loaded only after success. Why: Import-Module -Force resets module variables but must not repeat process-global registration or silently retry a broken dependency. The installed public ClientSettings API provides no registration-table getter (confirmed by reflection and the first normally cleaned diagnostic failure); no private table reflection is used. A separate library's earlier automatic registration cannot be inspected through this public surface; the bounded trade-off is one explicit standard-provider registration per driver/client-module identity. This is process-local state only; no named kernel objects or registry writes.

The cached failure scope is provider metadata/loading/registration inside Initialize-UiaStandardProxy. Add-Type and the preliminary RootElement accessor execute before that marker exists; their errors propagate normally and do not mark initialization successful, but are not cached by the proxy marker.

### D3: Separate metadata policy from native initialization

Use a pure identity-selection helper for portable tests; mock operations never call native initialization. Keep the own-native fixture explicit and outside default CTest. Why: a CI host without an interactive desktop can still validate name rejection, nonmutation, cache mechanics and mock behavior. An interactive fixture must assert actual public UIA values, never supply synthetic patterns.

### D4: Use the actual API return as the source thread ID

GetWindowThreadProcessId returns the thread ID and writes a process ID through its out parameter. Check the return for zero, then pass it to GetGUIThreadInfo. Preserve the existing any-nonzero-source-thread-capture result. Why: a child window on the same source thread may hold capture during a normal drag. No thread attachment, input, or foreign capture mutation is required.

## Risks / Trade-offs

- Installed runtimes may lack usable proxy metadata: fail clearly and cache the reason; do not treat generic Pane observations as certification.
- Process-global client registration cannot be undone: run diagnostic comparisons in disposable clients and use only the installed standard provider.
- A synthetic own-window success does not certify FastFiles or Explorer: keep actual application tasks unchecked and preserve prior failed receipts.
- The legacy baseline contains unrelated cleanup behavior: do not launch FastFiles here; integration requires the existing safe-cleanup dependency.

## Migration Plan

Deploy the driver fix with its portable tests and explicit diagnostic. Integrator then validates one exact-owned FastFiles standard control and genuine operation-title observation on the safe combined source, followed by the relevant acceptance suite. Rollback is a normal source revert and new client process; no persistent system state changes exist.
