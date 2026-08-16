# Stabilize Live UIA E2E

## Why

The live `windows-ui-automation-validation` capability (zero-touch task 3.x)
runs 27 scenarios inside ONE long-lived FastFiles process/session. After the
`fix-initial-snapshot-handshake` fix, `engine-connect-unavailable` was
eliminated and the capability records **15 PASS / 4 FAIL / 8 SKIP**. The four
remaining unexpected FAILs plus the late `in-column-error-state` scenario are
classified as harness/interaction-layer problems:

| Scenario | Failure mode |
|---|---|
| `search-deep-result-navigation` | Search result Invoke succeeds and the engine receives the containing-folder requests, but the post-activation UIA verification stalls and cannot confirm the landing Column View item |
| `drag-out-to-explorer` | Late-session interaction/navigation state prevents deterministic source setup/item lookup |
| `drag-in-from-explorer` | Explorer target/source window discovery is unreliable (title matching, broker-process reuse) |
| `drag-between-panes` | Deterministic discovery/setup of `Pane.1` fails |
| `in-column-error-state-late` | Subject to the same late-session keyboard/focus/navigation problem as the drag scenarios |

The drag scenarios execute after the search-panel scenarios in the shared
session; the historical record already notes that address-bar navigations are
"not reliably typed after the search panel has been opened once". This change
makes the harness deterministic by construction:

1. **Scenario lifecycle isolation** — the interaction-heavy scenarios run in
   fresh, isolated FastFiles lifecycles with deterministic fixtures, so
   scenario N never depends on scenario N-1 having cleaned up perfectly.
2. **Semantic waits** — bounded condition waits with rich timeout
   diagnostics replace sleep-then-search sequences.
3. **Targeted semantic queries** — post-activation verification and item
   lookups use ItemContainerPattern / pane-column identity instead of
   unbounded whole-window DFS walks.
4. **Structural Explorer discovery** — the test-owned Explorer window is
   identified by class + UIA address-bar semantics + expected fixture path,
   never by title heuristics alone; session-owned Explorer windows are
   closed, never shell-killed.
5. **Semantic dual-pane setup** — `Pane.1` is awaited through the real
   product command with reacquisition and identity validation.
6. **Direct scenario filtering** — a single scenario or group can be run
   directly through the normal capability options mechanism for exact
   reproduction and regression testing (and for the upcoming Treemap UIA
   change).

This is a harness change. No product source is expected to change unless a
real product defect surfaces; per the project rule, a proven product bug is
reclassified with evidence rather than hidden by the harness.

## What Changes

- `verify/verify.ps1`: a `-CapabilityOption` parameter merges key=value
  options into the capability `$Options` hashtable (normal capability-options
  mechanism; no private scripts).
- `verify/capabilities/windows-ui-automation-validation/UiAutomationValidation.psm1`:
  - scenario registry + `-Scenario <id|group>` filtering (`$Options.UiaScenario`);
  - `ScenarioContext` lifecycle (fixture → fresh FastFiles launch → ready
    await → fresh UIA root → execute → evidence → clean app exit → close
    session-owned Explorer → remove fixture);
  - isolated lifecycles for `search-deep-result-navigation`,
    `drag-out-to-explorer`, `drag-in-from-explorer`, `drag-between-panes`,
    `in-column-error-state-late`;
  - targeted post-activation verification (terminal column via
    ItemContainerPattern) with provider reacquisition;
  - ValuePattern-based address-bar assignment with `GetValue` verification
    and Enter commit (SendInput fallback retained and recorded);
  - semantic Explorer discovery/ready contract;
  - `Ensure-UiaDualPane` semantic dual-pane setup;
  - explicit pre-scenario clean-interaction-state assertion (search panel
    closed, no popup, foreground owned) with a diagnostic helper enumerating
    popup/owned windows, foreground HWND, and focused element;
  - modifier-key reset seam before interaction-heavy scenarios;
  - rich timeout diagnostics collected by the wait primitive (foreground
    HWND, focus identity, tree excerpt, popup state, last exception).
- `verify/uia-driver/UiaDriver.psm1` + `internal/*`: extend `Wait-UiaCondition`
  with optional timeout diagnostics; add small first-class helpers only where
  the capability would otherwise duplicate them (interactive-state
  enumeration, modifier reset, Explorer-window discovery support).
- `verify/uia-driver/tests/run-tests.ps1`: headless tests for every new
  driver helper (bounded wait success/timeout/diagnostics, ValuePattern
  assignment, modifier reset, Explorer-window filtering, interactive-state
  checks, drag input sequence via `-NoSend`).
- New evidence under `evidence/` recording the pre-fix reproduction, the
  isolation experiments (A–E), the 10-run repeatability gate, order
  permutations, and the final full-run matrix. The historical 15/4/8 result
  is preserved in the existing change evidence and re-referenced, never
  rewritten.

## Non-Goals

- Treemap semantic UIA (next change; this change only leaves the harness
  ready for it).
- Privileged `FastFilesIndexSvc` / mid-scan / service-toggle validation,
  production signing, trust-store work, or clean-host validation.
- Changing the FastFiles product's UIA provider surface (unless a real
  product defect is proven, in which case it is reclassified and fixed only
  if small and directly required).
- Pixel/OCR/image-based automation, hidden test controls, or private test
  IPC.
- Rewriting old evidence as though it passed.

## Open Questions

None blocking. The historical evidence records the four FAILs as harness
issues. During this change, two of them were proven to be genuine product
defects (reclassified, fixed product-side, with focused regressions):

- `search-deep-result-navigation`: search-result activation ran on a
  UIAutomationCore worker thread and rebuilt the tab strip there, orphaning
  tab buttons that the UI thread can never destroy. Fixed with thread
  marshaling of the activation (`WM_APP_SEARCH_RESULT_ACTIVATE`).
- `drag-between-panes`: the drop destination resolved to the ACTIVE pane
  (which the drag's own mouse-down activates), and the screen→client
  conversion dropped the ClientToScreen client-origin term, routing a
  pane-1 drop on a right-positioned window back to the source pane. Fixed
  with drop-point pane resolution threaded through the drop target, an
  exact conversion inversion, and the drop-point destination carried into
  the executed operation.

The remaining two scenarios carry documented independent prerequisites
(host Explorer DUIListView cannot start drags from synthetic input; the
in-column stale-with-error wording is only reachable on a refresh race).
