## Context

`establish-architecture-foundation` locked in the three-process architecture (`FastFilesIndexSvc` privileged / `FastFilesEngine` unprivileged index owner, running per-user as a logon-triggered Scheduled Task / `FastFiles` UI) and accepted, as a budgeted cost of native C++, that per-monitor DPI handling and Direct2D device-dependent resource management must be hand-built (see that change's D1 and Risks). `index-storage-and-scanning` is building the durable SQLite-backed index and the per-volume scan/reconciliation state machine that lives inside `filesystem-index-store`. `shell-integration-and-commands` is defining a keyboard-shortcut customization data model as part of its `keyboard-shortcuts` capability.

This change does not re-architect any of that. It adds the surface users actually touch: a settings UI, the light/dark theming behavior, and a presentation-plus-logging layer over index state that already exists. Three coordination points fall out of that framing, and this design exists mainly to fix each one precisely enough that the sibling changes have a stable contract to build against:

1. Where settings live and how they get from the UI process into `FastFilesEngine`'s in-memory decisions, without a restart.
2. How theme switching reuses the DPI-driven resource-recreation path instead of inventing a second one.
3. How five user-facing index-health states are derived from state that `index-engine` and `filesystem-index-store` already track, without inventing a duplicate copy of that state.

## Goals / Non-Goals

**Goals:**
- Fix the settings persistence format and location, and the load/fallback behavior when the file is missing or malformed.
- Define the indexing configuration surface (volume selection, include/exclude directory rules) precisely enough for `index-storage-and-scanning` to consume it, including the exact rule-matching model (prefix-path, justified below) and the change-propagation mechanism (control-plane message, no restart).
- Define the full settings-ui scope: search/appearance/navigation preferences, the keyboard-shortcut customization surface (editing, not redefining, `keyboard-shortcuts`' data model), and preview/storage-analysis behavior settings.
- Define theming as "one more trigger for the same resource-recreation path" the foundation's DPI handling already requires, plus the minimal-animation posture.
- Define the five-state index-health derivation as a pure presentation layer over existing state, plus the diagnostic-logging categories and their privacy posture (redacted-by-default diagnostic bundles).
- Define pause/resume/enable/disable indexing and add-newly-detected-volume as control-plane actions against `FastFilesEngine`.

**Non-Goals:**
- Implementing the actual MFT/USN scanning, matching, or reconciliation logic — that is `index-storage-and-scanning`'s `filesystem-index-store` and `privileged-index-service` work; this change only fixes the shape of the configuration those consume and the shape of the status they expose.
- Redefining the keyboard-shortcut data model (default bindings, chord/conflict semantics) — that belongs to `shell-integration-and-commands`' `keyboard-shortcuts` capability; `settings-ui` only provides the editing surface.
- Designing the DPI-change detection mechanism (`WM_DPICHANGED`, per-monitor transforms, device-lost handling) from scratch — that was already accepted as foundational cost in `establish-architecture-foundation`. This change extends that mechanism's trigger set and validates its correctness under theme changes and mixed-DPI multi-monitor setups; it does not redesign it.
- Enterprise/roaming settings sync, multi-user shared configuration, or any automatic (non-user-initiated) upload of diagnostic data.
- A general-purpose custom color-theme editor. Only Light, Dark, and Follow-System are in scope.

## Decisions

### D1: Settings persistence — a local, per-user JSON file, not the registry

Settings live at `%LOCALAPPDATA%\FastFiles\settings.json`, written by `FastFiles` (the UI process), one file per Windows user account.

**Why a file over the registry:** the registry is the more "native Windows" choice, but three things it does not give us actually matter here: (1) portability — a single file can be copied, versioned, or attached to a bug report; the registry cannot be inspected or backed up without a separate tool; (2) human-inspectability for troubleshooting — a JSON file a user or support engineer can open in Notepad and see exactly what's configured is a direct win for a utility whose whole pitch is transparency about what's indexed and why; (3) no registry-permission edge cases (virtualization under some sandboxed/managed environments, HKCU redirection oddities) to reason about, for a requirement the brief never actually asked for. The registry's traditional advantages — atomic multi-value transactions, `RegNotifyChangeKeyValue` change notification — aren't needed: this change already has a purpose-built engine-notification path (D4 below) that doesn't depend on either mechanism.

**Why per-user, not machine-wide:** `FastFilesEngine` itself runs as a per-user logon-triggered Scheduled Task (per `establish-architecture-foundation` D2/Migration Plan) even though the underlying index is machine-wide-visible to `FastFilesUsers` members. Each user's engine instance naturally reads its own per-user settings file, so two users sharing a machine can have different indexing include/exclude rules and different UI preferences without needing any shared/global settings store or write-arbitration between them.

**Format — JSON over INI:** the configuration is inherently nested (a list of volumes, each with a list of include/exclude rules; a shortcut-binding map; a file-type category map for storage analysis), which JSON expresses directly and INI only through ad hoc section-naming conventions. JSON also has broad tooling support for validation and diffing. The trade-off — JSON has no comments and is less forgiving of hand-edit mistakes (trailing commas) than INI — is mitigated by D2 below (resilient, section-scoped loading) rather than by picking a more permissive format.

**Alternative considered:** Windows Registry (`HKCU\Software\FastFiles`) — rejected per above; remains available as a fallback design if a future requirement (e.g., Group Policy-managed settings) needs registry-based central management, which is out of scope here.

### D2: Resilient loading — schema version plus section-scoped fallback

`settings.json` carries a top-level `schemaVersion`. On load, each top-level section (indexing, search, appearance, navigation, shortcuts, preview, storage-analysis) is validated independently; a malformed or unrecognized section falls back to that section's defaults rather than failing the whole load. The original file is preserved as `settings.json.bak` and the fallback is recorded as a diagnostic log entry (see `index-health-and-diagnostics`).

**Why section-scoped rather than all-or-nothing:** the file is explicitly human-editable (D1's stated benefit); a typo in a hand-edited shortcut binding should not silently discard the user's indexing configuration too. All-or-nothing was considered and rejected because it turns the human-inspectability benefit of D1 into a liability — one bad edit anywhere would erase every preference at once.

### D3: Indexing include/exclude rules are prefix-path based, not glob based

Each configured volume carries an ordered list of directory rules, each an absolute directory path plus `include`/`exclude`, matched by path-prefix (subtree) against the canonical path FastFilesEngine already computes at ingestion (per the foundation's filename-canonicalization decision). The most specific (longest) matching prefix wins when rules overlap.

**Why prefix-path over glob:** the brief's actual requirement is directory-level control (skip a folder subtree, a whole drive, etc.), not general pattern matching. Prefix-path matching is a cheap, single comparison against a small, sorted rule set, evaluable per record in the ingestion hot path (potentially millions of records per full volume scan) with no backtracking and no ambiguity. Glob matching — especially recursive `**`-style patterns — is asymptotically more expensive at that volume, and its edge cases (case sensitivity, whether `**` crosses directory boundaries, separator handling) are a disproportionate support/bug surface for a configuration surface whose stated scope is directory rules, not filename patterns. Prefix rules also compose cleanly with the foundation's ingestion-time canonicalization: no new untrusted-pattern-parsing surface is introduced near the privileged-adjacent ingestion pipeline.

**Alternative considered:** glob/`.gitignore`-style patterns — more expressive in a single rule (e.g., "any `node_modules` anywhere"), and a familiar syntax. Rejected for this change on cost and ambiguity grounds above; left as a possible additive, opt-in *file-level* filter layered on top of the prefix-path *directory* model in a future change (see Open Questions), not a replacement for it.

**Default rule set:** on first run, every fixed local volume `EnumerateVolumes` reports is included with no user-defined excludes, seeded with a small built-in exclude list for universally-noisy, access-restricted system paths (`$Recycle.Bin`, `System Volume Information`) that the user may override.

### D4: Indexing configuration changes reach `FastFilesEngine` via a control-plane notification, not polling or a restart

`FastFiles` is the sole writer of `settings.json`. When the user changes indexed-volume selection or include/exclude rules, `FastFiles` writes the file, then sends a new, lightweight control-plane message (e.g. `ReloadIndexingConfig`) over the existing `FastFilesEngine ↔ FastFiles` control pipe established in `establish-architecture-foundation` D3 — the same-privilege control seam, not the hardened elevation-boundary protocol to `FastFilesIndexSvc`, which is untouched by this change. `FastFilesEngine` re-reads `settings.json` on receipt, diffs the previous and new effective rule set per volume, and reacts: newly excluded subtrees are dropped from what it keeps scanning/watching; newly included subtrees are scheduled for evaluation; volume enable/disable maps to starting or stopping that volume within the engine's existing state handling. `FastFilesEngine` also reads `settings.json` independently at its own startup (it can be running with no `FastFiles` window open), so it has a correct effective configuration even without a live control-plane connection.

**Why notify-then-pull rather than push-the-full-config-over-the-pipe:** mirrors the pattern already established for search (foundation D3's snapshot-plus-notification design) — the pipe carries a small "something changed, re-derive" signal, and the receiver reads the authoritative source (here, the settings file; there, the mapped snapshot) itself. This avoids a second, parallel serialization format for indexing config on the wire and avoids the pipe being a single point of failure for a config value that the engine can equally obtain from its own filesystem read.

**Coordination point (flagged per the proposal):** the exact subtree-scan/stop mechanics that `ReloadIndexingConfig` triggers inside `FastFilesEngine` and `FastFilesIndexSvc` belong to `index-storage-and-scanning`; this design fixes only the UI-facing contract (a settings write plus a control-plane reload notification), not the scan-engine internals on the receiving end.

### D5: Theme switching is just another trigger for the existing Direct2D resource-recreation path

Light/Dark/Follow-System is a `settings-ui` selection (persisted like any other preference). `theming` treats a theme change — whether from an explicit user selection or the OS-level `WM_SETTINGSCHANGE`/`AppsUseLightTheme` signal while in Follow-System mode — as one more entry in the same invalidation trigger set that already forces device-dependent resource recreation for DPI changes and `D2DERR_RECREATE_TARGET`. There is one recreation routine; theme, DPI, and device-loss are three ways to invoke it, not three separate code paths.

**Why share the path rather than build a parallel "swap the palette" mechanism:** a bespoke theme-swap path risks drifting out of sync with the DPI-driven one (e.g., brushes recreated with the new theme's colors but the old DPI-scaled stroke widths, because two different routines each thought they owned recreation). Reusing one path also means theme correctness is validated by the same tests that already have to exist for DPI/device-loss recovery, rather than a second independent test surface.

**Animation posture:** per the brief's "avoid unnecessary visual effects" principle, theme changes apply with no animation by default; at most a short (≈100–150ms), non-blocking cross-fade limited to top-level chrome is permitted, and it is skipped entirely whenever Windows' "Show animations" setting is off or a UI operation is in flight. Input is never blocked while the swap happens — this is a resource-recreation-and-repaint, not a modal transition.

### D6: High-DPI/multi-monitor correctness is validated, not redesigned, here

Per-monitor DPI awareness and the `WM_DPICHANGED`-driven resource-recreation mechanism were already accepted as a hand-built cost in `establish-architecture-foundation` D1. This change's `theming` capability is where that mechanism's *observable correctness* — crisp text/icons/hit-testing as a window crosses a DPI boundary between monitors, correct bitmap scaling per monitor, no stale-DPI rendering after a topology change (monitor unplugged, sleep/wake) — is specified as testable requirements and validated, including in combination with a theme change (switching DPI while dark mode is active must not corrupt either dimension). It does not re-specify the DPI-change detection plumbing itself.

### D7: Index health is a five-state presentation layer with no new tracked state

`index-health-and-diagnostics` derives, per volume, exactly one of `Fully Indexed / Currently Indexing / Partially Indexed / Unavailable / Needs Reconciliation` from state that already exists: the connection state machine (`Disconnected/Connecting/Handshaking/Active`) owned by `index-engine`, and per-volume scan/reconciliation progress owned by `filesystem-index-store`. This mapping is a pure function recomputed on demand (or on a lightweight subscribe-to-status-change), never persisted as a second, independently-writable copy of health state.

**Precedence when more than one condition could apply** (e.g., a volume mid-scan for a newly included subtree while another subtree on the same volume awaits reconciliation): `Unavailable` (no active privileged connection or volume unreachable) outranks `Currently Indexing`, which outranks `Needs Reconciliation`, which outranks `Partially Indexed`, which outranks `Fully Indexed` — the most "something the user should know about right now" state wins the single headline display; the other conditions remain visible in the per-volume detail view, not lost.

**Why not add new engine-side state:** a second source of truth for "health" invites drift from the state the engine already maintains for its own operational purposes. Presentation-only keeps `index-health-and-diagnostics` a thin, low-risk layer whose correctness is entirely a function of the derivation rule, not of any new persistence or synchronization concern.

### D8: Diagnostic logging is local-first and redacted by default when exported

Logs (indexing errors, inaccessible-directory events, volume state transitions, database problems) are written per-user under `%LOCALAPPDATA%\FastFiles\logs`, alongside settings, and by default contain full paths — this is the user's own machine and data, the same posture Everything/WizTree take with their own local logs, and matches the foundation's already-accepted machine-visible-metadata model. Content is never logged; the foundation's ingestion pipeline never captures `$DATA` in the first place (D4 of the foundation design), so there is nothing to redact there.

The distinct case this change adds is the explicit "export a diagnostic bundle to share with someone else" action: that export defaults to aggregated/redacted form (e.g., "12 access-denied errors under 3 distinct top-level directories" rather than literal paths); including literal paths/filenames in an exported bundle requires a separate, explicit opt-in at export time. This draws the line the proposal calls for — local logs may be as detailed as needed for the user's own troubleshooting, but what leaves the machine by default is not.

### D9: Pause/resume/enable/disable and add-new-volume are control-plane actions, status is read back through D7

Settings actions to pause, resume, enable, or disable indexing (globally or per volume), and to accept an "add this newly detected volume to indexing" prompt, are expressed as control-plane messages to `FastFilesEngine` over the same UI↔engine pipe used in D4 — `FastFilesEngine` already owns the connection these actions need to act on. The engine acknowledges by updating the state D7 reads, so the settings UI and any status badge converge on the same presentation layer rather than tracking action-outcome state independently. A "newly detected volume" prompt is not new tracked state either: any volume `FastFilesEngine` observes (now or in the future, per `index-storage-and-scanning`) that has no matching entry in `settings.json`'s volume list is, by definition, pending-decision — the UI surfaces that derived condition rather than the engine maintaining a separate pending-volumes list.

**Coordination point:** what "pause" means at the `FastFilesIndexSvc` protocol level (e.g., whether it tears down an open `OpenUsnJournal` handle or just stops consuming from it) is `index-storage-and-scanning`'s mechanism to define; this design fixes only the UI-facing control-plane contract and the fact that the resulting status flows back through D7's derivation, not a new parallel status field.

## Risks / Trade-offs

- **[Risk] Two independent readers of `settings.json` (UI writes it, engine reads it) could observe a torn/partial write if the engine reads mid-write.** → **Mitigation:** writes are atomic (write to a temp file, then rename over the target), and the engine only re-reads on an explicit `ReloadIndexingConfig` notification sent *after* the write completes, plus at its own startup — never on a timer that could race an in-progress write.
- **[Risk] A hand-edited or corrupted `settings.json` could otherwise crash the app or the engine.** → **Mitigation:** D2's section-scoped validation and fallback-to-defaults, with the broken file preserved as `.bak` and surfaced as a diagnostic entry rather than silently discarded or fatal.
- **[Risk] Prefix-path rules are less expressive than glob for some power-user excludes (e.g., "every `node_modules` anywhere").** → **Mitigation:** documented as a deliberate scope cut (D3); a future additive, opt-in glob-based file-level filter layered on top of the prefix model is left open rather than closed off.
- **[Risk] Sharing one resource-recreation path between theme and DPI changes means a bug in that path affects both dimensions at once.** → **Mitigation:** accepted deliberately — the alternative (two parallel paths) risks silent drift, which is worse; combined theme+DPI scenarios are called out explicitly as required validation coverage (D6), not just each trigger tested in isolation.
- **[Risk] Default-redacted diagnostic bundle exports could still leak sensitive information through directory-name components that are themselves sensitive (e.g., a folder named after a person or project), even in aggregate form.** → **Mitigation:** redacted export mode reports structure and counts, not literal name components, by default; literal paths require an explicit, separate opt-in per export.
- **[Risk] The five-state index-health model can under-represent a volume in more than one condition simultaneously.** → **Mitigation:** D7's fixed precedence order guarantees one unambiguous headline state while per-volume detail remains available, instead of inventing a combinatorial state space.
- **[Risk] Settings are per-user while the underlying index is machine-wide-visible (per the foundation's accepted cross-user-visibility model), so two users on a shared machine could reasonably expect a "shared" indexing configuration that this design does not provide.** → **Mitigation:** consistent with `FastFilesEngine` itself running per-user (foundation D2); each user's engine instance and settings file pair off naturally. Documented as a deliberate consequence of following the existing per-user engine model, not an oversight.

## Migration Plan

Greenfield — no prior settings to migrate. First run creates `%LOCALAPPDATA%\FastFiles\settings.json` with the D3 defaults (all fixed local volumes included, built-in noise-directory excludes seeded, Follow-System theme, default shortcut bindings from `keyboard-shortcuts`). Forward migration on a `schemaVersion` bump applies per-section transforms for recognized keys and falls back to defaults for anything unrecognized (D2), rather than failing the load. A user-facing "Reset to Defaults" settings action deletes/rewrites `settings.json`; this affects preferences only — index data itself lives in `filesystem-index-store`'s SQLite database, not in this file, so resetting settings never discards index data. Uninstall removes `%LOCALAPPDATA%\FastFiles` per-user, consistent with the foundation's per-user Scheduled Task registration being removed on uninstall.

## Open Questions

- Whether an additive, opt-in glob-based *file-level* filter (e.g., `*.tmp`) should layer on top of the prefix-path *directory* model from D3 — deferred, not designed here.
- Exact default theme at first run is fixed as Follow-System (D5); whether to offer additional accent-color customization beyond Light/Dark/Follow-System is left open and out of scope for this change.
- `keyboard-shortcuts`' data model (from `shell-integration-and-commands`) is not yet finalized as of this writing (only its proposal exists) — `settings-ui`'s shortcut-editing surface is specified here against the *existence* of a rebindable, conflict-checkable model, not its exact field shape; a thin adapter may be needed if that shape lands differently than assumed.
- Diagnostic log retention/rotation policy (size cap, days-to-keep) is left as an implementation default (a fixed cap) rather than a user-facing setting in this change; revisit if users need control over it.
- Exact presentation (modal prompt vs. toast vs. passive sidebar badge) for the "add newly detected volume" condition is left to implementation-time UX detail, not fixed here.
