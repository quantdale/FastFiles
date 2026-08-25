## ADDED Requirements

### Requirement: Tab labels are display-only projections

Tab labels SHALL use a display-safe leaf derived from the tab's typed location,
including best-effort local 8.3 normalization and extended-prefix removal. Tab
selection and persistence SHALL continue to use the tab's canonical location.

#### Scenario: A short-name tab has a friendly label
- **WHEN** a filesystem tab points at a resolvable local short-name path
- **THEN** its label SHALL use the friendly leaf while tab activation returns the original canonical path

#### Scenario: Home and This PC tab labels remain typed
- **WHEN** a tab is Home or the This PC synthetic root
- **THEN** its label SHALL be `Home` or `This PC` respectively and SHALL NOT be derived through filesystem normalization
