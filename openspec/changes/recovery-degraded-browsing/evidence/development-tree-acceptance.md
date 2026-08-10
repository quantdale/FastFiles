# Development-tree degraded browsing acceptance evidence

Recorded 2026-08-11 on `recovery/fast-files-20260809`.

The current rebuilt Debug UI was launched from the development tree with a
disposable `LOCALAPPDATA` and reached the exact sibling Debug Engine:

```text
ui_pid=30268
engine_pid=36284
ui_path=build\\debug\\src\\ui\\FastFiles.exe
engine_path=build\\debug\\src\\engine\\FastFilesEngine.exe
ui_hung=False
```

The fixture was a disposable five-level hierarchy ending in
`level1\\level2\\level3\\level4\\level5\\acceptance.txt`. The read-only
Debug selection probe observed the deepest path and file, then observed the
following real navigation actions:

```text
deep ActivePath=...\\level1\\level2\\level3\\level4\\level5
Alt+Left ActivePath=...\\level1\\level2\\level3\\level4
Backspace/up ActivePath=...\\level1\\level2\\level3
```

Returning to the deepest folder and sending the normal selection path selected
the fixture file at index 0. The `Open` command launched a new Notepad process
for that disposable text file; only the newly-created PID 36356 was stopped
afterward. The UI reported the file-operation smoke as `complete — 1 of 1 — 0
failed` after actual Ctrl+C/Ctrl+V copied the file to its disposable parent;
the copied file contained the expected 39-character fixture text.

The search surface was opened through the real command path. Querying
`acceptance` produced:

```text
search list_count=5
search status=5 results
```

UI Automation observed the degraded scope items exactly as:

```text
Current Folder
Current Folder + Browsed Subfolders
Current Drive — Browsed Folders Only
All Browsed Locations
```

The synthetic-root probe observed `VisibleItems=C:\`, proving the available
drive was listed. Missing-engine exhaustion and the working retry affordance
are recorded separately in `evidence/status-retry.md`; together these runs
cover the full acceptance sequence without activating the privileged service.

All exact UI/Engine processes and the disposable hierarchy/profile were removed
after the run. No user settings, installed files, or system configuration were
changed.
