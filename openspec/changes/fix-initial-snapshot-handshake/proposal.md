# Fix Initial Snapshot Handshake (engine-connect-snapshot-map-failed)

## Why

Fresh FastFiles UI ↔ FastFilesEngine startup fails deterministically with:

    engine-connect-snapshot-map-failed
    error 0xd / ERROR_INVALID_DATA

and the UI parks in `EngineConnectionState::Unavailable` after its bounded
retry cycle, which the semantic-UIA E2E recorded as `engine-connect-
unavailable` SKIPs (12 of 13 SKIPs in the 2026-08-15 run). The previous
session classified this as a "pre-existing host environment blocker" because
it reproduced on the unmodified baseline. Repository inspection shows it is a
deterministic product startup/protocol defect, not environmental:

1. `SnapshotPublisher::Start()` commits generation 0 (a zero-payload mapping
   named `<prefix>.0`) and leaves `CurrentGeneration() == 0`.
2. `UiServer` is deliberately started before the durable index rebuild or any
   guaranteed directory publication (see `src/engine/src/Main.cpp`), so the
   control pipe is reachable while `CurrentGeneration() == 0`.
3. `UiServer`'s `Subscribe` handler acknowledges with
   `SubscribeAck{ generationId = CurrentGeneration() }` — a fresh UI
   subscribing before its first `RequestDirectory` (or before any index
   rebuild publishes) is offered `generationId == 0`.
4. `EngineClient` validates strictly and by design treats generation zero as
   "no committed user-readable generation": `IsSnapshotGenerationUsable`
   rejects `expectedGeneration == 0` with `ERROR_INVALID_DATA` (0xd), so
   `engine-connect-snapshot-map-failed` fires and the whole connect attempt
   fails.

Pre-fix reproduction evidence is captured in `evidence/reproduction-and-fix-
validation.md`: the failing `SubscribeAck` carries `generationId == 0`.

The fix makes every successful `UiServer` startup expose a valid, parseable
snapshot generation immediately — a canonical **empty** snapshot — so the
existing strict reader contract is met by the publisher instead of being
weakened. This unlocks the live engine-connected UIA scenarios
(`column-items-exposed-to-uia`, real search results, drag/drop, selection and
scroll against real data) that were blocked by `engine-connect-unavailable`.

## What Changes

- `SnapshotPublisher::Start()` publishes a canonical empty snapshot (zero
  directories, non-empty 4-byte payload in the ordinary serialized format) as
  its first committed generation instead of committing generation 0.
  Generation 0 remains the "no committed generation" sentinel and is never
  published. First content `Publish()` therefore returns 2 on a fresh
  publisher, not 1; generation IDs are identity values, not user-visible
  ordinals.
- `UiServer`'s `Subscribe` handler fails closed (closes the connection,
  emits a stage-specific diagnostic `subscribe-without-valid-snapshot`,
  code `FFDIAG_SNAPSHOT_SUBSCRIBE_INVALID`) if it would ever have to
  acknowledge a subscription without a committed nonzero generation — an
  internal invariant violation that normal operation cannot reach.
- `UiServer::Start` ordering is unchanged and already correct: snapshot
  epoch/prefix → first valid committed generation → security descriptor →
  listener. The invariant "listener reachable ⇒ current generation ≥ 1,
  mapping exists, payload parses" now holds by construction.
- No wire structure changes. `SubscribeAck`, `NewGeneration`, and the
  snapshot header/payload formats are untouched; no version bump.
- Tests: startup empty-snapshot coverage in `ffengine_snapshot_publisher_tests`
  (including failure-injection seams for initial publication failure),
  a subscribe-before-any-directory regression and multi-client and 100-cycle
  startup stress coverage in `ffengine_ui_server_lifecycle_tests`, and a new
  true `EngineClient` ↔ `UiServer` end-to-end handshake test
  (`ffengine_client_handshake_tests`).

## Non-Goals

- Treemap UIA (next UI accessibility slice; out of scope here).
- Privileged `FastFilesIndexSvc` production validation, signing, or trust-store
  work (unprivileged-only change).
- Weakening any client-side snapshot validation.
- Changing the SubscribeAck/NewGeneration wire contract.

## Open Questions

None — the startup-generation invariant is unambiguous: a reachable engine
must always be able to acknowledge a subscription with a usable snapshot.
