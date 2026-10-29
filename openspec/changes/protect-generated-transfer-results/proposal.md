## Why
A controlled native Move reproduces loss of an incoming file: Shell gives an ordinary transfer a vacated approved replacement name, then the replacement phase overwrites that newly created result and reports two successes. This violates the file manager's data-preservation contract.

## What Changes
- Protect callback-observed ordinary result namespaces before approved replacements execute.
- Reject the queued replacement group truthfully when a target overlaps a prior result; retain replacement sources.
- Exercise actual Copy, Move and holding-folder Restore controls and adverse cases, plus path boundaries.

## Capabilities
### New Capabilities
- `transfer-result-protection`: prevent approved replacement work from destroying earlier incoming results of the same request.
### Modified Capabilities
None.

## Impact
Native unprivileged FileOperations worker, pure policy and owned CTest fixtures. No IPC, schema, signing, service, installer or privileged changes. Existing PRs remain unmerged.
