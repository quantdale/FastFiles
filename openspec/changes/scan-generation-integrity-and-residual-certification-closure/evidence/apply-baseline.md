# Apply baseline and hosted hold (tasks 1.1, 8.1, 8.2)

## Task 1.1 — initial scaffold snapshot

The table below is the snapshot recorded when the change scaffold was
created. It is retained as historical evidence and is **not** the state from
the later apply resume.

Recorded from the local clone, not from any remembered SHA. `git fetch --prune
origin` ran first; nothing was reset, rewritten, or stashed.

| Fact | Live value |
| --- | --- |
| Branch | `main` |
| HEAD | `1172c1a1e6aa9c1a726d52abdade0975b6979e4b` |
| `origin/main` | `1172c1a1e6aa9c1a726d52abdade0975b6979e4b` |
| Ahead / behind | 0 / 0 |
| `git status -sb` | `## main...origin/main` |
| Porcelain (`-uall`) | only `?? openspec/changes/scan-generation-integrity-and-residual-certification-closure/` (the unapplied change directory) |
| Worktrees | `D:/Documents/tryPython/FastFiles` at `1172c1a [main]`; `.kilo/worktrees/scythe-cloud` detached at `0dc3356` (pre-existing, untouched) |
| Remote | `origin` `https://github.com/quantdale/FastFiles.git` |
| Local toolchain | Visual Studio x64 dev environment, CMake/Ninja/CTest discovered by `verify/core/Toolchain.psm1`; `build/debug` configured (Ninja, `Debug`, `FASTFILES_BUILD_TESTS=ON`) |

Baseline sanity before any edit: `ffengine_volume_session_manager_tests`
rebuilt and passed (1/1, 19.03 s) on this HEAD.

## Reconciled apply-resume state — 2026-09-28

After `git fetch --prune origin`, the apply session resumed with local `main`
at `4c014549759ff78929ad7c234d4270edce029889`, three commits ahead of
`origin/main` `1172c1a1e6aa9c1a726d52abdade0975b6979e4b` and zero behind. Those
three local commits belong to this change's implementation and fixture fixes.
The feature branch
`feat/scan-generation-integrity-and-residual-certification-closure` was
created at that same tip without rewriting history. The existing detached
`.kilo/worktrees/scythe-cloud` at `0dc3356` was left untouched. Subsequent
implementation edits are tracked as session work on the feature branch; this
checkpoint does not claim the later working tree was clean.

## Task 8.1 — hosted CI recheck at apply start

`gh auth status` is logged in as `quantdale` with `repo` and `workflow` scopes.
The live query (not a prompt-derived workflow id):

- `gh run list --repo quantdale/FastFiles --limit 8` — newest `main` push run is
  `36302493823` for `1172c1a1e6aa9c1a726d52abdade0975b6979e4b`, created
  `2026-09-27T07:14:34Z`, conclusion `failure`. This is the live observation for
  the campaign's own HEAD.
- `gh run view 36302493823` — six jobs, each `steps: []`, each about 2–4 s:

| Job | Conclusion | Steps |
| --- | --- | --- |
| `debug` | failure | 0 |
| `release-test` | failure | 0 |
| `analyze` | failure | 0 |
| `asan-protocol-ipc-lifecycle` | failure | 0 |
| `repository-hygiene-and-contracts` | failure | 0 |
| `unsigned-release-candidate-qualification` | failure | 0 |

Each job carries the annotation: *"The job was not started because recent
account payments have failed or your spending limit needs to be increased.
Please check the 'Billing & plans' section in your settings."* No job step
executed.

Conclusion for the spec: hosted certification is **externally blocked**
(`HOSTED_CI = BLOCKED_EXTERNAL`), not failed product code and not a pass. No
billing setting, spending limit, payment method, plan, branch protection, or
ruleset was changed, and no further runs of those six jobs were dispatched.

## Task 8.2 — protection and ruleset reads

| Query | Result |
| --- | --- |
| `GET repos/quantdale/FastFiles` | `private: true`, `visibility: private`, `default_branch: main` |
| `GET repos/quantdale/FastFiles/branches/main/protection` | HTTP **403** — `"Upgrade to GitHub Pro or make this repository public to enable this feature."` |
| `GET repos/quantdale/FastFiles/rulesets` | HTTP **403** — same message |

The read failure is recorded as a fact, not as permission to merge: the
missing rule cannot be inspected, so the campaign keeps the hosted hold and
does not treat local parity as hosted certification.

## Predecessor tasks that stay open

`legacy-protocol-correctness-and-raw-volume-cancellation-closure` task 5.4 and
`final-residual-hardening-and-hosted-ci-recovery` task 2.3 remain `[ ]`. The
synchronous raw-volume `DeviceIoControl` completion bound stays **unproven**;
this campaign neither closes it nor treats a simulated stall as that proof.
