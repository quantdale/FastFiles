## ADDED Requirements

### Requirement: Clipboard File List Is Read Through A Locked Handle
When FastFiles reads a file-list paste payload from the Windows clipboard (`CF_HDROP` and the Preferred DropEffect format), it SHALL lock the `HGLOBAL` returned by `GetClipboardData` before inspecting bytes, and SHALL unlock it before returning. A failed lock or a null handle SHALL yield an empty path list (paste is a no-op) rather than dereferencing an unlocked or invalid pointer. The write path that publishes `CF_HDROP` already locks; the read path SHALL match that contract.

#### Scenario: Paste after Copy uses locked HDROP
- **WHEN** a user copies files in FastFiles or another application that placed a valid `CF_HDROP` on the clipboard and then pastes into FastFiles
- **THEN** FastFiles SHALL read those paths via a locked handle and SHALL enqueue the existing copy or move operation

#### Scenario: Lock failure does not crash
- **WHEN** `GetClipboardData` returns a handle that cannot be locked
- **THEN** FastFiles SHALL treat the clipboard as containing no files, SHALL NOT crash, and SHALL NOT pass garbage paths to file operations

#### Scenario: Preferred DropEffect is locked too
- **WHEN** the clipboard carries a Preferred DropEffect `HGLOBAL` alongside `CF_HDROP`
- **THEN** FastFiles SHALL lock that handle before reading the `DWORD` effect (cut vs copy) and unlock it afterward
