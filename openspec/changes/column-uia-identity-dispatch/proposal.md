## Why
Column View resolves a provider's stable identity, then invokes a mutable row ordinal. An asynchronous directory refresh between these steps can apply selection, focus, scrolling or activation to a different file. This violates the existing stale-element contract and the original vision's trustworthy hierarchical navigation.

## What Changes
- Carry original item, column, owner and fragment identities through closed owner-thread action dispatch.
- Validate identity under the same model lock as mutation or activation target capture.
- Preserve canonical selection/navigation helpers and post-lock notifications.
- Reconcile accepted accessibility actions with native location/preview updates and visible column reveal, including narrow panes.
- Add deterministic provider regressions for replacement at the dispatch boundary.

## Capabilities
### New Capabilities
- `atomic-browse-uia-actions`: atomic stable-identity validation of Column View accessibility actions.
### Modified Capabilities
None.

## Impact
Native UIA projection, ColumnView interaction adapters and shell owner-window dispatch. No engine, IPC, privileged process, database, search ordering or file-operation changes. No visual redesign; existing Refero-grounded hierarchy and selection design remain authoritative.
