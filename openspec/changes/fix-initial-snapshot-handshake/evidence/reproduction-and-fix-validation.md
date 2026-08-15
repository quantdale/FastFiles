# Evidence — reproduction, fix, and validation

## Pre-fix reproduction (unmodified baseline, this session)

The regression `TestSubscribeBeforeAnyDirectory` (added first, run against
the unmodified code) reproduced the exact production failure path through the
real control pipe:

```
subscribe-first: acked generation 0, prefix length 74
FAIL: SubscribeAck never advertises generation zero
FAIL: the startup generation maps, validates, and parses with EngineClient semantics
FAIL: both clients are acked with a nonzero generation
FAIL: both clients map and parse the startup generation simultaneously
FAIL: both clients parse the same empty startup snapshot
FAIL: 100 rapid startup/subscription cycles produce no generation-0 ack, ...
exit code 1
```

- **SubscribeAck generation**: `0`
- **Section prefix**: `Local\FastFiles.IndexSnapshot.<session>.<GUID>` (74
  chars), no generation suffix — the client appended `.0`.
- **Exact failure**: `OpenFileMappingW(prefix + ".0")` succeeded (the
  mapping existed), `MapViewOfFile` succeeded, then
  `IsSnapshotGenerationUsable(view, expectedGeneration = 0)` rejected
  `expectedGeneration == 0` and `SetLastError(ERROR_INVALID_DATA)` → the
  UI recorded `engine-connect-snapshot-map-failed` with error `0xd` and,
  after the bounded 5-attempt cycle, `engine-unavailable`.
- **Diagnostic state**: `FFDIAG_ENGINE_CONNECT_FAILED`,
  state `engine-connect-snapshot-map-failed`, error `0xd`; engine-side
  startup had produced no `engine-connect-succeeded`.

**Hypothesis confirmed**: the failing SubscribeAck contained
`generationId == 0`, and `EngineClient` rejected it because generation 0 is
the invalid/uncommitted sentinel. This is a product startup defect
(`SnapshotPublisher::Start` committing generation 0 while `UiServer` was
already reachable), not a host-environment condition.

## Fix

- `SnapshotPublisher::Start` commits a canonical empty snapshot (`[u32 0]`,
  `SerializedSnapshotSize({}) == 4` bytes) as generation
  `kFirstGeneration == 1`; the generation-0 mapping creation is removed.
  Generation 0 remains the uncommitted sentinel on both sides; no client
  check was weakened.
- `UiServer`'s `Subscribe` handler fails closed (no unusable ack, connection
  closed, `FFDIAG_SNAPSHOT_SUBSCRIBE_INVALID` /
  `subscribe-without-valid-snapshot`) if no committed nonzero generation
  exists — unreachable in normal operation.
- Wire structures unchanged: `SubscribeAckPayload`,
  `NewGenerationPayload`, `SnapshotSharedHeader`, and the snapshot payload
  format are byte-identical; no version bump.

## Post-fix test evidence (Debug CTest, this host)

`ffengine_snapshot_publisher_tests` (additions):
- startup: `CurrentGeneration() == 1`, mapping opens read-only, header
  version current, `header.generation == 1`, payload size `== 4` (canonical
  empty), `ParseSnapshot` → empty map, immutable until publish; first
  content publish → `2`; failed oversized publish preserves `2`; restart →
  fresh prefix, usable generation 1 again; stale prior-generation reader
  stays safe across publication; `CreateFileMappingW`/`MapViewOfFile`
  injection → `Start` false with full rollback.

`ffengine_ui_server_lifecycle_tests` (additions):
- subscribe before any directory: acked generation `1` (post-fix),
  validation + parse of the empty snapshot succeeds, first
  `RequestDirectory` → `NewGeneration == 2` containing the fixture
  directory.
- multi-client: two clients acked with the same generation, both map/parse
  the empty snapshot, both receive `NewGeneration == ack + 1`.
- stress: 100 rapid start → connect → subscribe → map → parse → disconnect
  → stop cycles with unique epochs — zero failures, no sleeps.
- failure injection: initial publication failure → `UiServer::Start` false,
  pipe unreachable, snapshot state rolled back.

`ffengine_client_handshake_tests` (new target):
- fresh `UiServer` + fresh `EngineClient` over the real pipe/sections:
  `Connected` reached, `ReadSnapshotWithGeneration()` returns generation 1
  with an empty map, `RequestDirectory(fixture)` → generation 2 containing
  the fixture file. No Administrator, no service, no signing, no persistent
  index.

## Validation matrix (exact commands and outcomes)

- `ctest --preset debug --output-on-failure` → 61/61 passed (60 baseline +
  `ffengine_client_handshake_tests`), all three pwsh-gated entries ran.
- `ctest --preset release-test --output-on-failure` → 61/61 passed.
- `cmake --preset analyze && cmake --build --preset analyze` → warning-clean
  (/analyze, /W4 /WX).
- `pwsh ./verify/verify.ps1 build` and `pwsh ./verify/verify.ps1 doctor`
  → PASS (see below).
- `check_source_markers.ps1`, `check_diagnostic_policy.ps1`,
  `ffdoc_drift_tests` (pwsh-gated), OpenSpec strict validation,
  `git diff --check` → PASS.
- asan: snapshot/protocol parsing untouched (only the diagnostics name table
  gained a code); the pre-existing asan `ffprotocol_fuzz_tests` coverage
  still applies.

## Live runtime (development tree)

Fresh Debug launch from the build tree (engine not running beforehand, no
stale shared memory): the UI launched the sibling engine
(`FFDIAG_ENGINE_LAUNCHED` / `engine-launched`), then
`FFDIAG_ENGINE_CONNECT_SUCCEEDED` / `engine-connect-succeeded` —
**zero** `engine-connect-snapshot-map-failed` events. Engine state Connected
in degraded mode (`privilegedPathActive == false`, the privileged path
reports `disconnected/unverified-service-identity` on this development host,
as expected with placeholder pins). Real directory browsing followed
(hundreds of `ui-snapshot-column-refresh` events with real item counts),
and search returned real results from the browsed snapshot.

## Semantic UIA E2E re-run

`pwsh ./verify/verify.ps1 run -Capability windows-ui-automation-validation`
(final run: `verify/runs/ad-hoc/20260815-144412`): **15 passed, 4 failed,
8 skipped; `engine-connect-unavailable` is eliminated — no scenario carries
that reason anymore.**

Passing with real engine-served data (previously blocked):
- `column-populated-and-selected-item-in-tree` (3.2) — real fixture item
  discoverable as ListItem.
- `selection-and-scroll-patterns` (3.3) — SelectionItem select +
  Ctrl+click multi-select count=2 against real items.
- `scroll-pattern-real-viewport` (3.3) — real viewport scrolled to 100%.
- `search-results-discoverable` / `search-structured-filter-query`
  (3.11) — `alpha.txt` exposed in `Search.Results`; `ext:txt` returned
  173 results, all `.txt`.
- All non-data scenarios (launch, settings, keyboard nav, mouse tabs, Home,
  storage analysis, badge, responsive, geometry, evidence).

Remaining FAILs are harness-interaction issues in the E2E driver/capability
layer, not product defects (the engine data path is proven by the passing
scenarios above):
- `search-deep-result-navigation` — the result activation navigates (the
  engine receives the containing-folder requests) but the post-activation
  UIA tree walk stalls and cannot confirm the landing column item.
- `drag-out-to-explorer` / `drag-in-from-explorer` / `drag-between-panes`
  — the drag fixtures' address-bar navigations are not reliably typed after
  the search panel has been opened once (a keystroke-focus/popup
  interaction issue in the driver layer); the drag scenarios run and fail
  honestly with recorded reasons rather than skipping.
- `in-column-error-state-late` — the missing-path navigation is subject to
  the same late-session keystroke issue, so the error wording is not
  exercised live (the provider surface remains covered by the headless
  status-element tests and the engine-less early phase).

Remaining SKIPs (legitimate, out of scope): `treemap-readability-click-precision`
(treemap UIA follow-up), `storage-mid-scan-calculating` and
`search-toggle-service-while-open` (privileged path unverified in the dev
tree), `in-column-error-state` early phase on engine-connected hosts
(engine-connected-host), and the early engine-less check where applicable.

Harness fixes shipped with this change (uia-driver + capability): real-pattern
mock detection in `Invoke-UiaPattern`, pattern-scoped UIA property ids
(`ScrollPattern` fields) and `SelectionPattern.Current.GetSelection()` in
`Get-UiaElementProperty`/`Invoke-UiaPattern`, fixture root moved from
`%TEMP%` (a ~1800-entry column exceeds the driver's 2000-node walk) to the
user profile, all-tabs navigation label check, select-all-before-typing
address-bar hardening, search-panel ESC close (posted directly to the main
window — an open combo dropdown popup swallows SendInput), horizontal
pane-scroll-to-100% before item clicks, Explorer-window title fallback,
run-level engine-data memory flag, badge-based engine-connectivity probe.

## Performance

Initial empty snapshot: 24-byte header + 4-byte payload = 28-byte mapping;
one extra serialization of a zero-entry map at startup (microseconds); no
background thread, no polling, no steady-state cost. Measured on this host:
the 100-cycle startup/subscription stress test (full server start → pipe
connect → subscribe → map → parse → stop) completes in ~5.2 s total
(~50 ms/cycle, dominated by pipe setup, not the snapshot); the publisher
test suite runs in ~1.1 s.
