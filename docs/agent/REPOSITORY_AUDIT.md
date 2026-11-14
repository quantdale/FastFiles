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
| Persisted exclusions lost for bracket-containing paths | Regex stopped the rules array at `]` inside a JSON string. Honor quoted strings when finding the terminator. | `SaveSettings` -> `LoadSettings` round trip with `Private[old]` plus a second rule failed before fix and passes after. Broader structured-JSON review remains open. |
| Corrupt JSON can exhaust startup stack | Recursive validation had no depth limit before fallback. Bound nesting to 64 containers. | Public-load tests accept shallow unknown values and reject 80-level nesting through normal defaults fallback; red first, green after. File-size/resource review remains open. |
| Hosted retry test discards successful retry | Test notified the scheduler before draining stale controls, so a fast StartVolumeScan could be drained too. Drain while the retry is deferred. Production ownership/five-second cadence unchanged. | Hosted run `40947607004` reproduced lost capture while its in-flight assertion passed; fixed real session-manager executable passed 12 consecutive runs (`retry-repeat-*.log`). |
| Five source-policy tests fail external builds | Root discovery assumed the source checkout was an ancestor of the build. Set those read-only tests' CTest working directory explicitly. | Isolated baseline reproduced all five failures; focused external-build gate passes after fix. |
| Binary identity check validates wrong build | Every configuration inspected `build/debug`, allowing false failures or false certification of another configuration. CTest now passes its actual build root; standalone historical default retained. | Baseline mismatched stale binaries; corrected current-build binding passes; forced wrong identity still rejected (`identity-negative-control.log`). |
| Watcher test reports false timeout on long TEMP roots | Legacy-path fixture file creation silently failed beyond MAX_PATH; notification counts could also reuse prior callbacks, and purported emoji filename was literal ASCII text. Use extended paths for **all** fixture I/O/cleanup, verify the write, use actual supplementary Unicode, and require a new callback. | Long-root red control proves write and notification failure; first correction exposed recursive cleanup failure, fixed at fixture root; complete watcher executable green (`watcher-red.log`, `watcher-green2.log`). No product watcher change. |

## Coverage ledger

| Surface | Review/verification frontier |
| --- | --- |
| Protocol and snapshot formats | Frame/batch bounds, snapshot cursor, UI request/result parsing inspected; settings fixes above. Remaining adversarial parsing/semantic consistency review pending. |
| IPC | Framing cancellation/quarantine and listener lifetime inspected; admission/exception/deadline and client-ACE instance-creation review pending. |
| Privileged service | MFT/USN parsers reviewed, MFT fixes tested. Client authentication inspected; scan/journal loops and privilege/revalidation review pending. |
| Engine | Retry transitions, watcher, degraded enumeration and idle monitor inspected. Pipeline/publication/server/query/lifecycle review still incomplete. |
| Index store/projection | Store schema/UTF decoding/open/integrity entry points inspected; transactional, candidate-index, projection, memory and concurrency review pending. |
| Search | Literal/glob matching, query parsing, path helpers and history persistence inspected; differential/boundary/resource review pending. |
| UI/navigation/commands/operations/preview/accessibility | Icon worker/cache and recent-folder identity inspected; remaining surfaces and real runtime checks pending. |
| Setup/installer/security | Peer path matching, Authenticode, security descriptors and early installer staging inspected; package/ACL/lifecycle review incomplete. |
| Build/CI/verification/governance | Workflow/toolchain reviewed; out-of-tree test/identity defects fixed. Broader harness/policy tests pending. |
| Dependencies | SQLite 3.53.4 provenance/options checked; upstream CVE and 3.54.1 notes consulted. Session extension not enabled; no demonstrated reachable new SQLite vulnerability yet, no speculative vendor edits. Finish applicability review. |

## Validated implementation checkpoints

- `f81c66a`: NTFS stride and malformed MFT extents.
- `e5488fb`: directory-rule scope and first settings resilience fixes.
- `633fd2e`: build-specific validation, retry capture ordering, long-TEMP watcher fixture.

These identify implementation history, not an audit-complete or production-release verdict. Live HEAD, remote state and CI must be re-derived.

## Validation and unresolved queue

- Initial all-target Debug build: 507 steps, warning-clean. Baseline CTest: 138/144; six failures were five source-root assumptions plus wrong-build identity binding (`baseline-debug-*.log`).
- First focused parser/retry/portability gate: 9/9, including drift. Settings gate: protocol/pipeline/subtree 3/3. Active LSP checks were **inconclusive**, not clean; native MSVC compilation is the stronger available evidence.
- First full post-portability run found the long-TEMP watcher fixture failure above; its red and intermediate cleanup failures are preserved, not retried away.
- Full Debug and optimized release-test rebuild/CTest now each PASS **144/144**; seven-test ASan parser/IPC slice PASS **7/7** (66.96s). Strict OpenSpec **106/106**, source-marker and diagnostic policy checks PASS. These ran against the exact implementation contents before committing them; post-commit binary identity refresh is pending. Full MSVC analysis, remaining audit coverage, final source review, publication and hosted CI remain pending. Do not call this audit complete.
- Six additional **verified public-loader failures** remain open: reordered volume/rule fields lose exclusions, unknown sections override appearance, fractions truncate into integer settings, escaped JSON property names are missed, and non-object settings roots are accepted. `settings-structure-probe.cpp` is compiled against actual ffprotocol; `settings-structure-probe.log` records all six failures. Next slice replaces regex extraction with a bounded structured read, preserving section fallback and shortcut migration.
- Further probes: settings large-file bounds/atomic save contention; raw/parser randomized stress; service pipe client ACE creating additional instances (must distinguish SYSTEM service rights from same-user UI pipe rights); store/search differential and lifecycle/resource checks; remaining UI/installer/harness coverage.

External production signing/clean-host/elevated NTFS and physical-display qualifications remain unclaimed; ordinary audit work does not authorize changing those environments.
