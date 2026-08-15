# Design — Fix Initial Snapshot Handshake

## Context

`SnapshotPublisher` (design D3 of `establish-architecture-foundation`;
`index-storage-and-scanning` task 4.7/4.8) publishes immutable, generation-
named shared-memory sections. `UiServer` starts the publisher, then exposes
the control pipe. `EngineClient` subscribes, maps the acknowledged generation,
and validates it strictly.

The defect: a fresh publisher commits generation 0 as its "current" state and
the server is reachable before anything has published content, so a fresh UI
is acknowledged with `generationId == 0`, which every reader rejects by
design. The engine violates the reader's contract rather than the reader
being wrong.

## D1 — Generation zero is the "no committed generation" sentinel

- Generation 0 SHALL remain the failure/uninitialized sentinel on both sides.
  `EngineClient` keeps rejecting `expectedGeneration == 0`; `NewGeneration`
  with `generationId == 0` stays ignored; `SnapshotFreshness` keeps its
  zero-generation guards.
- Why: zero is already load-bearing as "invalid/not-yet-committed" in
  `IsSnapshotGenerationUsable`, `ReadSnapshotWithGeneration`,
  `NewGenerationPayload` handling, `SnapshotFreshness`, and `Publish`
  failure returns. Redefining it as a valid generation would weaken
  publication semantics to save a number.
- Accepted trade-off: `Publish()` return of 0 is overloaded (failure and the
  sentinel); that ambiguity is unchanged by this design.

## D2 — Successful Start commits a canonical empty snapshot as generation 1

- `SnapshotPublisher::Start()` ends by constructing the real serialized empty
  snapshot — `[u32 0]`, four bytes, `SerializedSnapshotSize({}) == 4` — and
  creating mapping `<prefix>.1` with `header.generation == 1`, then stores
  `generation_ = kFirstGeneration (1)`.
- Why in the publisher and not in `UiServer::Start`: the publisher owns the
  section lifecycle and every consumer's invariant is "a started publisher
  has a usable current generation"; placing the publication in the server
  would leave the publisher itself in an invalid state for any direct user
  (benchmarks, future consumers) and would require the server to know the
  publisher's internal "empty" representation. The publisher's `Start`
  contract is now self-contained: success ⇒ current generation ≥ 1, mapping
  exists, payload parses to zero directories.
- The previous generation-0 mapping creation is removed: nothing ever mapped
  `<prefix>.0` (readers reject generation 0), and the new mapping serves the
  same early validation purpose (prefix mappable) with real content.
- Empty-snapshot representation: the ordinary serializer output for a
  zero-entry map. No special wire form, no dummy entry, no zero-length
  payload, no generation-0 parse exception.

## D3 — UiServer start ordering and the startup invariant

`UiServer::Start` ordering stays: (1) establish snapshot publisher
epoch/prefix; (2) commit the first valid generation (now guaranteed by
`SnapshotPublisher::Start`); (3) configure the security descriptor; (4)
expose the listener.

Invariant: if `UiServer::Start(sessionId) == true`, then before the listener
can acknowledge any `Subscribe` request the current generation is ≥ 1, the
generation mapping exists, `SnapshotSharedHeader` is compatible,
`header.generation == acknowledged generation`, the payload parses, and the
parsed snapshot may legitimately contain zero directories.

Rollback on any Start failure closes mappings, views, listener state,
security-descriptor state, and connection-gate state — no half-started
server. This is unchanged code-wise; the fix only removes the invalid
transitional state ("listener reachable, no mappable snapshot") that existed
before.

## D4 — SubscribeAck contract

- Before writing `SubscribeAck`, `UiServer` verifies the section prefix is
  non-empty and `CurrentGeneration() != 0`. If the invariant is violated (an
  internal bug — unreachable in normal operation), the server fails closed:
  it does not send an unusable ack, closes the connection, and appends
  `FFDIAG_SNAPSHOT_SUBSCRIBE_INVALID` with state
  `subscribe-without-valid-snapshot`.
- Why: an internal regression must surface as a stage-specific diagnostic,
  not as an infinite client reconnect loop over an advertised invalid
  generation.
- The wire contract of `SubscribeAck` is unchanged — only its producer-side
  invariant is corrected, so no version bump.

## D5 — Client contract unchanged

`EngineClient` validation stays strict: format compatible, generation
matches expected, generation nonzero, payload within the explicit bound,
mapped region large enough, parser succeeds. The fix makes the engine meet
the existing reader contract; no reader check is weakened.

## D6 — Generation numbering

- New numbering: `Start` → usable empty generation 1; first content
  `Publish()` → generation 2.
- Generation IDs are synchronization/identity values, not user-visible
  ordinal counts; nothing semantically requires "first content = 1".
- Tests assert relational invariants (`initial > 0`, `next == initial + 1`)
  via the exported `kFirstGeneration` constant where an exact value is the
  publisher's own contract, rather than hardcoding 1 for the first content
  publish.

## D7 — Failure handling

- Initial publication failure (`CreateFileMappingW` / `MapViewOfFile` /
  serialization): `SnapshotPublisher::Start` returns false with prefix
  cleared and generation at the zero sentinel; `UiServer::Start` fails and
  rolls back. No control pipe is exposed.
- Subsequent publication failure: unchanged — the prior valid generation
  remains current, no invalid `NewGeneration` is broadcast,
  `PublishFailed`/diagnostic behavior intact.
- Test seams: `FF_SNAPSHOT_PUBLISHER_TESTING`-guarded
  `SetCreateFileMappingHook` / `SetMapViewOfFileHook` injection points in
  `SnapshotPublisher` (same convention as `FF_DIRECTORY_WATCHER_TESTING`),
  compiled only into the test targets that define the macro. Production
  builds contain no injection surface.

## D8 — Multi-client and degraded-mode behavior

- Multiple clients subscribing before any directory publication all receive
  the same current generation, map and parse the same empty snapshot, and
  each receive every subsequent `NewGeneration` broadcast. The shared
  generation is never mutated per client.
- The fix has zero dependency on the privileged service, the durable index
  database, existing index content, or browsed directories: an engine on a
  brand-new empty database with no volumes, no service, and nothing browsed
  still reaches `EngineConnectionState::Connected` with
  `privilegedPathActive == false`. "Engine connected in degraded mode" is
  valid; lack of privileged indexing is not a connection failure.
- Restart produces a fresh GUID epoch/prefix and again commits a usable
  initial empty generation; stale prior-generation readers remain safe
  because generations are immutable and a reader holding an old mapping
  keeps its view.

## Security

- No new protocol surface; the closed command set is unchanged.
- The initial empty snapshot contains no user data (it is zero directories),
  and the new diagnostic is metadata-only (code, state string, error code) —
  no file content, no sensitive raw index data.
- No changes to authentication, signing, ACLs, or the privileged service.

## Risks

- **Risk:** code or docs still assume "first content publish == 1". Mitigated
  by updating the affected tests (contention, restart, case-variant) to
  relational assertions and by the `kFirstGeneration` constant.
- **Risk:** a reader outside the tested set relies on `Start` leaving
  generation 0. Audit (grep of `CurrentGeneration`/`generation` across
  `src/` and `tests/`) found only `UiServer`, the publisher tests, and the
  benchmark (relative usage) — all updated or unaffected.
- **Risk:** stress/multi-client tests flake under load. Mitigated by unique
  per-iteration session epochs, real frame reads with deadlines, and no
  sleeps used to force ordering.
