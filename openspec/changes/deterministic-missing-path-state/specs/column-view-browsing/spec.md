# deterministic-missing-path-state — column-view-browsing delta

## Purpose

This delta makes the visible Column View state distinguish a confirmed missing directory from a legitimate empty directory and from temporary refresh failures. The visible error, retained-row policy, and recovery behavior follow the same snapshot status the engine publishes.

## ADDED Requirements

### Requirement: Confirmed missing directories render as unavailable, not empty
When the current column's snapshot status is `NotFound`, Column View SHALL transition that column to the existing no-longer-available state, clear rows that are no longer current, and display `This folder is no longer available.`. It SHALL NOT display the empty-directory state for a confirmed missing path.

#### Scenario: Current folder is externally deleted
- **GIVEN** a visible column contains one or more current items
- **WHEN** the engine publishes `NotFound` for that column's path
- **THEN** the prior items are no longer presented as current and the column displays `This folder is no longer available.`

#### Scenario: Existing folder is truly empty
- **WHEN** the engine publishes `Success` with zero entries for an existing folder
- **THEN** Column View displays its normal empty-directory affordance and does not display the no-longer-available error

### Requirement: Failure classes keep distinct freshness semantics
Column View SHALL preserve the semantic distinction between `NotFound`, `AccessDenied`, generic enumeration failure, snapshot-publication failure, and engine disconnection. Confirmed terminal path states SHALL clear rows where the existing freshness policy defines the column as unavailable; temporary refresh/disconnection states MAY retain previous rows only when they are explicitly labeled as previous/stale rather than current.

#### Scenario: Temporary refresh failure retains labeled previous results
- **WHEN** a refresh fails in a class whose existing freshness policy retains the prior generation
- **THEN** retained rows are visibly labeled as previous/stale and are not presented as a current successful enumeration

### Requirement: Recreated paths recover through normal navigation/refresh
After a path has been shown as no longer available, a later successful normal directory request for the same path SHALL clear the unavailable state and display the newly enumerated contents without requiring a FastFiles or engine restart.

#### Scenario: Deleted folder is recreated
- **GIVEN** path P is displayed as `This folder is no longer available.`
- **WHEN** P is recreated with a new sentinel item and the user re-requests/navigates to P normally
- **THEN** Column View displays the new sentinel item as current, removes the unavailable status, and does not resurrect rows from the pre-deletion listing

### Requirement: Stale row identity never retargets recreated contents
Rows removed by a confirmed missing-path transition SHALL become stale under the existing semantic identity contract. A later recreation of the same pathname SHALL create current row identities from the new enumeration; old row/provider references SHALL NOT silently operate on the new occupants.

#### Scenario: Old item reference after delete and recreate
- **GIVEN** a client retains a semantic item reference from before deletion
- **WHEN** the directory is deleted, becomes unavailable, and is later recreated
- **THEN** the old reference fails cleanly under the stale-element contract and cannot select or invoke a newly created row merely because its index matches
