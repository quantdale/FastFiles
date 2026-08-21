# Audit area: SETTINGS, PERSISTENCE &amp; FIRST-RUN — audit src/ui/src/SettingsDialog.{h,cpp}, workspace-state persistence (locate the JSON read/write for recentFolders/search history/theme/etc., likely WindowShell or Util), src/ui/src/UITheme.h theme handling, and the WindowShell startup sequence (WindowShell itself READ-ONLY for other auditors but you may read it). Cover: every exposed setting actually taking effect, persistence across restarts, corrupt/invalid persisted JSON handling, sensible defaults, follow-system theme, first-run experience clarity (does a normal user understand degraded mode?), restart requirements, stale state after setting changes, obsolete/dead settings.

(agent agent-7, outcome completed)

## Defects

### [P1] Opening Settings → Search and clicking OK silently changes default scope from "Everywhere" to "Current Folder" — SettingsDialog.cpp:366-386, 1030-1038
- Symptom: user's default search scope silently narrows; searches that used to hit all indexed locations now only search the current folder.
- Evidence: the combo is populated with only 4 items (`SettingsDialog.cpp:366-369`) but the persisted default is `L"Everywhere"` (`ffprotocol/Settings.h:25`). `PopulateSearchPage` maps "Everywhere" to `scopeIndex = 0` (falls through all comparisons, `SettingsDialog.cpp:378-385`), which displays "Current Folder". On OK, `SaveCurrentPage` writes back index 0 as `"Current Folder"` (`SettingsDialog.cpp:1033`). The `default:` branch writing `"Everywhere"` (`:1037`) is unreachable because `CB_GETCURSEL` on a 4-item combo can never exceed 3.
- Failure scenario: fresh install → open Settings → Search tab → OK. Scope permanently changed without the user touching anything.
- Root cause: the "Everywhere" enum value has no UI representation.
- Fix: add an "Everywhere" item to the combo and map it to index 4 (or make it index 0), and map unknown persisted scopes explicitly instead of falling into index 0.
- Regression test: round-trip `LoadSettings→populate→save` with `defaultSearchScope == L"Everywhere"` and assert it survives.

### [P1] Clicking OK on the Indexing page deletes configuration for drives not currently attached — SettingsDialog.cpp:1047-1064
- Symptom: a user who disabled indexing on a USB drive, unplugs it, later opens Settings → Indexing and clicks OK loses that volume's entry entirely — including its include/exclude rules — so re-attaching the drive makes it "Pending decision" again and it may be re-indexed from scratch.
- Evidence: `SaveCurrentPage` does `settings_->indexing.clear()` then rebuilds **only** from `volumes_`, which is populated exclusively from `GetLogicalDrives()` filtered to `DRIVE_FIXED || DRIVE_REMOVABLE` (`SettingsDialog.cpp:471-494`). The comment at `:1048-1050` claims rules "must survive" but only rules of *currently present* volumes are carried over.
- Failure scenario: configure `E:\` (removable) with exclude rules → unplug E: → open Settings → Indexing → OK → entry gone from settings.json.
- Fix: preserve `VolumeSetting`s whose key was not seen in the current enumeration instead of clearing (merge rather than rebuild).
- Regression test: save with a volume list missing one previously-configured key; assert the key and its rules persist.

### [P1] Cancel button does not cancel: rule/category edits are applied and persisted immediately — SettingsDialog.cpp:236-285, 715-811
- Symptom: user adds/removes indexing rules or storage categories, presses Cancel (or closes the dialog) expecting no changes — changes are already written to disk and pushed to the engine.
- Evidence: `AddVolumeRule`/`RemoveVolumeRule`/`MoveVolumeRule*` mutate `settings_` in place and call `onChanged_()` immediately (`SettingsDialog.cpp:757`, `773`, `789`, `807`), where `onChanged_` is `SaveAndNotifySettings` (`WindowShell.cpp:969-973`), which writes settings.json and calls `engineClient_.ReloadIndexingConfig()`. Same for add/remove category (`:267-285`). `IDCANCEL` (`:226-229`) only hides the dialog; nothing rolls back `settings_`.
- Fix: either operate on a draft copy committed on IDOK, or remove Cancel / relabel the dialog as apply-immediately.
- Regression test: invoke AddVolumeRule then simulate IDCANCEL; assert settings.json unchanged.

### [P2] Per-page save gating discards edits made on other tabs — SettingsDialog.cpp:1030-1077
- Symptom: user changes search scope on the Search tab, switches to Navigation, clicks OK → search-scope change silently discarded (only `currentPage_`'s controls are read).
- Evidence: each block is gated `if (currentPage_ == N && ...)` (`:1030`, `:1042`, `:1047`, `:1067`), yet all page controls persist for the dialog lifetime once created, so their HWND state is valid regardless of the visible page.
- Fix: drop the `currentPage_` gates (controls exist after first visit) or snapshot/save on `SwitchPage`.
- Regression test: set search controls, switch page, SaveCurrentPage, assert search fields saved.

### [P2] Storage category extensions column is uneditable — "Add" creates a category that can never be configured — SettingsDialog.cpp:267-275, 873-905
- Symptom: user clicks Add on the Storage page, gets row "New Category | (empty)", and there is no way to type a name or extensions: the listview has no `LVS_EDITLABELS`, no double-click edit handler (only `shortcutList_` gets an `NM_DBLCLK` editor at `:302-305`), so the new category is dead weight that also feeds `StorageAnalysis::SetCategories` with an empty extension match set.
- Fix: add an inline editor like the shortcut page's, or prompt dialogs for name/extensions.
- Regression test: UIA-driven test asserting the extensions cell can be edited.

### [P2] A failed shortcuts.json write silently suppresses engine reload and category update — WindowShell.cpp:1172-1180
- Symptom: indexing changes made in Settings appear not to take effect until restart, with no error shown.
- Evidence: `SaveAndNotifySettings` gates both `storageAnalysis_.SetCategories(...)` and `engineClient_.ReloadIndexingConfig()` behind `if (settingsSaved && shortcutsSaved)`. If `shortcuts_.Save` fails (settings.json already replaced atomically), the engine keeps the old indexing config while settings.json says otherwise — divergent state. Neither failure is surfaced to the user.
- Fix: notify/reload whenever `settingsSaved`; report the shortcut-save failure independently.
- Regression test: inject a save failure for shortcuts path and assert `ReloadIndexingConfig` still fires.

### [P2] "General" settings tab is completely empty — SettingsDialog.cpp:357-358
- Symptom: first-time user opens Settings, sees a blank "General" page — looks broken. Theme (Light/Dark/Follow System), preview toggle, and preview size limit exist only in the menu bar (`WindowShell.cpp:774-781`), not in Settings, and nothing on General points to them.
- Evidence: `void SettingsDialog::PopulateGeneralPage() {}` and `SwitchPage(0)` shows no controls (`SettingsDialog.cpp:425` — case 0 absent).
- Fix: populate General with theme preference + preview toggles (they're plain `settings_` fields), or remove the tab.

### [P3] Global pause checkbox state never syncs from engine; `globalPaused_` is write-only — SettingsDialog.cpp:559-568
- Symptom: if indexing is paused via another surface (or pause request fails because the engine is down), the checkbox still shows unchecked/checked stale state; `globalPaused_` is set but never read anywhere.
- Evidence: `ToggleGlobalPause` sets `globalPaused_` and sends `SetIndexingPaused(0, paused)`, but no code reads `globalPaused_` and `pauseAllCheck_` is never `BM_SETCHECK`-ed from `volumeStatusFlags_`. Also `SetIndexingPaused(0, …)` uses drive-letter 0 — verify the engine treats 0 as "all"; if not, the control is a fake affordance.
- Fix: derive checkbox state from returned status flags in `ApplyVolumeStatuses`.

### [P3] Misleading health text before first status report — SettingsDialog.cpp:629-661
- Symptom: right after opening the Indexing page with the engine connected but before `RequestVolumeStatus` returns, `volumeStatusFlags_` is empty so flags default to 0; an enabled, fully-indexed volume renders as "Search results … may be missing or stale: …" until the async reply lands.
- Evidence: `StatusTextForVolume` uses `flags = 0` when the letter is absent from `volumeStatusFlags_` (`:639`), then derives health from those zeroed conditions (`:655-661`).
- Fix: show "Checking…" when no status record has arrived for the volume yet.

### [P3] Dead constant / dead affordance: `WM_APP_SETTINGS_CHANGED` unused — SettingsDialog.cpp:18
- Symptom: none directly; indicates an abandoned notification path. Minor hygiene.
- Fix: delete it.

## Verified sound

- **settings.json atomic write**: temp file + `MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)` (`Settings.cpp:336-373`); corrupt file preserved as `.bak` and defaults loaded with a log line (`Settings.cpp:255-261`); full JSON validator before regex extraction (`JsonValidator`, `:81-147`); per-section fallbacks keep defaults for invalid values.
- **workspace-state.json atomic write** with same tmp+replace pattern and error-code-guarded directory creation (`NavigationWorkspace.cpp:647-670`); version check rejects future/absent versions (`HasSupportedVersion`, `:102-113`); malformed recentlyClosedTabs/recentFolders entries skipped without failing the document (`:538-609`); typed Home entries round-trip (`{"kind":"home"}`) with legacy bare-string compat.
- **Follow-system theme**: registry read defaults safely to light on failure (`WindowShell.cpp:1086-1091`); `WM_SETTINGCHANGE` re-applies theme only in FollowSystem mode (`:1646-1647`); theme change fans out dirty-marking to every surface including the settings dialog and flushes the icon cache (`MarkDeviceDirty`, `:1134-1162`).
- **Shortcut persistence/migration**: canonical shortcuts.json wins; legacy in-settings bindings migrated exactly once and persisted (`WindowShell.cpp:567-589`); shortcut draft is reverted on Cancel because `Show()` re-copies from live bindings (`SettingsDialog.cpp:135`); reserved-chord and conflict confirmations exist (`:986-999`).
- **Startup navigation resolution** honors restorePreviousSession → lastLocation → startupLocation ordering with path validation via `ParseNavigationPath` (`NavigationWorkspace.cpp:718+`, `WindowShell.cpp:794-801`).
- **Degraded-mode discoverability** is handled outside my area's surfaces (status chip in ColumnView, explicit "degraded mode" text in StorageAnalysis) — no first-run black hole found in the settings/persistence layer itself.
