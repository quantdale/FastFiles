# Snapshot versioning validation — 2026-08-11

## Scope

This slice closes tasks 4.1–4.4 of `address-engineering-audit-findings`.
It hardens the engine-to-UI read-only shared-memory snapshot seam without
changing named-pipe authentication, signing pins, or privileged service
behavior.

## Implementation

- `SnapshotSharedHeader` now carries `kSnapshotFormatVersion` and preserves
  natural alignment for the interlocked generation field.
- `SnapshotPublisher` initializes and republishes the version on every
  publication.
- `EngineClient` rejects an incompatible version before parsing, unmaps the
  read-only section, and re-maps the retained section name on the next
  `NewGeneration` notification. Failed remaps retain the name for a later
  retry.
- The UI freshness test covers detection of a future version and resetting
  state for the new mapping source.

## Evidence

- `ffengine_snapshot_publisher_tests`: PASS. The mapping exposes and accepts
  the current version and rejects a future version; existing slot and
  generation behavior remains covered.
- `ffsnapshot_freshness_tests`: PASS. Incompatible format detection and fresh
  source/re-map state pass.
- `FastFiles` Debug target: PASS.
- Serial Debug CTest focused snapshot tests: PASS, 3/3.
- Full Debug build: PASS.
- Full Analyze build (`/analyze`): PASS.
- Full Release build: PASS.
- Serial Debug CTest: PASS, 45/45, including the document-drift guard after
  line-ending normalization.
- Serial Release CTest: PASS, 45/45.
- `pwsh ./verify/verify.ps1 build`: PASS (`windows-build-validation`).
- `openspec validate --all --strict`: PASS, 45/45.
