# Bounded navigation verification — focused qualification

## Source truth
Sole writer/root, isolated address-discovery worktree, verified PR28 origin9f12c3a9c959f1682ad6ee44ed644034c5d1add0 after fresh fetch. No app source or process/security changes. Parent lacks combined registered-window cleanup, so no standalone application launch is attempted.

## Original behavior and limits
Combined b868 owned flow fails before first copy because recursive tab discovery does not find Source; zero cases certified, cleanup passes. Diagnostics contain the exact Source request and three-entry refresh. Two later unchanged-code single-navigation probes succeed. The first injection did not reach the exported reference (no observation claimed); corrected module-context probe records recursive/direct native tab1: Source and exact owned Source column9. This does not establish the first runtime failure's cause and is not a flaky-test waiver.

The actual helper DFS uses1500ms/2000-node bounds and may traverse browser rows before native chrome. Its unconditional recommit can send Enter after focus returns to browsing. Real-function closed headless test has six failed assertions on the original source; these prove deterministic source-contract weaknesses, not actual DFS starvation during the original run.

## Repair and focused results
Direct main/chrome children only; process/class/tab control IDs8300–8399 retained, excluding close buttons. No file-row traversal, broad timeout increase, application hooks or weaker leaf-success predicate. Recommit requires real boolean HasKeyboardFocus on the original edit plus owned foreground; unreadable/non-boolean state cannot authorize input. Existing any-tab/leaf predicate is retained and does not certify active-pane/full-path navigation by itself.

Final eleven actual-function closed checks PASS: readable direct tab when recursive budget is exhausted, zero recursive/row calls, no Enter onto file list, one genuine edit recommit, foreign tab/chrome rejection, close-control rejection, unreadable/stale/non-boolean state rejection, valid tab8310. Full existing driver tests PASS; strict93/93 and drift PASS. No skipped test relabeled. Independent reviewer unavailable; root self-review only. Actual safe combined application qualification remains pending.
