## Context

See `proposal.md` for motivation and `evidence/explore-findings.md` for the historical intake snapshot. At exploration, `origin/main` was `a26fe609439a18c128b2a2e557bc7eb2534fa128`; 23 open PRs reduced by exact ancestry to nine maximal heads. A separate local simulation at `e400d7065571271b5052fa6e58e9971814d331d6` contains useful composite fixes and evidence but lacks the newest rename/create repair. None of these observations replaces apply-time fetching or semantic inspection.

This change specifies a qualification process over existing runtime capabilities. It does not introduce a second navigation, indexing, file-operation or accessibility state model. Incoming feature specs and foundation D1–D6 remain authoritative.

## Goals / Non-Goals

**Goals:** preserve accepted behavior while converging source; make stage failures cheap to localize; bind independent review and actual journeys to the executed candidate; produce an honest terminal handoff even when external gates block merge.

**Non-Goals:** product integration during propose; blanket inheritance of old test totals; privileged validation or release deployment; making every historical experiment part of the product; treating unknown repository policy as permission.

## Decisions

### D1: One candidate, immutable integration inputs

**Why:** branch proliferation obscured which application had actually been tested. Reuse the dedicated `integration/fastfiles-final-product-20261029` worktree for planning and subsequent accepted integration. Preserve all unrelated worktrees and their dirt. Fetch/prune/tags and inspect local/remote ahead-behind and dirt before work; fast-forward only clean, strictly-behind branches relevant to the campaign, never unrelated experimental worktrees or a branch tracking its parent by accident.

At apply intake, write a versioned `evidence/integration-ledger.json` and freeze the acceptance matrix before any product edit. Record the fetched main SHA/tree, retrieval time, each PR's number/title/draft/head/base branch, both API-reported base SHA and actual fetched base-tip SHA, fork point, parent PRs, unique commit SHAs, changed paths and tests, associated OpenSpec changes, local/hosted validation, reviews and outstanding findings, conflict decisions and disposition. Distinguish changes unique relative to the parent from commits exclusive of all other heads. Record local-only branches/simulation with the same attribution fields and content-equivalence decisions. Explicit unknown/pending values are allowed; missing or contradictory required identity data blocks integration of that input. Do not store sensitive raw runtime material in this ledger.

Before final publication, refetch and compare all inputs. Any new/changed relevant input receives an explicit accept/reject/defer decision and affected validation; it cannot silently enter a certified candidate.

**Alternative/trade-off:** blindly merging every PR is simpler operationally but duplicates predecessors and ignores semantic dependencies. Nine maximal heads are an optimization hint, not the final acceptance decision.

### D2: Stage integration by behavior, not PR number

**Why:** the same source files protect multiple independent invariants. The initial stage plan is:

| Stage | Intended responsibility | Intake head hints, not permanent pins |
| --- | --- | --- |
| Core | lifecycle, persistent candidates, schema migration, ordinal correctness, residency, reconciliation | #17 contains #14/#15 |
| Navigation/storage | Home/reveal, Column View identity, status, ranking/partial totals, adaptive Storage, quit preservation | #19/#20/#29 contain #18/#26 as applicable |
| Operations | conflict access, errors/cancel, ownership/shutdown, Restore, consent, shortcuts, generated results, rename/exclusive creation, compact palette | #24/#36/#34 contain their predecessor chains |
| Verification | native proxies, bounded navigation input, failure evidence and non-vacuous results | #35 contains #28/#31 |
| Theme | rendered-theme F9 inversion and retained test registration | #16 |

Order can change to satisfy actual dependencies; bring fixture ownership and verification prerequisites forward before GUI execution. Core/harness headless tests can run earlier. After every material stage, commit a checkpoint, build Debug and release-test, run affected tests and limited safe smoke checks; run affected Analyze when appropriate and inspect test/source registrations and lifecycle validity. Do not pile another unvalidated stage onto a failed one.

For each material conflict, record input SHAs, both protected behaviors, chosen resolution, preserved regression tests and reviewer outcome. Never take an entire product file with unconditional ours/theirs. Use merge-base, range-diff, commit/tree and blob comparisons to distinguish ancestry from patch equivalence. Explicitly union test registrations and task evidence by identity, not by choosing the longest file.

**Alternative/trade-off:** importing the old simulation wholesale saves time but conceals unpublished fixes and stale metadata. It is inspected as an input and its valid evidence is reused, not presumed authoritative.

### D3: Freeze a finite product acceptance boundary

**Why:** “repair every gap” without a boundary creates another endless successor campaign. The acceptance matrix enumerates journeys A–I from the spec, predecessor invariants, owned fixtures, expected UI/filesystem observations, required environments and result/evidence locations. Five pillars are assessed independently: discovery, hierarchy, storage intelligence, daily operations and native polish.

New work is admitted only for a demonstrated data-safety/security defect, integration regression, or defect that blocks an existing required journey/gate. Record its repro, impact, owner, acceptance cell and focused test in the same workstream. Unrelated feature ideas stay in backlog. Large refactors need evidence that a smaller repair cannot preserve the contract and independent review; new product behavior requires an explicit delta before implementation.

An environment blockage is not a failed feature implementation, but does not close a required acceptance cell. Safe executable cells proceed independently. A known material P0/P1 or unresolved material P2 prevents acceptance; an unreproduced intermittent remains named and unqualified unless a bounded, evidence-backed disposition is independently reviewed. A later green run does not erase it.

**Alternative/trade-off:** a rigid no-new-defects scope would knowingly ship regressions; unrestricted scope would never finish. Tying additions to frozen acceptance permits necessary repair without feature expansion.

### D4: Actual native journeys with independent filesystem oracles

**Why:** UIA action success, worker correctness and visible pixels each leave different failure modes uncovered. Use the real native executable, public UIA/keyboard/pointer routes and the real operation handlers. Fixtures are marker-owned, isolated and bounded; independently capture pre/post inventories, content hashes, actual path/casing, success/skip/failure counts, cancellation and history outcomes. Never touch real user files or use global/privileged recovery experiments.

Required special cases include new file → inline rename → rename again → Undo; new folder → inline rename → enter; rename collision → error → recovery; copy/move conflicts with explicit choices; multi-conflict request-scoped apply-to-all, default unchecked and next-request reset; Keep-both reservation; Skip versus Cancel; real multi-item Recycle Bin Undo where safely supported; generated-name protection and partial completion; and journey I's external Explorer → FastFiles and FastFiles → Explorer transfer with independent destination byte oracles and visible source/destination refresh. Holding-folder Restore remains a distinct seam and cannot certify the real Recycle Bin journey. A blocked real journey remains unqualified.

Search checks compare ordered identities/fields with an independent canonical oracle, use actual cold/mixed/resident routes under the existing budget and preserve migration/readiness/durability regressions. Reopen probes do not replace in-panel snapshot-refresh coverage. Storage checks distinguish known-file ranking and partial lower bounds from whole-volume completeness. Tabs/dual panes assert source/destination/selection ownership and persistence, not just window presence.

**Alternative/trade-off:** end-to-end tests are slower and require serialized foreground access; retain cheap headless regressions for diagnosis but do not substitute them for signature journeys.

### D5: Resource and ownership isolation

**Why:** competing foreground drivers or giant databases can invalidate results and harm the workstation. One writer per worktree/build tree; independent readers may review without edits. Parallel writers require isolated worktrees, exclusive seams and a named integration destination. One native UIA/input owner at a time; serialize GUI/file-operation tests that share ownership resources. Serialize large benchmarks after disk/memory/CPU preflight, use owned bounded fixtures and preserve headroom. Never delete user data or unrelated artifacts to make room.

No production process-ownership check is weakened. Start the reaper question at PR #17's actual-image fixture repair and naturally exiting child proof. Restricted live termination remains registered, unexecuted and explicitly excluded unless a separately permitted scenario becomes available. Do not invoke unrestricted full-suite/parity wrappers that would execute a prohibited test; derive the exact selected commands and retain their exclusion manifest instead.

### D6: Independent review is a gate, not a label

**Why:** individual PR approval cannot inspect new composite behavior. Fresh-context read-only reviewers inspect the actual integrated source and material resolutions, especially file integrity, index/durability, UIA/navigation ownership, test unions and provenance. At least two distinct final review angles cover product/data correctness and evidence/security/regression integrity; reviewers cannot silently modify source. Record scope/ref, findings, dispositions and re-review of confirmed fixes. Prior local reviews can support unchanged component decisions, not approve a new composite.

Review-provider failure is an infrastructure fact. Use an explicitly available independent reviewer if permitted; if none is available, retain the pending review gate. Quota exhaustion or a missing GitHub review is neither approval nor permission to abandon independent executable work. No unresolved material P2 is auto-waived by the agent.

### D7: Certification and provenance are separate, bounded claims

**Why:** historical “full pass” totals excluded a named test and some runs used different sources. Enumerate configured tests at each certification SHA and record selected, passed, failed, skipped and excluded names/counts/reasons separately. Required local gates include Debug/release-test builds and available CTests, Analyze, supported ASan slice, source markers/registration, drift, strict OpenSpec/lifecycle, E2E, UIA core/full, visual catalog/Gallery, many-tabs/dual-pane and signature journeys. Missing environment support does not become PASS.

Freeze an implementation SHA before the final ladder. The provenance manifest records starting main, accepted PR/local heads, resolution commits, implementation/executed-test/evidence references, Git tree, build configuration/toolchain and SHA-256 of executed product/test/benchmark binaries. Compare all executable-impacting inputs, including headers, tests, verification scripts and build configuration, before carrying evidence across documentation-only commits. Component blob equality supports only a component claim; source equality is not bit-for-bit binary reproducibility. After the last tracked documentation commit, validate equivalence to the implementation baseline and derive repository tip externally, without a self-referential workstate SHA.

Performance evidence is reused only with explicitly matching inputs, fixture, configuration, route and methodology. Remeasure affected startup/search/cancellation/browsing/ingestion/residency/progress behavior; use representative 200k/1M/2M and 5M only when justified and feasible. Compare current candidate and baseline under compatible conditions and a measured noise floor. Canonical correctness and material common-path regressions cannot be traded for a speedup elsewhere.

### D8: Native design research follows a demonstrated problem

**Why:** existing Refero-grounded Native Precision guidance already exists. Read incoming `docs/product/original-vision-alignment.md` and the relevant follow-up research before new searches. Only if acceptance reveals a consequential UX problem, use Refero for a bounded decision and capture Evidence → Reference → Design → native Implementation → Runtime → Screenshot → Verification. No browser replacement, decorative feature expansion, token-role drift, or copying another brand. Preserve native tokens, keyboard/UIA, DPI, High Contrast and reduced-motion behavior; actual current-display evidence and synthetic geometry remain distinct.

### D9: Reconcile predecessor truth without erasing history

**Why:** task closure differs between incoming branches and the old simulation. Map each predecessor change/task to surviving implementation, qualified evidence and any residual obligation. In particular, original-vision 4.1–4.3 remain governing integrated acceptance; replace obsolete successor-publication wording with the central destination through an explicit recorded update. Storage 4.12 is already checked on PR #26 but stale on the simulation: verify and carry its evidence rather than reimplementing by assumption. Bounded completed repairs remain completed; importing them does not complete the original-vision parent.

Do not bulk-check tasks, set blanket supersession, or archive changes to conceal unexecuted requirements. Task-level transfer needs equivalent acceptance, explicit mapping and review; genuinely external tasks use schema-v2 evidence-backed overrides while remaining raw unchecked acceptance. Maintain exactly one current resume section in this candidate's WORKSTATE; older sections are historical, not competing instructions. Do not edit the unrelated dirty checkout's WORKSTATE.

### D10: Publish once; merge only with positive policy evidence

**Why:** engineering completion, merge permission and release readiness are independent. Publish the central branch without force and prepare one consolidation PR. No source integration into main occurs until current remote/input reconciliation, all required locally executable gates, independent reviews, no unresolved material findings, consistent OpenSpec/provenance and all applicable hosted/protection gates pass. Use ordinary PR merge, never a direct main push around checks. HTTP 403 while reading protection/rules means UNKNOWN and blocks any claim that no rules apply. When policy facts cannot be obtained, require authoritative owner/repository evidence before merge.

On hosted pre-step payment/spending failure, record `HOSTED_CI=BLOCKED_EXTERNAL`, `HOSTED_CERTIFICATION=NOT_OBTAINED`, inspect the pushed candidate's run once, and do not retry known-unstartable workflows or change billing/settings. Continue independent local work. A blocked final merge leaves a clean published candidate and exact owner action; it does not become main integration complete.

After an actually accepted consolidation merge, reconcile original PRs as MERGED, INCLUDED_BY_CONSOLIDATION, SUPERSEDED, REJECTED_WITH_EVIDENCE or STILL_OPEN with exact inclusion/rejection/test attribution. Close nothing merely because it is an ancestor; verify its accepted content/tests and dependencies first. Do not automatically delete branches.

### D11: Terminal handoff is a readiness vector

**Why:** one “complete” flag hides external release obligations. Publish all requested success states from `LATEST_REMOTE_RECONCILED` through `PRODUCTION_RELEASE_READY`, each with status/evidence/limitations. Report implemented, integrated, locally qualified, hosted qualified, signed, clean-host installed, privileged validated, physical-display validated and release-authorized separately. The product report classifies each pillar as functional-and-verified, implemented-but-defective, incomplete, not-implemented or environment-blocked with scope; degraded coverage never establishes whole-computer discovery.

The following table is the authoritative list of the campaign's 20 named success states. Every report record carries result, source/evidence references, tested scope and limitation/owner action; no aggregate flag overrides an unqualified member. Tasks 1.5 and 12.6 must preserve the complete list.

| State | Evidence required for PASS | Owning tasks |
| --- | --- | --- |
| `LATEST_REMOTE_RECONCILED` | Fresh main/input/worktree reconciliation, with all new deltas dispositioned in the ledger | 1.1–1.3, 12.1 |
| `ALL_OPEN_PRS_ACCOUNTED_FOR` | Every live PR and relevant local input has unique-content/test attribution and an explicit final or proposed blocked-merge disposition | 1.2–1.3, 6.1, 12.5 |
| `INTEGRATION_CONFLICTS_RESOLVED` | No unresolved source/registration conflict; all material semantic decisions validated and independently reviewed | 2.5, 3.5, 4.6, 5.5 |
| `NO_ACCEPTED_FIX_LOST` | Accepted invariant-to-source/test mapping survives candidate audit and final review | 1.4, 6.1, 10.3–10.4 |
| `FILE_OPERATION_DATA_SAFETY_VERIFIED` | Required actual create/rename/transfer/delete/Undo/Restore/conflict/cancel cells and independent filesystem oracles pass | 4.5–4.6, 7.6–7.8 |
| `SEARCH_CORRECTNESS_VERIFIED` | Required canonical/migration/readiness/route regressions and native reveal/refresh/identity cells pass | 2.2–2.4, 7.2 |
| `COLUMN_VIEW_JOURNEYS_VERIFIED` | Required keyboard hierarchy, reveal, history, pane/tab ownership and restored-state cells pass | 7.3–7.4 |
| `STORAGE_INTELLIGENCE_VERIFIED` | Scoped ranking, partial totals, exact drill-down/reveal and focus/selection cells pass without a whole-volume overclaim | 3.2–3.4, 7.5 |
| `ORIGINAL_VISION_ALIGNED` | All five pillars meet their frozen required product cells; degraded coverage alone cannot complete whole-computer discovery | 1.5, 8.5 |
| `REAL_NATIVE_E2E_VERIFIED` | Required current-source real executable journeys pass with UI and filesystem postconditions and safe cleanup | 7.1–7.9, 9.5 |
| `UIA_AND_ACCESSIBILITY_VERIFIED` | Required public keyboard/focus/identity/label/lifetime cells pass; physical or assistive profiles not exercised remain explicitly outside the claim | 7.10, 9.5 |
| `EXPLORER_INTEROPERABILITY_QUALIFIED` | Dedicated owned-fixture Explorer → FastFiles and FastFiles → Explorer transfer journeys pass with independent destination byte/hash/count verification and visible source/destination refresh in both directions; every non-delivery carries a bounded evidence-backed diagnosis across the delivery chain (target entry, drop feedback, handler invocation, destination I/O, destination bytes, refresh) before any disposition, and `BLOCKED_ENVIRONMENT` is never assigned automatically or inherited from suite skips | 7.11–7.12, 8.6, 9.5 |
| `VISUAL_COHERENCE_VERIFIED` | Required current-profile native captures are actually reviewed and contain no unresolved material visual defect; untested profiles are named | 8.4, 9.5 |
| `CRITICAL_PERFORMANCE_PRESERVED` | Affected comparable measurements and applicable retained evidence show no material correctness/common-path regression | 2.4, 10.1–10.2 |
| `INDEPENDENT_REVIEWS_COMPLETE` | Material-resolution and both final review angles complete, findings fixed/dispositioned and material fixes re-reviewed | 6.2–6.3, 10.3–10.4 |
| `OPENSPEC_RECONCILED` | Task-level evidence/transfer mapping and lifecycle/strict checks agree; raw unavailable tasks remain honest | 11.1–11.2 |
| `LOCAL_CERTIFICATION_COMPLETE` | Every mandatory local gate has executed and passed at applicable current inputs; a restricted required exclusion prevents this PASS | 9.1–9.6 |
| `HOSTED_CERTIFICATION_COMPLETE` | Actual candidate jobs execute and all applicable mandatory hosted checks pass; pre-step failures do not qualify | 12.3 |
| `MAIN_INTEGRATION_COMPLETE` | An ordinary permitted merge actually occurred and fetched main contains the accepted candidate with its merge attribution | 12.4–12.5 |
| `PRODUCTION_RELEASE_READY` | Separate signing/pins, clean-host/privileged/physical-environment requirements and owner release authorization are all evidenced (Explorer interop is owned by `EXPLORER_INTEROPERABILITY_QUALIFIED`, not deferred here); this campaign alone cannot supply that authorization | 12.6 and separately authorized release qualification |

Engineering acceptance requires all mandatory local cells passing, no material unresolved findings and required independent review. A terminal blocked handoff is allowed when every safe executable task is finished and each remaining task has specific evidence and an external action; those blocked tasks and associated success states remain unqualified, and the lifecycle cannot be marked completed with mandatory raw unchecked tasks. No new acceptance campaign is automatically started. Production release remains unauthorized by this change.

### D12: Explorer interoperability is owned acceptance, not release readiness

**Why:** the inherited UIA suite can skip failed external drag delivery as environmental, but the predecessor investigation
(`large-index-lifecycle-and-search-scalability/evidence/uia-explorer-drag-in-investigation.md`) fixed its own Explorer-side targeting defect and still could not establish Explorer → FastFiles delivery, leaving the journey `UNQUALIFIED` with an unresolved cause class — an application defect is NOT excluded. Burying interoperability inside release readiness therefore hides a real acceptance hole behind a gate this campaign cannot even execute.

Journey I owns both transfer directions on marker-owned fixtures and defines success as delivered destination bytes (independent pre/post oracles) plus visible source and destination refresh in the actual native UI — never UIA action success or absence of error alone. Non-delivery requires a bounded discriminating chain (valid target entry, drag/drop feedback, `IDropTarget` handler invocation, destination I/O initiation, destination bytes, visible refresh) before any disposition: a demonstrated product or harness defect is repaired with focused regression and requalification; `BLOCKED_ENVIRONMENT` is recorded only with evidence that places the failure outside the application; an unresolved non-delivery remains explicitly unqualified with a preserved cause class. The predecessor's `UNQUALIFIED` outcome is the baseline to advance, not a disposition.

**Alternative/trade-off:** keeping interop as a release-only readiness item would repeat the `UNQUALIFIED` limbo and let suite skips silently substitute for acceptance; owning it costs serialized Explorer-window fixtures and bounded diagnosis effort, but converts an unexcluded defect class into evidence either way.

## Risks / Trade-offs

- [Risk] Newer source revives an older overwrite or UI ownership defect → invariant-to-test mapping, semantic resolutions, integrated native oracles and independent review.
- [Risk] A correct test harness is overwritten by a parallel branch → compare actual changed bodies and preserve close/focus/evidence/result regressions and unique test registrations.
- [Risk] Stale lifecycle metadata looks like missing implementation → compare task-level evidence on all source heads before edits; validate merged lifecycle after each stage.
- [Risk] Intermittent input/UIA failure is greenwashed by retry → retain first failure and later observations, vary diagnostics only with new evidence, bounded attempts and reviewed disposition.
- [Risk] Interop non-delivery is greenwashed as environmental → journey I's owned-fixture cells and the evidence-backed non-delivery diagnosis chain (D12) keep classification honest.
- [Risk] External checks prevent the desired merge → deliver the qualified candidate without bypass; owner action is explicitly separate from engineering work.
- [Risk] Massive certification exceeds host resources → preflight, stage, serialize scarce resources and reuse only demonstrably applicable evidence.

## Migration Plan

1. Commit/publish these proposal artifacts on the central branch; apply remains unstarted.
2. During apply, refresh/freeze intake and acceptance, then integrate through progressive stage gates and reviewed resolutions.
3. Preserve incoming additive schema-v3 migration, atomic candidate maintenance, recovery and fail-closed downgrade behavior. Use disposable databases for migration tests, never the owner's index or files.
4. If a stage is wrong, repair it or create an ordinary history-preserving revert commit on the candidate. Do not reset shared history or open an older schema against a newer live database.
5. Finish reviews, certification, lifecycle/provenance reconciliation and normal candidate publication. Merge only through D10; otherwise stop at a clean, precisely blocked handoff. No production installation occurs.

## Open Questions

- Exact conflict count and local-only equivalence classes are intake outputs; they change resolution effort, not the acceptance contract.
- Exact test counts and relevant performance reruns depend on final registration and executable inputs; D7 defines how to determine them.
- Availability of permitted interactive/physical/hosted environments determines which required cells remain blocked, not whether those cells are silently waived.
