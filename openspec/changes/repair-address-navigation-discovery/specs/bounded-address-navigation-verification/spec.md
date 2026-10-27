## ADDED Requirements

### Requirement: Navigation labels use bounded native chrome discovery
The verifier SHALL discover tab labels through direct native chrome children without traversing file rows, while retaining process, class and tab identity checks.

#### Scenario: File tree exhausts recursive search
- **WHEN** recursive discovery cannot reach chrome within its budget but the direct native tab remains readable
- **THEN** navigation can verify its requested label without enumerating file rows

#### Scenario: Unreadable or foreign chrome
- **WHEN** the provider fails or the tab belongs to another process
- **THEN** the navigation cannot be certified from that control

### Requirement: Recommit remains bound to the address edit
The verifier SHALL resend Enter only when the original address edit still reports real boolean keyboard focus and the owned main is foreground.

#### Scenario: Focus has returned to browsing
- **WHEN** label discovery fails after focus leaves the address edit
- **THEN** no second Enter is sent onto the browser
- **AND** failure remains failure if the requested label cannot be verified

#### Scenario: Original address edit still has focus
- **WHEN** the edit remains focused and navigation is unverified
- **THEN** at most one bounded recommit is permitted
