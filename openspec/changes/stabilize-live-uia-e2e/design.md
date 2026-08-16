# Design — Stabilize Live UIA E2E

## Context

The live capability (`verify/capabilities/windows-ui-automation-validation/
UiAutomationValidation.psm1`, 1121 lines) runs all 27 scenarios in one
long-lived FastFiles process. The provider layer (semantic-uia-core-surfaces)
is proven by the passing data scenarios; the four FAILs all live in the
harness's interaction sequencing:

- `search-deep-result-navigation` — post-activation verification uses
  `Find-UiaColumnItem`, a whole-pane subtree walk executed repeatedly inside
  a 20 s deadline; after activation the panel state, the tree shape, and
  possibly stale providers make this walk non-deterministic.
- drag scenarios + `in-column-error-state-late` — their address-bar
  navigation runs after the search-panel scenarios; keystrokes can be
  swallowed by an open combo dropdown or lost foreground, and Explorer
  discovery relies on `CabinetWClass` + title matching that breaks with
  broker-process reuse and locale-variant titles.

The previous change's evidence (fix-initial-snapshot-handshake §E2E re-run)
documents exactly this hypothesis: "address-bar navigations are not reliably
typed after the search panel has been opened once".

## D1 — Scenario lifecycle isolation (the primary fix)

Each interaction-heavy scenario executes inside a `ScenarioContext` that
owns the full lifecycle:

1. create deterministic fixture (unique names under the user profile, per
   the tree-walk bound rule from fix-initial-snapshot-handshake);
2. launch a fresh `FastFiles.exe` (the launch helper already terminates
   stale FastFiles processes);
3. await ready/connected state via the semantic badge probe, bounded;
4. obtain a fresh UIA root and main-window element for that process;
5. execute the scenario against that root;
6. capture evidence (tree dumps, run log) scoped to the scenario;
7. terminate the app cleanly;
8. close session-owned Explorer windows (never kill the shell);
9. remove the fixture.

The five scenarios with fresh lifecycles:
`search-deep-result-navigation`, `drag-out-to-explorer`,
`drag-in-from-explorer`, `drag-between-panes`, `in-column-error-state-late`.

Non-interaction scenarios keep the shared session; sharing state is only
kept where it is genuinely part of what is tested (settings, home, storage
panels, selection/scroll on a real navigated column).

- Why: the goal is that scenario N must not depend on scenario N-1 having
  cleaned up perfectly. Isolation is a stronger guarantee than any cleanup
  heuristic, and it directly tests the state-leakage hypothesis (experiments
  A–E).
- Accepted trade-off: ~5 extra app launches per full run (~30 s of launch
  cost); this is small next to the minutes the interaction scenarios already
  spend, and it buys order independence.

## D2 — Scenario filtering through the normal options mechanism

`verify.ps1` gains `-CapabilityOption <key=value>` (repeatable). Values are
merged into the `$Options` hashtable passed to every capability
(`verify/core/CapabilityRunner.psm1` already forwards it). The UIA capability
reads:

- `Options.UiaScenario` — single scenario id (`search-deep-result-navigation`)
  or group id (`drag`, `search`, `error-state`);

and runs only the matching scenarios, in isolation when the scenario is an
isolated one. Unknown ids fail with a harness error (exit 3 semantics)
listing valid ids — never a silent no-op.

- Why: exact reproduction, rapid iteration, deterministic regression, and a
  future `-Scenario treemap` for the next change. No private one-off script;
  everything routes through `verify.ps1 run`.

## D3 — Bounded semantic waits with rich timeout diagnostics

`Wait-UiaCondition` already exists (poll with deadline, returns `$false` on
timeout). It gains:

- optional `-OnTimeout` scriptblock invoked on timeout; the capability
  supplies a diagnostic collector that records:
  - current foreground HWND and window title;
  - focused element identity (via UIA `FocusedElement` where available);
  - active/owned/popup top-level windows for the FastFiles PID
    (visible popup-class HWNDs, owned windows);
  - a bounded tree excerpt of the searched root;
  - last exception, elapsed time, and the expected condition description.

New driver helpers (each with headless tests):

- `Wait-UiaElement` / `Wait-UiaElementGone` — existence/disappearance waits
  built on `Find-UiaElement` polling with the same diagnostics;
- `Get-UiaInteractiveState` — foreground HWND, active window, focused UIA
  element identity, per-PID top-level/owned/popup windows;
- `Reset-UiaModifierKeys` — releases Ctrl/Shift/Alt/Win via SendInput
  key-ups, called only after the harness has established FastFiles-owned
  foreground (never injected into unrelated foreground apps);
- `Get-UiaExplorerWindow` — structural Explorer discovery (D5);
- `Set-UiaElementValue` — ValuePattern `SetValue` + `GetValue` verification
  returning which path was used (pattern vs SendInput fallback).

- Why: correctness must not depend primarily on fixed sleep durations;
  sleeps remain only where Windows exposes no semantic completion event, and
  every wait has a bounded deadline and rich failure output.

## D4 — Address-bar navigation via ValuePattern

The address bar is a native Win32 EDIT (control ID 0, hidden until editing)
whose proxy exposes ValuePattern. New sequence:

1. foreground FastFiles and activate the address bar through the normal UI
   (Ctrl+L);
2. identify the address EDIT (ClassName Edit, AutomationId null/'0', process
   filter);
3. `ValuePattern.SetValue(path)` when writable;
4. verify `Value.Value == path` (GetValue);
5. send only Enter as the commit key;
6. wait semantically for the tab label / column state to reflect the path.

`SendInput` typing remains the recorded fallback when ValuePattern is
unavailable or read-only. Test correctness never depends on Ctrl+A keyboard
selection when semantic assignment is available.

## D5 — Explorer window ownership and ready contract

Discovery (structural, never title-matching alone):

1. snapshot the set of `explorer.exe` PIDs before launch;
2. launch `explorer.exe <fixture-path>` for the test-owned window;
3. enumerate `CabinetWClass` top-level windows in the UIA tree;
4. filter by UIA `ProcessId` (new or pre-existing explorer PIDs that were
   alive before launch — broker reuse is normal);
5. confirm semantics: the window's address bar value (the native
   "Address: …" toolbar EDIT exposed by the shell proxy) equals the fixture
   path, case-insensitively; when the address value is not exposed, fall
   back to a bounded descendant search for the expected leaf/fixture name
   combined with the PID filter;
6. bind the harness to exactly that window.

Ready contract — before any drag:

- Explorer top-level window exists;
- expected destination/source folder is loaded (address value match);
- the folder's client area (list view) is exposed with a valid
  BoundingRectangle;
- for source-item drags, the expected item exists semantically
  (ListItem with the fixture name) or the folder client-area rectangle is
  used as the drop destination.

Cleanup: close only session-owned windows (WindowPattern.Close or WM_CLOSE);
the harness never `Stop-Process`es `explorer.exe` — killing the shell would
be a destructive side effect on the user's desktop, and the launched broker
process is not the window owner.

## D6 — Deterministic dual-pane setup

`Ensure-UiaDualPane`:

1. inspect the ColumnView semantic tree;
2. if `Pane.1` exists, return `Pane.0` + `Pane.1` with unique runtime
   identities validated;
3. otherwise invoke the real product command (palette "Toggle Dual Pane");
4. wait semantically until `Pane.1` exists (bounded);
5. reacquire the ColumnView fragment and both panes;
6. validate distinct AutomationIds and runtime identities;
7. fail with diagnostics if pane creation did not occur.

No fixed sleep-then-single-search; no hidden test hook.

## D7 — Targeted post-activation verification (deep search)

After `Invoke` on a search result:

1. capture the search result provider identity before activation;
2. `Invoke-UiaElementAction` (real Invoke pattern);
3. wait for the search surface to close (semantic, bounded);
4. re-acquire the main/root provider if a structural generation changed
   (providers are volatile across structure events by design —
   `UIA_E_ELEMENTNOTAVAILABLE` from a stale element is valid product
   behavior);
5. locate ColumnView → active pane → terminal/current column via
   AutomationId/fragment relationships;
6. use `ItemContainerPattern.FindItemByProperty(Name)` on the terminal
   column to retrieve `alpha.txt`;
7. only fall back to a bounded child walk of that single column;
8. verify the item exists with the expected identity and the terminal path
   is visible.

No unbounded whole-window DFS after activation. The driver treats stale
providers (expected), missing new providers, and traversal timeouts as three
distinct outcomes with distinct diagnostics.

## D8 — In-column error state in its own lifecycle

1. create fixture folder;
2. navigate to it (ValuePattern address bar);
3. delete the folder externally (filesystem);
4. trigger the normal refresh/navigation behavior (re-enter the path);
5. wait semantically for the `ColumnStatusUiaProvider` Text element whose
   Name matches the no-longer-available wording
   ("This folder is no longer available." / "Unable to load…");
6. record PASS with the exact Text element identity, or the honest
   classification if an independent blocker exists.

## D9 — Deterministic drag/drop with filesystem verification

All three drag scenarios verify the filesystem side effect programmatically
and, where applicable, the FastFiles UIA refresh:

- FastFiles → Explorer: semantic source item in ColumnView (bounds valid,
  onscreen, expected selection state); test-owned Explorer destination with
  ready contract; `Send-UiaMouseDrag`; assert `destination\file` exists;
  assert the operation's copy/move semantics on the source.
- Explorer → FastFiles: bound test-owned Explorer source window; semantic
  source item; FastFiles target pane/column bounds; drag; assert the file
  exists; wait for FastFiles directory refresh and assert the file appears
  in ColumnView UIA (proves operation AND UI refresh).
- Pane → pane: dual-pane via D6; pane ownership established explicitly
  before each navigation (never a global address-bar op that silently
  targets the active pane); source item found inside Pane.0 only;
  destination bounds from Pane.1; drag; assert filesystem state; assert UIA
  refresh in Pane.1. The same-index/wrong-pane problem cannot produce a
  false result because both pane identity and item identity are validated.

**Product defect fixed in this change (drop destination routing).** The
product's `FileDropTarget` resolved its destination to `ActivePanePath()`.
A drag's own mouse-down on the source item activates the source pane
(`ColumnView::OnMouseDown` → `ActivatePane`), so every cross-pane drag
executed against the source folder. Two related product fixes:

1. The destination now resolves from the DROP POINT's pane
   (`PaneIndexForDropPoint` + `ColumnView::PanePath`), threaded through
   DragEnter/DragOver/Drop.
2. The screen→client conversion inverts the UIA screen mapping exactly
   (`screen = (dip + originDip) * scale + clientOrigin` via
   `DropPointClientXDip`): the first conversion dropped the
   ClientToScreen client-origin term, so a pane-1 drop point on a window
   that sits more than a quarter-width from the screen edge computed past
   the viewport width, tripping the out-of-range fallback back to the
   ACTIVE (source) pane — observed live as the product's own conflict
   dialog ("The destination already contains 'pane-drag.txt'." with the
   source-folder path).
3. The executed operation receives the SAME drop-point destination as the
   effect negotiation: the handler signature carries the resolved
   destination instead of re-deriving `ActivePanePath()` at execution time.

Regression tests: `ffdrop_destination_tests` pins the pane split, the
conversion, and the pre-fix fallback premise; the live pane→pane scenario
verifies filesystem + UIA refresh.

## D10 — Honest error classification

- A scenario that fails in a fresh isolated lifecycle is a product defect
  candidate: evidence is collected, the finding is reclassified, and it is
  fixed only if small and directly required; the harness never contorts
  around incorrect product behavior.
- **Reclassified finding (this change):** `search-deep-result-navigation`
  exposed a product defect, not a harness problem. `SearchResultUiaProvider::
  Invoke` → `SearchPanel::UiaSearchActivateItem` executed on a
  UIAutomationCore worker thread (thread 49572 vs the UI thread 54328 on the
  evidence host) and the activation chain rebuilt the navigation chrome
  there. `CreateWindowExW` from that thread created tab buttons owned by the
  wrong thread, so the UI thread's `DestroyWindow` (which fails cross-thread)
  could never remove them: a permanent orphaned duplicate tab strip
  (verified live: two overlapping `8300`/`8400` button pairs, same control
  IDs, both `IsWindow == true`) whose UIA enumeration stalls ~20 s per call.
  That stall cascaded into every later whole-window UIA operation — the
  actual mechanism behind the historically recorded "address-bar navigations
  not reliably typed after the search panel has been opened once".
  Product fix (small, directly required, semantically correct for all UIA
  clients): `UiaSearchActivateItem` marshals the activation to the UI thread
  via `PostMessage(WM_APP_SEARCH_RESULT_ACTIVATE)` when the provider call
  arrives off-thread; `WindowShell` runs the exact activation path on the UI
  thread; the same-thread inline path is unchanged. Regression: the
  marshal predicate is unit-tested in `ffsearch_uia_tests`, and the live
  `search-deep-result-navigation` scenario validates the end-to-end behavior.
- **Reclassified finding (this change):** `drag-between-panes` exposed a
  product defect: the drop destination resolved to `ActivePanePath()`
  (which the drag's own mouse-down activates — the source pane), and the
  screen→client conversion dropped the ClientToScreen client-origin term,
  so a pane-1 drop on a right-positioned window computed past the viewport
  width and fell back to the active (source) pane — observed live as the
  product's own "The destination already contains 'pane-drag.txt'."
  conflict dialog with the source-folder path. Fixed product-side
  (drop-point pane resolution threaded through the drop target, an exact
  conversion inversion via `DropPointClientXDip`, and the drop-point
  destination carried into the executed operation); regression-tested in
  `ffdrop_destination_tests` and validated live with filesystem + UIA
  refresh proof.
- No legitimate failure becomes SKIPPED to make the run green. Every SKIP
  names a real independent prerequisite.
- No pixel/OCR/image fallback, no hidden test controls, no private test IPC.
  Mouse coordinates are always derived from semantic UIA BoundingRectangles
  or explicitly identified real HWND/client rectangles.
- The historical 15/4/8 record stays untouched in the previous change's
  evidence; this change records its own reproduction and post-fix results.

## D11 — Module structure

The capability module stays the single scenario orchestrator. Reusable
lifecycle/wait helpers are extracted only where they are shared across
scenarios and would otherwise be copy-pasted:

- driver-level primitives → `verify/uia-driver/` (testable headlessly);
- scenario/lifecycle orchestration → the capability module's own
  non-exported functions (kept in the same module to avoid a second module
  boundary).
