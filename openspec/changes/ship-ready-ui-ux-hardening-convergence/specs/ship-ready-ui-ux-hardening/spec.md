## Purpose

This capability defines the ship-readiness contract for user-facing path labels,
canonical identity separation, and resilient local UI presentation across the
navigation and Storage Analysis surfaces.

## ADDED Requirements

### Requirement: Display normalization is local, display-only, and best-effort

FastFiles SHALL normalize a filesystem path only for user-facing presentation.
Normalization MAY resolve a local drive path to its friendly long-name form and
MAY remove an extended-length display prefix. It SHALL NOT resolve UNC or other
network paths from paint, layout, or UIA property reads. If a path is missing,
inaccessible, short-name generation is disabled, malformed, or cannot be
resolved, the original display text SHALL be retained without an error surface.

#### Scenario: Local short-name presentation is friendly
- **WHEN** a local drive path contains a resolvable 8.3 component such as `MICHAE~1`
- **THEN** user-facing labels MAY show the corresponding long component while the canonical path remains unchanged

#### Scenario: UNC presentation never performs network resolution
- **WHEN** a breadcrumb, tab, Home label, Search location, details surface, or UIA property reads a UNC path containing a short-name marker
- **THEN** the surface SHALL return promptly using the path's display-safe form without initiating network filesystem resolution

#### Scenario: Unresolvable local path falls back safely
- **WHEN** a local path is nonexistent, inaccessible, or has no available long-name mapping
- **THEN** display normalization SHALL return a safe fallback label and SHALL NOT alter navigation or report a failure

### Requirement: Canonical identity is never replaced by a display label

The canonical path used for navigation, history, indexing, file operations,
copy-path commands, and typed filesystem locations SHALL remain byte-for-byte
unchanged by display normalization. Synthetic Home and This PC locations SHALL
remain distinct typed locations.

#### Scenario: Display normalization preserves an extended local identity
- **WHEN** an extended-length local path is rendered in navigation chrome
- **THEN** only the displayed label may omit the prefix and the stored/activated path SHALL retain the exact extended-length string

#### Scenario: Copy path exposes the canonical value
- **WHEN** the user invokes Copy Path for an item whose display label was normalized
- **THEN** the copied value SHALL be the canonical operational path, not the normalized label

#### Scenario: Typed synthetic locations remain distinct
- **WHEN** Home or This PC is rendered in any path-derived surface
- **THEN** it SHALL use its typed semantic label and SHALL NOT be converted into or passed as a filesystem path

### Requirement: Display labels remain independent across navigation contexts

Display normalization SHALL be evaluated from each pane, tab, and Home snapshot
independently. A label change in one context SHALL NOT mutate another context's
canonical location, history, or persisted workspace state.

#### Scenario: Dual-pane labels do not cross-contaminate
- **WHEN** one pane displays a normalized local path and the other displays a different canonical path
- **THEN** each pane SHALL retain its own label and canonical identity

#### Scenario: Recent and pinned labels do not rewrite history
- **WHEN** Home renders recent folders or bookmarks with display normalization
- **THEN** the persisted recent/bookmark path values SHALL remain the original canonical strings
