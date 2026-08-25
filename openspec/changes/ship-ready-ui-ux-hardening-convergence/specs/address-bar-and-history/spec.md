## ADDED Requirements

### Requirement: Editable address text exposes the canonical path

When the address bar enters editable-text mode, it SHALL populate the edit
control with the exact canonical filesystem path used by the active navigation
context. Display-only normalization SHALL apply only while the address bar is
in non-editable presentation mode.

#### Scenario: Ctrl+L retains an extended-length path
- **WHEN** the active context stores `\\?\C:\Users\MICHAE~1\Documents` and the user enters editable mode
- **THEN** the edit control SHALL contain that exact string, including its prefix and short-name component

#### Scenario: Editing a normalized display does not change identity
- **WHEN** a user enters editable mode after seeing a friendly long-name breadcrumb and commits the unchanged text
- **THEN** parsing SHALL operate on the edit text and the context's canonical identity SHALL remain the committed path, not a paint-time label
