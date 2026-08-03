## Context

FastFiles is a Windows-only, three-process native C++20 system (`FastFilesIndexSvc` privileged service, `FastFilesEngine` unprivileged index owner, `FastFiles` Direct2D UI). The `autonomous-runtime-verification` change already delivered a capability-registry verification harness (`verify/`) with verbs `build/install/run/diagnose/report/repair/gate/list/doctor`, a four-state archive gate, a `local` environment provider, and a repair loop. That harness proved Tier 0/1 work autonomous on this host. What remains — 22 open tasks across 7 changes — is blocked on four missing capabilities this design adds, plus a resumable orchestrator that ties them into one zero-touch loop.

Verified environment (2026-08-03, commit `855bfd6`): Windows 11 build 26200; interactive desktop session available (`UserInteractive=True`, Console); **non-elevated** token (Medium integrity, Administrators deny-only) — Tier-1 install/service/SCM work needs an admin token, but self-signed test-cert signing into the Current-User store does not; `openspec` CLI installed; node v24.3.0; pwsh 7.6.3; builds exist in `build/{debug,release,analyze}`; VM `C:\VMs\FastFiles-Matrix.vhdx` is 4 MiB (no OS), no install media, WinRM unavailable, Windows Sandbox absent.

## Goals / Non-Goals

**Goals:**
- Close the 22 open tasks by delivering the four capabilities they depend on and recording evidence against each existing-change task ID.
- Make subtree gating honor `IsPathIncluded` + `VolumeSetting::rules` across every ingestion path (the complete-implementation resolution of the previously-unresolved decision).
- Make UI validation autonomous via a UIA driver (semantic, not pixel), runnable headless + e2e in the interactive desktop.
- Make signed-install validation autonomous via test-PKI signing + a production-cert injection gate, without disabling verification or fabricating trust.
- Make isolated validation autonomous via a disposable snapshot-restorable VM provider, falling back lawfully when media is unavailable.
- Make the whole loop resumable and discoverable to future agents via one entry point + persistent state + docs.

**Non-Goals:**
- Replacing human review of product-code changes; the harness proposes and gates, never silently accepting Class-B fixes.
- A production OV/EV certificate (genuinely commercial/external); this design implements the full pluggable path and keeps its absence separate from software correctness.
- Cross-platform support (FastFiles is Windows-only by construction).
- Disabling any product hardening (Authenticode pinning, pipe ACLs, DLL hardening) to make a check pass.

## Decisions

### D1: One resumable orchestrator entry point, state-machine-driven, persistent
A single script `verify/intake.ps1` (verb `autonomous`) drives `discover → plan → provision → implement → build → test → diagnose → repair → re-test → validate → collect-evidence → update-tasks → commit → sync → archive`. State persists to `verify/runs/autonomous/<run-id>/state.json` (phase, completed-step ids, per-task evidence refs, failure classification, retry counts). On crash/restart it resumes at the last incomplete phase. Per-step timeouts, bounded retries (<=3), failure classification (Class A harness / Class B product / external-impossibility), and a hard iteration cap prevent uncontrolled loops. Exit codes are deterministic; a final machine-verifiable gate is the terminal phase. Reuses the existing `verify/` capability runner, gate, and repair loop rather than duplicating them.

### D2: Subtree gating = `IsPathIncluded` evaluated at the ingestion seam, not the store
`IsPathIncluded(canonicalPath, VolumeSetting.rules)` implements longest-prefix-match (the semantics already specified by `settings-ui`'s "Directory Include/Exclude Rules": ordered prefix rules, longest match wins, include/exclude). It is wired into `IndexPipeline::ApplyMftBatch`/`ApplyUsnBatch`/`BeginReconciliationPass`-observed-set/`RebuildAll`: each record's canonical path is reconstructed via the existing `Projection::ReconstructPath` (FRN chain), and excluded records are dropped before persist/apply. Because MFT ingestion is not guaranteed parent-first, a record whose parent chain is not yet resolvable is **deferred** (buffered per volume) until its parent lands, then decided — never silently included. Reconciliation's observed-set only includes rule-passing entries, so a previously-included-but-now-excluded entry is reconciled away. Rules are supplied to the pipeline via `VolumeSessionManager` (already holds `configuredVolumes_`) -> `IndexPipeline::SetVolumeRules(volumeId, rules)` on `ReloadConfiguration`. This is the only product-source edit; represented in the `index-engine` spec delta so the gate recognizes it.

### D3: UIA driver = reusable module, semantic-first, `SendInput` only as fallback
`verify/uia-driver/` (PowerShell wrapping `UIAutomationClient`/`UIAutomationTypes` COM + `SendInput`): element identity (Name/AutomationId/ControlType/ClassName), tree traversal (Raw/Control), pattern selection (Invoke/Selection/Scroll/Value/Window/Drag), property validation, event subscriptions, input abstraction, timeout handling, and diagnostic tree dumps (JSON + indented text). Headless unit tests use mock UIA providers (a minimal in-proc provider or recorded trees) for identity/tree/pattern/timeout logic. E2e runs in the interactive desktop: launch `FastFiles.exe`, multi-column nav (keyboard + mouse), selection, scroll, connection badge, dialogs, search, in-column error states, rendering-where-practical via UIA geometry. Cross-window drag uses UIA drag patterns where exposed, else `SendInput` with mouse sequences, validating resulting filesystem state programmatically. Fails clearly (structured error + tree dump) when a semantic target is missing; never falls back to whole-screen pixel diffing.

### D4: Test-PKI signing, production cert injectable, verification never disabled
`verify/capabilities/test-code-signing/`: generate an isolated self-signed code-signing cert into the **Current-User** `My` store (no admin needed) via `New-SelfSignedCertificate`, stored outside source control (`verify/.signing/` gitignored; key exportable to a temp PFX with a random password, never committed). Sign `FastFiles.exe`/`FastFilesEngine.exe`/`FastFilesIndexSvc.exe`/`FastFilesSetup.exe` with `signtool`. Verify with `Get-AuthenticodeSignature`/`WinTrust` and assert the pinned-thumbprint behavior the product already checks. The existing `binary-authenticode` capability now resolves PASS against the signed dev build. A production OV/EV cert is an **injectable secret** (`FF_PRODUCTION_CERT_PFX` + `FF_PRODUCTION_CERT_PASSWORD` env or a sealed artifact path); an automated production-signing gate runs only when the cert is present, otherwise `SKIPPED(production-cert-not-provided)` — never a fabricated pass and never a global verification disable.

### D5: Disposable VM provider, lawful media acquisition, snapshot-restorable
Extends the `local` provider model: `verify/providers/hyperv/` (or `tools/provision-vm/`) implementing `provision -> activate -> collect-logs -> cleanup -> snapshot-restore`. Provisioning: search approved local locations for Windows install media; if none and network/licensing permit, obtain official Windows evaluation media automatically (documented endpoint + checksum/signature verification); build an unattended-install config (`autounattend.xml`); create a temp automation account; enable WinRM (HTTPS or credential-secured) or another secure channel; install VS Build Tools + Windows SDK + pwsh prerequisites; create a clean reference snapshot. The same capability then runs unchanged across `local` and the VM (closes arv 2.3/2.4). If media acquisition is impossible, the design falls back to Windows Sandbox / an existing interactive runner / a new disposable VM / a supported cloud runner — the first secure, lawful, reversible option — and, if none is available, records machine evidence of the limitation, completes every unaffected task, and keeps VM-gated tasks as `REQUIRED-BUT-UNAVAILABLE` rather than fabricated.

### D6: Future-agent intake contract = docs + machine-readable run state
`AUTONOMOUS.md` (repo root) + `AGENTS.md` section: documents the one entry point (`verify/intake.ps1 autonomous`), the state file, the dependency graph, and the 13-step future-agent intake sequence (inspect state -> convert request to OpenSpec change -> implement -> provision -> test -> diagnose/repair -> evidence -> commit -> push when authed -> sync+archive -> resume if interrupted). Machine-readable `verify/autonomous/contract.json` advertises capabilities, exit codes, and the resume protocol so a later agent can bootstrap without reading prose.

### D7: Flaky-test policy = root-cause-first, retries never mask
On any intermittent failure: preserve initial output, reproduce under stress (repeated/parallel runs), capture timing/resource/thread/process/environment data, identify the race/isolation-leak/stale-object/ordering/env-dependency, fix the root cause where reasonable, add regression coverage, and run repeated clean tests to establish stability. Retries may gather evidence but a "passed on retry" is never the terminal state without a root-cause fix or a documented non-determinism bound.

## Risks / Trade-offs

- **MFT records arrive FRN-keyed, not parent-first** -> `IsPathIncluded` may not resolve a record's path on first sight. Mitigation: per-volume deferred buffer for records whose parent chain isn't yet resolvable; decide on flush when the parent lands; bounded buffer size with a "decide-on-reconciliation" fallback so ingestion never stalls.
- **Subtree gating changes index contents** -> existing tests assume full-volume ingestion. Mitigation: rule-honoring is additive (a volume with no rules includes everything, preserving current behavior); new unit tests cover include/exclude/overlap/longest-match; the degraded path is unaffected (rules apply at enumeration too).
- **UIA on a Direct2D/DirectComposition custom surface may expose few providers** -> some elements lack UIA identity. Mitigation: the capability `SKIP`s affected checks with a precise reason rather than pixel-fallbacking; where a custom UIA provider is needed in product code, that is tracked as a separate product change, not fabricated here.
- **Non-elevated session cannot install the service or write Local-Machine root trust** -> Tier-1 install/service validation and root-trust install need an admin token. Mitigation: the orchestrator requests elevation via the existing `-Elevate` path when available and records `SKIPPED(elevation-required)` otherwise; test-cert signing works without elevation (Current-User store).
- **No Windows install media / network restrictions** -> VM guest provisioning may be impossible. Mitigation: lawful evaluation-media acquisition first; else Windows Sandbox / cloud runner / existing interactive runner; else record machine evidence and keep VM-gated tasks `REQUIRED-BUT-UNAVAILABLE`.
- **Production OV/EV cert is commercial/external** -> cannot be fabricated. Mitigation: full pluggable signing path + production-signing gate that runs only when the cert is present; software correctness is proven regardless; production-trust availability is reported separately.

## Migration Plan

1. Land the subtree-gating product edit + unit tests (Tier 0, no elevation) — closes `settings-and-appearance` 2.5; build green.
2. Land the UIA driver module + headless unit tests (Tier 0) — closes arv 8.1.
3. Land the test-code-signing capability + sign the dev build — resolves the `binary-authenticode` FAIL; closes resolve-privilege 4.1 partially.
4. Run UIA e2e in the interactive desktop — closes arv 8.2/8.3, file-ops 8.7-8.9, instant-search 7.6/8.5/10.6, shell 2.14, storage 7.1-7.4.
5. Attempt VM provisioning; on success run the same capability in the VM — closes arv 2.3/2.4; on lawful impossibility, record evidence and keep `REQUIRED-BUT-UNAVAILABLE`.
6. Run signed install + service validation under elevation when available — closes resolve-privilege 4.2-4.4, arv 7.4.
7. Land the intake orchestrator + future-agent docs; run the full autonomous loop end-to-end; sync+archive when the gate passes.
8. Rollback: every addition is under `verify/` + one product edit in `src/engine/` represented in the `index-engine` delta; reverting the delta + the engine edit + the `verify/` additions restores prior behavior with no schema migration.

## Open Questions

- Subtree-gating deferred-buffer flush policy: flush on parent-land vs. batch-boundary vs. decide-on-reconciliation — leaning batch-boundary + reconciliation fallback for simplicity.
- UIA custom-provider gap: does `FastFiles`' Direct2D surface need a product-code UIA provider (tracked separately) before full coverage, and what is the interim SKIP surface? — leaning implement the driver now, SKIP unexposed elements with reason.
- VM media: is official Windows evaluation media obtainable on this network under evaluation terms, or must the design default to Windows Sandbox / a cloud runner? — resolved by attempt-then-fallback at runtime, not by assumption.
- Production-cert governance: does project governance permit a documented development exception for test-PKI during CI, separate from a production-trust gate? — leaning yes for CI (test-PKI), with the production gate as a separate injectable-secret step; documented as an OpenSpec decision if needed.