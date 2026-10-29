## ADDED Requirements

### Requirement: Owned message cleanup preserves pending quit

Removing cleanup drains SHALL dispose only their expected payload messages and SHALL preserve a retrieved WM_QUIT and its exit code for the outer message loop. A preserved quit SHALL be reposted after draining, avoiding repeated retrieval of the repost within the same drain. Unrelated window and message payloads SHALL remain queued.

#### Scenario: Quiet queue exposes quit during cleanup

- **WHEN** the UI thread has pending owned payloads and a quit signal with no unrelated posted work
- **THEN** each owned payload is disposed exactly once
- **AND** quit is never interpreted as a payload
- **AND** the outer message loop receives the original exit code

#### Scenario: Repeated and selective cleanup

- **WHEN** cleanup runs repeatedly with pending quit and messages belonging to another window or message ID
- **THEN** unrelated messages remain untouched
- **AND** repeated cleanup preserves quit without looping indefinitely
