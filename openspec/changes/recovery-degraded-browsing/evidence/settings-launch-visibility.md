# Settings launch-visibility acceptance evidence

Recorded 2026-08-11 on `recovery/fast-files-20260809`.

`SettingsDialog::Initialize` creates the dialog hidden and returns without
calling `ShowWindow`; `SettingsDialog::Show` is the on-demand path used by the
Settings command and `Ctrl+,`.

Runtime check used the rebuilt `build\debug\src\ui\FastFiles.exe` and a
read-only Win32 top-level-window enumeration two seconds after launch. The
result was:

```text
visible_window_count=1
settings_window_count=0
remaining_ui=0
remaining_engine=0
```

The probe did not modify settings or user files. It closed the UI through its
normal window path and stopped only the exact Debug Engine companion remaining
after UI exit.
