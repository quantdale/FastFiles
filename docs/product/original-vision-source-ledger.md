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

## 2026-10-26 verified successor checkpoint

Fresh fetch and PR API confirm maina26fe609; PR14d3884eec ->main OPEN; PR15 8bffb329 ->PR14 OPEN draft; PR16e75a230d ->main OPEN draft. Separate acceptance PR17 067eaab ->PR15, product PR18 981a9ba ->main, and identity/focus PR19 implementation93cd2b6 ->PR18 are all OPEN draft. No owner-controlled merge was executed. Main-sync and product/acceptance implementations remain clean/equal their remotes; original b77d61e remains dirty/diverged and untouched.

Additional worktrees: storage-uia090f7e2 and visible-uia5456f1 are clean isolated implementation history integrated into PR18; search8a4f588 is also preserved. Column-uia starts from verified PR18 and publishes PR19. Disposable integrationa0afd5f locally combines PR16/14/15/17/18/19; it is clean and unpushed. Native-text starts at fresh PR18 981a9ba as a purpose-specific sibling for narrow status text, currently session-owned edits; it contains no PR19 commits. The root is the only writer; frozen qualification worktrees are read-only and native sessions remain serialized. This ledger records checkpoint observations, never the SHA of its containing commit.

## Native status sibling and combined qualification — 2026-10-26

Fresh fetch/PR API: PR14d3884eec, PR15 8bffb329, PR16e75a230d, PR17 067eaab, PR18 981a9ba and PR19d2187aa remain OPEN; main remainsa26fe609. Original three dirty files remain preserved atb77d61e; main-sync and published implementation worktrees stay clean/equal remote. Status implementationcd1f429 starts from verified PR18 and contains no PR19/16 commits. Intended draft base is PR18. Disposable integrationa3e20ab combines all six existing PRs plus status without C++ conflicts; it is local/unpushed. No existing PR or main was merged. Qualification/equivalence is scoped in the status evidence, not inferred from ancestry alone.
