## ADDED Requirements

### Requirement: Failed Workspace Load Does Not Clobber A Good File
If the on-disk workspace-state document cannot be read, decoded, or parsed as a supported version, FastFiles SHALL still launch with in-memory defaults, but SHALL NOT write those defaults over the existing file unless the user subsequently mutates workspace state in this session and a recovery write is explicitly required. A decode failure is not license to persist an empty bookmark list.

#### Scenario: Undecodable workspace file at startup
- **WHEN** the workspace-state file exists but is not valid UTF-8 (or is an unsupported version) and the user does not add, remove, or reorder bookmarks in that session
- **THEN** FastFiles SHALL NOT replace the on-disk file with an empty default document

#### Scenario: User mutates state after a failed load
- **WHEN** load failed and the user then adds a bookmark in this session
- **THEN** FastFiles MAY write a new well-formed document that includes that bookmark; it SHALL NOT silently drop other list fields it could still recover from a partial parse

### Requirement: Concurrent Windows Do Not Silently Erase Each Other's Bookmarks
Two FastFiles UI processes of the same user writing workspace state SHALL NOT share a single fixed `.tmp` path, and a save SHALL NOT replace another process's bookmarks, recent folders, or recently-closed tabs with a stale in-memory snapshot of an older load. List fields SHALL be merged on write (union with documented caps and last-writer-wins on conflicting display names for the same path). Scalar flags (sidebar/section collapsed, last location) MAY last-writer-win.

#### Scenario: Two windows add different bookmarks
- **WHEN** window A and window B are both open, A adds bookmark `D:\Alpha`, B adds bookmark `D:\Beta`, and both flush
- **THEN** a subsequent restart SHALL show both `D:\Alpha` and `D:\Beta` in Bookmarks, not only the last process's list

#### Scenario: Unique temporary file
- **WHEN** two processes flush workspace state at the same time
- **THEN** each SHALL write through a unique temporary name (not a shared `workspace-state.json.tmp`) so the writers cannot truncate each other's temp file
