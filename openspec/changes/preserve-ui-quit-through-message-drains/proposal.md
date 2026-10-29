## Why

Actual combined5a0ccbb E2E fails restored Home close after WM_DESTROY provider release and before message-loop exit. Owned payload drains may consume a pending WM_QUIT outside their message range once the posted queue becomes quiet. A deterministic real Win32 fixture confirms this source defect; consumption in the earlier failed process is not directly observed.

## What Changes

Share a Win32 payload-drain helper that recognizes WM_QUIT, disposes only matching payload messages, and reposts the original quit exit code after the drain. Apply to Search, Storage list/ranking and file-operation conflict cleanup. Add actual message-only-window red/green regressions, preserving unrelated messages and repeated cleanup. Preserve original failed acceptance and qualify actual combined runtime separately.

## Capabilities

### New Capabilities
- `native-ui-shutdown`: bounded cleanup preserves a pending main-thread quit message.

### Modified Capabilities

## Impact

Native UI cleanup only; no process boundary, IPC, security, data contract, service or signing changes. No input, global settings or foreign process experiment required.
