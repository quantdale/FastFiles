# bookmarks-and-sidebar Specification

## Purpose
The sidebar: user-added bookmarks persisted locally, automatic known-folder discovery, collapsible Drives/Known Folders/Bookmarks sections, and navigation via sidebar entries.

## Requirements

### Requirement: User-Added Bookmarks Persisted Locally
The system SHALL allow the user to add a bookmark for any folder they can navigate to, assign or edit its display name, reorder it relative to other bookmarks, and remove it. The system SHALL persist the bookmark list locally (per user) so it survives an application restart, and SHALL load it at startup.

#### Scenario: Adding a bookmark for the current folder
- **WHEN** the user invokes "Add to bookmarks" while a navigation context's current path is `D:\Projects\FastFiles`
- **THEN** a new bookmark entry for `D:\Projects\FastFiles` SHALL appear in the sidebar's Bookmarks section

#### Scenario: Renaming a bookmark
- **WHEN** the user renames a bookmark's display name from its default to a custom label
- **THEN** the sidebar SHALL display the custom label for that bookmark instead of the default

#### Scenario: Reordering bookmarks
- **WHEN** the user drags a bookmark to a new position within the Bookmarks section
- **THEN** the bookmark list SHALL reflect the new order, and that order SHALL be preserved after an application restart

#### Scenario: Removing a bookmark
- **WHEN** the user removes a bookmark
- **THEN** that entry SHALL no longer appear in the sidebar's Bookmarks section, and SHALL NOT reappear after an application restart

#### Scenario: Bookmarks persist across restart
- **WHEN** the user adds one or more bookmarks and then restarts the application
- **THEN** all previously added bookmarks SHALL still appear in the sidebar's Bookmarks section, in their last saved order

#### Scenario: Bookmark pointing to a path that no longer exists is not silently removed
- **WHEN** a bookmarked folder has since been deleted or is temporarily unreachable (e.g. a disconnected external drive)
- **THEN** the bookmark SHALL remain visible in the sidebar, and clicking it SHALL surface the standard nonexistent/inaccessible-path feedback rather than being silently removed from the list

### Requirement: Automatic Known-Folder Discovery
The system SHALL automatically discover the current user's known folders (including at minimum Desktop, Documents, Downloads, Pictures, Videos, and Music) using the Windows Known Folder API (`SHGetKnownFolderPath` and/or `IKnownFolderManager`), and SHALL surface them in the sidebar's Known Folders section, separate from user-added bookmarks. The system SHALL reflect any known-folder redirection (a known folder relocated to a non-default path) rather than assuming default paths.

#### Scenario: Known folders appear without user action
- **WHEN** the application starts for a user who has never added any bookmarks
- **THEN** the sidebar's Known Folders section SHALL already list Desktop, Documents, Downloads, Pictures, Videos, and Music (or the subset registered on the system) pointing at their actual resolved paths

#### Scenario: Redirected known folder resolves to its actual location
- **WHEN** the user's Documents known folder has been redirected to a non-default path (e.g. a second drive)
- **THEN** the sidebar's Documents entry SHALL navigate to the redirected path, not the OS default path

#### Scenario: Known folders are not user-editable
- **WHEN** the user views the Known Folders section
- **THEN** the system SHALL NOT offer rename, reorder, or remove actions on those entries (distinguishing them from the user-editable Bookmarks section)

#### Scenario: Unresolvable known folder still appears with clear feedback
- **WHEN** a known folder is registered by the OS but its target path is currently unreachable
- **THEN** the entry SHALL still appear in the Known Folders section, and clicking it SHALL surface the standard nonexistent/inaccessible-path feedback rather than being omitted from the list

### Requirement: Collapsible Sidebar Combining Drives, Known Folders, and Bookmarks
The system SHALL provide a single sidebar panel containing three sections — Drives, Known Folders, and Bookmarks — each independently collapsible and expandable, and the sidebar as a whole SHALL be collapsible to a minimal-width or hidden state to reclaim horizontal space for the navigation surface. The system SHALL remember the sidebar's overall collapsed/expanded state and each section's collapsed/expanded state across application restarts.

#### Scenario: Sidebar shows all three sections by default
- **WHEN** the application starts for the first time with no prior saved sidebar state
- **THEN** the sidebar SHALL display Drives, Known Folders, and Bookmarks sections, each expanded

#### Scenario: Collapsing one section leaves the others expanded
- **WHEN** the user collapses the Bookmarks section
- **THEN** the Drives and Known Folders sections SHALL remain expanded and usable, and the Bookmarks section SHALL show only its header

#### Scenario: Collapsing the whole sidebar reclaims space for navigation
- **WHEN** the user collapses the entire sidebar
- **THEN** the sidebar SHALL shrink to a minimal-width or hidden state showing only a re-expand affordance, and the navigation surface SHALL expand to use the reclaimed width

#### Scenario: Sidebar and section collapse state persists across restart
- **WHEN** the user collapses the sidebar's Bookmarks section and the sidebar itself, then restarts the application
- **THEN** the sidebar SHALL restart in the same collapsed states for both the overall panel and the Bookmarks section

### Requirement: Navigating via Sidebar Entries
The system SHALL navigate the currently active navigation context (the active tab, or the active pane in dual-pane mode) to the path represented by a clicked Drives, Known Folders, or Bookmarks entry, without opening a new tab as the default action.

#### Scenario: Clicking a drive navigates the active context
- **WHEN** the user clicks a drive entry in the Drives section
- **THEN** the active navigation context SHALL navigate to that drive's root path

#### Scenario: Clicking a bookmark navigates the active context
- **WHEN** the user clicks a bookmark entry
- **THEN** the active navigation context SHALL navigate to that bookmark's path

#### Scenario: Secondary action opens a sidebar entry in a new tab
- **WHEN** the user middle-clicks (or uses the equivalent context-menu action on) a sidebar entry
- **THEN** the system SHALL open that entry's path in a new tab rather than navigating the currently active context

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
