## 1. Establish boundary
- [x] 1.1 Demonstrate row replacement after Resolve causes wrong-target actions.
- [x] 1.2 Verify current source and isolate successor worktree.

## 2. Atomic actions
- [x] 2.1 Introduce original-identity owner-thread action dispatch with lifetime validation.
- [x] 2.2 Validate identity atomically with selection/focus/scroll mutation and activation target capture.
- [x] 2.3 Preserve membership semantics, hidden/retired fragment rejection and post-lock callbacks.

- [x] 2.4 Reject stale column focus and marshal root/pane/column keyboard focus; assert real row focus without changing selection.

- [x] 2.5 Reconcile owner-thread actions with visible shell navigation, preview and focused-column reveal; retain filename edge in narrow panes and test actual focus bounds.

- [x] 2.6 Clip Browse paints to each pane and publish geometry from the actual frame offsets; inspect unmasked narrow before/after captures.

## 3. Qualification
- [x] 3.1 Run focused regressions and warning-clean builds.
- [ ] 3.2 Exercise real native journeys and broader local checks; record unavailable independent review honestly.
- [ ] 3.3 Reconcile OpenSpec, workstate, provenance and remote before committing/publishing an unmerged checkpoint.
