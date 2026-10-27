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

## Fetch checkpoint before shortcut successor publication — 2026-10-28T02:25:41.754195+08:00

This table is an observed fetch checkpoint, not a live Git database. The successor implementation is27daa17 with test-only eda; pre-documentation local baseline d14308c. Later evidence-only commits are derived externally. Main remains `a26fe609439a18c128b2a2e557bc7eb2534fa128`. No dirty or diverged branch was pulled/reset, and no shared history was rewritten.

| PR | Remote SHA | Same-name local SHA | Relationship | Worktree/dirty |
|---|---|---|---|---|
| 14 | `d3884eecd03805a91fe5d12d7a90ceb1b5c082f2` | `b77d61e7ad21b549be1b2c29bda35a85ce4a97e9` | diverged 4 ahead/21 behind | D:/Documents/tryPython/FastFiles; 3 dirty |
| 15 | `8bffb329779ee0a431aaed37fa92925a4b373d9b` | none | no same-name local branch | unattached; 0 dirty |
| 16 | `e75a230db9ac89f6a638754cb6ad672735e4d644` | none | no same-name local branch | unattached; 0 dirty |
| 17 | `4a1457660420f8021295ebadd505f8baf63dfbac` | `4a1457660420f8021295ebadd505f8baf63dfbac` | equal | D:/Documents/tryPython/FastFiles-worktrees/acceptance-20261025; 0 dirty |
| 18 | `981a9ba678a191107c78eec22a60c9620eb5848e` | `981a9ba678a191107c78eec22a60c9620eb5848e` | equal | D:/Documents/tryPython/FastFiles-worktrees/product-20261025; 0 dirty |
| 19 | `d2187aa45d0023a3878b94c8891989194ed4f9ac` | `d2187aa45d0023a3878b94c8891989194ed4f9ac` | equal | D:/Documents/tryPython/FastFiles-worktrees/column-uia-20261025; 0 dirty |
| 20 | `a07970ecb4a025a463d8809641fa5bf2c8718a0d` | `a07970ecb4a025a463d8809641fa5bf2c8718a0d` | equal | D:/Documents/tryPython/FastFiles-worktrees/native-text-20261026; 0 dirty |
| 21 | `b028e1165e7d6199bc3cc04d8fece0a9b5f443aa` | `b028e1165e7d6199bc3cc04d8fece0a9b5f443aa` | equal | D:/Documents/tryPython/FastFiles-worktrees/conflict-actions-20261026; 0 dirty |
| 22 | `3a9a2460b67d013f07db00c7016ee9b69c7f162a` | `3a9a2460b67d013f07db00c7016ee9b69c7f162a` | equal | D:/Documents/tryPython/FastFiles-worktrees/fixture-ownership-20261026; 0 dirty |
| 23 | `fef65837a30aeb6fa44283490d4e25cb584aab3e` | `fef65837a30aeb6fa44283490d4e25cb584aab3e` | equal | D:/Documents/tryPython/FastFiles-worktrees/fileops-feedback-20261026; 0 dirty |
| 24 | `b683f33ba271c0de2b7eb4bc38faf5f9f391fa8c` | `b683f33ba271c0de2b7eb4bc38faf5f9f391fa8c` | equal | D:/Documents/tryPython/FastFiles-worktrees/shutdown-lifetime-20261026; 0 dirty |
| 25 | `95c1589a7b3ca6511aea93961509b819a1c64270` | `95c1589a7b3ca6511aea93961509b819a1c64270` | equal | D:/Documents/tryPython/FastFiles-worktrees/cancel-command-20261026; 0 dirty |
| 26 | `275172ac97b65366a4a339359fc2cb60dfd754ba` | `275172ac97b65366a4a339359fc2cb60dfd754ba` | equal | D:/Documents/tryPython/FastFiles-worktrees/storage-ranking-20261027; 0 dirty |
| 27 | `ec5bd9579e9456088ffbda988a900c2ebe8bb934` | `ec5bd9579e9456088ffbda988a900c2ebe8bb934` | equal | D:/Documents/tryPython/FastFiles-worktrees/restore-conflicts-20261027; 0 dirty |
| 28 | `9f12c3a9c959f1682ad6ee44ed644034c5d1add0` | `9f12c3a9c959f1682ad6ee44ed644034c5d1add0` | equal | D:/Documents/tryPython/FastFiles-worktrees/uia-proxy-bootstrap-20261027; 0 dirty |
| 29 | `733a45b14d4a94cca1837248fa8f1c418fa55793` | `733a45b14d4a94cca1837248fa8f1c418fa55793` | equal | D:/Documents/tryPython/FastFiles-worktrees/quit-preservation-20261027; 0 dirty |
| 30 | `430b7111cb3acf91aadc28472d6fdf3146ee4f4d` | `430b7111cb3acf91aadc28472d6fdf3146ee4f4d` | equal | D:/Documents/tryPython/FastFiles-worktrees/execution-consent-20261027; 0 dirty |
| 31 | `30a2bc60bc802648259ca2dad819b54c9c420211` | `30a2bc60bc802648259ca2dad819b54c9c420211` | equal | D:/Documents/tryPython/FastFiles-worktrees/address-discovery-20261028; 0 dirty |

All listed PRs remain OPEN/unmerged;14 is non-draft and the others are draft. Full branch bases, worktree SHAs and uncommitted filenames are retained in `openspec/changes/prevent-shortcut-creation-overwrite/evidence/final-certification-20261028/source-ledger.json`. Original root remains b77 with its same three owner-dirty files, preserved. Clean implementation worktrees are isolated; local integration3ac is a simulation only. PR31 is the published verification successor on28; shortcut successor targets30. Current source freshness is established by fetch and exact parent43/9f equality before publishing.

Publication follow-up: draft [PR32](https://github.com/quantdale/FastFiles/pull/32) is the isolated shortcut successor on PR30; draft [PR31](https://github.com/quantdale/FastFiles/pull/31) targets PR28. Both remain unmerged. Their final live heads/upstreams/hosted status are derived after normal push, not embedded self-referentially here. Evidence sources27daa17/eda/d143/3ac remain immutable.
