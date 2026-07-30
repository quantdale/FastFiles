## ADDED Requirements

### Requirement: Preview Provider Extension Point
`FastFiles` SHALL provide a preview provider registry that resolves which provider renders a given file's preview through a defined priority order — an exact registered file-extension match first, then content-sniff probes for extensionless or ambiguous files, then no match — and this registry SHALL be the only mechanism used to select a preview provider.

#### Scenario: Extension match selects the registered provider
- **WHEN** the currently previewed file's extension matches a provider registered for that extension
- **THEN** that provider SHALL be used to render the preview without evaluating content-sniff probes

#### Scenario: Extensionless file falls back to content sniffing
- **WHEN** the currently previewed file has no extension or its extension matches no registered provider
- **THEN** the registry SHALL evaluate registered content-sniff probes in registration order to determine a provider before concluding no provider applies

#### Scenario: A provider that fails to produce a preview falls through instead of rendering garbage
- **WHEN** the provider selected for a file fails during preview generation (for example, a decode error on a file whose extension suggested a format it does not actually contain)
- **THEN** the registry SHALL treat that provider as not applicable to the file and SHALL fall through to the next-priority provider or to the no-preview fallback, rather than displaying a corrupted or partial render

### Requirement: Provider Registration Requires No Core Engine Changes
Registering a new preview provider SHALL require only implementing the preview provider interface and adding it to the registry within `FastFiles`; it SHALL NOT require any change to `FastFilesEngine`, the filesystem index schema, or the engine/service IPC protocol.

#### Scenario: Adding a new provider touches only the UI process
- **WHEN** a new preview provider is added for a previously unsupported file type
- **THEN** the implementation SHALL consist of a new provider registered in `FastFiles`'s preview registry, with no modifications required to `FastFilesEngine`, the index store, or the IPC protocol definitions

### Requirement: Image File Preview via WIC
`FastFiles` SHALL render a visual preview for common raster image formats (at minimum JPEG, PNG, BMP, GIF, and TIFF) by decoding the file's own bytes directly from disk using the Windows Imaging Component (WIC), independent of the filesystem index.

#### Scenario: Selecting a supported image file shows its rendered preview
- **WHEN** the user selects a single file with a supported image format
- **THEN** the preview pane SHALL display a rendered visual preview of that image's actual content, decoded directly from the file

#### Scenario: A corrupted image file does not crash the preview pane
- **WHEN** the user selects a file with an image extension whose content is corrupted or truncated such that WIC cannot decode it
- **THEN** the preview pane SHALL display the no-preview-available fallback rather than crashing or hanging

### Requirement: Text and Source-Code File Preview
`FastFiles` SHALL render a plain, monospace text preview for plain-text and source-code files, reading the file's bytes directly from disk, sniffing text encoding (UTF-8 or UTF-16 byte-order-mark detection, falling back to the system code page), with syntax highlighting explicitly out of scope for this preview.

#### Scenario: Selecting a text or source-code file shows its content
- **WHEN** the user selects a single plain-text or source-code file
- **THEN** the preview pane SHALL display the file's textual content rendered in a monospace font, with no syntax highlighting applied

#### Scenario: An oversized text file is truncated with a visible indicator
- **WHEN** the selected text file exceeds the defined preview size/line ceiling
- **THEN** the preview pane SHALL render only up to that ceiling and SHALL display a visible indicator that the preview is truncated, rather than attempting to read and render the entire file

### Requirement: Graceful Fallback for Unsupported or Unrecognized File Types
When no registered preview provider applies to the currently selected file, `FastFiles` SHALL display a clear, non-error "no preview available" state rather than an empty pane, an error dialog, or a crash.

#### Scenario: Selecting a file with no matching provider shows the fallback state
- **WHEN** the user selects a single file whose type matches no registered preview provider (for example, an executable or an unrecognized binary format)
- **THEN** the preview pane SHALL display a clear "no preview available" indication rather than leaving the pane blank or reporting an error

### Requirement: Preview Reflects the Current Single-Item Selection
The preview pane SHALL update to reflect the currently selected item when exactly one file is selected, SHALL discard the result of any in-flight preview generation that no longer corresponds to the current selection, and SHALL NOT display a live preview when zero items or more than one item is selected.

#### Scenario: Preview updates as selection changes
- **WHEN** the user selects a different single file while a preview is already displayed
- **THEN** the preview pane SHALL begin generating a preview for the newly selected file and SHALL replace the displayed preview once generation completes

#### Scenario: Rapid selection changes do not render a stale preview
- **WHEN** the user changes the selection again before a previously requested preview has finished generating
- **THEN** the superseded preview generation's result SHALL be discarded on completion and SHALL NOT be displayed, with only the most recently requested selection's preview shown

#### Scenario: Multi-item selection shows no live preview
- **WHEN** more than one item is selected
- **THEN** the preview pane SHALL NOT attempt to render a live preview for any individual item
