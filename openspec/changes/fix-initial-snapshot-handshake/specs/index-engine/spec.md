## Purpose

`FastFilesEngine`'s snapshot publication SHALL expose a valid, parseable
snapshot generation to a UI client the moment the control pipe becomes
reachable, so a fresh subscription is never acknowledged with an unusable
generation. This amendment to the canonical `index-engine` "Filesystem
Snapshot Publication" requirement fixes the startup handshake defect that
produced `engine-connect-snapshot-map-failed` / `ERROR_INVALID_DATA` and the
resulting `engine-connect-unavailable` UIA SKIPs.

## ADDED Requirements

### Requirement: Startup commits a usable empty snapshot generation
`SnapshotPublisher::Start` SHALL commit a canonical serialized empty snapshot
(zero directories, the ordinary `[u32 0]` payload — no special empty wire
form, no dummy entry) as its first usable generation before returning
success. Generation 0 SHALL remain the "no committed generation" sentinel and
SHALL NOT be published.

#### Scenario: Fresh engine, nothing browsed, nothing indexed
- **WHEN** a fresh engine starts with an empty index database, no indexed
  volumes, the privileged service unavailable, and no directory ever
  requested
- **THEN** `UiServer::Start` succeeds, the current snapshot generation is
  nonzero, the generation mapping opens read-only with a compatible header
  whose generation matches the acknowledged value, and `ParseSnapshot`
  succeeds with an empty directory map

#### Scenario: First content publication advances by one
- **WHEN** the first directory is requested after startup
- **THEN** the published generation is exactly the startup generation plus
  one and contains the requested directory

### Requirement: Reachable server implies a usable snapshot
If `UiServer::Start` returns true, then before the listener acknowledges any
`Subscribe` request the current generation SHALL be nonzero, the generation
mapping SHALL exist with a compatible header whose generation equals the
acknowledged generation, and the payload SHALL parse (a valid snapshot MAY
contain zero directories).

#### Scenario: Subscribe before any directory publication
- **WHEN** a UI client subscribes immediately after a fresh server start,
  before any directory request or index rebuild publication
- **THEN** the `SubscribeAck` carries a nonzero generation, the client maps
  and validates that generation, and reads an empty snapshot; the client
  later receives `NewGeneration` for the first directory request

#### Scenario: Impossible subscription state fails closed
- **WHEN** the server would have to acknowledge a subscription with no
  committed nonzero generation (internal invariant violation)
- **THEN** the server does not send an unusable `SubscribeAck`, closes the
  connection, and appends the `FFDIAG_SNAPSHOT_SUBSCRIBE_INVALID` diagnostic
  with state `subscribe-without-valid-snapshot`

### Requirement: Startup publication failure fails Start closed
If the initial snapshot publication fails (mapping creation, view mapping,
or serialization), `SnapshotPublisher::Start` SHALL fail, `UiServer::Start`
SHALL fail, no usable control pipe SHALL be exposed, and all snapshot,
listener, security-descriptor, and connection-gate state SHALL be rolled
back.

#### Scenario: Mapping failure at startup
- **WHEN** `CreateFileMappingW` or `MapViewOfFile` fails while committing
  the initial generation
- **THEN** `UiServer::Start` returns false, the pipe is not reachable, and
  no snapshot mapping state survives

#### Scenario: Subsequent publication failure preserves the current generation
- **WHEN** a later `Publish` fails (e.g. serialized size exceeds the bounded
  payload)
- **THEN** the prior valid generation remains current, no `NewGeneration`
  is broadcast, and the existing `PublishFailed` surfacing behaves unchanged

### Requirement: Multiple initial clients share the startup generation
Two or more clients subscribing before any directory publication SHALL
receive the same current generation, SHALL map and parse the same empty
snapshot simultaneously, and SHALL each receive subsequent `NewGeneration`
broadcasts. The shared generation SHALL NOT be mutated per client.

#### Scenario: Two clients subscribe before publication
- **WHEN** two clients subscribe before any directory is requested and one
  then requests a directory
- **THEN** both are acknowledged with the same nonzero generation, both
  parse the empty snapshot, and both receive the next `NewGeneration`

### Requirement: Startup snapshot has no privileged dependency
The startup snapshot SHALL be available with no `FastFilesIndexSvc`, no
production signing, no persisted index, and no browsed directory. The UI
SHALL reach `EngineConnectionState::Connected` in degraded mode
(`privilegedPathActive == false`); lack of privileged indexing SHALL NOT be
classified as a connection failure.

#### Scenario: Empty database, no service, connected in degraded mode
- **WHEN** the engine runs with an empty/fresh index database and no
  privileged service
- **THEN** `EngineClient` connects, `ReadSnapshotWithGeneration` succeeds
  with an empty map, and the engine state is Connected with the privileged
  path inactive
