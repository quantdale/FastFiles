## ADDED Requirements

### Requirement: Outstanding Conflict Question Is Answered On Owner Teardown
If the FastFiles owner window is destroyed or the file-operation worker is stopped while the in-app conflict dialog is still running its nested message pump, FastFiles SHALL treat the outstanding conflict as Cancel (no remaining transfers from that batch) and SHALL publish that decision to the worker before freeing the prompt object. The dialog's remaining UI-thread writes SHALL target only an object that is still alive.

#### Scenario: Taskbar close during the conflict dialog
- **WHEN** the conflict dialog is visible and the owner window processes WM_DESTROY (taskbar close, SC_CLOSE, or equivalent) before the user chooses Replace, Skip, Keep Both, or Cancel
- **THEN** the worker SHALL observe a Cancel decision, SHALL NOT wait indefinitely, and SHALL NOT write to a destroyed prompt object

#### Scenario: Apply-to-all does not survive a torn-down batch
- **WHEN** a batch is cancelled by owner teardown in the middle of conflict prompting
- **THEN** no apply-to-all choice from that batch SHALL apply to a later, separate operation
