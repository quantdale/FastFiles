## Context

Fresh baseline: origin/main a26fe609. See docs/product/original-vision-alignment.md and docs/product/original-vision-source-ledger.md. Source review identified wrong-target storage actions and a Home/search navigation split. Runtime evidence is collected separately; source presence is not runtime certification.

## Goals / Non-Goals

Restore trustworthy visible-selection commands and Find -> reveal -> navigate continuity. Preserve native rendering, UIA, DPI, degraded operation, canonical search and normal operation handlers. No framework rewrite or production privileged qualification.

## Decisions

### D1: Visible workspace owns command context

Why: a hidden Column View selection must never supply a Storage/Search file command. Resolve selection and applicability together; explicit invalid targets fail closed without fallback. Commands requiring a browsing destination remain unavailable in alternate workspaces unless that workspace defines the destination. Inline rename/select-all must not mutate a hidden view.

### D2: Category summaries are not files

Why: joining a category label to a folder can target an unrelated real sibling. Category activation may refine the current storage listing; category rows cannot supply filesystem command targets.

### D3: Search reveal uses the Home-aware navigation boundary

Why: history, visible pane kind, hierarchy, selection, focus, scroll and chrome must agree after activation. Preserve reconstructed indexed hierarchy and active-pane identity; test the actual resulting selected item rather than panel dismissal.

### D4: Preserve Native Precision Workspace

Primary existing Things direction, shadcn border discipline, Skiff filename/location hierarchy; live Refero style/screen/flow research recorded in the audit. Keep Segoe UI, current tokens and small native radii. Code-native geometry only, no bitmap assets. Location remains readable before secondary metadata; never scale a physical RECT twice. Functional tests and screenshots are separate gates.

### D5: Share virtual-list semantics for Storage accessibility

Live E2E found the native Storage owner-data list renders rows but the host exposes only a Pane with no rows or headers. Source inspection cannot qualify action targeting. Reuse the existing bounded result-list provider foundation with distinct surface identity/configuration and a real Storage row projection. Avoid an accessibility-only data model or copied provider implementation. Pointer, keyboard and UIA activation route through the same mutation; stale identities and owner lifetimes remain fail-closed. A successful semantic screenshot is still separate from filesystem-action correctness.

### D6: Presentation owns semantic workspace lifetime

Search and Storage replace Browse pixels, so hidden Browse/Home descendants must not remain discoverable or actionable. ColumnView owns the real Browse presentation state, updated by shell transitions and consumed by rendering and UIA. Reuse full-fragment retirement on hide/restore; structural pane counts remain intact. Native child tool controls and the separately active Treemap remain available. Do not emit semantic transitions from painting.

## Risks / Trade-offs

### Native palette paint correction

Visual review found a real command edit/list in hit-testing and UIA while Storage painted over it. SourceCopy/CAPTUREBLT captures were identical, disproving the capture-flag hypothesis. Setting sibling clipping in an isolated owned window exposed the palette. Apply WS_CLIPSIBLINGS to real direct child HWNDs at creation so workspace paints honor overlay z-order, including future controls. Remove the full-window layered dimmer, whose clipping hid useful surrounding context; keep the native edit/list and existing theme roles. Consume outside client clicks in the canonical message path to dismiss without accidentally activating a background file or navigation control. This adapts the existing Things/Linear direction's compact contextual command surface without new branding or a framework change.

Filesystem targets may disappear after validation; ordinary operation handlers still report real OS failures. This is UI intent validation, not a new security boundary. Storage summaries remain scoped to available snapshot coverage; recursive global ranking and privileged coverage are separate work. Full physical DPI/High Contrast coverage depends on the host and must be reported truthfully.

## Validation

Focused command/selection and geometry regressions, Home search E2E with destination identity, storage summary/action tests, native visual catalog and broader Debug/release-test suites. Avoid restricted termination tests and expensive unchanged PR15 benchmarks.

### Native conflict decisions remain readable and reachable

The live a3e20ab conflict audit cannot find the painted Replace label through UIA; the HWND caption is empty. Its actual120DPI dark screenshot also shows a white custom-window background and black checkbox text on a dark field. Source omits WS_TABSTOP on every action/checkbox. The ConflictDialog/UiComponents/UITheme source is unchanged from the new branch's verified PR18 base981a9ba; full application equivalence is not implied.

Retain the existing Native Precision/Things/shadcn reference lock and localized warning treatment. Set the native Replace caption, add real Tab stops while keeping Skip initially focused, erase the custom window with its token brush, and reuse Settings' ApplyNativeExplorerTheme for ordinary buttons/checkbox with High Contrast reset. No bespoke UIA state or operation-policy rewrite. Native public UIA/real pointer input verifies named actions, focus traversal and each decision against marked fixture content; screenshots separately qualify the actual theme and geometry. Native buttons on this host may omit InvokePattern, so use the existing E2E real-pointer fallback rather than hidden commands or synthetic success.
