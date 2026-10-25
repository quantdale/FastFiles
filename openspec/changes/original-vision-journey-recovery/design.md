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

## Risks / Trade-offs

Filesystem targets may disappear after validation; ordinary operation handlers still report real OS failures. This is UI intent validation, not a new security boundary. Storage summaries remain scoped to available snapshot coverage; recursive global ranking and privileged coverage are separate work. Full physical DPI/High Contrast coverage depends on the host and must be reported truthfully.

## Validation

Focused command/selection and geometry regressions, Home search E2E with destination identity, storage summary/action tests, native visual catalog and broader Debug/release-test suites. Avoid restricted termination tests and expensive unchanged PR15 benchmarks.
