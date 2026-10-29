# Prevent shortcut creation overwrite

Shortcut destinations can appear after UI planning. The worker currently overwrites them and ignores accepted batch cancellation. Preserve existing data at the actual write boundary and stop between items while retaining valid native shortcut serialization.

## Capabilities

### New Capabilities
- `safe-shortcut-creation`: exclusive creation, truthful failures and cooperative cancellation.

### Modified Capabilities
None.

## Impact
Unprivileged native UI file-operation worker only; no protocol, service, signing, persistence-schema or trust changes. Successor to preserve-execution-conflict-consent.
