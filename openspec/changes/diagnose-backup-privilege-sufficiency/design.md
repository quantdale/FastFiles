## Context

`establish-architecture-foundation` D4 commits `FastFilesIndexSvc` to running with `SeBackupPrivilege` only, never `LocalSystem`/Administrators — flagged in that change's `design.md` (line 79) as a Risk requiring empirical verification before the minimization argument can be trusted. `PrivilegeVerification.cpp` implements that verification as an in-process probe: it enables `SeBackupPrivilege` on its own token via `AdjustTokenPrivileges`, opens a raw volume with `FILE_FLAG_BACKUP_SEMANTICS`, and queries the USN journal — this is task 7.1.

Separately, `index-storage-and-scanning` task 4.6 ran the real production path — the installed `FastFilesIndexSvc`, running under its `LsaAddAccountRights`-granted virtual service account — and got `ERROR_ACCESS_DENIED` opening `\\.\D:`. The same operation succeeded only when the service was temporarily run as `LocalSystem`. This is evidence from a *different* execution context than the 7.1 probe (real service account + real service process + real service startup sequencing, vs. whatever process/context ran the probe), so the two results are not yet directly comparable, and the discrepancy could stem from at least two structurally different causes described in the proposal.

Known code-level nuance already present in this codebase: `UsnJournalReader.cpp:119` notes "SeBackupPrivilege must be explicitly enabled (not just held)" — i.e., `LsaAddAccountRights` grants the *right* to hold the privilege, but a token only carries it in enabled state if `AdjustTokenPrivileges` runs against that specific process's token, at some point after the account actually has the right. Service processes get their token at service start (from LSA), so grant-then-immediate-start ordering, or granting the right to an already-running service without a restart, are both plausible token-state bugs independent of any architectural question.

## Goals / Non-Goals

**Goals:**
- Determine, with real evidence from the real installed service under its real account, whether `SeBackupPrivilege` (properly granted and enabled) is sufficient for the raw volume open + USN journal query `FastFilesIndexSvc` needs.
- Distinguish a token/configuration defect (fixable inside the current privilege model) from a genuine privilege-model insufficiency (requires a design decision against D1/D2).
- If the root cause is a token/configuration defect, fix it and re-verify against the real installed service.
- If the root cause is genuine insufficiency, produce the specific, minimal evidence (exact `CreateFileW`/`DeviceIoControl` error codes, token privilege state at the call site, account/group context) a follow-up design decision against `establish-architecture-foundation` D1/D2 needs — without deciding that redesign here.

**Non-Goals:**
- Designing or implementing a new privileged-broker process, service SID scheme, or any change to the three-process architecture. That is explicitly deferred to a follow-up change against `establish-architecture-foundation`, informed by this change's findings.
- Re-litigating D1 (native C++ stack) or D2/D3 (three-process split, named-pipe IPC) — out of scope; this concerns only the specific privilege grant used by the already-established privileged service.
- Building the Hyper-V test VM itself (tracked separately for `autonomous-runtime-verification`); this change consumes that environment once available but doesn't provision it.

## Decisions

**Instrument the real call site, not a parallel synthetic path.** Add token-state capture (`GetTokenInformation(TokenPrivileges)`, plus `LookupAccountSid` on the token's user SID) immediately before the `CreateFileW` call in `VolumeScanner.cpp` — the actual code path that failed in task 4.6 — rather than only extending the separate `PrivilegeVerification.cpp` probe. The probe and the real path can diverge (different token, different timing relative to service start/privilege grant), which is precisely the ambiguity this change exists to resolve; instrumenting only the probe would not close that gap.

**Log structurally, not just to stderr.** Emit the token/privilege snapshot and the `CreateFileW`/`DeviceIoControl` result as a structured record (reusing whatever diagnostic logging exists in `DiagnosticsHardening.h`/local diagnostic infrastructure) so the evidence survives past a single console session and can be attached to the task 4.6 evidence trail (`verify/runs/index-storage-and-scanning/...`) — consistent with the existing verification-harness pattern already used for that task.

**Sequence the investigation before any code change.** First reproduce task 4.6's failure with the new instrumentation in place (no behavior change yet — the goal is a diagnostic no-op relative to today's behavior), read the evidence, then decide: (a) fix a token/config defect in `ServiceRegistration.cpp` (e.g., adjust grant timing, or explicitly refresh/restart the service after the grant, or ensure `AdjustTokenPrivileges` runs inside the actual service process at startup rather than assuming an inherited enabled state) and re-verify, or (b) stop and hand off findings as input to a follow-up architecture change. Do not guess at a fix before the evidence is in hand.

**Treat `LookupPrivilegeValueW`/`AdjustTokenPrivileges` failures as first-class, distinguishable outcomes.** The instrumentation must be able to tell "privilege not present in token at all" apart from "privilege present but not enabled" apart from "privilege enabled but `CreateFileW` still denied" — these map to different root causes (LSA grant never took effect / enable-call didn't run in the right process or ran too early / genuine architectural insufficiency respectively) and the evidence report must state which one occurred.

## Risks / Trade-offs

- **[Risk] The real service account's failure isn't reproducible on demand** (e.g., it depended on machine/session state from the original task 4.6 run). → **Mitigation:** the instrumentation ships as an always-on structured log at the real call site, so the next time the service runs against a real volume — including in a future Hyper-V VM run — the evidence is captured automatically rather than requiring another special one-off repro.
- **[Risk] This change concludes "insufficient" without fully ruling out configuration causes**, prematurely triggering the larger D1/D2 redesign the earlier discussion floated. → **Mitigation:** the decision above enumerates the distinguishable token-state outcomes explicitly; the verdict must cite which specific outcome occurred, not just "it failed again."
- **[Risk] No real elevated Windows service environment is available in the current sandbox**, so full closure of this change may itself be blocked pending the separately-tracked Hyper-V VM. → **Mitigation:** the instrumentation and any config fix can be written and unit-tested for compile-correctness now; the tasks below explicitly mark the real-environment verification step as depending on that VM, consistent with how `establish-architecture-foundation` 7.1/7.4/7.5/7.6 already track this same environment dependency.

## Migration Plan

No user-facing or protocol migration. This is diagnostic instrumentation plus, conditionally, a narrow fix to service-account privilege setup (`ServiceRegistration.cpp`) or startup-sequencing code — reversible by removing the instrumentation and reverting any config fix; no persisted data or wire format is touched.

## Open Questions

- If the verdict is "genuinely insufficient," what's the minimal next grant to try before accepting a bigger redesign — e.g., is there a narrower Windows privilege/right (beyond plain `SeBackupPrivilege`) documented for raw volume access that hasn't been tried, or does `\\.\D:` specifically (as opposed to per-file backup-semantics opens) require Administrators-group membership by kernel-level design regardless of privilege set? This change's evidence should inform, not answer, that question — the follow-up architecture change decides.
- Should the structured diagnostic log persist across service restarts (a small local log file) or is per-run stderr/ETW sufficient for this investigation's purposes? Left to implementation; either satisfies this change's goals.
