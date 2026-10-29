# Fit the native command palette to its results

A filtered command palette currently reserves360DIP regardless of whether it contains one command or a no-match placeholder. Fit the native result list to content while keeping the query field anchored and all Windows command/focus behavior intact.

## Capabilities

### New Capabilities
- `content-sized-command-palette`: bounded native command layout for sparse, empty and long lists.

### Modified Capabilities
None.

## Impact
Native Win32 CommandPalette presentation and centralized layout metrics only. No command availability, execution, engine, protocol, filesystem, signing or persistence change. Stacked on PR25 to preserve live cancellation availability.
