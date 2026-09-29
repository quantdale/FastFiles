# Explore findings — overnight truth audit

Propose-time ledger. These rows are the claims inspected before any product edit. Apply extends this file; it does not replace a row with a later result without leaving the original classification visible.

| Claim | Evidence | Status |
| --- | --- | --- |
| Primary checkout is clean `main` at `ea3d64f7c144343ec6faf849143d3f545e6fab36`, equal to `origin/main` | `git fetch`, `git status --short --branch`, `git rev-parse HEAD`, `git rev-parse origin/main`, ahead/behind `0 0` | `VERIFIED_CURRENT` |
| The prompt's observed main SHA matches the live tip, so no reset to an older SHA is required | Prompt historical SHA and live `HEAD` | `VERIFIED_CURRENT` |
| PR #10 is merged | `gh pr view 10`: state `MERGED`, `mergedAt` `2026-09-29T15:11:47Z`, merge commit `ea3d64f7c144343ec6faf849143d3f545e6fab36` | `VERIFIED_CURRENT` |
| `docs/agent/WORKSTATE.md` says PR #10 remains unmerged under a no-merge hold and that no next local action remains | Workstate resume checkpoint dated 2026-09-29 | `FALSE` |
| Hosted CI for `ea3d64f` failed before any job step because of billing or spending limit | Run `36588334920`, event `push`, six jobs, zero steps, completed in about three seconds. Check-run annotation: "The job was not started because recent account payments have failed or your spending limit needs to be increased." | `VERIFIED_CURRENT` as `BLOCKED_EXTERNAL` |
| Historical Debug and release-test CTest `124/124`, Analyze, ASan `6/6`, strict OpenSpec `88/88`, local parity `11/0/0`, E2E `8/8`, UIA, and visual catalog `20/20` certify current `main` | Workstate attributes that matrix to executable candidate `6c40ab35793f1f9c03837f4b4dfa84b5964a032b`. This session did not rebuild or rerun it. | `UNPROVEN` for current `HEAD`; historical for `6c40ab3` only |
| The synchronous raw-volume `DeviceIoControl` finite completion bound is unproven | `legacy-protocol-correctness-and-raw-volume-cancellation-closure` task 5.4 and `final-residual-hardening-and-hosted-ci-recovery` task 2.3 remain unchecked. `VolumeScanner.cpp` still issues synchronous `DeviceIoControl` calls. No elevated driver run was attempted. | `VERIFIED_WITH_LIMITATION` |
| A detached worktree exists at `.kilo/worktrees/scythe-cloud` on `0dc3356` | `git worktree list` | `VERIFIED_CURRENT`; unrelated and untouched |
| Informational index, search, and projection benches exist and are not CTest duration gates | `tests/engine/bench_large_index_scalability.cpp` states it is intentionally not registered with CTest. Sibling benches are built as executables. | `VERIFIED_CURRENT` |
| `windows-performance-baselines` is an advisory run-envelope store, not a user-visible latency proof | `verify/capabilities/windows-performance-baselines/capability.json` | `VERIFIED_CURRENT` |
| Current binaries embed `ea3d64f` | Not inspected in this session | `UNPROVEN` |
| Production signing, clean-host install, elevated live MFT/USN, and physical display variants are available on this host | Not retested. No trust-store or elevation action was taken. | `UNPROVEN` until the external-frontier inventory is rerun; not inherited as closed |

No production source was edited during explore. No hosted workflow was dispatched. No billing, protection, signing, or trust-store setting was changed.

---

## Apply-start ledger extension — overnight truth audit (apply, session 1)

Re-derived live at apply start (`git fetch origin`, `gh`, `git worktree list`).
The propose-time rows above are preserved; this section does not replace a row with a
later result without leaving the original classification visible.

| Claim | Evidence | Status |
| --- | --- | --- |
| Apply is running on the dedicated overnight branch `feat/overnight-truth-performance-hardening-20260929` | `git branch --show-current`, `git status --short --branch` (tracking `origin/feat/overnight-truth-performance-hardening-20260929`) | `VERIFIED_CURRENT` |
| The primary worktree is clean at apply start | `git status --porcelain` produced no output | `VERIFIED_CURRENT` |
| Apply-start `HEAD` is `90339b9bc4b8e22324c05092e0c1e328acc65920`, one commit ahead of `origin/main` and zero behind | `git rev-parse HEAD` = `90339b9bc4b8e22324c05092e0c1e328acc65920`, `git rev-parse origin/main` = `ea3d64f7c144343ec6faf849143d3f545e6fab36`, `git rev-list --left-right --count origin/main...HEAD` = `0\t1` | `VERIFIED_CURRENT` |
| `90339b9` is the propose-only commit: it touches only `docs/agent/WORKSTATE.md`, OpenSpec change files, and the change `.openspec.yaml`. No `src/`, `tests/`, `cmake/`, or `verify/` payload | `git show --stat 90339b9` | `VERIFIED_CURRENT` |
| `90339b9` is a documentation-only tip relative to the implementation candidate | same `git show --stat`; the implementation tree on this branch equals `ea3d64f`'s executive tree | `VERIFIED_CURRENT` |
| PR #10 is merged into `main` at `ea3d64f` and is not reversed by this change | `gh pr view 10` (`MERGED`, merge commit `ea3d64f7c144343ec6faf849143d3f545e6fab36`) | `VERIFIED_CURRENT` |
| PR #11 is open for this change against `main` and is the reviewable overnight pull request | `gh pr list --state all`: number `11`, head `feat/overnight-truth-performance-hardening-20260929`, base `main`, state `OPEN`, merge commit `null` | `VERIFIED_CURRENT` |
| No hosted run for the apply-start pushed SHA `90339b9b` executed a single step | Run `36595140981` (event `push`) and run `36595149447` (event `pull_request`) for `90339b9bc4b8e22324c05092e0c1e328acc65920`: each has six jobs, every job has `"steps": []`, all six conclusions `failure`, run window approximately five seconds | `VERIFIED_CURRENT` |
| The zero-step hosted failure for `90339b9b` is the known account-payment / spending-limit block, not a product or test failure | Check-run annotation for the same commit, retrieved with `gh api /repos/quantdale/FastFiles/check-runs/<id>/annotations`: "The job was not started because recent account payments have failed or your spending limit needs to be increased. Please check the 'Billing & plans' section in your settings" | `BLOCKED_EXTERNAL` (`HOSTED_CI`) |
| No further hosted workflow will be dispatched to obtain a different label for the same known-unstartable workflow | This apply session dispatched no `workflow_dispatch` and no manual rerun; each later push is inspected once at task 6.4 | `VERIFIED_CURRENT` |
| The detached auxiliary worktree at `.kilo/worktrees/scythe-cloud` is still on `0dc3356` and untouched | `git worktree list` | `VERIFIED_CURRENT` |
| The last recorded executable candidate `6c40ab35793f1f9c03837f4b4dfa84b5964a032b` is not the apply-start tip and certifies nothing here | Ledger row above; `90339b9` has no implementation payload over that ancestor | `STALE` as current-head certification |
| The historical `124/124` CTest, Analyze, ASan `6/6`, strict OpenSpec `88/88`, parity `11/0/0`, E2E `8/8`, and visual `20/20` results describe `6c40ab3` | `docs/agent/WORKSTATE.md` attributes them to `6c40ab35793f1f9c03837f4b4dfa84b5964a032b`; this apply session rebuilds and reruns them in section 3 | `UNPROVEN` for the apply-start tip |
| The synchronous raw-volume `DeviceIoControl` finite completion bound is unproven | Ledger row above: `legacy-protocol-correctness-and-raw-volume-cancellation-closure` task 5.4 and `final-residual-hardening-and-hosted-ci-recovery` task 2.3 remain unchecked | `VERIFIED_WITH_LIMITATION` |

**Commands used at apply start:** `git fetch origin`, `git status --short --branch`,
`git rev-parse HEAD`, `git rev-parse origin/main`,
`git rev-list --left-right --count origin/main...HEAD`, `git worktree list`,
`git show --stat HEAD`, `gh pr list --state all`,
`gh run view 36595140981 --json jobs`, `gh run view 36595149447 --json jobs`,
`gh api /repos/quantdale/FastFiles/actions/runs/36595140981`,
`gh api /repos/quantdale/FastFiles/commits/<sha>/check-runs`,
`gh api /repos/quantdale/FastFiles/check-runs/<id>/annotations`.

No production source was edited for this ledger entry. No billing, protection, signing,
or trust-store setting was touched.

---

## Review correction — post-apply audit (apply session 1 reviewed)

The apply finished and was then audited against the live tree. Four material defects were found
and fixed. Nothing was silently rewritten; the corrections are recorded here and in the affected
records.

| Defect found in the apply result | Classification | Correction |
| --- | --- | --- |
| `openspec/lifecycle.json` left the change `active` while all 28 tasks were checked, so `Get-ProjectStatusLifecycleModel` returned `FAIL` with `openspec-lifecycle-active-without-open-tasks`, and `verify/status/Get-ProjectStatus.ps1` printed `OpenSpec lifecycle model: FAIL`. The handoff had reported that gate PASS. | Defective completion claim | Set `state` to `completed`. The model now returns `PASS` with zero errors and the frontier `PASS`, verified live. |
| `benchmark-baseline.json` was the **candidate** (dirty worktree) run published as the baseline, and it carried no source SHA. `ffengine_bench_first_generation_publish`'s own raw logs embed `b449d95352460218820a79eb38486eed0c0592eb` in the header, and the JSON's `TOTAL` of 106.703 equals the apply session's "after" row. | Mislabeled evidence | Renamed to `candidate-run-mislabeled-as-baseline.json` and preserved, not deleted. The real baseline was re-measured from the clean shipped tree on an idle host and recorded as `baseline-shipped-tree.json` with `source_sha` `e2a573c284cdbb3399cf99cf1d1d9cec9ee17748`. |
| `successor-candidate-and-revert.md` cited `performance/before-after-literal-matcher.json` and `performance/before-after-first-generation.json`, which did not exist, and only one of the two runs behind its before/after table was committed, under the misleading name `benchmark-baseline.json` with no source SHA. | Missing evidence | Both files now exist. The review recovered the apply session's pre-edit run from the ignored build tree, where it had been left uncommitted as `build/_bench_baseline_pre.json`, and committed it as `apply-session-before-run.json`; its 14 rows match the prose before column. The post-edit run is committed as `apply-session-after-run.json`. The misnamed baseline is preserved renamed as `candidate-run-mislabeled-as-baseline.json`. |
| Task 6.3 claimed `git diff --check` passed, but `git diff ea3d64f..HEAD --check` reported 20 `new blank line at EOF` errors on the committed `evidence/performance/raw/*.log` files. Task 4.2 had lost its identifier and scope text in an apply-session edit, leaving a checked line reading `- [x] with warm-up and at least five measured repetitions…`. | False gate claim and document corruption | All 20 files normalized; the check is clean. Task 4.2's identifier, scope, and an evidence note were restored, and the numbering 4.1–4.4 is contiguous again. Task 6.3 now carries the missing evidence note. |

Separately, the apply session's claim that the 3.7 percent first-generation regression was
"consistent, not noise" is withdrawn: its own persisted raw logs span 104.103 to 148.927 ms. The
candidate remains correctly reverted, on the stronger and honest ground that no user-visible
improvement was demonstrated.

No product source changed in this correction. No workflow was dispatched. No billing, branch-rule,
protection, signing, or trust-store setting was touched.

### Correction of the correction

The review's own initial finding that the apply session's before column was unbacked was too harsh
and is withdrawn here. The apply session's pre-edit run was recoverable from the ignored build tree
as `build/_bench_baseline_pre.json` and is now committed as
`evidence/performance/apply-session-before-run.json`; all 14 rows match the prose table. The apply
session's real defect was committing only the post-edit run under the name
`benchmark-baseline.json` with no source SHA, which this review corrected by re-measuring the clean
shipped tree on an idle host (127.679 ms TOTAL) and by committing both of its runs as evidence.
