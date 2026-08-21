# Fresh product defect audit — wave 1 (2026-08-20)

12 read-only auditors over distinct product areas. Severity: P0 data-loss/security/crash, P1 major broken, P2 incorrect/unreliable, P3 polish.

## navigation.md — NAVIGATION &amp; WORKSPACE STATE
- [P2] All tabs render with the active-tab styling — active tab is visually indistinguishable — src/ui/src/NavigationChrome.cpp:341
- [P2] Address-bar error text survives Back/Forward/tab switches — src/ui/src/NavigationWorkspace.cpp:238
- [P3] Clicking the server segment of a UNC breadcrumb navigates to a non-enumerable `\\server` — src/ui/src/NavigationWorkspace.cpp:315
- [P3] "This PC" breadcrumb segment is drawn but can never be clicked — src/ui/src/NavigationWorkspace.cpp:263
- [P3] Drive dropdown is populated once at startup and never refreshed — src/ui/src/NavigationChrome.cpp:102
- [P3] Bookmarking a drive root produces a blank-named entry — src/ui/src/NavigationWorkspace.cpp:61
- [P3] Home snapshot goes stale: recents/bookmarks not refreshed on several navigation paths — src/ui/src/WindowShell.cpp:2190
- [P3] Ending a session on Home silently disables session restore — src/ui/src/NavigationWorkspace.cpp:673

## selection.md — SELECTION &amp; INPUT
- [P2] Mouse hit-test is off by ~one row whenever the status banner is visible — src/ui/src/ColumnView.cpp:1469
- [P2] Enter does not open files — dead "Open" affordance for keyboard users — src/ui/src/CommandSystem.cpp:270
- [P2] PageUp/PageDown/Home/End are completely unhandled in filesystem columns — src/ui/src/ColumnView.cpp:286
- [P2] Clicking empty area never clears the selection — src/ui/src/ColumnView.cpp:224
- [P2] Merely hovering the inactive pane steals active-pane (keyboard) focus in dual-pane mode — src/ui/src/ColumnView.cpp:418
- [P3] `UiaScrollPane` reads shared scroll state without the mutex — src/ui/src/ColumnView.cpp:2253
- [P3] Keyboard focus row is visually indistinguishable from other selected rows — src/ui/src/ColumnView.cpp:1517
- [P3] Hover highlight can stick to the wrong row after a snapshot refresh — src/ui/src/ColumnView.cpp:456

## search.md — SEARCH EXPERIENCE
- [P2] Invalid filter tokens are silently swallowed — no user feedback — src/search/src/Query.cpp:580
- [P2] Date filters mix UTC and local-time day boundaries — wrong results near midnight / non-UTC timezones — src/search/src/Query.cpp:306
- [P2] Every debounced keystroke records a history entry and synchronously flushes the history file on the UI thread — src/ui/src/SearchPanel.cpp:401
- [P2] Snapshot updates re-run a full indexed search and discard the candidate cache on every generation bump while the panel is open — src/ui/src/SearchPanel.cpp:960
- [P3] `SetQueryAndDispatch` lacks the `updatingHistory_` guard — programmatic text set triggers a second debounced dispatch — src/ui/src/SearchPanel.cpp:1165
- [P3] Esc does not close the search panel while focus is in the query box or result list — src/ui/src/WindowShell.cpp:2171
- [P3] Query placeholder promises indexed locations even in degraded mode — src/ui/src/SearchPanel.cpp:916
- [P3] "Clear history" button is unstyled native chrome in dark theme — src/ui/src/SearchPanel.cpp:222
- [P3] Search-history dedup is case-sensitive exact match — src/search/src/History.cpp:160

## fileops.md — FILE OPERATIONS
- [P1] Copy/Move onto itself: no same-folder or self-target guard anywhere in the drop/paste path — WindowShell.cpp:611
- [P1] Undo of Move and Restore silently overwrite existing files (FOF_NOCONFIRMATION with hand-built plans bypasses all conflict policy) — WindowShell.cpp:370
- [P2] Paste uses a stale app-local path vector that shadows the real clipboard — WindowShell.cpp:315
- [P2] Cut-paste never clears the shell clipboard, enabling a broken second paste — WindowShell.cpp:321
- [P2] Escape cancels the current operation even when none is running, and a cancel doesn't stop queued operations — WindowShell.cpp:2177, FileOperations.cpp:226
- [P2] Restart-after-Stop would leave every operation instantly cancelled — FileOperations.h:89 / FileOperations.cpp:164
- [P3] Long paths (>260 chars) silently unsupported across the operation pipeline — FileOperationPolicy.cpp:255-equivalent in FileOperations.cpp:255
- [P3] New folder/file inline-rename edit draws over the wrong row — WindowShell.cpp:1946-1949
- [P3] Partial Restore shows "Undo unavailable" even when some items restored — WindowShell.cpp:1942-1945
- [P3] Progress percent mixes byte-based and item-based units — FileOperations.cpp:77-78 vs :391

## preview.md — PREVIEW &amp; DETAILS PANE
- [P1] Full preview pixel buffer copied on every render frame — WindowShell.cpp:1482
- [P1] Synchronous `GetFileAttributesExW` on the UI thread every paint frame — WindowShell.cpp:1404
- [P2] Image preview bitmap not invalidated on D2D device loss — WindowShell.cpp:1134-1162 / 1489
- [P2] Folder "Size on disk" computed by the engine but silently discarded — WindowShell.cpp:1860-1865, 1424-1428
- [P2] Image preview drawn stretched to pane rect — aspect ratio destroyed — WindowShell.cpp:1493
- [P2] "Preview Limit" menu items have no effect on text previews — WindowShell.cpp:778-781, 2216-2218 vs Preview.cpp:97
- [P3] Non-BOM UTF-8 text previewed as ANSI mojibake — Preview.cpp:114,141
- [P3] Unbounded growth of type-name cache and async future list — WindowShell.cpp:1312-1333

## treemap.md — STORAGE ANALYSIS &amp; TREEMAP
- [P1] Treemap build recursively walks the entire indexed subtree and fires one async aggregate request per directory — TreemapView.cpp:263–330
- [P2] Failed folder aggregates render as perpetual "Calculating…" in the treemap, never "Partial" — TreemapView.cpp:465–473, TreemapSemantics.h:144
- [P2] ByCategory rows show "0 B" in the Size column and sort as zero — StorageAnalysis.cpp:565–576, 937
- [P2] Clicking a file tile in the treemap corrupts the storage panel's scope — StorageAnalysis.cpp:156–162, TreemapView.cpp:531–534
- [P2] Category filter claims "% recalculated relative to the filtered scope" but the parent denominator stays unfiltered — StorageAnalysis.cpp:250–262, 655–676
- [P3] Deferred-refresh timer is dead code — nothing ever calls SetTimer(kRefreshTimer) — StorageAnalysis.cpp:30, 1043–1048
- [P3] Known-zero items show "—" instead of "0.0%" for % of Parent / % of Volume — StoragePercentage.h:23
- [P3] First-time unavailable volume is labeled "(stale)" despite never having had figures — StorageAnalysis.cpp:387–397, 424
- [P3] Context menu positioned at raw lParam coordinates; keyboard-invoked menu lands offscreen — StorageAnalysis.cpp:1095–1096
- [P3] Dead/inconsistent synthetic catch-all failure derivation — TreemapLayout.h:119–120

## engine.md — ENGINE/SERVICE LIFECYCLE &amp; DEGRADED MODE
- [P1] Idle-drop of the privileged connection is instantly undone — connection churn every 10 idle minutes — src/engine/src/PrivilegedConnection.cpp:103
- [P2] Pending FolderAggregate callbacks are never failed on disconnect — perpetual "computing size" spinner — src/ui/src/EngineClient.cpp:655
- [P2] `IdleManager::NotifyUiWindowOpen` is dead code — the "no UI window" half of the idle condition can never be true — src/engine/src/IdleManager.cpp:53
- [P3] `LaunchEngineIfNotRunning` treats `ERROR_PIPE_BUSY` as "engine not running" — possible duplicate engine launch — src/ui/src/EngineClient.cpp:97
- [P3] Frame-read deadline is per-`ReadFile`, not per-frame — a trickling peer can stretch a "5 s" handshake indefinitely — src/ipc/src/PipeFraming.cpp:128
- [P3] Finished per-connection listener threads accumulate for the engine's lifetime — src/ipc/src/PipeListener.cpp:162
- [P3] Failed watch installation is completely silent to the UI — stale listings with no signal — src/engine/src/DirectoryWatcher.cpp:73

## settings.md — SETTINGS, PERSISTENCE &amp; FIRST-RUN
- [P1] Opening Settings → Search and clicking OK silently changes default scope from "Everywhere" to "Current Folder" — SettingsDialog.cpp:366-386, 1030-1038
- [P1] Clicking OK on the Indexing page deletes configuration for drives not currently attached — SettingsDialog.cpp:1047-1064
- [P1] Cancel button does not cancel: rule/category edits are applied and persisted immediately — SettingsDialog.cpp:236-285, 715-811
- [P2] Per-page save gating discards edits made on other tabs — SettingsDialog.cpp:1030-1077
- [P2] Storage category extensions column is uneditable — "Add" creates a category that can never be configured — SettingsDialog.cpp:267-275, 873-905
- [P2] A failed shortcuts.json write silently suppresses engine reload and category update — WindowShell.cpp:1172-1180
- [P2] "General" settings tab is completely empty — SettingsDialog.cpp:357-358
- [P3] Global pause checkbox state never syncs from engine; `globalPaused_` is write-only — SettingsDialog.cpp:559-568
- [P3] Misleading health text before first status report — SettingsDialog.cpp:629-661
- [P3] Dead constant / dead affordance: `WM_APP_SETTINGS_CHANGED` unused — SettingsDialog.cpp:18

## concurrency.md — CONCURRENCY, LIFETIME &amp; CANCELLATION (cross-cutting)
- [P1] Engine hangs: `ClientConnection::Close()` joins the search worker while holding `writeMutex_` the worker needs — src/engine/src/UiServer.cpp:240
- [P2] Folder-aggregate callbacks are never failed on disconnect — requests hang forever and leak — src/ui/src/EngineClient.cpp:655
- [P2] D2D device-loss recovery recreates only the swap chain, never the D2D device/context — src/ui/src/Renderer.cpp:197-204
- [P3] IconCache worker thread uses shell icon APIs without COM initialization — src/ui/src/IconCache.cpp:49-80
- [P3] `FileOperations::workerThreadId_` published unsynchronized; enqueue can be stranded — src/ui/src/FileOperations.cpp:212

## uia.md — UIA / ACCESSIBILITY SURFACE
- [P1] Entire Win32 control surface invisible to UIA — `get_HostRawElementProvider` never returns the host provider — UiaProviderBase.cpp:21
- [P1] Owner-drawn ComboBoxes have no accessible content even after host-provider exposure — SearchPanel.cpp:214–217
- [P2] Search-list hit-test uses the wrong word of `ListView_GetItemSpacing` — SearchUiaProvider.cpp:250–253
- [P2] Fragment-root group's `Navigate(LastChild)` always returns Pane 1 — ColumnViewUiaProvider.cpp:950–957
- [P2] Blind `static_cast` of foreign providers in `FindItemByProperty` startAfter resolution — ColumnViewUiaProvider.cpp:109–118, SearchUiaProvider.cpp:100–109, TreemapUiaProvider.cpp:222–231
- [P3] `std::wstring(nullptr, 0)` UB in treemap AutomationId match — TreemapUiaProvider.cpp:240–241
- [P3] Column status text always reports `IsOffscreen=false` — ColumnViewUiaProvider.cpp:1975–1977
- [P3] Pane elements hardcode `HasKeyboardFocus=false` — ColumnViewUiaProvider.cpp:1046–1048
- [P3] SearchListProc swallows WM_GETOBJECT when the provider can't be built — SearchPanel.cpp:165–174

## installer.md — INSTALL/SETUP LIFECYCLE
- [P1] Upgrade always fails while the app is in use — installer never stops the UI or engine processes — src/installer/src/InstallSteps.cpp:760
- [P1] Service stop is never awaited before DeleteService → intermittent `ERROR_SERVICE_MARKED_FOR_DELETE` on upgrade re-register — src/setup/src/ServiceRegistration.cpp:318
- [P1] Self-upgrade from the installed `FastFilesSetup.exe` is impossible — installer tries to replace its own running image — src/installer/src/InstallSteps.cpp:763
- [P2] Failed-upgrade rollback leaves new-version uninstall metadata and scheduled task behind — src/installer/src/InstallSteps.cpp:801
- [P2] Uninstall reports failure and leaves binaries because running UI/engine processes are never terminated — src/installer/src/Main.cpp:157
- [P2] No single-instance guard on the installer — concurrent installs race on SCM and the Backup directory — src/installer/src/InstallSteps.cpp:655
- [P3] Fresh-install failure leaves the security group, user membership, and an empty install dir behind — src/installer/src/InstallSteps.cpp:732
- [P3] Startup verification silently passes when the startup-token diagnostic line is absent — src/installer/src/InstallSteps.cpp:618

## errors.md — ERROR HANDLING &amp; MESSAGING SWEEP (cross-cutting)
- [P1] Pending folder-aggregate requests are never failed on engine disconnect — UI stuck on "Calculating…" forever — src/ui/src/EngineClient.cpp:656
- [P2] Ignored `GetFileAttributesExW` failure renders 1601-01-01 timestamps in Details pane — src/ui/src/WindowShell.cpp:1404
- [P2] Clipboard copy results silently dropped — copy commands can no-op without any feedback — src/ui/src/WindowShell.cpp:387,399
- [P3] Diagnostic export failure always reported as "no diagnostic log found" regardless of actual cause — src/ui/src/WindowShell.cpp:2266
- [P3] Indexing-pause toggle flips local state even when the command never reaches the engine — src/ui/src/SettingsDialog.cpp:559-563
- [P3] FileOperations worker dies silently if `CoInitializeEx` fails; queued operations sit in "Queued" forever — src/ui/src/FileOperations.cpp:211
