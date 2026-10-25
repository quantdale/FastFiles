# Acceptance follow-up — fixture identity, reachable contract and scoped review

## Source and scope

Worktree `D:/Documents/tryPython/FastFiles-worktrees/acceptance-20261025`, branch
`fix/acceptance-frontier-20261025`, starts from fetched PR #15 tip
`8bffb329779ee0a431aaed37fa92925a4b373d9b`. This is an isolated successor; no
existing PR, base branch or original dirty checkout was modified. The final
commit identity is derived from Git after commit, not embedded here.

Changes are test-fixture code/registration and acceptance documents. There is
no production `src/` change, no signing/budget/security change, and no new
benchmark run. Earlier source-bound performance/runtime evidence is preserved
with its original identities and limits.

## Reaper fixture root cause and repair

The historical live test built `path` from `Get-Command pwsh.exe.Source`.
That spelling can be a WindowsApps launch alias while the running process has
a different actual image. `Test-VerificationProcessIdentityCurrent` correctly
requires the actual normalized image and matching process start. Therefore the
old fixture could report a live child as already absent and subsequently fail
“terminated owned process is actually gone”. This is a source-proven fixture
identity defect; the failed historical child's actual path was not captured.

`ProcessIdentityFixture.psm1` now waits within a five-second bound for a
nonempty actual process image with the exact start captured from the launched
process. It throws if capture cannot be established and never substitutes the
launch path. Both existing live fixture captures use it. The termination test
also asserts its identity is current before invoking cleanup. Production
`CapabilityRunner.psm1`, path equality, start-time verification and cleanup
ownership are unchanged.

Current host observation differs from the historical host: discovery and actual
image are `C:/Program Files/PowerShell/7/pwsh.exe`; the per-user WindowsApps
alias is absent. This continuation does not claim to reproduce an actual
WindowsApps launch. The new independent identity test uses a synthetic alias
spelling for the same live PID/start to exercise the wrong-identity assumption
deterministically on either host type.

## Executed non-destructive checks

The child runs `Start-Sleep -Seconds 5` and exits naturally. These commands do
not call the reaper, `Stop-Process`, `Kill` or any termination operation:

```powershell
pwsh -NoProfile -File verify/core/tests/Test-ProcessIdentityFixture.ps1 -UseLaunchPathNegativeControl
pwsh -NoProfile -File verify/core/tests/Test-ProcessIdentityFixture.ps1
cmake --preset debug
ctest --test-dir build/debug -R '^ffverify_process_identity_fixture_ps_tests$' --output-on-failure
openspec validate durable-cold-search-index-and-hybrid-residency --strict --no-interactive
pwsh -NoProfile -File tests/drift/check_doc_drift.ps1
```

- Negative control: exit **1**, seven checks pass, exactly the live captured
  identity assertion fails after reconstructing the launch-path assumption.
- Actual capture: exit **0**, **8/8** checks pass. Correct actual image/start
  verifies; an alias spelling and a wrong start are rejected; natural exit is
  observed and the identity then no longer verifies.
- Debug configure: **PASS**; no C++ target was built.
- Registered focused CTest: **1/1 PASS** (13.13 s test time).
- Focused strict OpenSpec: **PASS**.
- Repository-wide strict OpenSpec: **93/93 PASS**.
- Doc drift, semantic truth and line endings: **PASS**.
- Static PowerShell AST parsing of all three changed scripts: **3/3 PASS**.

Transient outputs are under ignored `build/acceptance-20261025/`. The existing
`ffverify_process_reaper_ps_tests` stays registered and was **NOT_EXECUTED** in
this continuation. Its historical failure and the later full-suite parity
failure are not relabeled PASS. A full CTest rerun would invoke the excluded
live termination fixture, so no full-suite certification is claimed here.

## Contract clarification and independent review

The owner's 2026-10-25 directive section 9B explicitly authorizes the
production-reachable acceptance clarification. The proposal, design and delta
now require exact independent full-canonical equality at every scale plus
actual resident/mixed/transition/generation route coverage under the unchanged
default budget. Task 4.1 stays **OPEN**. The evidence inventory and precise
unverified cells are in `acceptance-decision.md`; no correctness requirement
was waived and no old execution was rebound to a newer source.

Fresh-context native independent review of the final
`4c1c450..db7887c1` subtree/scheduler source delta is **APPROVE**, no current
P1/P2 in scope, with four retained archive hash checks. The full report is
`reviews/native-final-delta-20261025.md`. Task 5.14 closes for that scoped
review; broader task 5.10 remains open. No runtime/merge/hosted approval is
inferred from source review.

A separate read-only review **APPROVES** the new fixture logic with no material
P1/P2 findings; canonical reviewed blob identities and nonblocking observations
are retained in `reviews/native-fixture-20261025.md`. The reviewer did not
independently rerun the tests or execute the live reaper.

## Remaining acceptance frontier

Resolve the bounded 1M independent oracle, explicitly prove resident/mixed and
transition canonical routes, and determine relevant-source equivalence for
the retained valid 200k/2M oracle evidence before closing 4.1. Complete broader
final acceptance review and separately qualified reaper/hosted execution.
Completed 5M ingestion/oracle, 2M pipeline and Store matrices, Gallery and UIA
remain retained evidence; do not repeat them absent changed executable inputs
or a specific uncovered question. Existing PR merges and production release
remain owner-controlled.
