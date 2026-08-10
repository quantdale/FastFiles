# Lost-Engine reconnect acceptance evidence

Recorded 2026-08-11 on `recovery/fast-files-20260809`.

The rebuilt Debug UI was launched with a disposable `LOCALAPPDATA` while
retaining the normal development-tree executable layout. The UI discovered
and connected to the exact sibling Debug Engine executable:

```text
ui_pid=36376
old_engine_pid=36468
old_engine_path=build\\debug\\src\\engine\\FastFilesEngine.exe
```

The exact old Engine process was terminated, without terminating the UI. The
UI remained responsive while the existing reconnect manager started a
replacement Engine through the same bounded connection path:

```text
new_engine_pid=36572
ui_responding_after_kill=True
connect_failed_before=0
connect_failed_after=0
```

The UI and replacement Engine were then stopped and verified absent:

```text
remaining_ui=0
remaining_engine=0
```

This validates the lost-connection state fan-out and reconnect lifecycle in
the normal degraded development layout. The privileged active state was not
claimed: configured Authenticode pins remain placeholders and the privileged
path remains fail-closed. No user settings or system installation files were
changed.
