## Why

The project-status generator currently treats every unchecked task in every
OpenSpec change as current work and relies on task-prose keywords to decide
whether that work is local or externally blocked. Older campaigns can remain
with intentionally open evidence tasks after their implementation and delivery
have moved into later campaigns. Those stale checkboxes then reappear as
`localOpen`, which can send autonomous continuation toward work that is already
complete or unavailable on the current host.

## What Changes

- Add a schema-validated repository lifecycle manifest for OpenSpec changes and
  explicit task dispositions where change-level state is not enough.
- Preserve the complete raw unchecked-task inventory while separating active
  local work from historical, superseded, mixed, administrative, and external
  evidence states.
- Reconcile historical delivery tasks only when later Git and CI evidence proves
  them complete; leave signing, provider, clean-host, privilege, and interactive
  evidence gates open.
- Extend project-status and release-readiness JSON/Markdown projections with
  lifecycle provenance and deterministic regression coverage.

## Non-Goals

- No product architecture, protocol, authentication, service, UI, or degraded
  browsing changes.
- No automatic closure of external evidence tasks and no trust-store, signer,
  guest, or privileged-host provisioning.
- No bulk archival or movement of OpenSpec changes.
