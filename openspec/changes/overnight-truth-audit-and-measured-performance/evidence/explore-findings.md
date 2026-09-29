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
