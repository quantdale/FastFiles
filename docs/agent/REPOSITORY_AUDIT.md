# Repository audit and remediation

## Contract and evidence

Owner-requested repository-wide correctness, security, reliability and performance audit, started 2026-11-14. Intake: fetched clean `main` at `55d885cc94ae9108b43e0f184184b1448b983b51`, equal to `origin/main`, no other worktrees or unpublished branches. This is an ongoing audit, **not a clean bill of health**. Counterexamples, not comments or passing checks, decide findings.

Session build trees, raw logs and scratch probes: `D:/Documents/tryPython/_AgentArtifacts/FastFiles/sessions/20261114-pi-repository-audit/`. Builds and tests use isolated artifacts and per-configuration TEMP roots. Production pins, trust stores, installed service and scheduled-task `build/debug` remain untouched. The initial identity test accidentally inspected that existing build; it did not rebuild or modify it.

## Confirmed and remediated (final certification pending)

| Finding | Root cause and fix | Verification |
| --- | --- | --- |
| Valid MFT records rejected on 4K-sector volumes | Fixup protection incorrectly used physical volume sector size. Use NTFS's specified fixed 512-byte stride. | Real parser regression fails before fix and passes after; Microsoft MULTI_SECTOR_HEADER documentation independently confirms stride. Synthetic bytes only, not a live privileged-volume qualification. |
| Malformed MFT fixup/layout accepted | Marker-only USA, partial final stride, USA overlapping protected tails, oversized bytes-in-use, resident header aliasing and unterminated attribute streams were accepted. Reject structurally inconsistent records; preserve valid four-byte end markers and already-restored records. | Seven failing checks before production edits; all existing/new parser checks green afterwards (`mft-red.log`, `mft-green.log`). `$DATA` exclusion tests retained. |
| Directory rules cross sibling boundaries | Raw string prefixes let a child include admit an excluded sibling and over-excluded unrelated siblings. Match complete components with ordinal case-insensitive comparison, normalize trailing separators, ignore empty rules, and honor disabled volumes. | Public `IsPathIncluded` regressions red first; protocol and actual pipeline/subtree-gating tests green (`settings-{red,green}.log`). |
| Persisted exclusions lost for bracket-containing paths | Regex stopped the rules array at `]` inside a JSON string. Initial quote-aware repair is now superseded by structural parsing. | `SaveSettings` -> `LoadSettings` round trip with `Private[old]` plus a second rule failed before fix and passes after. |
| Corrupt JSON can exhaust startup stack | Recursive validation had no depth limit before fallback. Bound nesting to 64 containers and input to 1 MiB plus a single overflow-detection byte. | Public-load tests cover exact byte/depth limits, large rejection without overwrite, overflow and non-decimal integers; red-first fallback regressions preserved. |
| Hosted retry test discards successful retry | Test notified the scheduler before draining stale controls, so a fast StartVolumeScan could be drained too. Drain while the retry is deferred. Production ownership/five-second cadence unchanged. | Hosted run `40947607004` reproduced lost capture while its in-flight assertion passed; fixed real session-manager executable passed 12 consecutive runs (`retry-repeat-*.log`). |
| Five source-policy tests fail external builds | Root discovery assumed the source checkout was an ancestor of the build. Set those read-only tests' CTest working directory explicitly. | Isolated baseline reproduced all five failures; focused external-build gate passes after fix. |
| Binary identity check validates wrong build | Every configuration inspected `build/debug`, allowing false failures or false certification of another configuration. CTest now passes its actual build root; standalone historical default retained. | Baseline mismatched stale binaries; corrected current-build binding passes; forced wrong identity still rejected (`identity-negative-control.log`). |
| Watcher test reports false timeout on long TEMP roots | Legacy-path fixture file creation silently failed beyond MAX_PATH; notification counts could also reuse prior callbacks, and purported emoji filename was literal ASCII text. Use extended paths for **all** fixture I/O/cleanup, verify the write, use actual supplementary Unicode, and require a new callback. | Long-root red control proves write and notification failure; first correction exposed recursive cleanup failure, fixed at fixture root; complete watcher executable green (`watcher-red.log`, `watcher-green2.log`). No product watcher change. |

## Structured settings remediation (wave 2)

- Replaced regex searches with a private bounded structural JSON reader, used by settings and legacy shortcut migration. Object order and escaped keys work; unknown sections cannot supply fields; duplicate aliases and non-object roots reject; malformed sections fall back atomically without discarding valid siblings.
- Preserves the exact complete bounded input as `.bak` without rewriting the original. Oversized/incomplete input stays intact in place, and locked-backup failures no longer destroy the corrupt source or falsely claim a backup succeeded.
- Save and backup use exclusive new temporary creation, flush and atomic replacement. Deterministic owned hard-link probes reproduced victim truncation by both the old fixed `.tmp` save and direct `.bak` overwrite; both now preserve the victim. Long-path spelling is handled throughout settings I/O. Oversized saves leave the original unchanged.
- **Red/green:** 11 public-loader controls failed before structural remediation (`settings-structure-red.log`); two additional hard-link controls failed before atomic-I/O remediation (`settings-atomic-red.log`). Actual public tests plus optimized hard-link probe now pass; controls include 10k strings, 100 nested-section-isolation cases, uint64 boundary, exact byte/depth limits and three volumes with eight rules each.
- **Measured performance:** five ordered old/new paired blocks, 2,000 loads each, optimized MSVC `/O2`, equal 735-byte canonical input and equality checks over every settings field: old **317.0–327.0 µs/load**, new **80.5–88.9 µs/load** (median **323.9 → 81.3 µs**, **3.98×**). This is a single-host warm-file microbenchmark, not end-to-end UI latency. Harness/log: `bench-settings.cpp`, `bench-settings-final-paired.log`.
- A larger canonical 3-volume/8-rule configuration failed the old parser: instrumented unchanged historical source catches `std::regex_error(error_stack)` and returns default volumes/rules (`settings-wave1-exception-probe.log`). New loader preserves all fields, protected by a real round-trip regression; no speedup claimed for the old incorrect result.
- Final atomic-I/O source: full Debug and release-test **144/144 each**, seven-test ASan **7/7**, incremental MSVC `/analyze` exit 0. Machine receipts and logs: `*-wave2-final-atomic-{result.json,build.log,test.log}`. Active LSP was inconclusive, never counted as a clean pass. Published at `7d88ad5`; both configurations' post-commit identity/package-trust gates pass. Derive hosted result for that SHA externally.

## Broker pipe permissions (wave 3)

- **Confirmed security defect:** the service client-group ACE granted `FILE_GENERIC_WRITE`, including the bit shared by `FILE_APPEND_DATA` and `FILE_CREATE_PIPE_INSTANCE`. A restricted authorized client could create an additional server instance after the real first server existed. First-instance collision protection alone does not stop that action.
- **Actual evidence:** the new portable `ffsetup_pipe_access_tests` constructs the production descriptor with an owned client-SID fixture, disables the admin SID in a restricted token, and runs both `AccessCheck` and actual `CreateNamedPipeW` under that token. Pre-fix: both server-creation assertions fail, kernel reports **ALLOWED** (`pipe-access-red.log`). Data exchange and DACL-write rejection positive/negative controls work throughout.
- **Fix:** remove only `FILE_CREATE_PIPE_INSTANCE` from the client-group allow-ACE. SYSTEM/administrator full broker access and same-user UI server permissions remain unchanged. Regression assertions check privileged server rights and actual bidirectional client data; focused descriptor/kernel/production-engine tests pass 3/3 (`pipe-access-green.log`).
- **Counter-hypothesis disproved:** changing the engine's `CreateFile(GENERIC_READ|GENERIC_WRITE)` request was **not needed**. Its real public connection/handshake succeeds on a test DACL with instance creation removed from every principal (including SYSTEM). The initial compatibility probe passed before the ACL fix; `data-only-client-red.log` is a historical filename, not a red verdict. Keep the regression and avoid unnecessary production-client changes.
- Independent reference: [Microsoft named-pipe security and access rights](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights). Mutual signed-peer authentication remains in force; this audit does not claim a reproduced LPE, privileged install/service qualification, or cross-session exploit. Tests create no accounts, grants, trust roots, or installed-service changes.
- **Qualification PASS:** Debug **145/145** (302.60s), release-test **145/145** (268.95s), expanded ASan **10/10** (90.99s), incremental MSVC analysis exit 0 (`*-wave3-pipe-{result.json,build.log,test.log}`). Restricted-token kernel/access/data-roundtrip regression also passed **20/20** repetitions. Drift/source/diagnostic policies and strict OpenSpec **106/106** pass. Publication/post-commit identity and hosted status are handoff gates, not an audit-complete verdict.

## Hosted process-identity fixture failure (2026-11-17)

- Wave-3 publication `0a1c443` completed; exact-SHA hosted run `43565293329` passed five jobs but failed Debug's normalized-path reaper identity assertion. That failure was not a pipe ACL regression or a passing certification.
- Reproduced in an owned-process probe: **11/200 launches** captured `ntdll.dll` through `Get-Process.Path`, then the verifier observed `pwsh.exe` at the same PID/start (**55/1,000 failed comparisons**, five reads per launch). Nonempty module-derived Path is not sufficient executable-image evidence during loader startup. The single baseline test passed, demonstrating why retrying it alone is insufficient.
- `ProcessIdentityFixture.psm1` now calls handle-bound `QueryFullProcessImageNameW` through a bounded Unicode buffer and SafeProcessHandle; exact start-time checking, timeout and disposal remain. Production CapabilityRunner ownership/path/start predicates are unchanged; no launch-path substitution or weaker identity acceptance.
- Deterministic regression injects the actual wrong nonempty Path while retaining a real owned child's PID/start/handle. Before fix: two decisive assertions fail (`fixture-red.log`); after: all nine pass (`fixture-green.log`). Existing launch-alias negative control still fails as required. Natural child lifetime/cleanup semantics preserved.
- Same 200-launch/1,000-comparison probe now **0 failures**. Reaper + identity fixture CTests each passed **30 consecutive executions**; full Debug **145/145** (285.75s), release-test **145/145** (291.99s). Active PowerShell LSP checks: two files clean. Evidence: `D:/Documents/tryPython/_AgentArtifacts/FastFiles/sessions/20261117-pi-production-readiness/`. No claim of exhaustive race elimination or production release qualification.
- API contract checked against [Microsoft QueryFullProcessImageNameW documentation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew). The code-search backend remains unavailable; direct documentation/web research was used instead.

## Store/projection model qualification (2026-11-17)

- Added `ffindexstore_store_projection_model_tests`, linked to the **production** indexstore library. Four fixed seeds run 160 twelve-operation batches each (**7,680 mutation attempts**) over three volumes with repeated 128-bit IDs, full/partial field masks, removes/reinserts, reparenting, uint64 metadata, SQL-pattern punctuation, mixed case, supplementary and unpaired-surrogate names.
- An independent ordered-map model checks every durable/resident row, all semantically meaningful fields (unknown allocation bytes are ignored), point lookup, child membership and canonically filtered hot/durable candidate identity sets after each batch. Canonical name matching uses Windows ordinal case-insensitive comparison, not either accelerator. Wrong metadata, missing rows and invented identities are explicit negative-oracle controls.
- A separate reader spans each writer commit and must return the exact **previous** model; recurring eviction/reload and close/open/rebuild must preserve current results. This qualifies a bounded mutation/lifecycle corpus, not arbitrary concurrency schedules, crash/power-loss behavior, large-scale memory bounds, or the complete engine/UI search route. Existing fault/crash suites remain separate evidence.
- All four seeds pass in Debug and optimized builds; focused ASan model **1/1** (57.49s), target MSVC `/analyze` warning-clean. Full Debug **146/146** (305.35s), release-test **146/146** (275.42s). LSP initially reported an unused include (removed); final active probe was inconclusive, not a clean result. Native compilation/analysis and executable tests are the acceptance evidence.
- No new production defect was demonstrated by this bounded model, so no speculative production change was made. Logs `*-model-*-{build,test}.log` are in the 2026-11-17 session folder. Original first compile rejected an implicit char16-to-char narrowing; explicit ASCII conversion repaired the test without relaxing warning policy.

## Coverage ledger

| Surface | Review/verification frontier |
| --- | --- |
| Protocol and snapshot formats | Frame/batch bounds, snapshot cursor, UI request/result parsing inspected; settings fixes above. Remaining adversarial parsing/semantic consistency review pending. |
| IPC | Framing cancellation/quarantine and listener lifetime inspected; broker client-ACE instance creation reproduced and fixed above. Remaining admission/exception/deadline review pending. |
| Privileged service | MFT/USN parsers reviewed, MFT fixes tested. Client authentication inspected; scan/journal loops and privilege/revalidation review pending. |
| Engine | Retry transitions, watcher, degraded enumeration and idle monitor inspected. Pipeline/publication/server/query/lifecycle review still incomplete. |
| Index store/projection | Store schema/UTF decoding/open/integrity, batch/mask/candidate maintenance and projection mutation code inspected; new seeded model above qualifies bounded rows/children/candidates/snapshots/rebuilds. Exception safety, memory bounds and wider concurrency/fault review remain incomplete. |
| Search | Literal/glob matching, query parsing, path helpers and history persistence inspected; model verifies hot/durable candidate outputs after canonical filtering. Complete pipeline differential/boundary/resource review pending. |
| UI/navigation/commands/operations/preview/accessibility | Icon worker/cache and recent-folder identity inspected; remaining surfaces and real runtime checks pending. |
| Setup/installer/security | Peer path matching, Authenticode, security descriptors and early installer staging inspected; package/ACL/lifecycle review incomplete. |
| Build/CI/verification/governance | Workflow/toolchain reviewed; out-of-tree test/identity defects fixed. Broader harness/policy tests pending. |
| Dependencies | SQLite 3.53.4 provenance/options checked; upstream CVE and 3.54.1 notes consulted. Session extension not enabled; no demonstrated reachable new SQLite vulnerability yet, no speculative vendor edits. Finish applicability review. |

## Validated implementation checkpoints

- `f81c66a`: NTFS stride and malformed MFT extents.
- `e5488fb`: directory-rule scope and first settings resilience fixes.
- `633fd2e`: build-specific validation, retry capture ordering, long-TEMP watcher fixture.
- `7d88ad5`: structured settings, bounded fallback and atomic save/backup replacement.

These identify implementation history, not an audit-complete or production-release verdict. Live HEAD, remote state and CI must be re-derived.

## Validation and unresolved queue

- Initial all-target Debug build: 507 steps, warning-clean. Baseline CTest: 138/144; six failures were five source-root assumptions plus wrong-build identity binding (`baseline-debug-*.log`).
- First focused parser/retry/portability gate: 9/9, including drift. Settings gate: protocol/pipeline/subtree 3/3. Active LSP checks were **inconclusive**, not clean; native MSVC compilation is the stronger available evidence.
- First full post-portability run found the long-TEMP watcher fixture failure above; its red and intermediate cleanup failures are preserved, not retried away.
- Wave 1 fully published through `6ebc392`: full Debug/release-test **144/144 each**, ASan **7/7**, MSVC analysis 507 steps warning-clean, strict OpenSpec **106/106**, source/diagnostic policy PASS, post-commit identity/package-trust checks PASS. Hosted run `43558980249` at this immutable checkpoint completed all six jobs successfully.
- Wave 2 settings verification is recorded above. Remaining repository coverage, publication, and exact-SHA hosted results must be derived; do not call this audit complete.
- Further probes: raw/parser randomized stress; store/search differential and lifecycle/resource checks; remaining IPC/UI/installer/harness coverage. The service client-ACE instance-creation defect is addressed in wave 3 above.

External production signing/clean-host/elevated NTFS and physical-display qualifications remain unclaimed; ordinary audit work does not authorize changing those environments.
