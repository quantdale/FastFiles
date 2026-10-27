# Design

## Evidence and limits
Combined b868 owned operations fails before first copy when the helper cannot find Source tab labels. Source request and three-entry refresh exist; cleanup passes. Two single-navigation probes later pass unchanged code. Corrected public observation captures recursive/direct tab1: Source and exact Source column9. The original runtime cause remains unproven; no OS or product attribution is made.

The actual helper searches all Button descendants with a1500ms/2000-node DFS and retries Enter solely on foreground ownership, although focus can already be in the file list. Existing Get-UiaTabCount and Find-UiaNavigationButton demonstrate direct native chrome enumeration. Closed headless tests of the real function reproduce six failing assertions for bounded discovery and unsafe recommit. These are deterministic source-contract proofs, not proof that DFS starvation caused the observed native failure.

## Decision
Read only direct main children and native navigation chrome children; validate process/class/tab control range before reading labels. Preserve the existing requested-leaf success predicate and timeout. Do not accept unreadable controls or provider failure. Retry commit only if the original edit still publicly reports boolean HasKeyboardFocus and the owned main remains foreground. Preserve one bounded retry when truly editing. Never focus a hidden edit or add application hooks.

## Boundaries
This does not strengthen the existing any-tab/leaf predicate into active-pane/full-path certification, qualify Explorer drag, or waive any failed journey. Native full operations and exact identity checks remain required. No full source equivalence from component equality, no independent review if provider resources are unavailable.
