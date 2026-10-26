## Why

The original Find/Navigate/Understand/Act vision is undermined by composition defects: Storage commands discard their selected target, category summaries masquerade as files, and search reveal from Home bypasses typed navigation. Recover these signature journeys before adding visual complexity.

## What Changes

- Bind item commands to the visible workspace selection and reject stale explicit targets.
- Keep storage category summaries separate from filesystem operations.
- Make search reveal leave Home, preserve active-pane ownership, expose the destination hierarchy and synchronize navigation chrome.
- Bound search location and metadata independently at real DPI.
- Record the five-pillar audit and validate actual native journeys with stronger destination assertions.
- Explain common file-operation failures with source/destination-aware recovery guidance while preserving original HRESULTs and manual retry semantics; qualify the real owned-file daily-driver journeys.

## Capabilities

### New Capabilities

- `workspace-command-targeting`: visible workspace ownership and explicit filesystem command targets.
- `search-reveal-continuity`: Home-aware search-to-hierarchy handoff and legible result context.

### Modified Capabilities

None. The new contracts make cross-surface composition explicit without weakening existing operation or search contracts.

## Impact

Native UI command routing, StorageAnalysis, SearchPanel, WindowShell, and E2E tests. No process, IPC, database, service, trust, or installer contract changes. Existing PRs remain separate and unmerged.

The native conflict-decision slice also restores real accessible action names, Windows Tab traversal and coherent dialog/checkbox theming while preserving default Skip, all decision semantics and marker-owned validation.

Native lifecycle qualification also corrects the E2E Alt+F4 fallback to the existing braced-modifier input contract and preserves close-route evidence without changing ownership controls, timeouts or product shutdown behavior.

The bounded 4.7 follow-up exposes Cancel File Operations through the native command palette and registry-driven Browse/Search File menus. Availability reflects running or queued work independently of selection; Storage's hardcoded menu is outside this slice. Native qualification remains pending.
