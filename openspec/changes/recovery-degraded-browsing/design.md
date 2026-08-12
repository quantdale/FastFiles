# Design — Recover Degraded-Mode Browsing

## Context

The forensic audit (see `proposal.md`) found the development build unusable: the UI never reaches the engine (same-directory launch assumption vs. sibling directories in the build tree), retries silently forever, and several failure modes (snapshot publish failure, watcher overflow, overclaimed search scopes) are invisible to the user. This change is strictly the stabilization work — no privileged-path, security-model, or architecture changes.

## Decisions

### D1 — Engine discovery is derived, ordered, and validated

Candidates in priority order:
1. UI executable's own directory (installed layout: all executables staged together under the ACL-locked install dir).
2. Sibling `engine` directory of the UI executable's parent, normalized via `GetFullPathNameW` (development layout: `build/<config>/src/ui` → `build/<config>/src/engine`).
3. `%ProgramFiles%\FastFiles` (installer layout).

Candidates are deduplicated case-insensitively. A candidate is launched only if it exists as a regular file (`IsValidEngineExecutable`).

Why: covers every layout the project actually produces, deterministically, without configuration and without any unsafe search strategy (no PATH lookup, no disk walk — a rogue binary in an arbitrary directory can never be picked up). Trade-off: a future layout needs a new candidate entry; layouts are a build/install concern the CMake and installer control, so this is acceptable.

### D2 — Bounded connection lifecycle with explicit terminal state

`EngineClient` models `Starting → Connected → (disconnect) → Starting… → Unavailable`. Each cycle allows at most 5 attempts at the existing 2 s cadence; after that the management thread parks on a retry condition variable — it never silently loops. `RetryConnect()` (badge click) resets the cycle; `Stop()` wakes the wait.

Why: the previous infinite loop produced a functional-looking app with a dead backend; the failure must be terminal-and-visible rather than endless-and-hidden. Trade-off: the UI no longer auto-heals a permanently missing engine — recovery is one click, which is the correct trade for honesty over silent churn.

### D3 — Status chip carries the connection state

The existing paint-only chip (`ColumnView::Render`) becomes state-aware and, for the `Unavailable` state only, clickable (retry). Texts: `Starting engine…` / `Instant search: enabled` / `Instant search: basic (degraded mode)` / `Engine unavailable — click to retry`.

Why: one small, already-visible surface; no new UI chrome. Note: `modernize-ui-appearance` task 3.6 deliberately kept the chip non-interactive because its "click to enable" text was not actually clickable; this change makes the chip interactive only where it genuinely offers an action.

### D4 — Publish failure is a protocol-visible error

`UiServer::RepublishAndBroadcastGeneration` reports success/failure. `HandleRequestDirectory` answers a publish failure with the new additive `DirectoryErrorReason::PublishFailed` so the requesting column renders a clear error instead of stale data. Other publish triggers (watch updates, index merges, volume removal) keep logging-only behavior — there is no single requesting client to answer.

Why: "do not silently display stale data" without redesigning the snapshot architecture (which is a later change). Trade-off: `PublishFailed` is a new value on the closed UI-seam enum; it is additive, both peers ship together, and the UI handles it explicitly.

### D5 — Watcher set is bounded; overflow is recovered

Cap of 32 concurrently watched roots with least-recently-watched eviction (a `Watch` of an already-watched path refreshes its recency, so actively browsed paths are never the victim). `ERROR_NOTIFY_ENUM_DIR` (change-buffer overflow) triggers: diagnostics log entry, re-enumeration via the change callback (fresh generation), and watch re-arm; after 8 consecutive overflows the watch stops with a log entry.

Why: typical browsing must neither accumulate watches without bound nor silently lose synchronization.

### D6 — Search scopes describe what search has

Scope items are coverage-labelled: degraded mode uses `Current Folder` / `Current Folder + Browsed Subfolders` / `Current Drive — Browsed Folders Only` / `All Browsed Locations`; persistent indexed coverage uses corresponding indexed labels. Every degraded scope is a finite browsed-snapshot query, so it remains available while the index service is disconnected. The UI reports the fallback source instead of silently rewriting the selected scope, and no label implies whole-drive or all-indexed coverage without that coverage.

Why: honest coverage claims while preserving useful degraded search. The indexed path remains separately identified and can use persistent-only filtering; a later change can broaden or narrow coverage only with corresponding data and labels.
