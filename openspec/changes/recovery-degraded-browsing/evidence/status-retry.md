# Engine status and retry acceptance evidence

Recorded 2026-08-11 on `recovery/fast-files-20260809`.

The rebuilt Debug UI was copied into a disposable directory and launched with
a native `CreateProcessW` environment block whose `ProgramFiles` and
`ProgramW6432` roots pointed to an empty disposable directory. This prevented
the installed Engine from being selected while leaving the production UI and
status path unchanged.

Initial missing-engine cycle:

```text
engine-connect-failed events before unavailable = 5
state=engine-unavailable present = true
UI process Responding = true
```

The real window was captured and inspected during the run. It showed the
browser still visible and the painted chip text `Engine unavailable — click to
retry`. A second capture after the chip click showed `Starting engine…`.

Retry acceptance posted a mouse down/up at the real status-chip client area:

```text
click_posted=1
connect_failed_before=5
connect_failed_after_400ms=6
connect_failed_after_3s=7
ui_responding=True
```

A normal development-tree launch was also captured while the sibling Engine
was connected in degraded mode; the real chip showed `Instant search: basic
(degraded mode)`. The privileged `Instant search: enabled` state was not
activated because the configured Authenticode pins remain placeholders and the
privileged path is fail-closed.

The state-to-text contract is also covered without requiring the privileged
runtime: `tests/ui/test_engine_status.cpp` exercises Starting, Connected with
the active privileged path, Connected in degraded mode, and Unavailable. The
Unavailable presentation explicitly cannot claim an active privileged path,
even if a stale/incorrect boolean is supplied by a caller.

All copied UI/Engine processes and disposable directories were removed after
the run. No user settings or system installation files were changed.
