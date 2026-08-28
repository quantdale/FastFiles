## Context

Starting tree: `main` = `origin/main` = `3fa9fd44d5cbee68cdf07a6522e182a2de0a564b`. Hosted CI run `33145596607` succeeded. The only untracked file is historical `docs/agent/HANDOFF_12H_2026-08-27.md`. PR #6 is superseded evidence; no unique code remains to port.

The UI already has `UITheme`, `UiComponents`, density *metrics*, responsive classification, Home, Column View, SearchPanel, and StorageAnalysis. Search and Storage already replace `contentRect` as alternate workspaces (`WindowShell::CurrentShellLayout`). They are not discoverable: sidebar only lists Home/Drives/Known Folders/Bookmarks; chrome is back/forward + breadcrumb + drive combo + tabs; `storage.analyze` has no default shortcut.

Completion-integrity remaining open tasks (live UIA journeys, final certification) stay honest: this change implements the product/UI work and then runs the local certification ladder. External signing, privileged MFT/USN, and host-foreground UIA remain external.

## Goals / Non-Goals

**Goals:**

- One shell silhouette: tabs, command row, sidebar, workspace, details, status.
- Fast Search and Storage Analysis as first-class workspaces.
- Compact/Comfortable density as a persisted setting.
- Preserve Home identity, UIA contracts, degraded mode, search/index correctness, and 72 DIP chrome height (existing layout tests and UIA geometry).
- Local production-readiness certification of the resulting SHA.

**Non-Goals:**

- WinUI/WPF/Electron rewrite.
- Dual query-edit (chrome and SearchPanel both owning independent queries).
- Per-pane Search/Storage in dual-pane (they remain full-content workspaces).
- Changing engine/service/protocol security.
- Claiming signed production release.

## Decisions

### D1 — Four workspaces, not extra windows

Browse (Column View), Home (Column View home mode), Fast Search (`SearchPanel`), and Storage Analysis (`StorageAnalysis`) are mutually exclusive content workspaces over the same `contentRect`. Opening Search hides Storage and vice versa. Folder/Home navigation hides both and returns to Browse/Home.

Why: Search and Storage already work this way. Dual-pane Column View cannot host a second search list without a new UIA surface. Keep one search/storage identity.

Trade-off: dual-pane is browse-only; search is global to the window.

### D2 — Sidebar is the product map

`NavigationTarget` gains `isSearch` / `isStorage` flags (same pattern as `isHome`). Empty path + tool flag MUST NOT be treated as unavailable or This PC. Order:

1. Home
2. Fast Search
3. Storage Analysis
4. Drives / Known Folders / Bookmarks (existing collapsible sections)

Why: the differentiators are currently command-only. Files-community DNA is a stable left nav; FastFiles identity is exposing search and storage there.

### D3 — Chrome composition inside 72 DIP, tabs first

Keep `UiMetrics::kChromeHeight = 72`. Swap rows:

- Row 1 (top): tabs + new-tab
- Row 2: Back, Forward, Up, breadcrumb/path, Fast Search field

Hide the chrome drive combo (drives remain in the sidebar). Replace its slot with the search field.

Why: Files-like tab-then-toolbar; 72 DIP preserves `ffshell_layout_tests` and UIA geometry. Drive combo duplicated sidebar and stole the search slot.

Trade-off: keyboard users who only used the combo must use sidebar/Home/Ctrl+L. Drives remain one click in the sidebar.

### D4 — Chrome search is an entry point, SearchPanel owns the query

The chrome Fast Search control is an owner-drawn field/button. Activate (click, or focus+Enter) invokes `search.focus`. The SearchPanel query edit remains the only query buffer. This avoids two competing edits and stale-generation races.

Complementary access: Ctrl+F, sidebar Fast Search, Home Search/Storage actions, command palette.

### D5 — Density is appearance, not a layout rewrite

Add `ffprotocol::DensityPreference { Compact, Comfortable }` to `Settings`, persisted under `appearance.density`, default Compact, missing field = Compact. `WindowShell::ApplyTheme` publishes `gUiDensityProfile`. Surfaces already calling `GetUiDensity()` pick it up; remaining hardcoded row heights are migrated where they affect browse/search/sidebar.

### D6 — Home quick actions include Storage

Raise `kHomeActionCount` from 4 to 5: New Tab, Search, Analyze Storage, Open Path, Settings. Existing Home layout tests assert the constant, not the labels.

### D7 — No engine/security change

No new IPC commands, no path-open primitive, no Authenticode pin change. Degraded mode stays first-class.

## Risks / Trade-offs

- [Risk] Swapping chrome rows breaks breadcrumb hit-testing (currently y=4..34). → Re-parameterize breadcrumb/edit/hit-test to the toolbar row (y≈38).
- [Risk] Sidebar tool rows with empty paths hit the unavailable-location path. → Guard `isSearch`/`isStorage` before `path.empty()` like Home.
- [Risk] Home action-count / UIA Home item labels drift. → Update ColumnView labels/commands and `kHomeActionCount` tests together.
- [Risk] Density Comfortable overflows narrow chrome. → Responsive hide of search label (icon-only) under the existing narrow breakpoint.
- [Risk] Visual overhaul regresses UIA RuntimeIds. → Do not change pane/item identity; new sidebar rows are ordinary invoke targets, not ColumnView fragments.
- [Risk] Hosted live UIA still fails at Filters disclosure. → Record as external/host-sensitive; do not convert to product PASS.

## Migration Plan

Additive settings field; old `settings.json` loads Compact. No index/schema migration. Rollback is revert of the UI commit train.

## Open Questions

None blocking implementation. Production signing/pins and privileged MFT remain explicitly external.
