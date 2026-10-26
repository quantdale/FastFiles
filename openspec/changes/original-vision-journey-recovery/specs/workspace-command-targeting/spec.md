## Purpose

Keep filesystem commands bound to the user's visible selection across browsing, search and storage workspaces.

## ADDED Requirements

### Requirement: Shutdown qualification observes real application closure

Native restore qualification SHALL close every app lifecycle through the actual public window-close path and retain each route and observed exit under the unchanged deadline. Forced fixture teardown SHALL NOT count as graceful application shutdown. Worker callback publication SHALL synchronize access to the retired shell target without claiming extended HWND lifetime.

#### Scenario: Restored Home does not close
- **WHEN** the third restored Home app does not exit within the native close deadline
- **THEN** restoration qualification fails and retains its close observation instead of accepting later fixture cleanup as a pass

### Requirement: Settings completion controls fit the real dialog

Settings SHALL reserve its intended DPI-scaled client area in addition to the actual non-client frame. OK and Cancel SHALL have positive, fully visible bounds inside the dialog, and visible Cancel SHALL remain pointer-reachable without changing saved settings.

#### Scenario: Settings at 120 DPI
- **WHEN** Settings is opened on the measured120DPI desktop
- **THEN** caption and borders do not consume the footer's client layout and both completion controls fit the visible frame

### Requirement: Command choices remain visibly above their workspace

The real native palette edit and command list SHALL paint above Search, Storage and browsing controls. Native child creation SHALL preserve sibling paint clipping as well as z-order. A client click outside the palette SHALL dismiss it without triggering the underlying workspace action.

#### Scenario: Palette over Storage
- **WHEN** the palette opens over Storage and the underlying controls repaint
- **THEN** its visible command choices remain legible and reachable

#### Scenario: Outside dismissal
- **WHEN** the user clicks the navigation region outside the palette
- **THEN** the palette closes and the current Storage workspace remains active

### Requirement: Accessibility follows workspace presentation

Browse and Home semantic descendants SHALL be unavailable while another workspace replaces their presentation. Retained elements SHALL remain unavailable after restoration; new Browse elements SHALL describe the restored presentation. Native Search/Storage controls and active Treemap descendants SHALL remain reachable.

#### Scenario: Search replaces Home
- **WHEN** Search replaces Home while a client retains a Home element
- **THEN** the retained element cannot invoke or expose hidden Home content, and closing Search exposes a fresh Home fragment

#### Scenario: Storage replaces Browse
- **WHEN** Storage replaces browsing columns
- **THEN** hidden browsing items cannot receive focus or actions through UIA, while Storage list and Treemap retain their real semantics

### Requirement: Accessibility actions retain original intent through dispatch

Native list and Home Invoke/Clear actions SHALL validate the original item identity and owner/fragment lifetime on the owning UI thread before invoking the canonical mutation. Failed dispatch SHALL NOT execute shell mutations on a foreign caller thread. Teardown SHALL invalidate accessibility lifetime before draining pending messages.

#### Scenario: Home changes before dispatch
- **WHEN** a Home item changes or its fragment retires between provider resolution and owner-thread action
- **THEN** the action fails without invoking its replacement or reviving the retired fragment

#### Scenario: Search changes before dispatch
- **WHEN** a search row is replaced while its action waits for the UI thread
- **THEN** identity validation rejects the action instead of opening the new occupant

### Requirement: Commands preserve visible target ownership

Item commands SHALL use the selection of the visible workspace, including applicability classification. They SHALL NOT fall back to a hidden browsing selection when that selection is empty, invalid, synthetic or unavailable. Commands requiring a browsing destination or inline browsing editor SHALL be unavailable in alternate workspaces unless explicitly supported there.

#### Scenario: Storage differs from browse selection
- **WHEN** browsing selected A and Storage selects B
- **THEN** Copy Path, Cut, Open and Delete target B through existing operation handlers, never A

#### Scenario: Target disappears
- **WHEN** an explicit selected target no longer exists
- **THEN** the command is rejected without invoking a handler on the hidden browse selection

### Requirement: Category summaries cannot become filesystem targets

Category summaries SHALL NOT be converted into filesystem paths or support filesystem mutation commands. Activation SHALL refine storage analysis or remain a non-file action.

#### Scenario: Category label matches real sibling
- **WHEN** a category named Images is selected and a sibling file named Images exists
- **THEN** category actions cannot open, cut or delete that sibling

#### Scenario: Category refinement enters Treemap
- **WHEN** a category summary refines the current list and the user enters Treemap
- **THEN** Treemap retains the same category scope instead of showing unfiltered siblings

### Requirement: Storage list exposes real semantic rows

Storage lists SHALL expose their rendered rows through semantic UI Automation with names, metadata, physical geometry, selection, scroll and activation actions backed by the same production model and mutations. Runtime identities SHALL be distinct from Search and stale rows SHALL fail rather than retarget. Client-held providers SHALL become safe unavailable elements after owner destruction.

#### Scenario: Storage file selection through accessibility
- **WHEN** an accessibility client selects a rendered file row and invokes Copy Path
- **THEN** the copied path identifies that visible file and sorting preserves the selected identity

#### Scenario: Synthetic category activation
- **WHEN** a client invokes a category summary
- **THEN** the same category refinement as pointer activation occurs without constructing a filesystem path

#### Scenario: Replaced row
- **WHEN** a client retains a provider while its row is replaced or its owner destroyed
- **THEN** access reports element unavailable and never targets a new occupant

### Requirement: Native conflict actions are accessible and truthful

Conflict decision controls SHALL expose their visible names through native accessibility and remain reachable through Windows Tab navigation. Skip SHALL retain initial focus. Dialog background and checkbox labels SHALL follow the shared light/dark tokens and existing High Contrast behavior. Cancel and Skip SHALL preserve both files, Keep both SHALL preserve the existing destination and create the incoming copy under a distinct name, and Replace SHALL update only the intended marked destination.

#### Scenario: Named keyboard-accessible conflict decisions
- **WHEN** copying a file into a folder containing the same name
- **THEN** Replace, Skip, Keep both, Cancel and the apply-to-all checkbox expose their visible names and participate in Tab navigation

#### Scenario: Safe cancellation
- **WHEN** Cancel is activated in the actual conflict dialog
- **THEN** incoming and existing destination contents remain unchanged

#### Scenario: Dark conflict dialog
- **WHEN** the workspace is dark and a conflict decision is presented
- **THEN** its real background and checkbox label remain legible without changing the operation policy

### Requirement: Native shutdown preserves owner lifetimes

The UI SHALL destroy shell COM owners before OLE teardown, abandon queued icon work on stop while joining active resolution, and decline saturated file-type lookup admission without releasing unfinished task state or caching an unstarted lookup. Opt-in shutdown diagnostics SHALL carry phase/outcome, PID and timestamp without file paths or content.

#### Scenario: Saturated file-type lookup
- **WHEN** the unfinished lookup limit is reached
- **THEN** a new lookup is declined without joining or discarding existing tasks, and completed tasks later reopen admission

#### Scenario: Close qualification remains truthful
- **WHEN** native close does not exit within the existing10s bound
- **THEN** qualification remains failed and phase evidence is retained without attributing an unproven cause
