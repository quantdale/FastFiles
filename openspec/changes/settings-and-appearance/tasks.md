## 1. Settings Persistence Foundation

- [x] 1.1 Define the `settings.json` schema (top-level `schemaVersion` plus indexing, search, appearance, navigation, shortcuts, preview, and storage-analysis sections) and its location under `%LOCALAPPDATA%\FastFiles\settings.json`.
- [x] 1.2 Implement atomic write (write-temp-then-rename) and load of `settings.json` from `FastFiles`.
- [x] 1.3 Implement section-scoped validation with per-section fallback-to-defaults, `.bak` preservation of an unparseable file, and a diagnostic log entry on fallback.
- [x] 1.4 Implement first-run default seeding: all fixed local volumes included, built-in noise-directory excludes (`$Recycle.Bin`, `System Volume Information`), Follow-System theme, default shortcut bindings sourced from `keyboard-shortcuts`.
- [x] 1.5 Implement "Reset to Defaults" (rewrite all sections to defaults; verify no persisted index data is touched).

## 2. Indexing Configuration Surface

- [ ] 2.1 Implement the indexed-volume selection UI (list discoverable volumes, toggle inclusion) backed by the persisted volume list.
- [ ] 2.2 Implement the directory include/exclude rule editor per volume (ordered prefix-path rules, longest-match-wins precedence, add/remove/reorder).
- [x] 2.3 Define and implement the `ReloadIndexingConfig` control-plane message on the existing `FastFilesEngine ↔ FastFiles` pipe, sent after a successful settings write.
- [x] 2.4 Implement `FastFilesEngine`-side re-read of `settings.json` on `ReloadIndexingConfig` receipt and at its own startup (coordinate the diff/re-evaluation hook-in point with `index-storage-and-scanning`).
- [ ] 2.5 Verify volume/rule changes take effect without restarting `FastFiles`, `FastFilesEngine`, or `FastFilesIndexSvc`.

> Consolidation disposition: task 2.4 covers only message transport plus settings reload and is complete. Task 2.5 remains open until `index-storage-and-scanning` exposes and validates live scope re-evaluation semantics.

## 3. Search, Navigation, and Preview/Storage-Analysis Preference Settings

- [ ] 3.1 Implement search preference settings (default search scope, search-history retention and clear-history action).
- [ ] 3.2 Implement navigation preference settings (default startup location, restore-previous-session toggle).
- [x] 3.3 Implement preview behavior settings (enable/disable preview pane, maximum auto-preview file size).
- [ ] 3.4 Implement storage-analysis behavior settings (editable file-type/extension category definitions).
- [ ] 3.5 Wire each preference setting to the section of `settings.json` it belongs to and confirm consumers (search, navigation, preview, storage-analysis capabilities) can read the persisted values.

## 4. Keyboard Shortcut Customization Surface

- [ ] 4.1 Build the shortcut settings screen listing current bindings sourced from `keyboard-shortcuts`' data model.
- [ ] 4.2 Implement rebinding UI with conflict detection against existing bindings before a rebind is committed.
- [ ] 4.3 Implement "reset shortcuts to defaults."
- [ ] 4.4 Coordinate with `shell-integration-and-commands` on the exact shortcut data model shape; add a thin adapter layer if needed rather than redefining the model.

> Consolidation disposition: tasks 4.1-4.4 remain open until `shell-integration-and-commands` publishes its `ShortcutBinding` read/write contract. The persisted settings section is not a substitute for that authoritative runtime model.

## 5. Appearance Theme Selection

- [x] 5.1 Implement the Light/Dark/Follow-System theme selection setting and its persistence.
- [x] 5.2 Implement OS theme-change detection (`WM_SETTINGSCHANGE`/`AppsUseLightTheme`) for Follow-System mode.

## 6. Theming Mechanism (Direct2D Resource Recreation)

- [ ] 6.1 Extend the existing DPI-change/device-loss device-dependent resource-recreation routine to also trigger on a theme change, rather than adding a parallel theme-swap path.
- [ ] 6.2 Implement the minimal/no-animation theme-change transition (instant by default; optional short non-blocking cross-fade on top-level chrome only), gated off when Windows "Show animations" is disabled.
- [ ] 6.3 Verify theme changes never block input and recover via the existing device-loss path on resource-recreation failure.
- [ ] 6.4 Validate correct per-monitor high-DPI rendering (crisp text/icons, aligned hit-testing) across monitors at different DPI scale factors, including a window dragged between them.
- [ ] 6.5 Validate combined theme-change-while-DPI-differs and DPI-change-while-non-default-theme-active scenarios render correctly in both dimensions.

## 7. Index Health Status Derivation

- [x] 7.1 Implement the pure derivation function mapping `index-engine`'s connection state and `filesystem-index-store`'s per-volume scan/reconciliation state to the five status values (`Fully Indexed`, `Currently Indexing`, `Partially Indexed`, `Unavailable`, `Needs Reconciliation`), introducing no new persisted state.
- [ ] 7.2 Implement the fixed precedence order for resolving a single headline status when multiple conditions apply, plus a per-volume detail view showing all applicable conditions.
- [ ] 7.3 Build the per-volume status display UI, including messaging clear enough to explain a possibly-missing search result.

## 8. Diagnostic Logging

- [x] 8.1 Implement local per-user diagnostic logging for indexing errors, inaccessible directories, volume state transitions, and database problems.
- [x] 8.2 Enforce that log entries never contain file content, only path/metadata/error information.
- [ ] 8.3 Implement the diagnostic bundle export feature with aggregated/redacted output by default (counts and directory-structure summaries).
- [ ] 8.4 Implement the explicit opt-in required to include literal paths/filenames in an exported bundle, keeping file content excluded regardless.

## 9. Indexing Controls

- [ ] 9.1 Define and implement pause/resume control-plane messages to `FastFilesEngine`, global and per-volume.
- [ ] 9.2 Define and implement enable/disable control-plane messages to `FastFilesEngine`, distinct from pause/resume, global and per-volume.
- [ ] 9.3 Implement the newly-detected-volume pending-decision surface (derived from any engine-observed volume absent from the persisted volume list, not a new tracked list) and the "add to indexing" action.
- [ ] 9.4 Verify pause/resume/enable/disable/add-volume actions are reflected back through the status display from section 7 rather than tracked as separate outcome state.

> Consolidation disposition: section 9 remains ordinary unfinished cross-change work, not a merge blocker. It requires the scanning/session owner to finalize the runtime pause, disable, and per-volume state transitions before the settings UI can truthfully validate them.

## 10. Cross-Cutting Validation

- [ ] 10.1 Verify settings-file resilience: simulate a malformed section, a fully corrupt file, and a concurrent write during an engine reload, confirming no crash and correct fallback behavior.
- [ ] 10.2 Verify end-to-end indexing-configuration propagation: change a rule in settings and confirm `FastFilesEngine` re-evaluates scope without any restart.
- [ ] 10.3 Verify diagnostic bundle export contains no literal paths by default and no file content in either export mode.
- [ ] 10.4 Verify the full settings UI surface (indexing, search, appearance, navigation, shortcuts, preview, storage-analysis) round-trips correctly through save/reload/reset.
