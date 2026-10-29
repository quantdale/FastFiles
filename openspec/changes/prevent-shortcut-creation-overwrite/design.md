# Design

## Evidence
Controlled real worker tests on PR30 430b711 reproduce three collision assertions and one cancellation assertion failing. A CREATE_NEW sentinel introduced after BuildLinkPlan is overwritten; a public CancelCurrent request at Started still creates 256/256 links. No shortcut is executed; all owned temporary fixtures clean normally.

## Decision
Serialize IShellLink through IPersistStream in memory, then write using a CREATE_NEW handle. Existing paths fail without opening them for write. On stream/write failure delete only the newly created handle, reporting cleanup failure separately if it occurs. Preserve extended-length Windows paths. Cancellation is cooperative at each item boundary, with quantified progress and one truthful terminal event. Keep completed links and count only successful writes.

## Risks
Shell stream serialization must round-trip through real IPersistFile.Load/GetPath. Partial cleanup can fail on a filesystem; retain its HRESULT alongside the original error. Never delete by a raced pathname. Cancellation does not interrupt one COM serialization or filesystem write. This does not qualify Alt-drag, Explorer interoperability, Recycle Bin Undo or privileged NTFS.
