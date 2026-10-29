## Decisions

1. Rename has no Replace consent UI. Use MoveFileExW without MOVEFILE_REPLACE_EXISTING or MOVEFILE_COPY_ALLOWED: fail on an occupied target at the OS mutation boundary, not after a racy existence check. Keep same-directory validation, exact requested names, case-only rename and reversible history. Expose the production primitive for deterministic late-target testing, without callbacks or test-only hooks in the worker.
2. NewItem is not idempotent creation. Existing destinations must not become success/createdPaths entries. Empty files reuse WriteNewFile's CREATE_NEW boundary with an empty COM stream; directories use CreateDirectoryW. Preserve native error codes and cleanup errors. Validate the new leaf name before mutation.
3. Tests own an exclusively created temporary root, use CREATE_NEW for sentinel files and a message-only event window, assert bytes/outcomes/history and clean only owned data. Legacy Shell queue-to-perform behavior is a diagnostic mode, never conflated with injection inside the worker. No Explorer, Recycle Bin, user files or foreground input.

## Risks and limits

MoveFileExW and CreateDirectoryW apply native Windows namespace semantics; this change does not claim protection against source identity replacement, ancestor reparse swaps, aliases or cross-volume moves. The worker creates the target in the source directory; this is not a generic move primitive. Creation remains empty-file/folder creation, not arbitrary Shell templates. Cancellation is honored before the short native mutation; an accepted atomic mutation is reported as completed rather than falsely rolled back. Shell undo is not used; existing FastFiles rename history remains the undo mechanism.

## Evidence

Initial focused probe: six assertions fail in 0.57 s. Real worker CreateFile/CreateFolder each reports completed=1/failures=0/createdPaths=1 on existing items (bytes remain intact). Legacy Shell late RenameItem returns S_OK while replacing the late destination and removing the incoming source. Ordinary occupied-target worker Rename already refuses (0x80070050). The primitive probe establishes the missing write-boundary guarantee, not a timed reproduction inside the precheck.
