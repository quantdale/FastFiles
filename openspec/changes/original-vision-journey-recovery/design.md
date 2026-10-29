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

### Bounded status text follow-up — 2026-10-26

Actual120DPI narrow captures show the engine badge and footer wrapping into clipped second lines. Preserve the existing Things/Native Precision target and native UITheme roles. Live Refero style retrieval reaffirms native typography/quiet secondary hierarchy; screen f4eced6d separates file metadata from primary names and screen1a805882 separates indexing status from its detailed explanation. Neither is evidence of Windows truncation behavior. The native decision is a cached DirectWrite single-line layout, with character ellipsis and clipping, rebuilt only for text/width changes. Do not change shared wrapping formats used by details/loading/error panels. A compact badge keeps degraded/retry meaning visible while the existing full description remains available to semantic status consumers. New code does not alter connection/privilege state, data coverage or search semantics. This isolated sibling successor starts from verified PR18; PR19's unrelated action/paint work remains separately stacked.

A real DirectWrite red regression confirms NO_WRAP alone still lays out explicit CR/LF as a second line. The cached layout creator flattens CR/LF/Unicode line separators only in its display copy, preserving the original model/path and the shared wrapping format. The regression uses long Unicode/error text, actual line/trimming metrics and zero-width stale-layout rejection. Caption copy explicitly distinguishes degraded/unavailable/retry states even with a stale privileged flag.

Transient recovery/completion notices take precedence over selection/path metadata in the one-line footer, so truncation cannot hide feedback behind a long folder path. The original status/model text remains available; only displayed composition changes.
