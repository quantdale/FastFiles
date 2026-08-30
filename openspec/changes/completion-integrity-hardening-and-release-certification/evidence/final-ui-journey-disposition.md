# Final UI and runtime journey disposition

Date: 2026-08-30

This record resolves the unchecked UI/UX tasks without converting partial
coverage into a product PASS. The implementation baseline used for the local
evidence is `0ad79a8caeab30f6c129c0f285279d1508b62c48`; the most recently
observed repository tip before this freeze pass was
`d1e9c0ba2875419cd632964aacb3dc9849f2d54f`. Those are immutable evidence
identifiers, not a claim about the live SHA after this record is committed.

The local component seams are exercised and no new locally reproducible P0/P1
was found. The remaining acceptance gap is the composition of those seams in a
single stable, foreground-controlled live matrix. That gap is recorded as
explicit mixed local/external disposition in the lifecycle manifest.

## 4.5.1 — complete major journey

### Executed locally

- Home activation and Home/filesystem identity are covered by the navigation
  and Home headless tests and by the 14-capture visual catalog, including the
  `home-default`, `home-wide`, `home-from-command`, `home-narrow`, and
  `home-dark` captures.
- Browse, deep navigation, tabs, and dual-pane transitions are covered by the
  navigation/workspace and Column View UIA suites. The isolated
  `selection-model-interactions` run completed 20/20 consecutive clean
  lifecycles, and the visual catalog includes a `dual-pane` capture.
- Search is covered by the headless search suites and the post-fix isolated
  search group, which completed 20/20 clean lifecycles with one stable
  all-PASS scenario set.
- Preview/properties, file operations, Settings, and Storage have their own
  headless production seams and focused/full CTest coverage; Settings and
  Storage are also represented in the visual catalog.

### Remaining externally blocked clause

The required single continuous Home-to-Settings/Storage journey, with every
transition executed in one foreground-owned live session and its side effects
observed, was not run to a stable terminal result. The current host's
foreground ownership and native/Explorer overlays make that composition
environment-bound. This task therefore remains unchecked with disposition
`mixed`: its local component coverage is complete, while the live composition
criterion is `BLOCKED_ENVIRONMENT` under the `interactive` category.

## 4.5.2 — keyboard/UIA and accessibility matrix

### Executed locally

- Headless UIA/provider, keyboard command, focus, stale-element, selection
  identity, dynamic-event, layout, and accessibility seams are covered by the
  current Debug/release-test suites and the UIA driver fixtures.
- The corrected selection model completed 20/20 isolated lifecycles; the
  search group completed 20/20; and the refreshed visual catalog completed
  14/14 captures with semantic postconditions and redaction checks.
- Empty/error/degraded states and Settings/Storage state membership have
  deterministic headless coverage. Current evidence preserves the explicit
  skips and pointer/foreground observations from the full live capability.

### Remaining externally blocked clause

The complete keyboard/UIA matrix across long names, Unicode, empty
directories/searches, narrow/wide layouts, and physical display variations
was not executed as one stable foreground-owned acceptance run. The retained
live records include environment-owned foreground/pointer boundaries, so this
task remains unchecked with disposition `mixed`: local semantic coverage is
complete, while the remaining interactive matrix is
`BLOCKED_ENVIRONMENT`. No ordinary keyboard, provider-identity, or search
defect is being hidden under this disposition.

## 4.5.3 — availability and recovery matrix

### Executed locally

- Degraded browsing is a first-class path with headless enumerator/watcher,
  Home, status, and Storage coverage; it does not depend on the privileged
  service.
- Indexed snapshot publication/search refresh, unavailable/partial aggregate
  states, and engine reconnect/retry seams are covered by the engine,
  snapshot, search, and recovery tests. The lost-engine reconnect evidence
  records the UI status fan-out and replacement Engine lifecycle.
- Durable failure/retry and close/open recovery are directly covered by the
  rule-pruning and cold-reload regressions, including durable/projection
  parity after restart.

### Remaining externally blocked clause

The full live transition matrix that toggles unavailable/offline/indexed and
privileged service states, then observes reconnect and restart in a clean
foreground/elevated environment, remains unavailable. In particular, the
production-signed authenticated service and real MFT/USN path are external
prerequisites; the local degraded/indexed/recovery behavior is not being
reclassified as incomplete. This task remains unchecked with disposition
`mixed`, categories `local`, `interactive`, `clean-host`, and `elevated`, and
status `BLOCKED_ENVIRONMENT`.

## Evidence references

- `openspec/changes/completion-integrity-hardening-and-release-certification/evidence/uia-live-determinism-root-cause.md`
- `openspec/changes/completion-integrity-hardening-and-release-certification/evidence/uia-search-20run-postfix.jsonl`
- `openspec/changes/final-product-completion-and-production-readiness/evidence/visual-catalog/CATALOG.md`
- `openspec/changes/final-product-completion-and-production-readiness/evidence/visual-catalog/summary.md`
- `openspec/changes/interactive-ui-runtime-determinism/evidence/implementation-and-validation.md`
- `openspec/changes/recovery-degraded-browsing/evidence/lost-engine-reconnect.md`
- `docs/agent/WORKSTATE.md`
