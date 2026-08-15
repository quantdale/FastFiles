# Tasks — fix-initial-snapshot-handshake

Code comments cite tasks as `// fix-initial-snapshot-handshake N.x:` where the
change slug disambiguates across changes.

## 1. Snapshot publisher startup snapshot

- [x] 1.1 `SnapshotPublisher::Start` commits a canonical empty snapshot
  (zero directories, `[u32 0]` four-byte payload in the ordinary serialized
  format) as its first usable generation instead of committing generation 0;
  generation 0 remains the uncommitted sentinel and is never published. —
  complete; `src/engine/src/SnapshotPublisher.cpp` `Start()` uses
  `kFirstGeneration` and the two-pass serializer over an empty map.
- [x] 1.2 Remove the generation-0 mapping creation; nothing mapped it and the
  first generation mapping performs the same early prefix validation. —
  complete; `Start()` no longer creates `<prefix>.0`.
- [x] 1.3 `Publish` failure (serialized size over the bounded payload) still
  preserves the prior valid generation and returns 0. — complete; unchanged
  `PublishPayload` path, covered by `TestPublisherStartupEmptySnapshot` and
  `TestPublisherUsesImmutableGenerations`.

## 2. Startup invariant and SubscribeAck

- [x] 2.1 `UiServer::Start` ordering unchanged: publisher epoch/prefix →
  first valid committed generation → security descriptor → listener, so a
  subscriber can never race ahead of the committed generation. —
  complete; ordering verified in `src/engine/src/UiServer.cpp` `Start()`.
- [x] 2.2 `Subscribe` handler verifies prefix non-empty and
  `CurrentGeneration() != 0` before writing `SubscribeAck`; on violation it
  fails closed (no unusable ack, connection closed) and appends
  `FFDIAG_SNAPSHOT_SUBSCRIBE_INVALID` with state
  `subscribe-without-valid-snapshot`. — complete; `UiServer.cpp`
  `HandleConnection`, new diagnostic code 36.
- [x] 2.3 No client-side validation weakened: `IsSnapshotGenerationUsable`,
  `ReadSnapshotWithGeneration`, and `NewGeneration` zero-guards unchanged. —
  complete; `src/ui/src/EngineClient.cpp` untouched.

## 3. EngineClient ↔ UiServer handshake test

- [x] 3.1 New `ffengine_client_handshake_tests` target: fresh `UiServer` +
  fresh `EngineClient` over the real per-session control pipe and real
  shared-memory sections; asserts `Connected`, nonzero startup generation,
  empty initial snapshot via `ReadSnapshotWithGeneration`, then a fixture
  directory request advances the generation by one and the fixture file is
  present. Runs unprivileged with no service, no signing, no persistent
  index, no UI. — complete; `tests/engine/test_engine_client_handshake.cpp`.

## 4. Subscribe-before-any-directory regression

- [x] 4.1 `ffengine_ui_server_lifecycle_tests` gains a real-pipe regression:
  start server → connect → `Subscribe` → parse `SubscribeAck` → assert
  nonzero generation → map/validate/parse the acked generation with
  EngineClient semantics → assert empty snapshot → only then
  `RequestDirectory` → assert `NewGeneration == ack + 1` and the directory
  present. This reproduces the exact previously-broken ordering. —
  complete; `TestSubscribeBeforeAnyDirectory`.

## 5. Multi-client startup

- [x] 5.1 Two clients subscribing before any directory publication receive
  the same current generation, map and parse the same empty snapshot
  simultaneously, and both receive the next `NewGeneration` after one
  client's directory request. — complete; `TestMultiClientSubscribeBeforePublication`.

## 6. Startup stress

- [x] 6.1 100 rapid server start → connect → subscribe → map → parse →
  disconnect → stop cycles with unique per-iteration session epochs; no
  generation-0 ack, no `ERROR_INVALID_DATA`, no missing-mapping race, no
  leaked named-mapping collision, no sleeps used to force ordering. —
  complete; `TestStartupSubscribeStress`.

## 7. Snapshot publisher startup coverage

- [x] 7.1 Startup before any publication: nonzero generation,
  `CurrentSectionName()` opens read-only, header version current,
  header generation == current, payload size == canonical empty snapshot
  size, `ParseSnapshot` succeeds, parsed map empty, generation immutable
  until publish, non-empty publish advances by exactly one, restart yields a
  fresh epoch/prefix with a usable initial generation, failed later publish
  preserves the prior generation, stale prior-generation reader stays safe
  across publication. — complete; `TestPublisherStartupEmptySnapshot` and
  updates to `TestPublisherUsesImmutableGenerations` /
  `TestPublisherUnderThreadContention` /
  `TestCaseVariantSnapshotKeysCoalesce` (relational generations via
  `kFirstGeneration`).

## 8. Failure injection

- [x] 8.1 `FF_SNAPSHOT_PUBLISHER_TESTING`-guarded
  `SetCreateFileMappingHook`/`SetMapViewOfFileHook` seams in
  `SnapshotPublisher` (convention mirrors `FF_DIRECTORY_WATCHER_TESTING`);
  production builds contain no injection surface. — complete;
  `SnapshotPublisher.h/.cpp`.
- [x] 8.2 Initial publication failure makes `SnapshotPublisher::Start`
  false with full rollback (zero sentinel, empty prefix) and makes
  `UiServer::Start` false with no usable control pipe and no surviving
  snapshot mapping. — complete; `TestPublisherStartFailureInjection`,
  `TestUiServerStartFailsOnInitialPublicationFailure`; listener-start
  failure rollback remains covered by the existing
  `TestFailedStartRollsBackSnapshot`.

## 9. Diagnostics

- [x] 9.1 Add `FFDIAG_SNAPSHOT_SUBSCRIBE_INVALID = 36` with its
  `DiagnosticCodeName`; metadata-only (no content fields). — complete;
  `src/protocol/include/ffprotocol/Diagnostics.h`,
  `src/protocol/src/Diagnostics.cpp`.

## 10. Validation and acceptance

- [x] 10.1 Debug CTest: 61/61 pass (60 baseline + `ffengine_client_handshake_tests`).
- [x] 10.2 Release-test CTest passes.
- [x] 10.3 `analyze` preset builds warning-clean.
- [x] 10.4 `pwsh ./verify/verify.ps1 build` and `doctor` pass.
- [x] 10.5 `check_source_markers.ps1`, `check_diagnostic_policy.ps1`,
  `check_doc_drift.ps1`, OpenSpec strict validation, `git diff --check`
  pass.
- [x] 10.6 Live development-tree launch: fresh engine, UI reaches
  Connected/degraded, diagnostics show `engine-launched` and
  `engine-connect-succeeded` (not `engine-connect-snapshot-map-failed`),
  Column View populates from real directory data, search runs against the
  snapshot. — evidence in `evidence/reproduction-and-fix-validation.md`.
- [x] 10.7 Semantic UIA E2E re-run: `engine-connect-unavailable` SKIPs
  eliminated where the engine path is required; remaining SKIPs limited to
  out-of-scope (treemap, privileged service, service toggle, independent
  host prerequisites). — evidence in `evidence/reproduction-and-fix-validation.md`.
