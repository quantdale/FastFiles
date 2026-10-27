# Shortcut safety qualification — 2026-10-27

## Proven defect and repair
Fresh verified base PR30 430b7111cb3acf91aadc28472d6fdf3146ee4f4d equals origin/fix/execution-conflict-consent-20261027 after fetch. Sole writer/root uses isolated shortcut-safety worktree. Existing owner dirty files and other PRs preserved.

Real controlled worker on original code fails four assertions: late destination overwritten; false completion; manual retry finds damaged sentinel; accepted cancellation creates 256/256. Raw cancel-baseline.log.gz is retained, not relabeled. Generated fixtures clean normally, no shortcut executed and no user data touched.

IShellLink IPersistStream serialization now round-trips through native IPersistFile.Load and IShellLink.GetPath. CREATE_NEW exclusively creates output. Handle-bound disposition cleanup removes only newly created files after stream/write failure; original and cleanup errors remain independently reportable. Cooperative cancellation checks each item, posts bounded progress and one truthful terminal event. It cannot interrupt a single serialization/write.

Focused real-worker shortcut test PASS (0.31s); separate writer PASS (0.09s): exact 12,000-byte multi-chunk/final short read; existing/locked bytes preserved; seek failure, failed second read after 4KiB and malformed oversized count leave no partial file; null stream; actual unprefixed >MAX_PATH output and checked owned cleanup. UI target warning-clean 68/68. No test forces filesystem cleanup failure or CloseHandle failure; reporting exists without claiming those OS failures reproduced.

## Boundaries
Worker tests use a real unprivileged STA and own message-only event window. They do not qualify Explorer drag, Alt-drag UI, the rare cleanup-error dialog's visual layout, storage, privileged NTFS, installer, physical displays or Recycle Bin recovery. Existing UI cleanup safety dependencies remain necessary before native application execution. New independent review unavailable after provider quota; root reviews source and evidence without claiming independence. Broader certification pending.

Native API references: [Shell Links](https://learn.microsoft.com/en-us/windows/win32/shell/links), [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew), [SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle). IPersistFile.Save second argument is remember-state, not an overwrite permission.
