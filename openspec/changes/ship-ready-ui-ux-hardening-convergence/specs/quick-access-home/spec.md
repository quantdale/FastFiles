## ADDED Requirements

### Requirement: Home path labels do not rewrite local state

Pinned bookmarks, recent folders, and known-folder presentation on Home SHALL
use display-only labels. Activating an entry, persisting it, or clearing it
SHALL continue to use the typed/canonical value held by workspace state.

#### Scenario: A recent short-name folder keeps its stored path
- **WHEN** Home renders a recent folder whose canonical path contains an 8.3 component
- **THEN** the visible label MAY use the long form while activation and persistence retain the canonical recent-folder string

#### Scenario: Home remains local in degraded mode
- **WHEN** the engine or privileged service is unavailable and Home renders recent or pinned folders
- **THEN** label generation SHALL use only already available local state and SHALL not require privileged or network filesystem access
