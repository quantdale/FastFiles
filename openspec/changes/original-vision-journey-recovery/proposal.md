## Why

The original Find/Navigate/Understand/Act vision is undermined by composition defects: Storage commands discard their selected target, category summaries masquerade as files, and search reveal from Home bypasses typed navigation. Recover these signature journeys before adding visual complexity.

## What Changes

- Bind item commands to the visible workspace selection and reject stale explicit targets.
- Keep storage category summaries separate from filesystem operations.
- Make search reveal leave Home, preserve active-pane ownership, expose the destination hierarchy and synchronize navigation chrome.
- Bound search location and metadata independently at real DPI.
- Keep narrow engine/status feedback legible through bounded one-line presentation without changing full status or connection semantics.
- Record the five-pillar audit and validate actual native journeys with stronger destination assertions.
- Extend All-category Largest Files from immediate children to the100 largest logical-size known files under the selected folder, with source/coverage labels, visible containing locations and exact descendant actions. Preserve category refinements as explicitly immediate-folder views and retain hierarchical Treemap data.

## Capabilities

### New Capabilities

- `workspace-command-targeting`: visible workspace ownership and explicit filesystem command targets.
- `search-reveal-continuity`: Home-aware search-to-hierarchy handoff and legible result context.

### Modified Capabilities

None. The new contracts make cross-surface composition explicit without weakening existing operation or search contracts.

## Impact

The bounded Treemap follow-up preserves partial folder lower bounds through the actual rollup and makes logical size and coverage visible without hover, retaining existing native hierarchy, navigation and data sources.

Native UI command routing, StorageAnalysis, SearchPanel, WindowShell, and E2E tests. No process, IPC, database, service, trust, or installer contract changes. Existing PRs remain separate and unmerged.

The native conflict-decision slice also restores real accessible action names, Windows Tab traversal and coherent dialog/checkbox theming while preserving default Skip, all decision semantics and marker-owned validation.

Native lifecycle qualification also corrects the E2E Alt+F4 fallback to the existing braced-modifier input contract and preserves close-route evidence without changing ownership controls, timeouts or product shutdown behavior.

The bounded shutdown successor corrects UI COM lifetime, abandons pending icon work and declines saturated file-type lookup admission while retaining active task ownership. Opt-in metadata phases identify actual shutdown progress. This does not establish the cause of the observed Home close timeout or qualify native exit.
