# Stream C — Hosted CI & Branch Protection Facts (read-only)

Change under apply: `openspec/changes/legacy-protocol-correctness-and-raw-volume-cancellation-closure`
Repo: `quantdale/FastFiles`

## 1. `gh run list --limit 12`
Command: `gh run list --limit 12 --json databaseId,headSha,headBranch,status,conclusion,workflowName,event,createdAt,updatedAt,displayTitle`
All 12 runs: `status=completed`, `conclusion=failure`, `workflowName=CI`, `headBranch=main`, `event=push`.

| run id | headSha | created | duration(updated-created) | title |
|---|---|---|---|---|
| 36261023218 | 9566f1cc3210278ffab8d156473e01e27a5a1e7d | 2026-09-26T18:00:34Z | 6s | Record final hardening handoff |
| 36059336384 | 1b7d1c4a30431a8a171483b5958bb92f5c1daf9a | 2026-09-24T21:06:43Z | 4s | Tighten final residual hardening proposal |
| 36056612461 | 78b4e2f197af75cfedfa9014354549eb9f7f50e1 | 2026-09-24T20:41:53Z | 5s | Propose final residual certification hardening |
| 36040316595 | a8533a772048ea39ad44c319a96ee91dc844506f | 2026-09-24T18:18:31Z | 7s | finalize residual campaign handoff |
| 36040086847 | bf3964c7d2ae34735f6a632f4f6724ef7c57eb71 | 2026-09-24T18:16:29Z | 4s | record residual campaign validation |
| 35929765578 | 55bef85b8db6fec2ae738065725d2750fcc38a11 | 2026-09-23T22:41:55Z | 3s | docs: reconcile certification truth |
| 35504530869 | 3f3a58cae502ba88db24a0bf0f12980f96d31a11 | 2026-09-20T10:15:05Z | 5s | docs: close native precision workspace follow-up ... |
| 35181755616 | 4ec9c93e2bd835576e794c6773ffda51be61f53a | 2026-09-17T04:23:39Z | 4s | docs: close native precision evidence checkpoint |
| 35172670765 | fa538470314b940c47165fb5b3b2f9a7cd86671d | 2026-09-17T01:59:37Z | 3s | Close exact-head recertification checkpoint |
| 35082506394 | f803e3c2bae95747c05593ed856198d9a5d6825a | 2026-09-16T09:59:37Z | 5s | Record final FastFiles certification evidence |
| 35015844344 | 0dc33562ad6380c8b505ab221769acb189cd83b1 | 2026-09-15T19:48:40Z | 5s | docs: Correct recovery handoff state |
| 35015561835 | efb1c9146924ce37e872cbdde89d332aed910b06 | 2026-09-15T19:45:51Z | 5s | docs: Record overnight recovery handoff |

Local git tip vs run tip: `git rev-parse HEAD` = `cfaa226d3476ad4ed9772f4b9778d87034b2a075`; `git rev-parse origin/main` = `9566f1cc3210278ffab8d156473e01e27a5a1e7d`.
The most recent run (36261023218) headSha == `origin/main` (9566f1cc...). Local HEAD differs (cfaa226...).

## 2. Most recent run annotations — `gh run view 36261023218`
Verbatim `JOBS` (all failed, none started):
```
X analyze in 3s (ID 108456718355)
X asan-protocol-ipc-lifecycle in 2s (ID 108456718451)
X debug in 2s (ID 108456718486)
X unsigned-release-candidate-qualification in 3s (ID 108456718489)
X repository-hygiene-and-contracts in 5s (ID 108456718504)
X release-test in 1s (ID 108456718869)
```
Verbatim `ANNOTATIONS` (identical text repeated per job, source `.github#1`):
> "The job was not started because recent account payments have failed or your spending limit needs to be increased. Please check the 'Billing & plans' section in your settings"

Per-job annotation labels: `analyze`, `asan-protocol-ipc-lifecycle`, `debug`, `unsigned-release-candidate-qualification`, `repository-hygiene-and-contracts`, `release-test` (each `.github#1`).
`gh run view 36261023218 --log-failed` returned: `log not found: 108456718355` (no step logs exist — jobs were never started).

## 3. Workflow topology — `.github/workflows/ci.yml` (224 lines)
Triggers (lines 4-6): `push`, `pull_request`, `workflow_dispatch`.
Four job definitions, all `runs-on: windows-latest`:

1. `build` (L9), `name: ${{ matrix.preset }}` (L10), `timeout-minutes: 30` (L12), `fail-fast: false` (L14)
   matrix `preset: [debug, release-test, analyze]` (L19) → **3 executions**
   Steps: Checkout (L21); `./verify/check_source_markers.ps1` (L26); `./verify/check_diagnostic_policy.ps1` (L30); configure/build/test via `Toolchain.psm1` (L34); runs `ctest --preset ${{ matrix.preset }}` only when preset != `analyze` (L51).
2. `asan` (L65), `name: asan-protocol-ipc-lifecycle` (L66), timeout 30 (L68)
   Steps: Checkout; check_source_markers; check_diagnostic_policy; `--preset asan`, build targets `ffprotocol_fuzz_tests ffipc_framing_tests ffipc_listener_lifecycle_tests ffvolumescanner_terminal_tests` (L96), ctest `-R "^(ffprotocol_fuzz_tests|ffipc_framing_tests|ffipc_listener_lifecycle_tests|ffvolumescanner_terminal_tests)$"` (L100).
3. `hygiene` (L116), `name: repository-hygiene-and-contracts` (L117), timeout 20 (L119)
   Steps: Checkout; Source+diagnostic policy; `openspec validate --all --strict` (L134); Get-ProjectStatus/New-ReleaseReadinessReport/Test-ProjectStatus/Test-ProviderContract + `git diff --check` (L138); Visual contract tests (L153); Exact committed-tree qualification (L162); Upload artifact (L174).
4. `release-qualification` (L184), `name: unsigned-release-candidate-qualification` (L185), timeout 45 (L187)
   Steps: Checkout; Configure/build `release-test` (L193); `Test-ReleaseCandidate.ps1` (L205); Upload artifact (L211).

**Job count verification:** 4 job *definitions* in the file; the `build` matrix expands `[debug, release-test, analyze]` into 3 runs → **6 total execution jobs**. This matches the real `gh run view` JOBS list (6 jobs: debug, release-test, analyze, asan-protocol-ipc-lifecycle, repository-hygiene-and-contracts, unsigned-release-candidate-qualification). So the "six-job required matrix" = 6 executions from 4 definitions.

## 4. Branch protection / rulesets — API results
- `gh api repos/quantdale/FastFiles/branches/main/protection`
  → HTTP **403**: `{"message":"Upgrade to GitHub Pro or make this repository public to enable this feature.","documentation_url":"https://docs.github.com/rest/branches/branch-protection#get-branch-protection","status":"403"}` (NOT 404).
- `gh api repos/quantdale/FastFiles/rulesets`
  → HTTP **403**: `{"message":"Upgrade to GitHub Pro or make this repository public to enable this feature.","documentation_url":"https://docs.github.com/rest/repos/rules#get-all-repository-rulesets","status":"403"}`
- `gh api repos/quantdale/FastFiles/rules/branches/main`
  → HTTP **403**: `{"message":"Upgrade to GitHub Pro or make this repository public to enable this feature.","documentation_url":"https://docs.github.com/rest/repos/rules#get-rules-for-a-branch","status":"403"}`
All three are 403 (feature unavailable on private free plan), not 404. No protection/ruleset JSON could be retrieved.

## 5. Repository visibility — `gh repo view --json visibility,nameWithOwner,defaultBranchRef`
```json
{"defaultBranchRef":{"name":"main"},"nameWithOwner":"quantdale/FastFiles","visibility":"PRIVATE"}
```
`gh api repos/quantdale/FastFiles` also: `"private":true`, `"full_name":"quantdale/FastFiles"`, `"fork":false`.

## 6. Existing ruleset / branch-rule objects
NONE retrievable: the rulesets and rules/branches endpoints both returned HTTP 403 ("Upgrade to GitHub Pro or make this repository public"). No ruleset JSON names/targets/enforcement could be dumped. Cannot confirm or deny existence of any classic protection or ruleset from the API. **UNVERIFIED** whether any protection actually exists (endpoints inaccessible on this plan).

## HOSTED_CI verdict
**HOSTED_CI = BLOCKED_EXTERNAL.**
Evidence: every one of the 12 most recent CI runs (all `event=push`, `branch=main`) concluded `failure` in 1–7s with **no step logs** (`--log-failed` → "log not found"), and each job annotation states verbatim: "The job was not started because recent account payments have failed or your spending limit needs to be increased." Jobs never started → failures are billing/spending-limit driven, external to the code under apply. The current `origin/main` tip run (36261023218, sha 9566f1cc...) is affected identically.

_No fixes proposed (read-only stream)._
