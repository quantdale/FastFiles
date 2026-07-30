## Context

FastFiles' sibling changes each implement real, substantial behavior — file manipulation (`file-operations`), navigation (`navigation-and-workspace`, `column-view-browsing`), search (`instant-search`), and storage analysis (`storage-analysis`) — but none of them own how a user actually *discovers and triggers* that behavior beyond whatever ad hoc entry point gets built alongside each one. Without a shared, data-driven command surface, an action like "New Folder" risks being defined three separate times — once for a right-click menu item, once for a command-palette entry, once for a keyboard shortcut — with implementations that inevitably drift out of sync as the app grows. This change is deliberately thin and last-mile: it builds the menu/palette/shortcut *chrome* and the shared registry those three surfaces read from, and wires that chrome to actions implemented elsewhere (`file-operations-core`, `conflict-resolution`, `multi-selection-and-dragdrop`, the `navigation-and-workspace` capabilities, `instant-search`, `storage-analysis`, `properties-and-details`). It introduces no new file-manipulation or navigation logic of its own.

This also inherits a specific consequence of `establish-architecture-foundation`'s D1 (native C++/Win32/COM/Direct2D stack, chosen partly *because* "native Windows 11 context menus need a raw `HMENU` that a managed framework's `ContextMenu` control cannot produce either"). This change is where that consequence is actually implemented: context menus here are built as real Win32 popup menus, not a custom-drawn or embedded-framework control.

## Goals / Non-Goals

**Goals:**
- Define a single command-registry data structure that context menus, the command palette, and keyboard shortcuts all read from, so every command is defined exactly once and exposed multiple ways.
- Implement selection-aware context menus (single file, single folder, multi-selection, mixed selection, empty/background) as native Win32 popup menus (`CreatePopupMenu`/`TrackPopupMenuEx`).
- Implement a fuzzy-searchable, fully keyboard-operable command palette over that same registry.
- Implement a default keyboard shortcut map, a persisted customization data model, and conflict detection on rebinding.
- Make shortcuts flagged as application-global (most notably search) fire regardless of which pane, column, or control currently owns keyboard focus within the app.
- Leave an explicit, documented seam so a later change can register the same actions with the Windows shell (default file manager, `IExplorerCommand`/"Open with" integration) without redesigning the registry.

**Non-Goals:**
- Any new file-manipulation logic (copy/move/delete/rename/create, conflict handling) — invoked via `file-operations-core`/`conflict-resolution`.
- Any new navigation logic (address bar, tabs, dual-pane, bookmarks, Column View population) — invoked via `column-view-browsing` and `navigation-and-workspace` capabilities.
- Any new search or storage-analysis logic — invoked via `instant-search`/`storage-analysis`.
- Any new properties/details rendering — invoked via `file-preview-and-properties`'s `properties-and-details` capability, which already replaces the need for a separate Explorer Properties dialog; this change does not launch the native Windows Properties dialog.
- Actually registering FastFiles as the Windows default file manager, contributing an `IExplorerCommand`/`IContextMenu` entry to Explorer's own right-click menu, or any system-wide "Open with" registration — explicitly deferred (see Decision D7).
- The settings screen for editing shortcut bindings — that UI belongs to `settings-and-appearance`'s `settings-ui` capability; this change owns only the underlying data model, persistence, and conflict-detection logic that screen will read/write, exactly the kind of coordination point `settings-and-appearance`'s own proposal already calls out for its indexing configuration.

## Decisions

### D1: Command registry as the single source of truth

A central descriptor: `CommandDescriptor { CommandId, DisplayName, Category, DefaultShortcut (optional), SelectionApplicability, Scope, EnabledPredicate, Handler }`. One `CommandRegistry`, populated at startup by each capability area registering its own commands (e.g. `file-operations-core` registers `Delete`; `navigation-and-workspace` registers `GoBack`; `instant-search` registers `FocusSearch`). The context-menu builder, the command palette, and the shortcut dispatcher are all thin consumers over this one table — none of them owns action logic; they filter, present, and invoke.

**Why:** the proposal explicitly calls out the "same command defined once, exposed three ways" requirement — this is a design constraint handed down from the proposal, not a nice-to-have.

**Alternatives considered:**
- *Three independent tables* (a menu-definition list, a separate palette command list, a separate shortcut map). Rejected: this is precisely the "New Folder drifts out of sync" failure mode the proposal names, and triples the maintenance cost of adding one new action.
- *Reflection/attribute-based auto-registration* (macro-annotated functions scanned by a codegen build step). Rejected for this codebase: adds custom build tooling disproportionate to an app with dozens, not thousands, of commands; explicit registration calls are simpler to grep and debug in a native C++ codebase with no reflection facility.

### D2: Context menu rendering — native Win32 popup menu, not custom-drawn or managed

Context menus are built from `CommandRegistry` entries filtered by `SelectionApplicability`, using `CreatePopupMenu`, `InsertMenuItem`/`AppendMenu` for entries and separators, and `TrackPopupMenuEx` anchored at the cursor (mouse invocation) or the selected item's screen rect (keyboard invocation via the Menu key / Shift+F10).

**Why:** direct continuation of `establish-architecture-foundation` D1's own rationale — a managed framework menu control cannot produce a genuine `HMENU`, and this is the change where that need is actually realized. Native popup menus also get keyboard mnemonics, OS-consistent submenu flyout timing, correct multi-monitor/DPI placement, and UI Automation menu patterns for free from `USER32` — one of the few places in this app where "free from the platform" is actually available despite D1's broader "hand-build everything" trade-off.

**Alternatives considered:**
- *Custom Direct2D-drawn popup*, pixel-consistent with the rest of FastFiles' custom-rendered chrome. Rejected for now: would require reimplementing keyboard navigation, submenu timing, screen-edge flipping, and accessibility that `TrackPopupMenu` provides free, for a surface (right-click menus) where users have strong Explorer-trained expectations of exact native behavior. Revisit only if visual-consistency complaints justify the cost.
- *Embedded managed-framework menu control.* Rejected outright: contradicts `establish-architecture-foundation` D1's no-managed-framework decision.

### D3: "Open Terminal/PowerShell Here" — a `CreateProcess` convenience launch, not an embedded terminal

Resolves the target directory (the selected folder, or the folder backing the current view if the selection is files/empty), then calls `CreateProcess` against a configured shell executable (default `powershell.exe`, falling back to `cmd.exe` if unavailable) with `lpCurrentDirectory` set to that path. The path is passed only via `lpCurrentDirectory` — never interpolated into a command-line string — so a folder name containing unusual characters cannot inject shell syntax.

Explicitly **not**: an embedded terminal pane, a ConPTY-hosted terminal view inside `FastFiles`, or a full "which terminal" configuration surface — that is a materially larger scope (real terminal emulation) that would duplicate Windows Terminal, and the proposal itself frames this as "a convenience integration, explicitly NOT a replacement for a real terminal."

**Alternatives considered:**
- *Dispatch through a registered "Open in Terminal" shell verb* that Windows Terminal or a third-party tool may install. Rejected as the primary path: not guaranteed present on a clean Windows install. `CreateProcess` against an explicit, known shell path is a reliable, dependency-free baseline. Preferring `wt.exe` when detected is tracked as an Open Question, not built now.

### D4: Command palette — a thin fuzzy/filtered view over the same registry

The palette lists every `CommandRegistry` entry whose `EnabledPredicate` passes in the current context, fuzzy-matches typed input against `DisplayName` (and an optional `Keywords` field), ranks results (favoring prefix/word-boundary matches over pure subsequence matches so near-miss names like "Copy" / "Copy Path" / "Copy Relative Path" stay disambiguated), displays each result's bound shortcut alongside it for discoverability, and invokes the same `Handler` the menu and shortcut paths use.

**Why the shared registry matters here specifically:** showing a shortcut hint next to a palette entry is only trivially correct if the palette reads the exact same table the shortcut dispatcher does — a separately authored palette command list risks showing a stale or wrong hint.

**Alternatives considered:**
- *Statically authored, palette-only command list* (simplest to build in isolation). Rejected: this is the exact "separately defined" drift the proposal calls out; it also cannot show a trustworthy shortcut hint.

### D5: Keyboard shortcut model — default map, persisted customization, conflict detection

Data model: `ShortcutBinding { CommandId, KeyChord (modifier bitmask + virtual key), Scope }`. Persisted state stores only the *diffs* from the built-in default map (keyed by the stable string `CommandId`, never an ordinal), so a future release that adds a new default shortcut for a new command is never silently shadowed by an old persisted file, and an unresolvable `CommandId` (from a later rename/removal) is dropped and logged at load rather than crashing.

This change owns the data model, the load/merge-with-defaults logic, and conflict detection. It does **not** own the settings screen for editing bindings — that belongs to `settings-and-appearance`'s `settings-ui` capability, which reads/writes through this data model, mirroring the same "coordinate the exact hook-in point at implementation time" pattern `settings-and-appearance`'s own proposal already uses for indexing configuration.

**Conflict detection:** rebinding a chord already bound to a different command in an overlapping `Scope` is rejected until resolved (warn, and either let the user reassign — unbinding the other command — or cancel the rebind). Binding a chord reserved by Windows itself (e.g. `Alt+F4`) is allowed to save, but flagged with a warning that it may not be reliably received, since FastFiles can only see keys that reach its own message loop.

**Alternatives considered:**
- *Silent last-write-wins rebinding.* Rejected: silently breaking a different command's shortcut with no warning directly contradicts the proposal's explicit call for conflict detection.

### D6: Global search hotkey wiring — top-of-chain accelerator, not per-view handling

Shortcuts flagged `Scope = Global` in the registry (most notably "focus/open search") are checked once, at the top of the main window's input-handling chain — an application-level accelerator check that runs before the key message is routed to whichever child control, pane, column, or tab currently owns focus — rather than being wired independently inside each view. `Scope = ActiveView` bindings (e.g. back/forward, which act on whichever pane/tab is currently active in dual-pane mode) are still resolved after that global check, against the active view, regardless of which specific control within that view has focus.

**Why not per-view:** wiring "focus search" separately inside Column View, dual-pane mode, and any future view type reintroduces exactly the drift risk D1 avoids for menus and the palette — a new view added later would silently lack the shortcut unless a developer remembered to re-wire it.

**Scope carve-out:** this is application-global (works anywhere inside any FastFiles window), not OS-global — it does not use `RegisterHotKey` to intercept the chord system-wide across other applications. The proposal's wording ("regardless of current focus/navigation context within the app") is explicitly scoped to *within* the app.

**Alternatives considered:**
- *OS-level `RegisterHotKey`* so search works even when FastFiles isn't the foreground app. Rejected/out of scope: the proposal's requirement is about in-app focus, not system-wide capture; OS-global hotkeys carry materially higher collision risk with other running apps and only one process can own a given hotkey system-wide.

### D7: Extension point for future Windows shell registration

`CommandDescriptor` deliberately stores only generic, caller-agnostic metadata (`CommandId`, `DisplayName`, an icon reference, `SelectionApplicability`, a `Handler` that takes an abstracted selection — a list of file-system paths — rather than any FastFiles-internal window/object handle). This means a later change could add a second renderer — an `IExplorerCommand`/`IContextMenu` COM shell-extension DLL that reads a subset of the same registry and invokes the same `Handler`s — without redefining the actions themselves.

This change does **not** implement the shell-extension DLL, its CLSID/registry registration, any Explorer verb registration, or "set as default file manager" flows — purely a structural decision that avoids precluding that work later, at effectively zero cost now.

**Alternatives considered:**
- *Couple `Handler`s tightly to FastFiles' own window/command-target objects* (simplest for this change in isolation). Rejected: would force a full `Handler` rewrite later to support shell-extension reuse, defeating the point of leaving room for it while it is still cheap to do so.

### D8: "Copy Relative Path" base-folder resolution

Relative path is computed against: the *other* pane's current location when dual-pane mode is active (the natural "I want to reference the other side" use case that motivates having a relative path at all); otherwise, the current view's root — the leftmost column currently shown in Column View. If the selection is on a different volume than the resolved base (no valid relative path exists), the action falls back to copying the absolute path and surfaces a brief notification that a relative path was not possible.

**Alternatives considered:**
- *Always relative to the immediate parent folder.* Rejected: trivially reduces to just the item's own name in the common case, which is not useful.
- *Always fall back to absolute path with no attempt at a meaningful base.* Rejected: discards the one case (dual-pane, two related locations) where a relative path is actually valuable.

## Risks / Trade-offs

- **[Risk] A single shared `CommandRegistry` becomes a de facto global object touched by every feature area, risking a "God object" and tight coupling.** → **Mitigation:** the registry stores only descriptors plus a function-pointer/`std::function` handler reference — no business logic lives in the registry itself; each capability module owns and registers its own handler logic, keeping the registry a lookup table, not a coordinator.
- **[Risk] Raw `TrackPopupMenu` is lower-level than a retained-mode framework menu** (owner-draw needed for icons/custom styling, manual submenu construction) **— more implementation effort per menu item.** → **Mitigation:** consistent with `establish-architecture-foundation`'s already-accepted "hand-build chrome" trade-off; a small internal helper layer over the raw `HMENU` calls amortizes this across every menu-building call site rather than repeating it.
- **[Risk] A persisted shortcut-customization file can become stale if a `CommandId` is renamed or removed in a later release, silently dropping a user's custom binding.** → **Mitigation:** bindings key off the stable string `CommandId` (never an ordinal/index); unresolvable `CommandId`s are ignored-and-logged at load, not fatal; `CommandId` is documented as an append-only/rename-with-migration-note identifier for future changes.
- **[Risk] The application-scoped (not OS-scoped) global search hotkey can still collide with a true OS-wide hotkey registered by Windows or another application outside FastFiles' control**, which FastFiles cannot detect since its own hotkey only fires within its own message loop. → **Mitigation:** accepted trade-off of choosing app-scope over OS-scope (D6); default chord is chosen conservatively to avoid extremely common OS-reserved combinations, and the shortcut is user-rebindable if a real conflict surfaces.
- **[Risk] "Open Terminal/PowerShell Here" spawns a real interactive process from a file manager**, a plausible (if narrow) confused-deputy concern if invoked against a path with attacker-influenced or unusual characters. → **Mitigation:** the path is passed only via `CreateProcess`'s `lpCurrentDirectory`, never interpolated into an executed command-line string, so folder/file names cannot inject shell syntax.
- **[Risk] Fuzzy command-palette matching over a growing command set (dozens today, more later) can surface confusing near-miss results** for similarly named commands ("Copy" vs. "Copy Path" vs. "Copy Relative Path"). → **Mitigation:** ranking favors exact-prefix and word-boundary matches over pure subsequence matches; each result shows its shortcut hint and category label to disambiguate.

## Migration Plan

Greenfield, additive UI-only change — no existing persisted state or users to migrate. The only forward-looking "migration" concern is the shortcut-customization file tolerating future `CommandId` additions/removals gracefully, which is designed in from the start (D5) rather than retrofitted later.

## Open Questions

- Whether to auto-detect and prefer Windows Terminal (`wt.exe`) over `powershell.exe`/`cmd.exe` when present, for "Open Terminal Here" (D3) — deferred, does not block this change.
- The exact fuzzy-matching scoring algorithm for the command palette (D4) is left to implementation; it does not affect the registry architecture.
- The exact persisted file format/location for shortcut customization (D5) is assumed to be a small structured file under the app's per-user settings location, compatible with `settings-and-appearance`'s eventual `settings-ui`; the concrete format should be confirmed jointly when that change is implemented, per that change's own stated coordination pattern for its indexing configuration.
- Whether "mixed" multi-selection (files and folders together) warrants its own reduced action set versus simply the intersection of file-only and folder-only actions — assumed intersection-based for now (see spec); revisit if user testing shows a dedicated mixed-selection action set is warranted.
