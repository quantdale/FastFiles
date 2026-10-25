# Original-vision campaign source ledger

Checkpoint: 2026-10-25, after `git fetch origin --prune --tags`. Re-derive live facts on resume; these are historical observations, not reset targets.

| Reference | Local / remote at intake | Relationship / state |
| --- | --- | --- |
| main | local 7f9ce9a1; remote a26fe609439a18c128b2a2e557bc7eb2534fa128 | Clean, behind one; fast-forwarded using `git pull --ff-only` in main-sync worktree |
| PR14 large-index branch | original checkout b77d61e7; remote d3884eecd03805a91fe5d12d7a90ceb1b5c082f2 | Diverged: 4 local-only / 21 remote-only; original checkout dirty; preserved |
| PR15 cold-search branch | remote 8bffb329779ee0a431aaed37fa92925a4b373d9b | OPEN draft, base PR14 branch; fresh acceptance branch created at exact remote tip |
| PR16 theme-toggle branch | remote e75a230db9ac89f6a638754cb6ad672735e4d644 | OPEN, base main; preserved unmerged |
| measured-performance branch | bdd7d7b local and remote | Equal; PR12 already merged |
| performance-closure branch | 3c1c9c2 local and remote | Equal; PR13 already merged |

`gh pr list --state all --json ...` confirmed PR14 -> main, PR15 -> PR14, PR16 -> main. No newer published feature branches appeared in the fetched inventory. No existing PR was merged or rewritten.

## Worktree ownership

| Worktree | Session responsibility | Intake |
| --- | --- | --- |
| `D:/Documents/tryPython/FastFiles` | Pre-existing owner work; untouched | b77d61e7, dirty WORKSTATE, rebuild-root-cause evidence and test_projection.cpp |
| `.kilo/worktrees/scythe-cloud` | Pre-existing detached work; untouched | 0dc33562 |
| `../FastFiles-worktrees/main-sync` | Clean local main synchronization | Fast-forwarded to a26fe609 |
| `../FastFiles-worktrees/product-20261025` | Primary product integrator / native runtime owner | Clean a26fe609; branch feat/original-vision-product-20261025 |
| `../FastFiles-worktrees/acceptance-20261025` | Acceptance fixture/spec writer | Clean 8bffb329; branch fix/acceptance-frontier-20261025 |
| `../FastFiles-worktrees/search-20261025` | Bounded search implementation writer | Clean a26fe609; isolated source/test commit 93b054c8, integrated by primary |

Each writer owns one worktree. Read-only audit/review agents never modify implementation worktrees. Native UI sessions and heavy builds are serialized. The original dirty files are not session-owned and must not be committed, reset or pulled over.

## Freshness and dependency decision

First product changes were made only after `HEAD == origin/main == a26fe609` and a clean status in the product worktree. Acceptance changes start at exact PR15 tip. Product work depends on native UI contracts already in main, so its diff does not duplicate PR14/15/16. Acceptance repair remains stacked on PR15. Owner controls all merges; use a disposable merge simulation for combined validation.

Native conflict successor uses conflict-actions-20261026 / fix/accessible-conflict-actions-20261026, verified fresh PR18 base981a9ba before edits. Immutable qualified sourcef106541 and C++ implementation1dbff32 remain separate from PR19/20. Disposable combined63ffa59 includes both siblings and the conflict/harness changes, with actual8/8 ordinary and8/8 conflict journeys plus14 focused checks; no shared merge. Fresh fetch still resolves maina26fe609 and PR18 981a9ba. Publication state is derived from live refs/PR API.
